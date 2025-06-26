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
#include "SpeculationRules.h"

#include "JSCInlines.h"
#include "JSGlobalObject.h"
#include "JSONObject.h"
#include "SourceCode.h"
#include <wtf/JSONValues.h>
#include <wtf/URL.h>
#include <wtf/URLHash.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace JSC {

static std::optional<Vector<String>> parseStringOrStringList(JSON::Object& object, const String& key)
{
    Vector<String> result;
    String stringValue;
    if (object.getString(key, stringValue)) {
        result.append(stringValue);
        return result;
    }

    RefPtr<JSON::Array> arrayValue;
    if (object.getArray(key, arrayValue)) {
        for (const auto& value : *arrayValue) {
            String element;
            if (!value->asString(element))
                return std::nullopt;
            result.append(element);
        }
        return result;
    }

    return Vector<String> { };
}

static std::optional<SpeculationRules::DocumentPredicate> parseDocumentPredicate(JSON::Object&);

// https://wicg.github.io/nav-speculation/speculation-rules.html#parsing-a-document-rule-predicate-from-a-map
static std::optional<SpeculationRules::DocumentPredicate> parseDocumentPredicate(JSON::Object& object)
{
    RefPtr<JSON::Array> array;
    if (object.getArray("and"_s, array)) {
        SpeculationRules::Conjunction conjunction;
        for (auto& item : *array) {
            RefPtr<JSON::Object> clauseObject = item->asObject();
            if (!clauseObject)
                return std::nullopt;
            auto predicate = parseDocumentPredicate(*clauseObject);
            if (!predicate)
                return std::nullopt;
            conjunction.clauses.append(WTFMove(*predicate));
        }
        return SpeculationRules::DocumentPredicate { Box<SpeculationRules::Conjunction>::create(WTFMove(conjunction)) };
    }

    if (object.getArray("or"_s, array)) {
        SpeculationRules::Disjunction disjunction;
        for (auto& item : *array) {
            RefPtr<JSON::Object> clauseObject = item->asObject();
            if (!clauseObject)
                return std::nullopt;
            auto predicate = parseDocumentPredicate(*clauseObject);
            if (!predicate)
                return std::nullopt;
            disjunction.clauses.append(WTFMove(*predicate));
        }
        return SpeculationRules::DocumentPredicate { Box<SpeculationRules::Disjunction>::create(WTFMove(disjunction)) };
    }

    RefPtr<JSON::Object> clauseObject = object.getObject("not"_s);
    if (clauseObject) {
        auto predicate = parseDocumentPredicate(*clauseObject);
        if (!predicate)
            return std::nullopt;
        SpeculationRules::Negation negation { Box<SpeculationRules::DocumentPredicate>::create(WTFMove(*predicate)) };
        return SpeculationRules::DocumentPredicate { Box<SpeculationRules::Negation>::create(WTFMove(negation)) };
    }

    SpeculationRules::URLPatternPredicate urlPredicate;
    auto urlMatches = parseStringOrStringList(object, "url_matches"_s);
    if (urlMatches)
        urlPredicate.patterns.appendVector(*urlMatches);
    auto hrefMatches = parseStringOrStringList(object, "href_matches"_s);
    if (hrefMatches)
        urlPredicate.patterns.appendVector(*hrefMatches);

    SpeculationRules::CSSSelectorPredicate selectorPredicate;
    auto selectorMatches = parseStringOrStringList(object, "selector_matches"_s);
    if (selectorMatches)
        selectorPredicate.selectors.appendVector(*selectorMatches);

    bool hasURLPredicate = !urlPredicate.patterns.isEmpty();
    bool hasSelectorPredicate = !selectorPredicate.selectors.isEmpty();

    if (hasURLPredicate && hasSelectorPredicate) {
        SpeculationRules::Conjunction conjunction;
        conjunction.clauses.append(SpeculationRules::DocumentPredicate { WTFMove(urlPredicate) });
        conjunction.clauses.append(SpeculationRules::DocumentPredicate { WTFMove(selectorPredicate) });
        return SpeculationRules::DocumentPredicate { Box<SpeculationRules::Conjunction>::create(WTFMove(conjunction)) };
    }

    if (hasURLPredicate)
        return SpeculationRules::DocumentPredicate { WTFMove(urlPredicate) };

    if (hasSelectorPredicate)
        return SpeculationRules::DocumentPredicate { WTFMove(selectorPredicate) };

    return std::nullopt;
}

// https://wicg.github.io/nav-speculation/speculation-rules.html#parse-a-speculation-rule
static std::optional<SpeculationRules::Rule> parseSingleRule(JSON::Object& input, const String& rulesetLevelTag, const URL& rulesetBaseURL, const URL& documentBaseURL)
{
    const HashSet<String> allowedKeys = {
        "source"_s, "urls"_s, "where"_s, "requires"_s, "target_hint"_s,
        "referrer_policy"_s, "relative_to"_s, "eagerness"_s,
        "expects_no_vary_search"_s, "tag"_s
    };
    for (const auto& key : input.keys()) {
        if (!allowedKeys.contains(key))
            return std::nullopt;
    }

    String source;
    if (!input.getString("source"_s, source)) {
        bool hasURLs = !!input.getValue("urls"_s);
        bool hasWhere = !!input.getValue("where"_s);
        if (hasURLs && !hasWhere)
            source = "list"_s;
        else if (hasWhere && !hasURLs)
            source = "document"_s;
        else
            return std::nullopt;
    }

    if (source != "list"_s && source != "document"_s)
        return std::nullopt;

    SpeculationRules::Rule rule;

    if (source == "list"_s) {
        if (input.getValue("where"_s))
            return std::nullopt;

        RefPtr<JSON::Array> urlsArray;
        if (!input.getArray("urls"_s, urlsArray))
            return std::nullopt;

        URL currentBaseURL = rulesetBaseURL;
        String relativeTo;
        if (input.getString("relative_to"_s, relativeTo)) {
            if (relativeTo != "ruleset"_s && relativeTo != "document"_s)
                return std::nullopt;
            if (relativeTo == "document"_s)
                currentBaseURL = documentBaseURL;
        }

        for (const auto& urlValue : *urlsArray) {
            String urlString;
            if (!urlValue->asString(urlString))
                return std::nullopt;
            URL parsedURL(currentBaseURL, urlString);
            if (parsedURL.isValid() && (parsedURL.protocolIs("http"_s) || parsedURL.protocolIs("https"_s)))
                rule.urls.append(parsedURL);
        }
        rule.eagerness = SpeculationRules::Eagerness::Immediate;

    } else { // source == "document"_s
        if (input.getValue("urls"_s) || input.getValue("relative_to"_s))
            return std::nullopt;

        RefPtr<JSON::Object> whereObject;
        if (input.getObject("where"_s, whereObject)) {
            auto predicate = parseDocumentPredicate(*whereObject);
            if (!predicate)
                return std::nullopt;
            rule.predicate = WTFMove(*predicate);
        } else {
            // No "where" means match all links, which is an empty conjunction.
            rule.predicate = SpeculationRules::DocumentPredicate { Box<SpeculationRules::Conjunction>::create() };
        }
        rule.eagerness = SpeculationRules::Eagerness::Conservative;
    }

    RefPtr<JSON::Array> requiresArray;
    if (input.getArray("requires"_s, requiresArray)) {
        for (const auto& reqValue : *requiresArray) {
            String requirement;
            if (!reqValue->asString(requirement) || requirement != "anonymous-client-ip-when-cross-origin"_s)
                return std::nullopt;
            rule.requirements.append(requirement);
        }
    }

    input.getString("referrer_policy"_s, rule.referrerPolicy);

    String eagernessString;
    if (input.getString("eagerness"_s, eagernessString)) {
        if (eagernessString == "immediate"_s)
            rule.eagerness = SpeculationRules::Eagerness::Immediate;
        else if (eagernessString == "eager"_s)
            rule.eagerness = SpeculationRules::Eagerness::Eager;
        else if (eagernessString == "moderate"_s)
            rule.eagerness = SpeculationRules::Eagerness::Moderate;
        else if (eagernessString == "conservative"_s)
            rule.eagerness = SpeculationRules::Eagerness::Conservative;
        else
            return std::nullopt;
    }

    input.getString("expects_no_vary_search"_s, rule.noVarySearchHint);
    // Note: Treating No-Vary-Search hint as a string for now.

    if (!rulesetLevelTag.isNull())
        rule.tags.append(rulesetLevelTag);

    String ruleTag;
    if (input.getString("tag"_s, ruleTag)) {
        if (!ruleTag.containsOnlyASCII())
            return std::nullopt;
        rule.tags.append(WTFMove(ruleTag));
    }

    if (rule.tags.isEmpty())
        rule.tags.append(String()); // Append null string

    return rule;
}

static std::optional<Vector<SpeculationRules::Rule>> parseRules(JSON::Object& object, const String& key, const String& rulesetLevelTag, const URL& rulesetBaseURL, const URL& documentBaseURL)
{
    RefPtr<JSON::Array> array;
    if (!object.getArray(key, array))
        return Vector<SpeculationRules::Rule> { };

    Vector<SpeculationRules::Rule> rules;
    for (auto& value : *array) {
        RefPtr<JSON::Object> ruleObject = value->asObject();
        if (!ruleObject)
            return std::nullopt;
        if (auto rule = parseSingleRule(*ruleObject, rulesetLevelTag, rulesetBaseURL, documentBaseURL))
            rules.append(WTFMove(*rule));
        else
            return std::nullopt; // Invalid rule in the list.
    }
    return rules;
}

// https://wicg.github.io/nav-speculation/speculation-rules.html#parse-speculation-rules
void SpeculationRules::parseSpeculationRules(const StringView& text, const URL& rulesetBaseURL, const URL& documentBaseURL)
{
    auto jsonValue = JSON::Value::parseJSON(text);
    if (!jsonValue)
        return;

    RefPtr<JSON::Object> jsonObject = jsonValue->asObject();
    if (!jsonObject)
        return;

    auto speculationRules = SpeculationRules::create();

    String rulesetLevelTag;
    jsonObject->getString("tag"_s, rulesetLevelTag);

    auto prefetch = parseRules(*jsonObject, "prefetch"_s, rulesetLevelTag, rulesetBaseURL, documentBaseURL);
    if (!prefetch)
        return;
    m_prefetchRules.appendVector(WTFMove(*prefetch));
}

} // namespace JSC
