/*
 * Copyright (C) 2007-2025 Apple Inc. All rights reserved.
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

#pragma once

#include "ActiveDOMObject.h"
#include "CSSFontFace.h"
#include "CSSFontFaceSet.h"
#include "CachedResourceHandle.h"
#include "Font.h"
#include "FontSelector.h"
#include "ScriptExecutionContext.h"
#include "StyleRule.h"
#include "Timer.h"
#include "WebKitFontFamilyNames.h"
#include <memory>
#include <wtf/Forward.h>
#include <wtf/HashSet.h>
#include <wtf/RefPtr.h>
#include <wtf/text/StringHash.h>

namespace WebCore {

class CSSPrimitiveValue;
class CSSSegmentedFontFace;
class CSSValueList;
class CachedFont;
class ScriptExecutionContext;

class CSSFontSelector final : public FontSelector, public CSSFontFaceClient, public ActiveDOMObject {
public:
    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

    USING_CAN_MAKE_WEAKPTR(FontSelector);

    using FontSelector::ref;
    using FontSelector::deref;

    static Ref<CSSFontSelector> create(ScriptExecutionContext&);
    virtual ~CSSFontSelector();

    unsigned version() const final { return m_version; }
    unsigned uniqueId() const final { return m_uniqueId; }

    FontRanges fontRangesForFamily(const FontDescription&, const AtomString&) final;
    size_t fallbackFontCount() final;
    RefPtr<Font> fallbackFontAt(const FontDescription&, size_t) final;

    void clearFonts();
    void emptyCaches();
    void buildStarted();
    void buildCompleted();

    void addFontFaceRule(StyleRuleFontFace&, bool isInitiatingElementInUserAgentShadowTree);
    void addFontPaletteValuesRule(const StyleRuleFontPaletteValues&);
    void addFontFeatureValuesRule(const StyleRuleFontFeatureValues&);

    void fontCacheInvalidated() final;

    bool isEmpty() const;

    void registerForInvalidationCallbacks(FontSelectorClient&) final;
    void unregisterForInvalidationCallbacks(FontSelectorClient&) final;

    bool isSimpleFontSelectorForDescription() const final;

    bool isCSSFontSelector() const final { return true; }

    ScriptExecutionContext* scriptExecutionContext() const { return m_context.get(); }
    Ref<ScriptExecutionContext> protectedScriptExecutionContext() const { return *m_context; }

    FontFaceSet* fontFaceSetIfExists();
    FontFaceSet& fontFaceSet();
    CSSFontFaceSet& cssFontFaceSet() { return m_cssFontFaceSet; }

    void incrementIsComputingRootStyleFont() { ++m_computingRootStyleFontCount; }
    void decrementIsComputingRootStyleFont() { --m_computingRootStyleFontCount; }

    void loadPendingFonts();

    void updateStyleIfNeeded();

private:
    explicit CSSFontSelector(ScriptExecutionContext&);

    void dispatchInvalidationCallbacks();

    void opportunisticallyStartFontDataURLLoading(const FontCascadeDescription&, const AtomString& family) final;

    std::optional<AtomString> resolveGenericFamily(const FontDescription&, const AtomString& family);

    const FontPaletteValues& lookupFontPaletteValues(const AtomString& familyName, const FontDescription&) const;
    RefPtr<FontFeatureValues> lookupFontFeatureValues(const AtomString& familyName) const;

    // CSSFontFaceClient
    void fontLoaded(CSSFontFace&) final;
    void updateStyleIfNeeded(CSSFontFace&) final;

    void fontModified();

    struct PendingFontFaceRule {
        const Ref<StyleRuleFontFace> styleRuleFontFace;
        bool isInitiatingElementInUserAgentShadowTree;
    };
    Vector<PendingFontFaceRule> m_stagingArea;

    WeakPtr<ScriptExecutionContext> m_context;
    RefPtr<FontFaceSet> m_fontFaceSet;
    const Ref<CSSFontFaceSet> m_cssFontFaceSet;
    HashSet<FontSelectorClient*> m_clients;

    struct PaletteMapHash : DefaultHash<std::pair<AtomString, AtomString>> {
        static unsigned hash(const std::pair<AtomString, AtomString>& key)
        {
            return pairIntHash(ASCIICaseInsensitiveHash::hash(key.first), DefaultHash<AtomString>::hash(key.second));
        }

        static bool equal(const std::pair<AtomString, AtomString>& a, const std::pair<AtomString, AtomString>& b)
        {
            return ASCIICaseInsensitiveHash::equal(a.first, b.first) && DefaultHash<AtomString>::equal(a.second, b.second);
        }
    };
    HashMap<std::pair<AtomString, AtomString>, FontPaletteValues, PaletteMapHash> m_paletteMap;
    HashMap<String, Ref<FontFeatureValues>> m_featureValues;

    HashSet<RefPtr<CSSFontFace>> m_cssConnectionsPossiblyToRemove;
    HashSet<RefPtr<StyleRuleFontFace>> m_cssConnectionsEncounteredDuringBuild;

    CSSFontFaceSet::FontModifiedObserver m_fontModifiedObserver;

    unsigned m_uniqueId;
    unsigned m_version;
    unsigned m_computingRootStyleFontCount { 0 };
    bool m_creatingFont { false };
    bool m_buildIsUnderway { false };
    bool m_isStopped { false };

    WebKitFontFamilyNames::FamilyNamesList<AtomString> m_fontFamilyNames;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::CSSFontSelector)
    static bool isType(const WebCore::FontSelector& selector) { return selector.isCSSFontSelector(); }
SPECIALIZE_TYPE_TRAITS_END()
