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

#include "CachedResourceClient.h"
#include "CachedResourceHandle.h"

#include <wtf/CompletionHandler.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/URLHash.h>


namespace WebCore {

class CachedRawResource;
class DocumentLoader;
class FrameLoader;
class ResourceRequest;
class NetworkLoadMetrics;

class DocumentPrefetcher : public RefCounted<DocumentPrefetcher>, public CachedRawResourceClient {
public:
    static Ref<DocumentPrefetcher> create(FrameLoader& frameLoader) { return adoptRef(*new DocumentPrefetcher(frameLoader)); }
    explicit DocumentPrefetcher(FrameLoader&);
    ~DocumentPrefetcher();

    void prefetch(const URL&, const Vector<String>& tags, const String& referrerPolicyString, bool lowPriority = false);

    // CachedRawResourceClient
    void responseReceived(const CachedResource&, const ResourceResponse&, CompletionHandler<void()>&&) override;
    void redirectReceived(CachedResource&, ResourceRequest&&, const ResourceResponse&, CompletionHandler<void(ResourceRequest&&)>&&) override;
    void notifyFinished(CachedResource&, const NetworkLoadMetrics&, LoadWillContinueInAnotherProcess = LoadWillContinueInAnotherProcess::No) override;
    CachedResourceClientType resourceClientType() const override { return RawResourceType; }

    bool isFinished(const URL& url) const { return m_finishedURLs.contains(url); }

    void notifyWhenFinished(const URL& url) { m_notifyWhenFinishedURLs.add(url); }
    bool isNotifyingWhenFinished(const URL& url) const { return m_notifyWhenFinishedURLs.contains(url); }

private:
    void clear();
    void clearPrefetchedAssets();

    WeakRef<FrameLoader> m_frameLoader;
    HashMap<URL, CachedResourceHandle<CachedRawResource>> m_prefetchedResources;
    HashMap<URL, Box<NetworkLoadMetrics>> m_prefetchedNetworkLoadMetrics;
    HashSet<URL> m_notifyWhenFinishedURLs;
    HashSet<URL> m_finishedURLs;
};

} // namespace WebCore
