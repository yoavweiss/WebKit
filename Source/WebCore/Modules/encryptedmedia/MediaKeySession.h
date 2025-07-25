/*
 * Copyright (C) 2016 Metrological Group B.V.
 * Copyright (C) 2016 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(ENCRYPTED_MEDIA)

#include "ActiveDOMObject.h"
#include "CDMInstanceSession.h"
#include "ContextDestructionObserverInlines.h"
#include "EventTarget.h"
#include "EventTargetInterfaces.h"
#include "IDLTypes.h"
#include "MediaKeyMessageType.h"
#include "MediaKeySessionType.h"
#include "MediaKeyStatus.h"
#include <wtf/Observer.h>
#include <wtf/RefCounted.h>
#include <wtf/UniqueRef.h>
#include <wtf/Vector.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/WTFString.h>

#if !RELEASE_LOG_DISABLED
namespace WTF {
class Logger;
}
#endif

namespace WebCore {

class BufferSource;
class CDM;
class DeferredPromise;
class MediaKeyStatusMap;
class MediaKeys;
class SharedBuffer;

template<typename IDLType> class DOMPromiseProxy;

class MediaKeySession final : public RefCounted<MediaKeySession>, public EventTarget, public ActiveDOMObject, public CDMInstanceSessionClient {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED_EXPORT(MediaKeySession, WEBCORE_EXPORT);
public:
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    static Ref<MediaKeySession> create(Document&, WeakPtr<MediaKeys>&&, MediaKeySessionType, bool useDistinctiveIdentifier, Ref<CDM>&&, Ref<CDMInstanceSession>&&);
    WEBCORE_EXPORT virtual ~MediaKeySession();

    USING_CAN_MAKE_WEAKPTR(CDMInstanceSessionClient);

    bool isClosed() const { return m_closed; }

    const String& sessionId() const;
    double expiration() const;
    Ref<MediaKeyStatusMap> keyStatuses() const;

    void generateRequest(const AtomString&, const BufferSource&, Ref<DeferredPromise>&&);
    void load(const String&, Ref<DeferredPromise>&&);
    void update(const BufferSource&, Ref<DeferredPromise>&&);
    void close(Ref<DeferredPromise>&&);
    void remove(Ref<DeferredPromise>&&);

    using ClosedPromise = DOMPromiseProxy<IDLUndefined>;
    ClosedPromise& closed() { return m_closedPromise.get(); }

    const Vector<std::pair<Ref<SharedBuffer>, MediaKeyStatus>>& statuses() const { return m_statuses; }

    unsigned internalInstanceSessionObjectRefCount() const { return m_instanceSession->refCount(); }

private:
    MediaKeySession(Document&, WeakPtr<MediaKeys>&&, MediaKeySessionType, bool useDistinctiveIdentifier, Ref<CDM>&&, Ref<CDMInstanceSession>&&);
    void enqueueMessage(MediaKeyMessageType, const SharedBuffer&);
    void updateExpiration(double);
    void sessionClosed();
    String mediaKeysStorageDirectory() const;
    CDMKeyGroupingStrategy keyGroupingStrategy() const;

    // CDMInstanceSessionClient
    void updateKeyStatuses(CDMInstanceSessionClient::KeyStatusVector&&) override;
    void sendMessage(CDMMessageType, Ref<SharedBuffer>&& message) final;
    void sessionIdChanged(const String&) final;
    PlatformDisplayID displayID() final;

    // EventTarget
    enum EventTargetInterfaceType eventTargetInterface() const override { return EventTargetInterfaceType::MediaKeySession; }
    ScriptExecutionContext* scriptExecutionContext() const override { return ActiveDOMObject::scriptExecutionContext(); }
    void refEventTarget() override { ref(); }
    void derefEventTarget() override { deref(); }

    // ActiveDOMObject
    bool virtualHasPendingActivity() const final;
    void stop() final;

    // DisplayChangedObserver
    void displayChanged(PlatformDisplayID);

#if !RELEASE_LOG_DISABLED
    // LoggerHelper
    const Logger& logger() const { return m_logger; }
    ASCIILiteral logClassName() const { return "MediaKeySession"_s; }
    WTFLogChannel& logChannel() const;
    uint64_t logIdentifier() const { return m_logIdentifier; }

    const Ref<const Logger> m_logger;
    const uint64_t m_logIdentifier;
#endif

    WeakPtr<MediaKeys> m_keys;
    String m_sessionId;
    double m_expiration;
    const UniqueRef<ClosedPromise> m_closedPromise;
    const Ref<MediaKeyStatusMap> m_keyStatuses;
    bool m_closed { false };
    bool m_uninitialized { true };
    bool m_callable { false };
    bool m_useDistinctiveIdentifier;
    MediaKeySessionType m_sessionType;
    const Ref<CDM> m_implementation;
    const Ref<CDMInstanceSession> m_instanceSession;
    Vector<Ref<SharedBuffer>> m_recordOfKeyUsage;
    double m_firstDecryptTime { 0 };
    double m_latestDecryptTime { 0 };
    Vector<std::pair<Ref<SharedBuffer>, MediaKeyStatus>> m_statuses;

    using DisplayChangedObserver = Observer<void(PlatformDisplayID)>;
    DisplayChangedObserver m_displayChangedObserver;
};

} // namespace WebCore

#endif // ENABLE(ENCRYPTED_MEDIA)
