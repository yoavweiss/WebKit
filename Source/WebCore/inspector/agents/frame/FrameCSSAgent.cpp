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

#include "InstrumentingAgents.h"
#include "LocalFrame.h"
#include <JavaScriptCore/InspectorFrontendDispatchers.h>
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

    return { };
}

Inspector::CommandResult<void> FrameCSSAgent::disable()
{
    Ref { m_instrumentingAgents.get() }->setEnabledFrameCSSAgent(nullptr);

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
    // FIXME: <https://webkit.org/b/314147>: Implement per-frame CSS functions without DOM or node id support.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSStyleSheetBody>> FrameCSSAgent::getStyleSheet(const Inspector::Protocol::CSS::StyleSheetId&)
{
    // FIXME: <https://webkit.org/b/314147>: Implement per-frame CSS functions without DOM or node id support.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<String> FrameCSSAgent::getStyleSheetText(const Inspector::Protocol::CSS::StyleSheetId&)
{
    // FIXME: <https://webkit.org/b/314147>: Implement per-frame CSS functions without DOM or node id support.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<void> FrameCSSAgent::setStyleSheetText(const Inspector::Protocol::CSS::StyleSheetId&, const String&)
{
    // FIXME: <https://webkit.org/b/314147>: Implement per-frame CSS functions without DOM or node id support.
    return makeUnexpected("Not supported on frame targets"_s);
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
    // FIXME: <https://webkit.org/b/314147>: Implement per-frame CSS functions without DOM or node id support.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<Ref<Inspector::Protocol::CSS::CSSRule>> FrameCSSAgent::addRule(const Inspector::Protocol::CSS::StyleSheetId&, const String&)
{
    // FIXME: <https://webkit.org/b/316843>: Implement style/rule editing CSS commands for frame targets.
    return makeUnexpected("Not supported on frame targets"_s);
}

Inspector::CommandResult<Ref<JSON::ArrayOf<Inspector::Protocol::CSS::CSSPropertyInfo>>> FrameCSSAgent::getSupportedCSSProperties()
{
    // FIXME: <https://webkit.org/b/314147>: Implement per-frame CSS functions without DOM or node id support.
    return makeUnexpected("Not supported on frame targets"_s);
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

} // namespace WebCore
