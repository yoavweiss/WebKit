/*
 * Copyright (C) 2008 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 *
 */

#pragma once

#include "ActiveDOMObject.h"
#include "ContextDestructionObserverInlines.h"
#include "EventTarget.h"
#include "EventTargetInterfaces.h"
#include "MessagePortChannel.h"
#include "MessagePortIdentifier.h"
#include "MessageWithMessagePorts.h"
#include <wtf/WeakPtr.h>

namespace JSC {
class CallFrame;
class JSObject;
class JSValue;
}

namespace WebCore {

class LocalFrame;
class WebCoreOpaqueRoot;

struct StructuredSerializeOptions;

template<typename> class ExceptionOr;

class MessagePort final : public ActiveDOMObject, public EventTarget, public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<MessagePort> {
    WTF_MAKE_NONCOPYABLE(MessagePort);
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED_EXPORT(MessagePort, WEBCORE_EXPORT);
public:
    static Ref<MessagePort> create(ScriptExecutionContext&, const MessagePortIdentifier& local, const MessagePortIdentifier& remote);
    WEBCORE_EXPORT virtual ~MessagePort();

    // ActiveDOMObject.
    void ref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::ref(); }
    void deref() const final { ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr::deref(); }

    ExceptionOr<void> postMessage(JSC::JSGlobalObject&, JSC::JSValue message, StructuredSerializeOptions&&);

    void start();
    void close();
    void entangle();

    // Returns nullptr if the passed-in vector is empty.
    static ExceptionOr<Vector<TransferredMessagePort>> disentanglePorts(Vector<Ref<MessagePort>>&&);
    static Vector<Ref<MessagePort>> entanglePorts(ScriptExecutionContext&, Vector<TransferredMessagePort>&&);

    WEBCORE_EXPORT static bool isMessagePortAliveForTesting(const MessagePortIdentifier&);
    WEBCORE_EXPORT static void notifyMessageAvailable(const MessagePortIdentifier&);

    WEBCORE_EXPORT void messageAvailable();
    bool started() const { return m_started; }
    bool isDetached() const { return m_isDetached; }

    void dispatchMessages();

    // Returns null if there is no entangled port, or if the entangled port is run by a different thread.
    // This is used solely to enable a GC optimization. Some platforms may not be able to determine ownership
    // of the remote port (since it may live cross-process) - those platforms may always return null.
    MessagePort* locallyEntangledPort() const;

    const MessagePortIdentifier& identifier() const { return m_identifier; }
    const MessagePortIdentifier& remoteIdentifier() const { return m_remoteIdentifier; }

    // EventTarget.
    enum EventTargetInterfaceType eventTargetInterface() const final { return EventTargetInterfaceType::MessagePort; }
    ScriptExecutionContext* scriptExecutionContext() const final { return ActiveDOMObject::scriptExecutionContext(); }
    void refEventTarget() final { ref(); }
    void derefEventTarget() final { deref(); }

    void dispatchEvent(Event&) final;

    TransferredMessagePort disentangle();
    static Ref<MessagePort> entangle(ScriptExecutionContext&, TransferredMessagePort&&);

private:
    explicit MessagePort(ScriptExecutionContext&, const MessagePortIdentifier& local, const MessagePortIdentifier& remote);

    bool addEventListener(const AtomString& eventType, Ref<EventListener>&&, const AddEventListenerOptions&) final;
    bool removeEventListener(const AtomString& eventType, EventListener&, const EventListenerOptions&) final;

    // ActiveDOMObject.
    void contextDestroyed() final;
    void stop() final { close(); }
    bool virtualHasPendingActivity() const final;

    // A port starts out its life entangled, and remains entangled until it is detached or is cloned.
    bool isEntangled() const { return !m_isDetached && m_entangled; }

    bool m_started { false };
    bool m_isDetached { false };
    bool m_entangled { true };
    bool m_hasMessageEventListener { false };

    MessagePortIdentifier m_identifier;
    MessagePortIdentifier m_remoteIdentifier;
};

WebCoreOpaqueRoot root(MessagePort*);

} // namespace WebCore
