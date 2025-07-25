/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2011 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "Connection.h"

#include "IPCUtilities.h"
#include "UnixMessage.h"
#include <WebCore/SharedMemory.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <wtf/Assertions.h>
#include <wtf/MallocSpan.h>
#include <wtf/SafeStrerror.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/UniStdExtras.h>

#if USE(GLIB)
#include <gio/gio.h>
#include <wtf/glib/GUniquePtr.h>
#endif

#if OS(DARWIN)
#define MSG_NOSIGNAL 0
#endif

// Although it's available on Darwin, SOCK_SEQPACKET seems to work differently
// than in traditional Unix so fallback to STREAM on that platform.
#if defined(SOCK_SEQPACKET) && !OS(DARWIN)
#define SOCKET_TYPE SOCK_SEQPACKET
#else
#if USE(GLIB)
#define SOCKET_TYPE SOCK_STREAM
#else
#define SOCKET_TYPE SOCK_DGRAM
#endif
#endif // SOCK_SEQPACKET

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN // Unix port

namespace IPC {

static const size_t messageMaxSize = 4096;
static const size_t attachmentMaxAmount = 254;

class AttachmentInfo {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(AttachmentInfo);
public:
    AttachmentInfo()
    {
        // The entire AttachmentInfo is passed to write(), so we have to zero our
        // padding bytes to avoid writing uninitialized memory.
        memset(static_cast<void*>(this), 0, sizeof(*this));
    }

    AttachmentInfo(const AttachmentInfo& info)
    {
        memset(static_cast<void*>(this), 0, sizeof(*this));
        *this = info;
    }

    AttachmentInfo& operator=(const AttachmentInfo&) = default;

    // The attachment is not null unless explicitly set.
    void setNull() { m_isNull = true; }
    bool isNull() const { return m_isNull; }

private:
    // The AttachmentInfo will be copied using memcpy, so all members must be trivially copyable.
    bool m_isNull;
};

static_assert(sizeof(MessageInfo) + sizeof(AttachmentInfo) * attachmentMaxAmount <= messageMaxSize, "messageMaxSize is too small.");

int Connection::socketDescriptor() const
{
#if USE(GLIB)
    return g_socket_get_fd(m_socket.get());
#else
    return m_socketDescriptor.value();
#endif
}

void Connection::platformInitialize(Identifier&& identifier)
{
#if USE(GLIB)
    GUniqueOutPtr<GError> error;
    m_socket = adoptGRef(g_socket_new_from_fd(identifier.handle.release(), &error.outPtr()));
    if (!m_socket) {
        // Note: g_socket_new_from_fd() takes ownership of the fd only on success, so if this error
        // were not fatal, we would need to close it here.
        g_error("Failed to adopt IPC::Connection socket: %s", error->message);
    }
#else
    m_socketDescriptor = WTFMove(identifier.handle);
#endif
    m_readBuffer.reserveInitialCapacity(messageMaxSize);
    m_fileDescriptors.reserveInitialCapacity(attachmentMaxAmount);
}

void Connection::platformInvalidate()
{
#if USE(GLIB)
    GUniqueOutPtr<GError> error;
    g_socket_close(m_socket.get(), &error.outPtr());
    if (error)
        g_warning("Failed to close WebKit IPC socket: %s", error->message);
#else
    if (m_socketDescriptor.value() != -1)
        closeWithRetry(m_socketDescriptor.release());
#endif

    if (!m_isConnected)
        return;

#if USE(GLIB)
    m_readSocketMonitor.stop();
    m_writeSocketMonitor.stop();
#endif

#if PLATFORM(PLAYSTATION)
    if (m_socketMonitor) {
        m_socketMonitor->detach();
        m_socketMonitor = nullptr;
    }
#endif

    m_isConnected = false;
}

bool Connection::processMessage()
{
    if (m_readBuffer.size() < sizeof(MessageInfo))
        return false;

    auto messageData = m_readBuffer.mutableSpan();
    MessageInfo messageInfo;
    memcpySpan(asMutableByteSpan(messageInfo), consumeSpan(messageData, sizeof(messageInfo)));

    if (messageInfo.attachmentCount() > attachmentMaxAmount || (!messageInfo.isBodyOutOfLine() && messageInfo.bodySize() > messageMaxSize)) {
        ASSERT_NOT_REACHED();
        return false;
    }

    size_t messageLength = sizeof(MessageInfo) + messageInfo.attachmentCount() * sizeof(AttachmentInfo) + (messageInfo.isBodyOutOfLine() ? 0 : messageInfo.bodySize());
    if (m_readBuffer.size() < messageLength)
        return false;

    size_t attachmentFileDescriptorCount = 0;
    size_t attachmentCount = messageInfo.attachmentCount();
    Vector<AttachmentInfo> attachmentInfo(attachmentCount);

    if (attachmentCount) {
        memcpySpan(asMutableByteSpan(attachmentInfo.mutableSpan()), consumeSpan(messageData, sizeof(AttachmentInfo) * attachmentCount));
        for (size_t i = 0; i < attachmentCount; ++i) {
            if (!attachmentInfo[i].isNull())
                attachmentFileDescriptorCount++;
        }

        if (messageInfo.isBodyOutOfLine())
            attachmentCount--;
    }

    Vector<Attachment> attachments(attachmentCount);
    RefPtr<WebCore::SharedMemory> oolMessageBody;

    size_t fdIndex = 0;
    for (size_t i = 0; i < attachmentCount; ++i) {
        int fd = !attachmentInfo[i].isNull() ? m_fileDescriptors[fdIndex++] : -1;
        attachments[attachmentCount - i - 1] = UnixFileDescriptor { fd, UnixFileDescriptor::Adopt };
    }

    if (messageInfo.isBodyOutOfLine()) {
        ASSERT(messageInfo.bodySize());

        if (attachmentInfo[attachmentCount].isNull()) {
            ASSERT_NOT_REACHED();
            return false;
        }

        auto fd = UnixFileDescriptor { m_fileDescriptors[attachmentFileDescriptorCount - 1], UnixFileDescriptor::Adopt };
        if (!fd) {
            ASSERT_NOT_REACHED();
            return false;
        }

        auto handle = WebCore::SharedMemory::Handle { WTFMove(fd), messageInfo.bodySize() };
        oolMessageBody = WebCore::SharedMemory::map(WTFMove(handle), WebCore::SharedMemory::Protection::ReadOnly);
        if (!oolMessageBody) {
            ASSERT_NOT_REACHED();
            return false;
        }
    }

    ASSERT(attachments.size() == (messageInfo.isBodyOutOfLine() ? messageInfo.attachmentCount() - 1 : messageInfo.attachmentCount()));

    auto messageBody = messageData;
    if (messageInfo.isBodyOutOfLine())
        messageBody = oolMessageBody->mutableSpan();

    auto decoder = Decoder::create(messageBody.first(messageInfo.bodySize()), WTFMove(attachments));
    ASSERT(decoder);
    if (!decoder)
        return false;

    processIncomingMessage(makeUniqueRefFromNonNullUniquePtr(WTFMove(decoder)));

    if (m_readBuffer.size() > messageLength) {
        memmoveSpan(m_readBuffer.mutableSpan(), m_readBuffer.subspan(messageLength));
        m_readBuffer.shrink(m_readBuffer.size() - messageLength);
    } else
        m_readBuffer.shrink(0);

    if (attachmentFileDescriptorCount) {
        if (m_fileDescriptors.size() > attachmentFileDescriptorCount) {
            memmoveSpan(m_fileDescriptors.mutableSpan(), m_fileDescriptors.subspan(attachmentFileDescriptorCount));
            m_fileDescriptors.shrink(m_fileDescriptors.size() - attachmentFileDescriptorCount);
        } else
            m_fileDescriptors.shrink(0);
    }


    return true;
}

static ssize_t readBytesFromSocket(int socketDescriptor, Vector<uint8_t>& buffer, Vector<int>& fileDescriptors)
{
    struct msghdr message;
    memset(&message, 0, sizeof(message));

    struct iovec iov[1];
    memset(&iov, 0, sizeof(iov));

    auto attachmentDescriptorBuffer = MallocSpan<char>::zeroedMalloc(CMSG_SPACE(sizeof(int) * attachmentMaxAmount));
    auto attachmentDescriptorSpan = attachmentDescriptorBuffer.mutableSpan();
    message.msg_control = attachmentDescriptorSpan.data();
    message.msg_controllen = attachmentDescriptorSpan.size();

    size_t previousBufferSize = buffer.size();
    buffer.grow(buffer.capacity());
    iov[0].iov_base = buffer.mutableSpan().data() + previousBufferSize;
    iov[0].iov_len = buffer.size() - previousBufferSize;

    message.msg_iov = iov;
    message.msg_iovlen = 1;

    while (true) {
        ssize_t bytesRead = recvmsg(socketDescriptor, &message, MSG_NOSIGNAL);

        if (bytesRead < 0) {
            if (errno == EINTR)
                continue;

            buffer.shrink(previousBufferSize);
            return -1;
        }

        if (message.msg_flags & MSG_CTRUNC) {
            // Control data has been discarded, which is expected by processMessage(), so consider this a read failure.
            buffer.shrink(previousBufferSize);
            return -1;
        }

        struct cmsghdr* controlMessage;
        for (controlMessage = CMSG_FIRSTHDR(&message); controlMessage; controlMessage = CMSG_NXTHDR(&message, controlMessage)) {
            if (controlMessage->cmsg_level == SOL_SOCKET && controlMessage->cmsg_type == SCM_RIGHTS) {
                if (controlMessage->cmsg_len < CMSG_LEN(0) || controlMessage->cmsg_len > CMSG_LEN(sizeof(int) * attachmentMaxAmount)) {
                    ASSERT_NOT_REACHED();
                    break;
                }
                size_t previousFileDescriptorsSize = fileDescriptors.size();
                size_t fileDescriptorsCount = (controlMessage->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                fileDescriptors.grow(fileDescriptors.size() + fileDescriptorsCount);
                memcpy(fileDescriptors.mutableSpan().subspan(previousFileDescriptorsSize).data(), CMSG_DATA(controlMessage), sizeof(int) * fileDescriptorsCount);

                for (size_t i = 0; i < fileDescriptorsCount; ++i) {
                    if (!setCloseOnExec(fileDescriptors[previousFileDescriptorsSize + i])) {
                        ASSERT_NOT_REACHED();
                        break;
                    }
                }
                break;
            }
        }

        buffer.shrink(previousBufferSize + bytesRead);
        return bytesRead;
    }

    return -1;
}

void Connection::readyReadHandler()
{
    while (true) {
        ssize_t bytesRead = readBytesFromSocket(socketDescriptor(), m_readBuffer, m_fileDescriptors);

        if (bytesRead < 0) {
            // EINTR was already handled by readBytesFromSocket.
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;

            if (errno == ECONNRESET) {
                connectionDidClose();
                return;
            }

            if (m_isConnected) {
                WTFLogAlways("Error receiving IPC message on socket %d in process %d: %s", socketDescriptor(), getpid(), safeStrerror(errno).data());
                connectionDidClose();
            }
            return;
        }

        if (!bytesRead) {
            connectionDidClose();
            return;
        }

        // Process messages from data received.
        while (true) {
            if (!processMessage())
                break;
        }
    }
}

bool Connection::platformPrepareForOpen()
{
    if (setNonBlock(socketDescriptor()))
        return true;
    ASSERT_NOT_REACHED();
    return false;
}

void Connection::platformOpen()
{
    RefPtr<Connection> protectedThis(this);
    m_isConnected = true;
#if USE(GLIB)
    m_readSocketMonitor.start(m_socket.get(), G_IO_IN, m_connectionQueue->runLoop(), [protectedThis] (GIOCondition condition) -> gboolean {
        if (condition & G_IO_HUP || condition & G_IO_ERR || condition & G_IO_NVAL) {
            protectedThis->connectionDidClose();
            return G_SOURCE_REMOVE;
        }

        if (condition & G_IO_IN) {
            protectedThis->readyReadHandler();
            return G_SOURCE_CONTINUE;
        }

        ASSERT_NOT_REACHED();
        return G_SOURCE_REMOVE;
    });
#endif

#if PLATFORM(PLAYSTATION)
    m_socketMonitor = Thread::create("SocketMonitor"_s, [protectedThis] {
        {
            int fd;
            while ((fd = protectedThis->socketDescriptor()) != -1) {
                int maxFd = fd;
                fd_set fdSet;
                FD_ZERO(&fdSet);
                FD_SET(fd, &fdSet);

                if (-1 != select(maxFd + 1, &fdSet, 0, 0, 0)) {
                    if (FD_ISSET(fd, &fdSet))
                        protectedThis->readyReadHandler();
                }
            }

        }
    });
    return;
#endif

    // Schedule a call to readyReadHandler. Data may have arrived before installation of the signal handler.
    m_connectionQueue->dispatch([protectedThis] {
        protectedThis->readyReadHandler();
    });
}

bool Connection::platformCanSendOutgoingMessages() const
{
    return !m_pendingOutputMessage;
}

bool Connection::sendOutgoingMessage(UniqueRef<Encoder>&& encoder)
{
    static_assert(sizeof(MessageInfo) + attachmentMaxAmount * sizeof(size_t) <= messageMaxSize, "Attachments fit to message inline");

    UnixMessage outputMessage(encoder.get());
    if (outputMessage.attachments().size() > (attachmentMaxAmount - 1)) {
        ASSERT_NOT_REACHED();
        return false;
    }

    size_t messageSizeWithBodyInline = sizeof(MessageInfo) + (outputMessage.attachments().size() * sizeof(AttachmentInfo)) + outputMessage.bodySize();
    if (messageSizeWithBodyInline > messageMaxSize && outputMessage.bodySize()) {
        RefPtr oolMessageBody = WebCore::SharedMemory::allocate(outputMessage.bodySize());
        if (!oolMessageBody)
            return false;

        auto handle = oolMessageBody->createHandle(WebCore::SharedMemory::Protection::ReadOnly);
        if (!handle)
            return false;

        outputMessage.messageInfo().setBodyOutOfLine();

        memcpySpan(oolMessageBody->mutableSpan(), outputMessage.body());

        outputMessage.appendAttachment(handle->releaseHandle());
    }

    return sendOutputMessage(outputMessage);
}

bool Connection::sendOutputMessage(UnixMessage& outputMessage)
{
    ASSERT(!m_pendingOutputMessage);

    auto& messageInfo = outputMessage.messageInfo();
    struct msghdr message;
    memset(&message, 0, sizeof(message));

    struct iovec iov[3];
    memset(&iov, 0, sizeof(iov));

    message.msg_iov = iov;
    int iovLength = 1;

    iov[0].iov_base = reinterpret_cast<void*>(&messageInfo);
    iov[0].iov_len = sizeof(messageInfo);

    Vector<AttachmentInfo> attachmentInfo;
    MallocSpan<char> attachmentFDBuffer;

    auto& attachments = outputMessage.attachments();
    if (!attachments.isEmpty()) {
        int* fdPtr = 0;

        size_t attachmentFDBufferLength = std::count_if(attachments.begin(), attachments.end(),
            [](const Attachment& attachment) {
                return !!attachment;
            });

        if (attachmentFDBufferLength) {
            attachmentFDBuffer = MallocSpan<char>::zeroedMalloc(CMSG_SPACE(sizeof(int) * attachmentFDBufferLength));
            auto span = attachmentFDBuffer.mutableSpan();
            message.msg_control = span.data();
            message.msg_controllen = span.size();

            struct cmsghdr* cmsg = CMSG_FIRSTHDR(&message);
            cmsg->cmsg_level = SOL_SOCKET;
            cmsg->cmsg_type = SCM_RIGHTS;
            cmsg->cmsg_len = CMSG_LEN(sizeof(int) * attachmentFDBufferLength);

            fdPtr = reinterpret_cast<int*>(CMSG_DATA(cmsg));
        }

        attachmentInfo.resize(attachments.size());
        int fdIndex = 0;
        for (size_t i = 0; i < attachments.size(); ++i) {
            if (!!attachments[i]) {
                ASSERT(fdPtr);
                fdPtr[fdIndex++] = attachments[i].value();
            } else
                attachmentInfo[i].setNull();
        }

        iov[iovLength].iov_base = attachmentInfo.mutableSpan().data();
        iov[iovLength].iov_len = sizeof(AttachmentInfo) * attachments.size();
        ++iovLength;
    }

    if (!messageInfo.isBodyOutOfLine() && outputMessage.bodySize()) {
        iov[iovLength].iov_base = reinterpret_cast<void*>(outputMessage.body().data());
        iov[iovLength].iov_len = outputMessage.bodySize();
        ++iovLength;
    }

    message.msg_iovlen = iovLength;

    while (sendmsg(socketDescriptor(), &message, MSG_NOSIGNAL) == -1) {
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
#if USE(GLIB)
            m_pendingOutputMessage = makeUnique<UnixMessage>(WTFMove(outputMessage));
            m_writeSocketMonitor.start(m_socket.get(), G_IO_OUT, m_connectionQueue->runLoop(), [this, protectedThis = Ref { *this }] (GIOCondition condition) -> gboolean {
                if (condition & G_IO_OUT) {
                    ASSERT(m_pendingOutputMessage);
                    // We can't stop the monitor from this lambda, because stop destroys the lambda.
                    m_connectionQueue->dispatch([this, protectedThis = Ref { *this }] {
                        m_writeSocketMonitor.stop();
                        auto message = WTFMove(m_pendingOutputMessage);
                        if (m_isConnected) {
                            sendOutputMessage(*message);
                            sendOutgoingMessages();
                        }
                    });
                }
                return G_SOURCE_REMOVE;
            });
            return false;
#else
            struct pollfd pollfd;

            pollfd.fd = socketDescriptor();
            pollfd.events = POLLOUT;
            pollfd.revents = 0;
            poll(&pollfd, 1, -1);
            continue;
#endif
        }

#if OS(LINUX)
        // Linux can return EPIPE instead of ECONNRESET
        if (errno == EPIPE || errno == ECONNRESET)
#else
        if (errno == ECONNRESET)
#endif
        {
            connectionDidClose();
            return false;
        }

        if (m_isConnected)
            WTFLogAlways("Error sending IPC message: %s", safeStrerror(errno).data());
        return false;
    }

    return true;
}

SocketPair createPlatformConnection(unsigned options)
{
    int sockets[2];

#if USE(GLIB) && OS(LINUX)
    auto setPasscredIfNeeded = [options, &sockets] {
        if (options & SetPasscredOnServer) {
            int enable = 1;
            RELEASE_ASSERT(!setsockopt(sockets[1], SOL_SOCKET, SO_PASSCRED, &enable, sizeof(enable)));
        }
    };
#else
    auto setPasscredIfNeeded = [] { };
#endif

#if OS(LINUX)
    if ((options & SetCloexecOnServer) || (options & SetCloexecOnClient)) {
        RELEASE_ASSERT(socketpair(AF_UNIX, SOCKET_TYPE | SOCK_CLOEXEC, 0, sockets) != -1);

        if (!(options & SetCloexecOnServer))
            RELEASE_ASSERT(unsetCloseOnExec(sockets[1]));
        if (!(options & SetCloexecOnClient))
            RELEASE_ASSERT(unsetCloseOnExec(sockets[0]));

        setPasscredIfNeeded();

        return { { sockets[0], UnixFileDescriptor::Adopt }, { sockets[1], UnixFileDescriptor::Adopt } };
    }
#endif

    RELEASE_ASSERT(socketpair(AF_UNIX, SOCKET_TYPE, 0, sockets) != -1);

    if (options & SetCloexecOnServer)
        RELEASE_ASSERT(setCloseOnExec(sockets[1]));
    if (options & SetCloexecOnClient)
        RELEASE_ASSERT(setCloseOnExec(sockets[0]));

    setPasscredIfNeeded();

    return { { sockets[0], UnixFileDescriptor::Adopt }, { sockets[1], UnixFileDescriptor::Adopt } };
}

std::optional<Connection::ConnectionIdentifierPair> Connection::createConnectionIdentifierPair()
{
    SocketPair socketPair = createPlatformConnection();
    return { { Identifier { WTFMove(socketPair.server) }, ConnectionHandle { WTFMove(socketPair.client) } } };
}

#if USE(GLIB) && OS(LINUX)
void sendPIDToPeer(int socket)
{
    char buffer[1] = { 0 };
    struct msghdr message = { };
    struct iovec iov = { buffer, sizeof(buffer) };

    // Write one null byte. Credentials will be attached regardless of what we send.
    message.msg_iov = &iov;
    message.msg_iovlen = 1;

    int ret;
    do {
        ret = sendmsg(socket, &message, 0);
    } while (ret == -1 && errno == EINTR);

    if (ret == -1) {
        // Don't crash if the parent process merely closed its pid socket.
        // That's equivalent to canceling the process launch.
        if (errno == EPIPE)
            exit(1);
        g_error("sendPIDToPeer: Failed to send pid: %s", g_strerror(errno));
    }
}

// The goal here is to receive the pid of the sandboxed child in the parent process's pid namespace.
// It's impossible for the child to know this, but the kernel will translate it for us.
//
// Based on read_pid_from_socket() from bubblewrap's utils.c
// SPDX-License-Identifier: LGPL-2.0-or-later
pid_t readPIDFromPeer(int socket)
{
    char receiveBuffer[1] = { 0 };
    struct msghdr message = { };
    struct iovec iov = { receiveBuffer, sizeof(receiveBuffer) };
    const ssize_t controlLength = CMSG_SPACE(sizeof(struct ucred));
    union {
        char buffer[controlLength];
        cmsghdr forceAlignment;
    } controlMessage;

    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = controlMessage.buffer;
    message.msg_controllen = controlLength;

    int ret;
    do {
        ret = recvmsg(socket, &message, 0);
    } while (ret == -1 && errno == EINTR);

    if (ret == -1)
        g_error("readPIDFromPeer: Failed to read pid from PID socket: %s", g_strerror(errno));

    if (message.msg_controllen <= 0)
        g_error("readPIDFromPeer: Unexpected short read from PID socket. (This usually means the auxiliary process crashed immediately. Investigate that instead!)");

    for (cmsghdr* header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
        const unsigned payloadLength = header->cmsg_len - CMSG_LEN(0);
        if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_CREDENTIALS && payloadLength == sizeof(struct ucred)) {
            struct ucred credentials;
            memcpy(&credentials, CMSG_DATA(header), sizeof(struct ucred));
            return credentials.pid;
        }
    }

    g_error("readPIDFromPeer: No pid returned on PID socket");
}
#endif

} // namespace IPC

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
