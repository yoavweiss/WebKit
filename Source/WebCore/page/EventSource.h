/*
 * Copyright (C) 2009, 2012 Ericsson AB. All rights reserved.
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "ActiveDOMObject.h"
#include "EventLoop.h"
#include "EventTarget.h"
#include "EventTargetInterfaces.h"
#include "ThreadableLoaderClient.h"
#include "Timer.h"
#include <wtf/URL.h>
#include <wtf/Vector.h>

namespace WebCore {

class MessageEvent;
class TextResourceDecoder;
class ThreadableLoader;
template<typename> class ExceptionOr;

class EventSource final : public RefCounted<EventSource>, public EventTarget, private ThreadableLoaderClient, public ActiveDOMObject {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(EventSource);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(EventSource);
public:
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    struct Init {
        bool withCredentials;
    };
    static ExceptionOr<Ref<EventSource>> create(ScriptExecutionContext&, const String& url, const Init&);
    virtual ~EventSource();

    USING_CAN_MAKE_WEAKPTR(EventTarget);

    const String& url() const;
    bool withCredentials() const;

    using State = short;
    static const State CONNECTING = 0;
    static const State OPEN = 1;
    static const State CLOSED = 2;

    State readyState() const;

    void close();

private:
    EventSource(ScriptExecutionContext&, const URL&, const Init&);

    enum EventTargetInterfaceType eventTargetInterface() const final { return EventTargetInterfaceType::EventSource; }
    ScriptExecutionContext* scriptExecutionContext() const final { return ActiveDOMObject::scriptExecutionContext(); }

    void refEventTarget() final { ref(); }
    void derefEventTarget() final { deref(); }

    void dispatchErrorEvent();
    void doExplicitLoadCancellation();

    // ThreadableLoaderClient
    void didReceiveResponse(ScriptExecutionContextIdentifier, std::optional<ResourceLoaderIdentifier>, const ResourceResponse&) final;
    void didReceiveData(const SharedBuffer&) final;
    void didFinishLoading(ScriptExecutionContextIdentifier, std::optional<ResourceLoaderIdentifier>, const NetworkLoadMetrics&) final;
    void didFail(std::optional<ScriptExecutionContextIdentifier>, const ResourceError&) final;

    // ActiveDOMObject
    void stop() final;
    void suspend(ReasonForSuspension) final;
    void resume() final;
    bool virtualHasPendingActivity() const final;

    void connect();
    void networkRequestEnded();
    void scheduleInitialConnect();
    void scheduleReconnect();
    void abortConnectionAttempt();
    void parseEventStream();
    void parseEventStreamLine(unsigned position, std::optional<unsigned> fieldLength, unsigned lineLength);
    void dispatchMessageEvent();

    bool responseIsValid(const ResourceResponse&) const;

    static const uint64_t defaultReconnectDelay;

    URL m_url;
    bool m_withCredentials;
    State m_state { CONNECTING };

    const Ref<TextResourceDecoder> m_decoder;
    RefPtr<ThreadableLoader> m_loader;
    EventLoopTimerHandle m_connectTimer;
    Vector<char16_t> m_receiveBuffer;
    bool m_discardTrailingNewline { false };
    bool m_requestInFlight { false };
    bool m_isSuspendedForBackForwardCache { false };
    bool m_isDoingExplicitCancellation { false };
    bool m_shouldReconnectOnResume { false };

    AtomString m_eventName;
    Vector<char16_t> m_data;
    String m_currentlyParsedEventId;
    String m_lastEventId;
    uint64_t m_reconnectDelay { defaultReconnectDelay };
    String m_eventStreamOrigin;
};

inline const String& EventSource::url() const
{
    return m_url.string();
}

inline bool EventSource::withCredentials() const
{
    return m_withCredentials;
}

inline EventSource::State EventSource::readyState() const
{
    return m_state;
}

} // namespace WebCore
