/*
 * Copyright (C) 2017-2022 Apple Inc. All rights reserved.
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

#include "WebPageProxyIdentifier.h"
#include <WebCore/EmptyFrameLoaderClient.h>
#include <WebCore/PageIdentifier.h>
#include <WebCore/ScriptExecutionContextIdentifier.h>

namespace WebCore {
class FrameLoader;
}

namespace WebKit {

class RemoteWorkerFrameLoaderClient final : public WebCore::EmptyFrameLoaderClient {
public:
    RemoteWorkerFrameLoaderClient(WebCore::FrameLoader&, WebPageProxyIdentifier, WebCore::PageIdentifier, const String& userAgent);

    WebPageProxyIdentifier webPageProxyID() const { return m_webPageProxyID; }

    void setUserAgent(String&& userAgent) { m_userAgent = WTFMove(userAgent); }

    void setServiceWorkerPageIdentifier(WebCore::ScriptExecutionContextIdentifier serviceWorkerPageIdentifier) { m_serviceWorkerPageIdentifier = serviceWorkerPageIdentifier; }
    std::optional<WebCore::ScriptExecutionContextIdentifier> serviceWorkerPageIdentifier() const { return m_serviceWorkerPageIdentifier; }

private:
    Ref<WebCore::DocumentLoader> createDocumentLoader(WebCore::ResourceRequest&&, WebCore::SubstituteData&&) final;

    bool shouldUseCredentialStorage(WebCore::DocumentLoader*, WebCore::ResourceLoaderIdentifier) final { return true; }
    bool isRemoteWorkerFrameLoaderClient() const final { return true; }

    String userAgent(const URL&) const final { return m_userAgent; }

    WebPageProxyIdentifier m_webPageProxyID;
    String m_userAgent;
    std::optional<WebCore::ScriptExecutionContextIdentifier> m_serviceWorkerPageIdentifier;
};

} // namespace WebKit

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::RemoteWorkerFrameLoaderClient)
    static bool isType(const WebCore::LocalFrameLoaderClient& frameLoaderClient) { return frameLoaderClient.isRemoteWorkerFrameLoaderClient(); }
SPECIALIZE_TYPE_TRAITS_END()
