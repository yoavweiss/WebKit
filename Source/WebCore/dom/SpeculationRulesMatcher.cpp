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
#include "SpeculationRulesMatcher.h"

#include "Document.h"
#include "Element.h"
#include "HTMLAnchorElement.h"
#include "JSDOMGlobalObject.h"
#include "ScriptController.h"
#include "SelectorQuery.h"
#include "URLPattern.h"
#include "URLPatternOptions.h"
#include <JavaScriptCore/SpeculationRules.h>

namespace WebCore {

static bool matches(const JSC::SpeculationRules::DocumentPredicate&, Ref<Document>, Ref<HTMLAnchorElement>);

static bool matches(const JSC::SpeculationRules::URLPatternPredicate& predicate, Ref<HTMLAnchorElement> anchor)
{
    for (const auto& patternString : predicate.patterns) {
        ExceptionOr<Ref<URLPattern>> patternExceptionOr = URLPattern::create(anchor->protectedDocument(), patternString, String(anchor->document().baseURL().string()), URLPatternOptions());
        if (!patternExceptionOr.hasException()) {
            Ref<URLPattern> pattern = patternExceptionOr.returnValue();
            auto result = pattern->test(anchor->protectedDocument(), anchor->href().string(), String(anchor->document().baseURL().string()));
            if (!result.hasException() && result.returnValue())
                return true;
        }
    }
    return false;
}

static bool matches(const JSC::SpeculationRules::CSSSelectorPredicate& predicate, Ref<Element> element)
{
    for (const auto& selectorString : predicate.selectors) {
        auto query = element->protectedDocument()->selectorQueryForString(selectorString);
        if (query.hasException())
            continue;
        if (query.returnValue().matches(element.get()))
            return true;
    }
    return false;
}

static bool matches(const Box<JSC::SpeculationRules::Conjunction>& predicate, Ref<Document> document, Ref<HTMLAnchorElement> anchor)
{
    for (const auto& clause : predicate->clauses) {
        if (!matches(clause, document, anchor))
            return false;
    }
    return true;
}

static bool matches(const Box<JSC::SpeculationRules::Disjunction>& predicate, Ref<Document> document, Ref<HTMLAnchorElement> anchor)
{
    if (predicate->clauses.isEmpty())
        return false;

    for (const auto& clause : predicate->clauses) {
        if (matches(clause, document, anchor))
            return true;
    }
    return false;
}

static bool matches(const Box<JSC::SpeculationRules::Negation>& predicate, Ref<Document> document, Ref<HTMLAnchorElement> anchor)
{
    return !matches(*predicate->clause, document, anchor);
}

static bool matches(const JSC::SpeculationRules::DocumentPredicate& predicate, Ref<Document> document, Ref<HTMLAnchorElement> anchor)
{
    return WTF::switchOn(predicate.value(),
        [&] (const JSC::SpeculationRules::URLPatternPredicate& p) { return matches(p, anchor); },
        [&] (const JSC::SpeculationRules::CSSSelectorPredicate& p) { return matches(p, anchor); },
        [&] (const Box<JSC::SpeculationRules::Conjunction>& p) { return matches(p, document, anchor); },
        [&] (const Box<JSC::SpeculationRules::Disjunction>& p) { return matches(p, document, anchor); },
        [&] (const Box<JSC::SpeculationRules::Negation>& p) { return matches(p, document, anchor); }
    );
}

// https://wicg.github.io/nav-speculation/speculation-rules.html#document-rule-predicate-matching
std::optional<PrefetchRule> SpeculationRulesMatcher::hasMatchingRule(Ref<Document> document, Ref<HTMLAnchorElement> anchor)
{
    auto* globalObject = document->globalObject();
    if (!globalObject)
        return std::nullopt;

    const auto& speculationRules = globalObject->speculationRules();
    const auto& url = anchor->href();

    for (const auto& rule : speculationRules.prefetchRules()) {
        for (const auto& href : rule.urls) {
            if (href == url)
                return PrefetchRule { rule.tags, rule.referrerPolicy, rule.eagerness == JSC::SpeculationRules::Eagerness::Conservative };
        }

        if (rule.predicate && matches(rule.predicate.value(), document, anchor))
            return PrefetchRule { rule.tags, rule.referrerPolicy, rule.eagerness == JSC::SpeculationRules::Eagerness::Conservative || rule.eagerness == JSC::SpeculationRules::Eagerness::Moderate };
    }

    return std::nullopt;
}

} // namespace WebCore
