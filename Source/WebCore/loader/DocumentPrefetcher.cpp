/*
 * Copyright (C) 2025 Shopify Inc. All rights reserved.
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
 */

#include "config.h"
#include "DocumentPrefetcher.h"

#include "CachedRawResource.h"
#include "DocumentLoader.h"
#include "Frame.h"
#include "FrameLoader.h"
#include "FrameLoaderClient.h"
#include "ResourceRequest.h"
#include "SubstituteData.h"

namespace WebCore {

DocumentPrefetcher::DocumentPrefetcher(FrameLoader& frameLoader)
    : m_frameLoader(frameLoader)
    , m_prefetchedResource(nullptr)
{
}

DocumentPrefetcher::~DocumentPrefetcher()
{
    clear();
}

void DocumentPrefetcher::prefetch(ResourceRequest&& request, bool lowPriority)
{

    URL url = request.url();
    if (m_prefetchedDocumentURL == url)
        return;

    ResourceLoaderOptions prefetchOptions(
        SendCallbackPolicy::DoNotSendCallbacks,
        ContentSniffingPolicy::DoNotSniffContent,
        DataBufferingPolicy::BufferData,
        StoredCredentialsPolicy::Use,
        ClientCredentialPolicy::MayAskClientForCredentials,
        FetchOptions::Credentials::Include,
        SecurityCheckPolicy::DoSecurityCheck,
        FetchOptions::Mode::Navigate,
        CertificateInfoPolicy::IncludeCertificateInfo,
        ContentSecurityPolicyImposition::DoPolicyCheck,
        DefersLoadingPolicy::AllowDefersLoading,
        CachingPolicy::AllowCaching
    );

    prefetchOptions.cachingPolicy = CachingPolicy::AllowCaching;

    request.setHTTPHeaderField(HTTPHeaderName::SecPurpose, "prefetch"_s);
    CachedResourceRequest prefetchRequest(WTFMove(request), prefetchOptions);
    if (lowPriority)
        prefetchRequest.setPriority(ResourceLoadPriority::Low);

    WeakRef<FrameLoader> frameLoader = m_frameLoader;
    if (!frameLoader.ptr())
        return;
    ResourceErrorOr<CachedResourceHandle<CachedRawResource>> resourceErrorOr = frameLoader->protectedDocumentLoader()->cachedResourceLoader().requestMainResource(WTFMove(prefetchRequest));
    if (!resourceErrorOr)
        return;
    m_prefetchedResource = resourceErrorOr.value();
    m_prefetchedDocumentURL = url.string();
}

CachedRawResource* DocumentPrefetcher::matchPrefetchedDocument(const URL& url)
{
    if (m_prefetchedDocumentURL == url.string())
        return m_prefetchedResource.get();
    return nullptr;
}

void DocumentPrefetcher::clear()
{
    m_prefetchedResource = nullptr;
}

} // namespace WebCore
