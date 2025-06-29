/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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

#ifndef WebContextMenuClient_h
#define WebContextMenuClient_h

#if ENABLE(CONTEXT_MENUS)

#include "WebPage.h"
#include <WebCore/ContextMenuClient.h>
#include <wtf/TZoneMalloc.h>

namespace WebKit {

class WebContextMenuClient : public WebCore::ContextMenuClient {
    WTF_MAKE_TZONE_ALLOCATED(WebContextMenuClient);
public:
    WebContextMenuClient(WebPage* page)
        : m_page(page)
    {
    }
    
private:
    void downloadURL(const URL&) override;
    void searchWithGoogle(const WebCore::LocalFrame*) override;
    void lookUpInDictionary(WebCore::LocalFrame*) override;
    bool isSpeaking() const override;
    void speak(const String&) override;
    void stopSpeaking() override;

#if ENABLE(IMAGE_ANALYSIS)
    bool supportsLookUpInImages() final { return true; }
#endif

#if ENABLE(IMAGE_ANALYSIS_ENHANCEMENTS)
    bool supportsCopySubject() final { return true; }
#endif

#if HAVE(TRANSLATION_UI_SERVICES)
    void handleTranslation(const WebCore::TranslationContextMenuInfo&) final;
#endif

#if PLATFORM(GTK)
    void insertEmoji(WebCore::LocalFrame&) override;
#endif

#if USE(ACCESSIBILITY_CONTEXT_MENUS)
    void showContextMenu() override;
#endif

    RefPtr<WebPage> protectedPage() const;

    WeakPtr<WebPage> m_page;
};

} // namespace WebKit

#endif // ENABLE(CONTEXT_MENUS)
#endif // WebContextMenuClient_h
