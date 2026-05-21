/*
 * Copyright (C) 2026 Apple Inc. All rights reserved.
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

#include "FrameIdentifier.h"
#include "ProcessIdentifier.h"
#include "ResourceLoaderIdentifier.h"
#include "ScriptExecutionContextIdentifier.h"
#include <JavaScriptCore/InspectorProtocolObjects.h>
#include <wtf/Forward.h>
#include <wtf/RefCountedAndCanMakeWeakPtr.h>
#include <wtf/RobinHoodHashMap.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakHashMap.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
class DocumentLoader;
class Frame;
class LocalFrame;
}

namespace Inspector {

// Abstract interface for mapping WebCore objects to inspector protocol identifier strings.
// InspectorPageAgent, NetworkAgent, and other agents use these to produce consistent
// frameId/loaderId values. Under Site Isolation, the subclass can produce deterministic
// IDs from FrameIdentifier so that UIProcess and WebProcess agree without syncing state.
class IdentifierRegistry : public RefCountedAndCanMakeWeakPtr<IdentifierRegistry> {
    WTF_MAKE_TZONE_ALLOCATED(IdentifierRegistry);
public:
    virtual ~IdentifierRegistry() = default;

    virtual WebCore::Frame* frameForId(const Protocol::Network::FrameId&) = 0;
    WEBCORE_EXPORT virtual Protocol::Network::FrameId frameId(const WebCore::Frame*) = 0;
    virtual Protocol::Network::LoaderId loaderId(WebCore::DocumentLoader*) = 0;
    virtual WebCore::LocalFrame* assertFrame(Protocol::ErrorString&, const Protocol::Network::FrameId&) = 0;

    // Called when a frame is detached; returns the protocol ID that was assigned.
    // Callers must ensure takeFrame is called via InspectorInstrumentation::frameDetached
    // to clean up the identifier maps. The WeakHashMap handles frame destruction gracefully,
    // but the reverse map (identifier-to-frame) would retain stale entries without this call.
    virtual Protocol::Network::FrameId takeFrame(const WebCore::Frame&) = 0;
    // Called when a document loader is detached; returns the protocol ID that was assigned.
    virtual Protocol::Network::LoaderId takeLoader(WebCore::DocumentLoader&) = 0;

    // Protocol ID helpers for Site Isolation. These produce type-prefixed strings that
    // encode the ProcessIdentifier, so both UIProcess and WebContent process can
    // independently compute the same ID from the same typed identifier.
    // Format: "type-processID.objectID" (e.g., "frame-12345.3", "request-12345.17")
    //
    // Prefer the 2-arg protocolFrameId(FrameIdentifier, ProcessIdentifier) which uses
    // an explicit hosting process. The 1-arg version extracts the process from the
    // FrameIdentifier's upper bits, which is incorrect for provisional frames where
    // the identifier is preserved across process swaps. See webkit.org/b/310164.
    static inline String protocolFrameId(WebCore::FrameIdentifier frameID, WebCore::ProcessIdentifier processID)
    {
        return makeString("frame-"_s, processID.toUInt64(), '.', static_cast<uint32_t>(frameID.toRawValue()));
    }

    // FIXME: <https://webkit.org/b/310164> Callers that receive FrameIdentifier via IPC
    // without a separate ProcessIdentifier should be updated to pass one explicitly.
    // This extracts the process from FrameIdentifier upper bits, which is incorrect
    // for provisional frames.
    static inline String protocolFrameId(WebCore::FrameIdentifier frameID)
    {
        return makeString("frame-"_s, frameID.toRawValue() >> 32, '.', static_cast<uint32_t>(frameID.toRawValue()));
    }

    static inline String protocolRequestId(WebCore::ProcessIdentifier pid, WebCore::ResourceLoaderIdentifier resourceID)
    {
        return makeString("request-"_s, pid.toUInt64(), '.', resourceID.toUInt64());
    }

    static inline String protocolLoaderId(WebCore::ScriptExecutionContextIdentifier contextID)
    {
        return makeString("loader-"_s, contextID.processIdentifier().toUInt64(), '.', contextID.object().toString());
    }
};

// Default implementation using IdentifiersFactory sequential counters.
// This is the pre-Site-Isolation behavior where IDs are opaque strings
// with no relationship to FrameIdentifier values.
class LegacyIdentifierRegistry final : public IdentifierRegistry {
    WTF_MAKE_TZONE_ALLOCATED(LegacyIdentifierRegistry);
public:
    static Ref<LegacyIdentifierRegistry> create() { return adoptRef(*new LegacyIdentifierRegistry()); }
    ~LegacyIdentifierRegistry();

    // IdentifierRegistry
    WebCore::Frame* frameForId(const Protocol::Network::FrameId&) final;
    WEBCORE_EXPORT Protocol::Network::FrameId frameId(const WebCore::Frame*) final;
    Protocol::Network::LoaderId loaderId(WebCore::DocumentLoader*) final;
    WebCore::LocalFrame* assertFrame(Protocol::ErrorString&, const Protocol::Network::FrameId&) final;
    Protocol::Network::FrameId takeFrame(const WebCore::Frame&) final;
    Protocol::Network::LoaderId takeLoader(WebCore::DocumentLoader&) final;

private:
    LegacyIdentifierRegistry();
    WeakHashMap<WebCore::Frame, String> m_frameToIdentifier;
    MemoryCompactRobinHoodHashMap<String, WeakPtr<WebCore::Frame>> m_identifierToFrame;
    // FIXME: DocumentLoader should use a smart pointer key (CheckedPtr or RefPtr).
    // It currently holds raw DocumentLoader* which prevents making loaderId/takeLoader
    // parameters const. See webkit.org/b/310162 for follow-up.
    HashMap<WebCore::DocumentLoader*, String> m_loaderToIdentifier;
};

// Deterministic implementation for Site Isolation. Produces type-prefixed IDs
// derived from FrameIdentifier and ScriptExecutionContextIdentifier so that
// UIProcess and WebContent processes independently compute matching protocol IDs.
class BackendIdentifierRegistry final : public IdentifierRegistry {
    WTF_MAKE_TZONE_ALLOCATED(BackendIdentifierRegistry);
public:
    static Ref<BackendIdentifierRegistry> create() { return adoptRef(*new BackendIdentifierRegistry()); }
    ~BackendIdentifierRegistry();

    // IdentifierRegistry
    WebCore::Frame* frameForId(const Protocol::Network::FrameId&) final;
    WEBCORE_EXPORT Protocol::Network::FrameId frameId(const WebCore::Frame*) final;
    Protocol::Network::LoaderId loaderId(WebCore::DocumentLoader*) final;
    WebCore::LocalFrame* assertFrame(Protocol::ErrorString&, const Protocol::Network::FrameId&) final;
    Protocol::Network::FrameId takeFrame(const WebCore::Frame&) final;
    Protocol::Network::LoaderId takeLoader(WebCore::DocumentLoader&) final;

private:
    BackendIdentifierRegistry();

    // Reverse map for frameForId() lookups. Populated by frameId().
    MemoryCompactRobinHoodHashMap<String, WeakPtr<WebCore::Frame>> m_identifierToFrame;
    // FIXME: DocumentLoader should use a smart pointer key (CheckedPtr or RefPtr).
    // See webkit.org/b/310162 for follow-up.
    HashMap<WebCore::DocumentLoader*, String> m_loaderToIdentifier;
};

} // namespace Inspector
