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

#include "InspectorStyleSheet.h"
#include "InspectorWebAgentBase.h"
#include <JavaScriptCore/InspectorBackendDispatchers.h>
#include <wtf/CheckedPtr.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakRef.h>

namespace Inspector {
class CSSFrontendDispatcher;
}

namespace WebCore {

class CSSStyleSheet;
class Document;
class LocalFrame;

class FrameCSSAgent final : public InspectorAgentBase, public Inspector::CSSBackendDispatcherHandler, public InspectorStyleSheet::Listener, public CanMakeCheckedPtr<FrameCSSAgent> {
    WTF_MAKE_NONCOPYABLE(FrameCSSAgent);
    WTF_MAKE_TZONE_ALLOCATED(FrameCSSAgent);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(FrameCSSAgent);
public:
    FrameCSSAgent(FrameAgentContext&);
    ~FrameCSSAgent();

    // InspectorAgentBase
    void didCreateFrontendAndBackend() override;
    void willDestroyFrontendAndBackend(Inspector::DisconnectReason) override;

    // CSSBackendDispatcherHandler
    Inspector::CommandResult<void> enable() override;
    Inspector::CommandResult<void> disable() override;
    Inspector::CommandResult<Ref<JSON::ArrayOf<Inspector::Protocol::CSS::CSSComputedStyleProperty>>> getComputedStyleForNode(Inspector::Protocol::DOM::NodeId) override;
    Inspector::CommandResult<Ref<Inspector::Protocol::CSS::Font>> getFontDataForNode(Inspector::Protocol::DOM::NodeId) override;
    Inspector::CommandResultOf<RefPtr<Inspector::Protocol::CSS::CSSStyle>, RefPtr<Inspector::Protocol::CSS::CSSStyle>> getInlineStylesForNode(Inspector::Protocol::DOM::NodeId) override;
    Inspector::CommandResultOf<RefPtr<JSON::ArrayOf<Inspector::Protocol::CSS::RuleMatch>>, RefPtr<JSON::ArrayOf<Inspector::Protocol::CSS::PseudoIdMatches>>, RefPtr<JSON::ArrayOf<Inspector::Protocol::CSS::InheritedStyleEntry>>> getMatchedStylesForNode(Inspector::Protocol::DOM::NodeId, std::optional<bool>&& includePseudo, std::optional<bool>&& includeInherited) override;
    Inspector::CommandResult<Ref<JSON::ArrayOf<Inspector::Protocol::CSS::CSSStyleSheetHeader>>> getAllStyleSheets() override;
    Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSStyleSheetBody>> getStyleSheet(const Inspector::Protocol::CSS::StyleSheetId&) override;
    Inspector::CommandResult<String> getStyleSheetText(const Inspector::Protocol::CSS::StyleSheetId&) override;
    Inspector::CommandResult<void> setStyleSheetText(const Inspector::Protocol::CSS::StyleSheetId&, const String& text) override;
    Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSStyle>> setStyleText(Ref<JSON::Object>&& styleId, const String& text) override;
    Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSRule>> setRuleSelector(Ref<JSON::Object>&& ruleId, const String& selector) override;
    Inspector::CommandResult<Ref<Inspector::Protocol::CSS::Grouping>> setGroupingHeaderText(Ref<JSON::Object>&& ruleId, const String& headerText) override;
    Inspector::CommandResult<Inspector::Protocol::CSS::StyleSheetId> createStyleSheet(const Inspector::Protocol::Network::FrameId&) override;
    Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSRule>> addRule(const Inspector::Protocol::CSS::StyleSheetId&, const String& selector) override;
    Inspector::CommandResult<Ref<JSON::ArrayOf<Inspector::Protocol::CSS::CSSPropertyInfo>>> getSupportedCSSProperties() override;
    Inspector::CommandResult<Ref<JSON::ArrayOf<String>>> getSupportedSystemFontFamilyNames() override;
    Inspector::CommandResult<void> forcePseudoState(Inspector::Protocol::DOM::NodeId, Ref<JSON::Array>&& forcedPseudoClasses) override;
    Inspector::CommandResult<void> setLayoutContextTypeChangedMode(Inspector::Protocol::CSS::LayoutContextTypeChangedMode) override;

    // InspectorStyleSheet::Listener
    void styleSheetChanged(InspectorStyleSheet*) override;

    // InspectorInstrumentation
    void documentDetached(Document&);
    void mediaQueryResultChanged();
    void activeStyleSheetsUpdated(Document&);

private:
    void reset();
    InspectorStyleSheet& bindStyleSheet(CSSStyleSheet*);
    InspectorStyleSheet* assertStyleSheetForId(Inspector::Protocol::ErrorString&, const Inspector::Protocol::CSS::StyleSheetId&);
    void collectAllDocumentStyleSheets(Document&, Vector<CSSStyleSheet*>&);
    void collectStyleSheets(CSSStyleSheet*, Vector<CSSStyleSheet*>&);
    void setActiveStyleSheetsForDocument(Document&, Vector<CSSStyleSheet*>&);
    InspectorStyleSheet* createInspectorStyleSheetForDocument(Document&);
    Inspector::Protocol::CSS::StyleSheetOrigin detectOrigin(CSSStyleSheet*, Document*);

    UniqueRef<Inspector::CSSFrontendDispatcher> m_frontendDispatcher;
    Ref<Inspector::CSSBackendDispatcher> m_backendDispatcher;
    WeakRef<LocalFrame> m_inspectedFrame;

    HashMap<Inspector::Protocol::CSS::StyleSheetId, Ref<InspectorStyleSheet>> m_idToInspectorStyleSheet;
    HashMap<CSSStyleSheet*, Ref<InspectorStyleSheet>> m_cssStyleSheetToInspectorStyleSheet;
    HashMap<Ref<Document>, Vector<Ref<InspectorStyleSheet>>> m_documentToInspectorStyleSheet;
    HashMap<Document*, HashSet<CSSStyleSheet*>> m_documentToKnownCSSStyleSheets;
    int m_lastStyleSheetId { 1 };
    bool m_creatingViaInspectorStyleSheet { false };
};

} // namespace WebCore
