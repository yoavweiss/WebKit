/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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

#pragma once

#if USE(LIBWEBRTC)

#include "LibWebRTCResolver.h"
#include "LibWebRTCSocket.h"
#include "WebPageProxyIdentifier.h"
#include <WebCore/LibWebRTCMacros.h>
#include <WebCore/LibWebRTCSocketIdentifier.h>
#include <wtf/CheckedPtr.h>
#include <wtf/Deque.h>
#include <wtf/Function.h>
#include <wtf/HashMap.h>
#include <wtf/WeakRef.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN
#include <webrtc/rtc_base/net_helpers.h>
#include <webrtc/api/packet_socket_factory.h>
WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

namespace WebKit {

class LibWebRTCNetwork;
class LibWebRTCSocket;

class LibWebRTCSocketFactory : public CanMakeThreadSafeCheckedPtr<LibWebRTCSocketFactory> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(LibWebRTCSocketFactory);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(LibWebRTCSocketFactory);
public:
    LibWebRTCSocketFactory() = default;

    void addSocket(LibWebRTCSocket&);
    void removeSocket(LibWebRTCSocket&);
    LibWebRTCSocket* socket(WebCore::LibWebRTCSocketIdentifier identifier) { return m_sockets.get(identifier); }

    void forSocketInGroup(WebCore::ScriptExecutionContextIdentifier, NOESCAPE const Function<void(LibWebRTCSocket&)>&);
    webrtc::AsyncPacketSocket* createUdpSocket(WebCore::ScriptExecutionContextIdentifier, const webrtc::SocketAddress&, uint16_t minPort, uint16_t maxPort, WebPageProxyIdentifier, bool isFirstParty, bool isRelayDisabled, const WebCore::RegistrableDomain&);
    webrtc::AsyncPacketSocket* createClientTcpSocket(WebCore::ScriptExecutionContextIdentifier, const webrtc::SocketAddress& localAddress, const webrtc::SocketAddress& remoteAddress, String&& userAgent, const webrtc::PacketSocketTcpOptions&, WebPageProxyIdentifier, bool isFirstParty, bool isRelayDisabled, const WebCore::RegistrableDomain&);

    CheckedPtr<LibWebRTCResolver> resolver(LibWebRTCResolverIdentifier identifier) { return m_resolvers.get(identifier); }
    void removeResolver(LibWebRTCResolverIdentifier identifier) { m_resolvers.remove(identifier); }

    std::unique_ptr<LibWebRTCResolver> createAsyncDnsResolver();

    void disableNonLocalhostConnections() { m_disableNonLocalhostConnections = true; }

    void setConnection(RefPtr<IPC::Connection>&&);
    IPC::Connection* connection();

private:
    // We cannot own sockets, clients of the factory are responsible to free them.
    HashMap<WebCore::LibWebRTCSocketIdentifier, CheckedRef<LibWebRTCSocket>> m_sockets;

    HashMap<LibWebRTCResolverIdentifier, CheckedRef<LibWebRTCResolver>> m_resolvers;
    bool m_disableNonLocalhostConnections { false };

    RefPtr<IPC::Connection> m_connection;
    Deque<Function<void(IPC::Connection&)>> m_pendingMessageTasks;
};

} // namespace WebKit

#endif // USE(LIBWEBRTC)
