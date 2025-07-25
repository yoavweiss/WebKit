/*
 * Copyright (C) 2020 Igalia S.L.
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
#include "WebContentReader.h"

#if PLATFORM(GTK) || PLATFORM(WPE)
#include "Blob.h"
#include "BlobURL.h"
#include "DOMURL.h"
#include "Document.h"
#include "DocumentFragment.h"
#include "Editor.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "Page.h"
#include "Settings.h"
#include "markup.h"
#include <wtf/URL.h>

namespace WebCore {

bool WebContentReader::readFilePath(const String& path, PresentationSize, const String&)
{
    if (path.isEmpty() || !frame().document())
        return false;

    auto markup = urlToMarkup(URL({ }, path), path);
    addFragment(createFragmentFromMarkup(*frame().protectedDocument(), markup, "file://"_s, { }));

    return true;
}

bool WebContentReader::readHTML(const String& string)
{
    if (frame().settings().preferMIMETypeForImages() || !frame().document())
        return false;

    addFragment(createFragmentFromMarkup(*frame().protectedDocument(), string, emptyString(), { }));
    return true;
}

bool WebContentReader::readPlainText(const String& text)
{
    if (!m_allowPlainText)
        return false;

    addFragment(createFragmentFromText(m_context, text));

    m_madeFragmentFromPlainText = true;
    return true;
}

bool WebContentReader::readImage(Ref<FragmentedSharedBuffer>&& buffer, const String& type, PresentationSize preferredPresentationSize)
{
    ASSERT(frame().document());
    Ref document = *frame().document();
    addFragment(createFragmentForImageAndURL(document, DOMURL::createObjectURL(document, Blob::create(document.ptr(), buffer->extractData(), type)), preferredPresentationSize));

    return m_fragment;
}

bool WebContentReader::readURL(const URL&, const String&)
{
    return false;
}

static bool shouldReplaceSubresourceURL(const URL& url)
{
    return !(url.protocolIsInHTTPFamily() || url.protocolIsData());
}

bool WebContentMarkupReader::readHTML(const String& string)
{
    if (!frame().document())
        return false;

    if (shouldSanitize()) {
        m_markup = sanitizeMarkup(string, MSOListQuirks::Disabled, Function<void(DocumentFragment&)> { [](DocumentFragment& fragment) {
            removeSubresourceURLAttributes(fragment, [](const URL& url) {
                return shouldReplaceSubresourceURL(url);
            });
        } });
    } else
        m_markup = string;

    return !m_markup.isEmpty();
}

} // namespace WebCore

#endif // PLATFORM(GTK) || PLATFORM(WPE)
