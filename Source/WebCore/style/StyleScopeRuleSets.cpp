/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 2004-2005 Allan Sandfeld Jensen (kde@carewolf.com)
 * Copyright (C) 2006, 2007 Nicholas Shanks (webkit@nickshanks.com)
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Alexey Proskuryakov <ap@webkit.org>
 * Copyright (C) 2007, 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 * Copyright (C) Research In Motion Limited 2011. All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "StyleScopeRuleSets.h"

#include "CSSPropertyParser.h"
#include "CSSStyleSheet.h"
#include "CSSViewTransitionRule.h"
#include "CascadeLevel.h"
#include "DocumentInlines.h"
#include "ExtensionStyleSheets.h"
#include "FrameLoader.h"
#include "HTMLNames.h"
#include "LocalFrame.h"
#include "LocalFrameLoaderClient.h"
#include "MediaQueryEvaluator.h"
#include "Page.h"
#include "RuleSetBuilder.h"
#include "StyleResolver.h"
#include "StyleScope.h"
#include "StyleSheetContents.h"

namespace WebCore {
namespace Style {

ScopeRuleSets::ScopeRuleSets(Resolver& styleResolver)
    : m_authorStyle(RuleSet::create())
    , m_styleResolver(styleResolver)
{
}

ScopeRuleSets::~ScopeRuleSets()
{
    RELEASE_ASSERT(!m_isInvalidatingStyleWithRuleSets);
}

RuleSet* ScopeRuleSets::userAgentMediaQueryStyle() const
{
    updateUserAgentMediaQueryStyleIfNeeded();
    return m_userAgentMediaQueryStyle.get();
}

void ScopeRuleSets::updateUserAgentMediaQueryStyleIfNeeded() const
{
    if (!UserAgentStyle::mediaQueryStyleSheet)
        return;

    auto ruleCount = UserAgentStyle::mediaQueryStyleSheet->ruleCount();
    if (m_userAgentMediaQueryStyle && ruleCount == m_userAgentMediaQueryRuleCountOnUpdate)
        return;
    m_userAgentMediaQueryRuleCountOnUpdate = ruleCount;

    // Media queries on user agent sheet need to evaluated in document context. They behave like author sheets in this respect.
    auto& mediaQueryEvaluator = m_styleResolver.mediaQueryEvaluator();

    m_userAgentMediaQueryStyle = RuleSet::create();

    RuleSetBuilder builder(*m_userAgentMediaQueryStyle, mediaQueryEvaluator, &m_styleResolver);
    builder.addRulesFromSheet(*UserAgentStyle::mediaQueryStyleSheet);
}

RuleSet* ScopeRuleSets::dynamicViewTransitionsStyle() const
{
    return m_dynamicViewTransitionsStyle.get();
}

RuleSet* ScopeRuleSets::userStyle() const
{
    if (m_usesSharedUserStyle)
        return m_styleResolver.document().styleScope().resolver().ruleSets().userStyle();
    return m_userStyle.get();
}

RuleSet* ScopeRuleSets::styleForCascadeLevel(CascadeLevel level)
{
    switch (level) {
    case CascadeLevel::Author:
        return m_authorStyle.get();

    case CascadeLevel::User:
        return userStyle();

    case CascadeLevel::UserAgent:
        return userAgentMediaQueryStyle();
    }

    ASSERT_NOT_REACHED();
    return nullptr;
}

void ScopeRuleSets::initializeUserStyle()
{
    CheckedRef extensionStyleSheets = m_styleResolver.document().extensionStyleSheets();
    auto& mediaQueryEvaluator = m_styleResolver.mediaQueryEvaluator();

    auto userStyle = RuleSet::create();

    if (auto* pageUserSheet = extensionStyleSheets->pageUserSheet()) {
        RuleSetBuilder builder(userStyle, mediaQueryEvaluator, &m_styleResolver);
        builder.addRulesFromSheet(pageUserSheet->contents());
    }

#if ENABLE(APP_BOUND_DOMAINS)
    auto* page = m_styleResolver.document().page();
    auto* localMainFrame = page ? dynamicDowncast<LocalFrame>(page->mainFrame()) : nullptr;
    if (!extensionStyleSheets->injectedUserStyleSheets().isEmpty() && page && localMainFrame && localMainFrame->loader().client().shouldEnableInAppBrowserPrivacyProtections())
        m_styleResolver.document().addConsoleMessage(MessageSource::Security, MessageLevel::Warning, "Ignoring user style sheet for non-app bound domain."_s);
    else {
        collectRulesFromUserStyleSheets(extensionStyleSheets->injectedUserStyleSheets(), userStyle, mediaQueryEvaluator);
        if (page && localMainFrame && !extensionStyleSheets->injectedUserStyleSheets().isEmpty())
            localMainFrame->loader().client().notifyPageOfAppBoundBehavior();
    }
#else
    collectRulesFromUserStyleSheets(extensionStyleSheets->injectedUserStyleSheets(), userStyle, mediaQueryEvaluator);
#endif
    collectRulesFromUserStyleSheets(extensionStyleSheets->documentUserStyleSheets(), userStyle, mediaQueryEvaluator);

    if (userStyle->ruleCount() > 0 || userStyle->pageRules().size() > 0)
        m_userStyle = WTFMove(userStyle);
}

void ScopeRuleSets::collectRulesFromUserStyleSheets(const Vector<RefPtr<CSSStyleSheet>>& userSheets, RuleSet& userStyle, const MQ::MediaQueryEvaluator& mediaQueryEvaluator)
{
    RuleSetBuilder builder(userStyle, mediaQueryEvaluator, &m_styleResolver);
    for (auto& sheet : userSheets) {
        ASSERT(sheet->contents().isUserStyleSheet());
        builder.addRulesFromSheet(sheet->contents());
    }
}

template<typename Rules>
RefPtr<RuleSet> makeRuleSet(const Rules& rules)
{
    size_t size = rules.size();
    if (!size)
        return nullptr;
    auto ruleSet = RuleSet::create();
    for (size_t i = 0; i < size; ++i)
        ruleSet->addRule(*rules[i].styleRule, rules[i].selectorIndex, rules[i].selectorListIndex);
    ruleSet->shrinkToFit();
    return ruleSet;
}

void ScopeRuleSets::resetAuthorStyle()
{
    m_isAuthorStyleDefined = true;
    m_authorStyle = RuleSet::create();
}

void ScopeRuleSets::resetUserAgentMediaQueryStyle()
{
    m_userAgentMediaQueryStyle = nullptr;
}

bool ScopeRuleSets::hasViewportDependentMediaQueries() const
{
    if (m_authorStyle->hasViewportDependentMediaQueries())
        return true;
    if (m_userStyle && m_userStyle->hasViewportDependentMediaQueries())
        return true;
    if (m_userAgentMediaQueryStyle && m_userAgentMediaQueryStyle->hasViewportDependentMediaQueries())
        return true;

    return false;
}

bool ScopeRuleSets::hasContainerQueries() const
{
    if (m_authorStyle->hasContainerQueries())
        return true;
    if (m_userStyle && m_userStyle->hasContainerQueries())
        return true;
    if (m_userAgentMediaQueryStyle && m_userAgentMediaQueryStyle->hasContainerQueries())
        return true;

    return false;
}

bool ScopeRuleSets::hasScopeRules() const
{
    if (m_authorStyle->hasScopeRules())
        return true;
    if (m_userStyle && m_userStyle->hasScopeRules())
        return true;
    if (m_userAgentMediaQueryStyle && m_userAgentMediaQueryStyle->hasScopeRules())
        return true;

    return false;
}

RefPtr<StyleRuleViewTransition> ScopeRuleSets::viewTransitionRule() const
{
    if (auto viewTransitionRule = m_authorStyle->viewTransitionRule())
        return viewTransitionRule;
    if (m_userStyle && m_userStyle->viewTransitionRule())
        return m_userStyle->viewTransitionRule();
    if (m_userAgentMediaQueryStyle && m_userAgentMediaQueryStyle->viewTransitionRule())
        return m_userAgentMediaQueryStyle->viewTransitionRule();
    return nullptr;
}

std::optional<DynamicMediaQueryEvaluationChanges> ScopeRuleSets::evaluateDynamicMediaQueryRules(const MQ::MediaQueryEvaluator& evaluator)
{
    std::optional<DynamicMediaQueryEvaluationChanges> evaluationChanges;

    auto evaluate = [&](auto* ruleSet) {
        if (!ruleSet)
            return;
        if (auto changes = ruleSet->evaluateDynamicMediaQueryRules(evaluator)) {
            if (evaluationChanges)
                evaluationChanges->append(WTFMove(*changes));
            else
                evaluationChanges = changes;
        }
    };

    evaluate(&authorStyle());
    evaluate(userStyle());
    evaluate(userAgentMediaQueryStyle());

    return evaluationChanges;
}

void ScopeRuleSets::appendAuthorStyleSheets(std::span<const RefPtr<CSSStyleSheet>> styleSheets, MQ::MediaQueryEvaluator* mediaQueryEvaluator, InspectorCSSOMWrappers& inspectorCSSOMWrappers)
{
    RuleSetBuilder builder(*m_authorStyle, *mediaQueryEvaluator, &m_styleResolver, RuleSetBuilder::ShrinkToFit::Enable, RuleSetBuilder::ShouldResolveNesting::Yes);

    RefPtr<CSSStyleSheet> previous;
    for (auto& cssSheet : styleSheets) {
        ASSERT(!cssSheet->disabled());
        // In some cases, we have many identical <style> tags. To avoid pathological behavior, we check the one-previous <style> and skip adding a new one when
        // the content is exact same to the previous one.
        if (previous) {
            if (&previous->contents() == &cssSheet->contents() && previous->mediaQueries().isEmpty() && cssSheet->mediaQueries().isEmpty()) {
                inspectorCSSOMWrappers.collectFromStyleSheetIfNeeded(cssSheet.get());
                continue;
            }
        }

        builder.addRulesFromSheet(cssSheet->contents(), cssSheet->mediaQueries());
        inspectorCSSOMWrappers.collectFromStyleSheetIfNeeded(cssSheet.get());
        previous = cssSheet;
    }

    collectFeatures();
}

void ScopeRuleSets::collectFeatures() const
{
    RELEASE_ASSERT(!m_isInvalidatingStyleWithRuleSets);

    m_features.clear();
    if (UserAgentStyle::defaultStyle)
        m_features.add(UserAgentStyle::defaultStyle->features());
    m_defaultStyleVersionOnFeatureCollection = UserAgentStyle::defaultStyleVersion;

    if (auto* userAgentMediaQueryStyle = this->userAgentMediaQueryStyle())
        m_features.add(userAgentMediaQueryStyle->features());

    if (m_authorStyle)
        m_features.add(m_authorStyle->features());
    if (auto* userStyle = this->userStyle())
        m_features.add(userStyle->features());

    m_scopeBreakingHasPseudoClassInvalidationRuleSet = makeRuleSet(m_features.scopeBreakingHasPseudoClassRules);

    m_idInvalidationRuleSets.clear();
    m_classInvalidationRuleSets.clear();
    m_attributeInvalidationRuleSets.clear();
    m_pseudoClassInvalidationRuleSets.clear();
    m_hasPseudoClassInvalidationRuleSets.clear();

    m_customPropertyNamesInStyleContainerQueries = std::nullopt;

    m_cachedSelectorsForStyleAttribute = std::nullopt;

    m_features.shrinkToFit();
}

template<typename KeyType, typename RuleFeatureVectorType, typename Hash, typename HashTraits>
static Vector<InvalidationRuleSet>* ensureInvalidationRuleSets(const KeyType& key, HashMap<KeyType, std::unique_ptr<Vector<InvalidationRuleSet>>, Hash, HashTraits>& ruleSetMap, const HashMap<KeyType, std::unique_ptr<RuleFeatureVectorType>, Hash, HashTraits>& ruleFeatures)
{
    return ruleSetMap.ensure(key, [&] () -> std::unique_ptr<Vector<InvalidationRuleSet>> {
        auto* features = ruleFeatures.get(key);
        if (!features)
            return nullptr;

        HashMap<std::tuple<uint8_t, bool, bool>, InvalidationRuleSet> invalidationRuleSetMap;

        for (auto& feature : *features) {
            auto key = std::tuple { static_cast<uint8_t>(feature.matchElement), static_cast<bool>(feature.isNegation), true };

            auto& invalidationRuleSet = invalidationRuleSetMap.ensure(key, [&] {
                return InvalidationRuleSet {
                    RuleSet::create(),
                    { },
                    feature.matchElement,
                    feature.isNegation,
                };
            }).iterator->value;

            invalidationRuleSet.ruleSet->addRule(*feature.styleRule, feature.selectorIndex, feature.selectorListIndex);

            if constexpr (std::is_same<typename RuleFeatureVectorType::ValueType, RuleFeatureWithInvalidationSelector>::value)
                invalidationRuleSet.invalidationSelectors = CSSSelectorList::makeJoining(invalidationRuleSet.invalidationSelectors, feature.invalidationSelector);
        }

        return makeUnique<Vector<InvalidationRuleSet>>(WTF::map(invalidationRuleSetMap.values(), [](auto&& invalidationRuleSet) {
            invalidationRuleSet.ruleSet->shrinkToFit();
            return WTFMove(invalidationRuleSet);
        }));
    }).iterator->value.get();
}

const Vector<InvalidationRuleSet>* ScopeRuleSets::idInvalidationRuleSets(const AtomString& id) const
{
    return ensureInvalidationRuleSets(id, m_idInvalidationRuleSets, m_features.idRules);
}

const Vector<InvalidationRuleSet>* ScopeRuleSets::classInvalidationRuleSets(const AtomString& className) const
{
    return ensureInvalidationRuleSets(className, m_classInvalidationRuleSets, m_features.classRules);
}

const Vector<InvalidationRuleSet>* ScopeRuleSets::attributeInvalidationRuleSets(const AtomString& attributeName) const
{
    return ensureInvalidationRuleSets(attributeName, m_attributeInvalidationRuleSets, m_features.attributeRules);
}

const Vector<InvalidationRuleSet>* ScopeRuleSets::pseudoClassInvalidationRuleSets(const PseudoClassInvalidationKey& pseudoClassKey) const
{
    return ensureInvalidationRuleSets(pseudoClassKey, m_pseudoClassInvalidationRuleSets, m_features.pseudoClassRules);
}

const Vector<InvalidationRuleSet>* ScopeRuleSets::hasPseudoClassInvalidationRuleSets(const PseudoClassInvalidationKey& key) const
{
    return ensureInvalidationRuleSets(key, m_hasPseudoClassInvalidationRuleSets, m_features.hasPseudoClassRules);
}

const HashSet<AtomString>& ScopeRuleSets::customPropertyNamesInStyleContainerQueries() const
{
    if (!m_customPropertyNamesInStyleContainerQueries) {
        HashSet<AtomString> propertyNames;

        auto collectPropertyNames = [&](auto* ruleSet) {
            if (!ruleSet)
                return;
            for (auto query : ruleSet->containerQueryRules()) {
                traverseFeatures(query->containerQuery().condition, [&](auto& containerFeature) {
                    if (isCustomPropertyName(containerFeature.name))
                        propertyNames.add(containerFeature.name);
                });
            }
        };

        collectPropertyNames(&authorStyle());
        collectPropertyNames(userStyle());

        m_customPropertyNamesInStyleContainerQueries = propertyNames;
    }
    return *m_customPropertyNamesInStyleContainerQueries;
}

SelectorsForStyleAttribute ScopeRuleSets::selectorsForStyleAttribute() const
{
    auto compute = [&] {
        auto* ruleSets = attributeInvalidationRuleSets(HTMLNames::styleAttr->localName());
        if (!ruleSets)
            return SelectorsForStyleAttribute::None;
        for (auto& ruleSet : *ruleSets) {
            if (ruleSet.matchElement != MatchElement::Subject)
                return SelectorsForStyleAttribute::NonSubjectPosition;
        }
        return SelectorsForStyleAttribute::SubjectPositionOnly;
    };

    if (!m_cachedSelectorsForStyleAttribute)
        m_cachedSelectorsForStyleAttribute = compute();

    return *m_cachedSelectorsForStyleAttribute;
}

bool ScopeRuleSets::hasMatchingUserOrAuthorStyle(NOESCAPE const WTF::Function<bool(RuleSet&)>& predicate)
{
    if (m_authorStyle && predicate(*m_authorStyle))
        return true;

    if (auto* userStyle = this->userStyle(); userStyle && predicate(*userStyle))
        return true;

    return false;
}

} // namespace Style
} // namespace WebCore
