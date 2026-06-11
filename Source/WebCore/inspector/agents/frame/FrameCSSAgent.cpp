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

#include "config.h"
#include "FrameCSSAgent.h"

#include "CSSImportRule.h"
#include "CSSParserContext.h"
#include "CSSProperty.h"
#include "CSSPropertyNames.h"
#include "CSSPropertyParserState.h"
#include "CSSPropertyParsing.h"
#include "CSSStyleSheet.h"
#include "CSSValueKeywords.h"
#include "CommonAtomStrings.h"
#include "ContentSecurityPolicy.h"
#include "Document.h"
#include "HTMLHeadElement.h"
#include "HTMLStyleElement.h"
#include "InspectorDOMAgent.h"
#include "InspectorIdentifierRegistry.h"
#include "InstrumentingAgents.h"
#include "LocalFrame.h"
#include "Page.h"
#include "PageInspectorController.h"
#include "StyleDocumentScope.h"
#include "StylePropertyShorthand.h"
#include "StyleScope.h"
#include "StyleSheetContents.h"
#include <JavaScriptCore/InspectorFrontendDispatchers.h>
#include <JavaScriptCore/InspectorProtocolObjects.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(FrameCSSAgent);

FrameCSSAgent::FrameCSSAgent(FrameAgentContext& context)
    : InspectorAgentBase("CSS"_s, context)
    , m_frontendDispatcher(makeUniqueRef<Inspector::CSSFrontendDispatcher>(context.frontendRouter))
    , m_backendDispatcher(Inspector::CSSBackendDispatcher::create(Ref { context.backendDispatcher }, this))
    , m_inspectedFrame(context.inspectedFrame)
{
}

FrameCSSAgent::~FrameCSSAgent() = default;

void FrameCSSAgent::didCreateFrontendAndBackend()
{
}

void FrameCSSAgent::willDestroyFrontendAndBackend(Inspector::DisconnectReason)
{
    disable();
}

Inspector::CommandResult<void> FrameCSSAgent::enable()
{
    Ref agents = m_instrumentingAgents.get();
    if (agents->enabledFrameCSSAgent() == this)
        return { };

    agents->setEnabledFrameCSSAgent(this);

    if (RefPtr document = m_inspectedFrame->document())
        activeStyleSheetsUpdated(*document);

    return { };
}

Inspector::CommandResult<void> FrameCSSAgent::disable()
{
    Ref { m_instrumentingAgents.get() }->setEnabledFrameCSSAgent(nullptr);

    reset();

    return { };
}

Inspector::CommandResult<Ref<JSON::ArrayOf<Inspector::Protocol::CSS::CSSComputedStyleProperty>>> FrameCSSAgent::getComputedStyleForNode(Inspector::Protocol::DOM::NodeId)
{
    // FIXME: <https://webkit.org/b/314424>: Implement nodeId-dependent CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<Ref<Inspector::Protocol::CSS::Font>> FrameCSSAgent::getFontDataForNode(Inspector::Protocol::DOM::NodeId)
{
    // FIXME: <https://webkit.org/b/314424>: Implement nodeId-dependent CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResultOf<RefPtr<Inspector::Protocol::CSS::CSSStyle>, RefPtr<Inspector::Protocol::CSS::CSSStyle>> FrameCSSAgent::getInlineStylesForNode(Inspector::Protocol::DOM::NodeId)
{
    // FIXME: <https://webkit.org/b/314424>: Implement nodeId-dependent CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResultOf<RefPtr<JSON::ArrayOf<Inspector::Protocol::CSS::RuleMatch>>, RefPtr<JSON::ArrayOf<Inspector::Protocol::CSS::PseudoIdMatches>>, RefPtr<JSON::ArrayOf<Inspector::Protocol::CSS::InheritedStyleEntry>>> FrameCSSAgent::getMatchedStylesForNode(Inspector::Protocol::DOM::NodeId, std::optional<bool>&&, std::optional<bool>&&)
{
    // FIXME: <https://webkit.org/b/314424>: Implement nodeId-dependent CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<Ref<JSON::ArrayOf<Inspector::Protocol::CSS::CSSStyleSheetHeader>>> FrameCSSAgent::getAllStyleSheets()
{
    auto headers = JSON::ArrayOf<Inspector::Protocol::CSS::CSSStyleSheetHeader>::create();

    RefPtr document = m_inspectedFrame->document();
    if (!document)
        return headers;

    Vector<CSSStyleSheet*> cssStyleSheets;
    collectAllDocumentStyleSheets(*document, cssStyleSheets);

    for (RefPtr cssStyleSheet : cssStyleSheets) {
        Ref inspectorStyleSheet = bindStyleSheet(cssStyleSheet.get());
        if (auto header = inspectorStyleSheet->buildObjectForStyleSheetInfo())
            headers->addItem(header.releaseNonNull());
    }

    return headers;
}

Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSStyleSheetBody>> FrameCSSAgent::getStyleSheet(const Inspector::Protocol::CSS::StyleSheetId& styleSheetId)
{
    Inspector::Protocol::ErrorString errorString;

    RefPtr inspectorStyleSheet = assertStyleSheetForId(errorString, styleSheetId);
    if (!inspectorStyleSheet)
        return makeUnexpected(errorString);

    auto styleSheet = inspectorStyleSheet->buildObjectForStyleSheet();
    if (!styleSheet)
        return makeUnexpected("Internal error: missing style sheet"_s);

    return styleSheet.releaseNonNull();
}

Inspector::CommandResult<String> FrameCSSAgent::getStyleSheetText(const Inspector::Protocol::CSS::StyleSheetId& styleSheetId)
{
    Inspector::Protocol::ErrorString errorString;

    RefPtr inspectorStyleSheet = assertStyleSheetForId(errorString, styleSheetId);
    if (!inspectorStyleSheet)
        return makeUnexpected(errorString);

    auto text = inspectorStyleSheet->text();
    if (text.hasException())
        return makeUnexpected(InspectorDOMAgent::toErrorString(text.releaseException()));

    return text.releaseReturnValue();
}

Inspector::CommandResult<void> FrameCSSAgent::setStyleSheetText(const Inspector::Protocol::CSS::StyleSheetId& styleSheetId, const String& text)
{
    Inspector::Protocol::ErrorString errorString;

    RefPtr inspectorStyleSheet = assertStyleSheetForId(errorString, styleSheetId);
    if (!inspectorStyleSheet)
        return makeUnexpected(errorString);

    // FIXME <https://webkit.org/b/314244>: Integrate with InspectorHistory for undo/redo support.
    auto result = inspectorStyleSheet->setText(text);
    if (result.hasException())
        return makeUnexpected(InspectorDOMAgent::toErrorString(result.releaseException()));

    inspectorStyleSheet->reparseStyleSheet(text);
    return { };
}

Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSStyle>> FrameCSSAgent::setStyleText(Ref<JSON::Object>&&, const String&)
{
    // FIXME: <https://webkit.org/b/316843>: Implement style/rule editing CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSRule>> FrameCSSAgent::setRuleSelector(Ref<JSON::Object>&&, const String&)
{
    // FIXME: <https://webkit.org/b/316843>: Implement style/rule editing CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<Ref<Inspector::Protocol::CSS::Grouping>> FrameCSSAgent::setGroupingHeaderText(Ref<JSON::Object>&&, const String&)
{
    // FIXME: <https://webkit.org/b/316843>: Implement style/rule editing CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<Inspector::Protocol::CSS::StyleSheetId> FrameCSSAgent::createStyleSheet(const Inspector::Protocol::Network::FrameId&)
{
    RefPtr document = m_inspectedFrame->document();
    if (!document)
        return makeUnexpected("No document for frame"_s);

    RefPtr inspectorStyleSheet = createInspectorStyleSheetForDocument(*document);
    if (!inspectorStyleSheet)
        return makeUnexpected("Could not create stylesheet for document"_s);

    return inspectorStyleSheet->id();
}

Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSRule>> FrameCSSAgent::addRule(const Inspector::Protocol::CSS::StyleSheetId&, const String&)
{
    // FIXME: <https://webkit.org/b/316843>: Implement style/rule editing CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<Ref<JSON::ArrayOf<Inspector::Protocol::CSS::CSSPropertyInfo>>> FrameCSSAgent::getSupportedCSSProperties()
{
    RefPtr page = m_inspectedFrame->page();
    if (!page)
        return makeUnexpected("No page for frame"_s);

    auto cssProperties = JSON::ArrayOf<Inspector::Protocol::CSS::CSSPropertyInfo>::create();

    auto& settings = page->settings();
    auto parserContext = CSSParserContext { settings };

    for (auto propertyID : allCSSProperties()) {
        if (!isExposed(propertyID, &settings))
            continue;

        auto property = Inspector::Protocol::CSS::CSSPropertyInfo::create()
            .setName(nameString(propertyID))
            .release();

        auto aliases = CSSProperty::aliasesForProperty(propertyID);
        if (!aliases.isEmpty()) {
            auto aliasesArray = JSON::ArrayOf<String>::create();
            for (auto& alias : aliases)
                aliasesArray->addItem(alias);
            property->setAliases(WTF::move(aliasesArray));
        }

        auto shorthand = shorthandForProperty(propertyID);
        if (shorthand.length()) {
            auto longhands = JSON::ArrayOf<String>::create();
            for (auto longhand : shorthand) {
                if (isExposed(longhand, &settings))
                    longhands->addItem(nameString(longhand));
            }
            if (longhands->length())
                property->setLonghands(WTF::move(longhands));
        }

        if (CSSPropertyParsing::isKeywordFastPathEligibleStyleProperty(propertyID)) {
            auto propertyParserState = CSS::PropertyParserState {
                .context = parserContext,
            };
            auto values = JSON::ArrayOf<String>::create();
            for (auto valueID : allCSSValueKeywords()) {
                if (CSSPropertyParsing::isKeywordValidForStyleProperty(propertyID, valueID, propertyParserState))
                    values->addItem(nameString(valueID));
            }
            if (values->length())
                property->setValues(WTF::move(values));
        } else {
            auto validKeywords = CSSProperty::validKeywordsForProperty(propertyID);
            if (!validKeywords.empty()) {
                auto values = JSON::ArrayOf<String>::create();
                for (auto valueID : validKeywords) {
                    if (CSSProperty::isKeywordValidForPropertyValues(propertyID, valueID, parserContext))
                        values->addItem(nameString(valueID));
                }
                if (values->length())
                    property->setValues(WTF::move(values));
            }
        }

        if (CSSProperty::isInheritedProperty(propertyID))
            property->setInherited(true);

        cssProperties->addItem(WTF::move(property));
    }

    return cssProperties;
}

Inspector::CommandResult<Ref<JSON::ArrayOf<String>>> FrameCSSAgent::getSupportedSystemFontFamilyNames()
{
    // FIXME: <https://webkit.org/b/316844>: Consider how to deal with global CSS commands.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<void> FrameCSSAgent::forcePseudoState(Inspector::Protocol::DOM::NodeId, Ref<JSON::Array>&&)
{
    // FIXME: <https://webkit.org/b/314424>: Implement nodeId-dependent CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<void> FrameCSSAgent::setLayoutContextTypeChangedMode(Inspector::Protocol::CSS::LayoutContextTypeChangedMode)
{
    // FIXME: <https://webkit.org/b/316844>: Consider how to deal with global CSS commands.
    return makeUnexpected("Not supported on frame targets"_s);
}

void FrameCSSAgent::styleSheetChanged(InspectorStyleSheet* inspectorStyleSheet)
{
    if (!inspectorStyleSheet)
        return;

    m_frontendDispatcher->styleSheetChanged(inspectorStyleSheet->id());
}

void FrameCSSAgent::documentDetached(Document& document)
{
    Vector<CSSStyleSheet*> emptyList;
    setActiveStyleSheetsForDocument(document, emptyList);
    m_documentToKnownCSSStyleSheets.remove(&document);
}

void FrameCSSAgent::mediaQueryResultChanged()
{
    m_frontendDispatcher->mediaQueryResultChanged();
}

void FrameCSSAgent::activeStyleSheetsUpdated(Document& document)
{
    Vector<CSSStyleSheet*> cssStyleSheets;
    collectAllDocumentStyleSheets(document, cssStyleSheets);
    setActiveStyleSheetsForDocument(document, cssStyleSheets);
}

void FrameCSSAgent::reset()
{
    m_idToInspectorStyleSheet.clear();
    m_cssStyleSheetToInspectorStyleSheet.clear();
    m_documentToInspectorStyleSheet.clear();
    m_documentToKnownCSSStyleSheets.clear();
}

InspectorStyleSheet& FrameCSSAgent::bindStyleSheet(CSSStyleSheet* styleSheet)
{
    auto it = m_cssStyleSheetToInspectorStyleSheet.find(styleSheet);
    if (it != m_cssStyleSheetToInspectorStyleSheet.end())
        return it->value;

    auto id = String::number(m_lastStyleSheetId++);
    RefPtr document = styleSheet->ownerDocument();
    Ref inspectorStyleSheet = InspectorStyleSheet::create(protect(m_inspectedFrame->page()->inspectorController().identifierRegistry()), id, styleSheet, detectOrigin(styleSheet, document.get()), InspectorDOMAgent::documentURLString(document.get()), this);
    m_idToInspectorStyleSheet.set(id, inspectorStyleSheet);
    if (m_creatingViaInspectorStyleSheet && document)
        m_documentToInspectorStyleSheet.add(document.releaseNonNull(), Vector<Ref<InspectorStyleSheet>>()).iterator->value.append(inspectorStyleSheet);
    return m_cssStyleSheetToInspectorStyleSheet.set(styleSheet, WTF::move(inspectorStyleSheet)).iterator->value;
}

InspectorStyleSheet* FrameCSSAgent::assertStyleSheetForId(Inspector::Protocol::ErrorString& errorString, const String& styleSheetId)
{
    auto it = m_idToInspectorStyleSheet.find(styleSheetId);
    if (it == m_idToInspectorStyleSheet.end()) {
        errorString = "Missing style sheet for given styleSheetId"_s;
        return nullptr;
    }
    return it->value.ptr();
}

void FrameCSSAgent::collectAllDocumentStyleSheets(Document& document, Vector<CSSStyleSheet*>& result)
{
    auto cssStyleSheets = document.styleScope().activeStyleSheetsForInspector();
    for (auto& cssStyleSheet : cssStyleSheets)
        collectStyleSheets(cssStyleSheet.ptr(), result);
}

void FrameCSSAgent::collectStyleSheets(CSSStyleSheet* styleSheet, Vector<CSSStyleSheet*>& result)
{
    result.append(styleSheet);

    for (unsigned i = 0, size = styleSheet->length(); i < size; ++i) {
        if (RefPtr rule = dynamicDowncast<CSSImportRule>(styleSheet->item(i))) {
            if (RefPtr importedStyleSheet = rule->styleSheet())
                collectStyleSheets(importedStyleSheet.get(), result);
        }
    }
}

void FrameCSSAgent::setActiveStyleSheetsForDocument(Document& document, Vector<CSSStyleSheet*>& activeStyleSheets)
{
    HashSet<CSSStyleSheet*>& previouslyKnownActiveStyleSheets = m_documentToKnownCSSStyleSheets.add(&document, HashSet<CSSStyleSheet*>()).iterator->value;

    HashSet<CSSStyleSheet*> removedStyleSheets(previouslyKnownActiveStyleSheets);
    Vector<CSSStyleSheet*> addedStyleSheets;
    for (auto& activeStyleSheet : activeStyleSheets) {
        if (removedStyleSheets.contains(activeStyleSheet))
            removedStyleSheets.remove(activeStyleSheet);
        else
            addedStyleSheets.append(activeStyleSheet);
    }

    for (RefPtr cssStyleSheet : removedStyleSheets) {
        previouslyKnownActiveStyleSheets.remove(cssStyleSheet.get());
        RefPtr<InspectorStyleSheet> inspectorStyleSheet = m_cssStyleSheetToInspectorStyleSheet.get(cssStyleSheet.get());
        if (inspectorStyleSheet && m_idToInspectorStyleSheet.contains(inspectorStyleSheet->id())) {
            auto id = inspectorStyleSheet->id();
            m_idToInspectorStyleSheet.remove(id);
            m_cssStyleSheetToInspectorStyleSheet.remove(cssStyleSheet.get());
            m_frontendDispatcher->styleSheetRemoved(id);
        }
    }

    for (RefPtr cssStyleSheet : addedStyleSheets) {
        previouslyKnownActiveStyleSheets.add(cssStyleSheet.get());
        if (!m_cssStyleSheetToInspectorStyleSheet.contains(cssStyleSheet.get())) {
            Ref inspectorStyleSheet = bindStyleSheet(cssStyleSheet.get());
            if (auto header = inspectorStyleSheet->buildObjectForStyleSheetInfo())
                m_frontendDispatcher->styleSheetAdded(header.releaseNonNull());
        }
    }
}

InspectorStyleSheet* FrameCSSAgent::createInspectorStyleSheetForDocument(Document& document)
{
    if (!document.isHTMLDocument() && !document.isSVGDocument())
        return nullptr;

    auto styleElement = HTMLStyleElement::create(document);
    styleElement->setAttributeWithoutSynchronization(HTMLNames::typeAttr, cssContentTypeAtom());

    ContainerNode* targetNode;
    if (auto* head = document.head())
        targetNode = head;
    else if (auto* body = document.bodyOrFrameset())
        targetNode = body;
    else
        return nullptr;

    m_creatingViaInspectorStyleSheet = true;
    document.contentSecurityPolicy()->setOverrideAllowInlineStyle(true);
    auto appendResult = targetNode->appendChild(styleElement);
    document.styleScope().flushPendingUpdate();
    document.contentSecurityPolicy()->setOverrideAllowInlineStyle(false);
    m_creatingViaInspectorStyleSheet = false;
    if (appendResult.hasException())
        return nullptr;

    auto iterator = m_documentToInspectorStyleSheet.find(document);
    ASSERT(iterator != m_documentToInspectorStyleSheet.end());
    if (iterator == m_documentToInspectorStyleSheet.end())
        return nullptr;

    auto& inspectorStyleSheetsForDocument = iterator->value;
    ASSERT(!inspectorStyleSheetsForDocument.isEmpty());
    if (inspectorStyleSheetsForDocument.isEmpty())
        return nullptr;

    return inspectorStyleSheetsForDocument.last().ptr();
}

Inspector::Protocol::CSS::StyleSheetOrigin FrameCSSAgent::detectOrigin(CSSStyleSheet* pageStyleSheet, Document* ownerDocument)
{
    if (m_creatingViaInspectorStyleSheet)
        return Inspector::Protocol::CSS::StyleSheetOrigin::Inspector;

    if (pageStyleSheet && !pageStyleSheet->ownerNode() && pageStyleSheet->href().isEmpty())
        return Inspector::Protocol::CSS::StyleSheetOrigin::UserAgent;

    if (pageStyleSheet && pageStyleSheet->contents().isUserStyleSheet())
        return Inspector::Protocol::CSS::StyleSheetOrigin::User;

    if (!ownerDocument)
        return Inspector::Protocol::CSS::StyleSheetOrigin::Author;

    auto iterator = m_documentToInspectorStyleSheet.find(*ownerDocument);
    if (iterator != m_documentToInspectorStyleSheet.end()) {
        for (auto& inspectorStyleSheet : iterator->value) {
            if (inspectorStyleSheet->pageStyleSheet() == pageStyleSheet)
                return Inspector::Protocol::CSS::StyleSheetOrigin::Inspector;
        }
    }

    return Inspector::Protocol::CSS::StyleSheetOrigin::Author;
}

} // namespace WebCore
