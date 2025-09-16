/*
 * Copyright (C) 2022-2024 Apple Inc. All rights reserved.
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

#include "config.h"
#include "WebExtensionContext.h"

#if ENABLE(WK_WEB_EXTENSIONS)

#include "APIContentRuleList.h"
#include "APIContentRuleListStore.h"
#include "InjectUserScriptImmediately.h"
#include "Logging.h"
#include "WebExtensionContextParameters.h"
#include "WebExtensionContextProxyMessages.h"
#include "WebExtensionController.h"
#include "WebExtensionPermission.h"
#include "WebPageProxy.h"
#include <WebCore/LocalizedStrings.h>
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>

namespace WebKit {

using namespace WebCore;

int WebExtensionContext::toAPIError(WebExtensionContext::Error error)
{
    switch (error) {
    case WebExtensionContext::Error::Unknown:
        return static_cast<int>(WebExtensionContext::APIError::Unknown);
    case WebExtensionContext::Error::AlreadyLoaded:
        return static_cast<int>(WebExtensionContext::APIError::AlreadyLoaded);
    case WebExtensionContext::Error::NotLoaded:
        return static_cast<int>(WebExtensionContext::APIError::NotLoaded);
    case WebExtensionContext::Error::BaseURLAlreadyInUse:
        return static_cast<int>(WebExtensionContext::APIError::BaseURLAlreadyInUse);
    case WebExtensionContext::Error::NoBackgroundContent:
        return static_cast<int>(WebExtensionContext::APIError::NoBackgroundContent);
    case WebExtensionContext::Error::BackgroundContentFailedToLoad:
        return static_cast<int>(WebExtensionContext::APIError::BackgroundContentFailedToLoad);
    }

    ASSERT_NOT_REACHED();
    return static_cast<int>(WebExtensionContext::APIError::Unknown);
}

Ref<API::Error> WebExtensionContext::createError(Error error, const String& customLocalizedDescription, RefPtr<API::Error> underlyingError)
{
    auto errorCode = toAPIError(error);
    String localizedDescription;

    switch (error) {
    case Error::Unknown:
        localizedDescription = WEB_UI_STRING_KEY("An unknown error has occurred.", "An unknown error has occurred. (WKWebExtensionContext)", "WKWebExtensionContextErrorUnknown description");
        break;

    case Error::AlreadyLoaded:
        localizedDescription = WEB_UI_STRING("Extension context is already loaded.", "WKWebExtensionContextErrorAlreadyLoaded description");
        break;

    case Error::NotLoaded:
        localizedDescription = WEB_UI_STRING("Extension context is not loaded.", "WKWebExtensionContextErrorNotLoaded description");
        break;

    case Error::BaseURLAlreadyInUse:
        localizedDescription = WEB_UI_STRING("Another extension context is loaded with the same base URL.", "WKWebExtensionContextErrorBaseURLAlreadyInUse description");
        break;

    case Error::NoBackgroundContent:
        localizedDescription = WEB_UI_STRING("No background content is available to load.", "WKWebExtensionContextErrorNoBackgroundContent description");
        break;

    case Error::BackgroundContentFailedToLoad:
        localizedDescription = WEB_UI_STRING("The background content failed to load due to an error.", "WKWebExtensionContextErrorBackgroundContentFailedToLoad description");
        break;
    }

    if (!customLocalizedDescription.isEmpty())
        localizedDescription = customLocalizedDescription;

    return API::Error::create({ "WKWebExtensionContextErrorDomain"_s, errorCode, { }, localizedDescription }, underlyingError);
}

Vector<Ref<API::Error>> WebExtensionContext::errors()
{
    auto array = protectedExtension()->errors();
    array.appendVector(m_errors);
    return array;
}

bool WebExtensionContext::hasContentModificationRules()
{
    return declarativeNetRequestEnabledRulesetCount() || !m_sessionRulesIDs.isEmpty() || !m_dynamicRulesIDs.isEmpty();
}

WebExtensionContext::InjectedContentVector WebExtensionContext::injectedContents() const
{
    InjectedContentVector result = protectedExtension()->staticInjectedContents();

    for (auto& entry : m_registeredScriptsMap)
        result.append(entry.value->injectedContent());

    return result;
}

bool WebExtensionContext::hasInjectedContentForURL(const URL& url)
{
    for (auto& injectedContent : injectedContents()) {
        // FIXME: <https://webkit.org/b/246492> Add support for exclude globs.
        bool isExcluded = false;
        for (auto& excludeMatchPattern : injectedContent.excludeMatchPatterns) {
            if (excludeMatchPattern->matchesURL(url)) {
                isExcluded = true;
                break;
            }
        }

        if (isExcluded)
            continue;

        // FIXME: <https://webkit.org/b/246492> Add support for include globs.
        for (auto& includeMatchPattern : injectedContent.includeMatchPatterns) {
            if (includeMatchPattern->matchesURL(url))
                return true;
        }
    }

    return false;
}

bool WebExtensionContext::hasInjectedContent()
{
    return !injectedContents().isEmpty();
}

void WebExtensionContext::addInjectedContent(const InjectedContentVector& injectedContents)
{
    if (!safeToInjectContent())
        return;

    // Only add content for one "all hosts" pattern if the extension has the permission.
    // This avoids duplicate injected content if individual hosts are granted in addition to "all hosts".
    if (hasAccessToAllHosts()) {
        addInjectedContent(injectedContents, WebExtensionMatchPattern::allHostsAndSchemesMatchPattern());
        return;
    }

    MatchPatternSet grantedMatchPatterns;
    for (auto& pattern : currentPermissionMatchPatterns())
        grantedMatchPatterns.add(pattern);

    addInjectedContent(injectedContents, grantedMatchPatterns);
}

void WebExtensionContext::addInjectedContent(const InjectedContentVector& injectedContents, const MatchPatternSet& grantedMatchPatterns)
{
    if (!safeToInjectContent())
        return;

    if (hasAccessToAllHosts()) {
        // If this is not currently granting "all hosts", then we can return early. This means
        // the "all hosts" pattern injected content was added already, and no content needs added.
        // Continuing here would add multiple copies of injected content, one for "all hosts" and
        // another for individually granted hosts.
        if (!WebExtensionMatchPattern::patternsMatchAllHosts(grantedMatchPatterns))
            return;

        // Since we are granting "all hosts" we want to remove any previously added content since
        // "all hosts" will cover any hosts previously added, and we don't want duplicate scripts.
        MatchPatternSet patternsToRemove;
        for (auto& entry : m_injectedScriptsPerPatternMap)
            patternsToRemove.add(entry.key);

        for (auto& entry : m_injectedStyleSheetsPerPatternMap)
            patternsToRemove.add(entry.key);

        for (auto& pattern : patternsToRemove)
            removeInjectedContent(pattern);
    }

    for (auto& pattern : grantedMatchPatterns)
        addInjectedContent(injectedContents, pattern);
}

static WebCore::UserScriptInjectionTime toImpl(WebExtension::InjectionTime injectionTime)
{
    switch (injectionTime) {
    case WebExtension::InjectionTime::DocumentStart:
        return WebCore::UserScriptInjectionTime::DocumentStart;
    case WebExtension::InjectionTime::DocumentIdle:
        // FIXME: <rdar://problem/57613315> Implement idle injection time. For now, the end injection time is fine.
    case WebExtension::InjectionTime::DocumentEnd:
        return WebCore::UserScriptInjectionTime::DocumentEnd;
    }

    return WebCore::UserScriptInjectionTime::DocumentEnd;
}

API::ContentWorld& WebExtensionContext::toContentWorld(WebExtensionContentWorldType contentWorldType) const
{
    ASSERT(isLoaded());

    switch (contentWorldType) {
    case WebExtensionContentWorldType::Main:
    case WebExtensionContentWorldType::WebPage:
#if ENABLE(INSPECTOR_EXTENSIONS)
    case WebExtensionContentWorldType::Inspector:
#endif
        return API::ContentWorld::pageContentWorldSingleton();
    case WebExtensionContentWorldType::ContentScript:
        return *m_contentScriptWorld;
    case WebExtensionContentWorldType::Native:
        ASSERT_NOT_REACHED();
        return API::ContentWorld::pageContentWorldSingleton();
    }

    return API::ContentWorld::pageContentWorldSingleton();
}

void WebExtensionContext::addInjectedContent(const InjectedContentVector& injectedContents, WebExtensionMatchPattern& pattern)
{
    if (!safeToInjectContent())
        return;

    auto scriptAddResult = m_injectedScriptsPerPatternMap.ensure(pattern, [&] {
        return UserScriptVector { };
    });

    auto styleSheetAddResult = m_injectedStyleSheetsPerPatternMap.ensure(pattern, [&] {
        return UserStyleSheetVector { };
    });

    auto& originInjectedScripts = scriptAddResult.iterator->value;
    auto& originInjectedStyleSheets = styleSheetAddResult.iterator->value;

    HashSet<String> baseExcludeMatchPatternsSet;

    auto& deniedMatchPatterns = deniedPermissionMatchPatterns();
    for (auto& deniedEntry : deniedMatchPatterns) {
        // Granted host patterns always win over revoked host patterns. Skip any revoked "all hosts" patterns.
        // This supports the case where "all hosts" is revoked and a handful of specific hosts are granted.
        Ref deniedMatchPattern = deniedEntry.key;
        if (deniedMatchPattern->matchesAllHosts())
            continue;

        // Only revoked patterns that match the granted pattern need to be included. This limits
        // the size of the exclude match patterns list to speed up processing.
        if (!pattern.matchesPattern(deniedMatchPattern, { WebExtensionMatchPattern::Options::IgnorePaths, WebExtensionMatchPattern::Options::MatchBidirectionally }))
            continue;

        for (const auto& deniedMatchPattern : deniedMatchPattern->expandedStrings())
            baseExcludeMatchPatternsSet.add(deniedMatchPattern);
    }

    auto& userContentControllers = this->userContentControllers();

    for (auto& injectedContentData : injectedContents) {
        HashSet<String> includeMatchPatternsSet;

        for (auto& includeMatchPattern : injectedContentData.includeMatchPatterns) {
            // Paths are not matched here since all we need to match at this point is scheme and host.
            // The path matching will happen in WebKit when deciding to inject content into a frame.

            // When the include pattern matches all hosts, we can generate a restricted patten here and skip
            // the more expensive calls to matchesPattern() below since we know they will match.
            if (includeMatchPattern->matchesAllHosts()) {
                auto restrictedPattern = WebExtensionMatchPattern::getOrCreate(includeMatchPattern->scheme(), pattern.host(), includeMatchPattern->path());
                if (!restrictedPattern)
                    continue;

                for (const auto& restrictedPattern : restrictedPattern->expandedStrings())
                    includeMatchPatternsSet.add(restrictedPattern);
                continue;
            }

            // When deciding if injected content patterns match, we need to check bidirectionally.
            // This allows an extension that requests *.wikipedia.org, to still inject content when
            // it is granted more specific access to *.en.wikipedia.org.
            if (!includeMatchPattern->matchesPattern(pattern, { WebExtensionMatchPattern::Options::IgnorePaths, WebExtensionMatchPattern::Options::MatchBidirectionally }))
                continue;

            // Pick the most restrictive match pattern by comparing unidirectionally to the granted origin pattern.
            // If the include pattern still matches the granted origin pattern, it is not restrictive enough.
            // In that case we need to use the include pattern scheme and path, but with the granted pattern host.
            RefPtr restrictedPattern = includeMatchPattern.ptr();
            if (includeMatchPattern->matchesPattern(pattern, { WebExtensionMatchPattern::Options::IgnoreSchemes, WebExtensionMatchPattern::Options::IgnorePaths }))
                restrictedPattern = WebExtensionMatchPattern::getOrCreate(includeMatchPattern->scheme(), pattern.host(), includeMatchPattern->path());
            if (!restrictedPattern)
                continue;

            for (const auto& restrictedPattern : restrictedPattern->expandedStrings())
                includeMatchPatternsSet.add(restrictedPattern);
        }

        if (includeMatchPatternsSet.isEmpty())
            continue;

        // FIXME: <rdar://problem/57613243> Support injecting into about:blank, honoring self.contentMatchesAboutBlank. Appending @"about:blank" to the includeMatchPatterns does not work currently.
        Vector<String> includeMatchPatterns;
        for (const auto& includeMatchPattern : includeMatchPatternsSet)
            includeMatchPatterns.append(includeMatchPattern);

        HashSet<String> excludeMatchPatternsSet;
        excludeMatchPatternsSet.addAll(injectedContentData.expandedExcludeMatchPatternStrings());
        excludeMatchPatternsSet.unionWith(baseExcludeMatchPatternsSet);

        Vector<String> excludeMatchPatterns;
        for (const auto& excludeMatchPattern : excludeMatchPatterns)
            excludeMatchPatterns.append(excludeMatchPattern);

        auto injectedFrames = injectedContentData.injectsIntoAllFrames ? WebCore::UserContentInjectedFrames::InjectInAllFrames : WebCore::UserContentInjectedFrames::InjectInTopFrameOnly;
        auto injectionTime = toImpl(injectedContentData.injectionTime);
        Ref executionWorld = toContentWorld(injectedContentData.contentWorldType);
        auto styleLevel = injectedContentData.styleLevel;
        auto matchParentFrame = injectedContentData.matchParentFrame;

        auto scriptID = injectedContentData.identifier;
        bool isRegisteredScript = !scriptID.isEmpty();

        RefPtr extension = m_extension;

        for (auto& scriptPath : injectedContentData.scriptPaths) {
            auto scriptStringResult = extension->resourceStringForPath(scriptPath, WebExtension::CacheResult::Yes);
            if (!scriptStringResult) {
                recordErrorIfNeeded(scriptStringResult.error());
                continue;
            }

            auto scriptString = scriptStringResult.value();

            Ref userScript = API::UserScript::create(WebCore::UserScript { WTFMove(scriptString), URL { m_baseURL, scriptPath }, WTFMove(includeMatchPatterns), WTFMove(excludeMatchPatterns), injectionTime, injectedFrames, matchParentFrame }, executionWorld);
            originInjectedScripts.append(userScript);

            for (Ref userContentController : userContentControllers)
                userContentController->addUserScript(userScript, InjectUserScriptImmediately::Yes);

            if (isRegisteredScript) {
                RefPtr registeredScript = m_registeredScriptsMap.get(scriptID);
                ASSERT(registeredScript);
                if (!registeredScript)
                    continue;

                registeredScript->addUserScript(scriptID, userScript);
            }
        }

        for (auto& styleSheetPath : injectedContentData.styleSheetPaths) {
            auto styleSheetStringResult = extension->resourceStringForPath(styleSheetPath, WebExtension::CacheResult::Yes);
            if (!styleSheetStringResult) {
                recordErrorIfNeeded(styleSheetStringResult.error());
                continue;
            }

            auto styleSheetString = styleSheetStringResult.value();

            styleSheetString = localizedResourceString(styleSheetString, "text/css"_s);

            Ref userStyleSheet = API::UserStyleSheet::create(WebCore::UserStyleSheet { WTFMove(styleSheetString), URL { m_baseURL, styleSheetPath }, WTFMove(includeMatchPatterns), WTFMove(excludeMatchPatterns), injectedFrames, matchParentFrame, styleLevel, std::nullopt }, executionWorld);
            originInjectedStyleSheets.append(userStyleSheet);

            for (Ref userContentController : userContentControllers)
                userContentController->addUserStyleSheet(userStyleSheet);

            if (isRegisteredScript) {
                RefPtr registeredScript = m_registeredScriptsMap.get(scriptID);
                ASSERT(registeredScript);
                if (!registeredScript)
                    continue;

                registeredScript->addUserStyleSheet(scriptID, userStyleSheet);
            }
        }
    }
}

void WebExtensionContext::addInjectedContent(WebUserContentControllerProxy& userContentController)
{
    if (!safeToInjectContent())
        return;

    for (auto& entry : m_injectedScriptsPerPatternMap) {
        for (auto& userScript : entry.value)
            userContentController.addUserScript(userScript, InjectUserScriptImmediately::Yes);
    }

    for (auto& entry : m_injectedStyleSheetsPerPatternMap) {
        for (auto& userStyleSheet : entry.value)
            userContentController.addUserStyleSheet(userStyleSheet);
    }
}

void WebExtensionContext::removeInjectedContent()
{
    if (!isLoaded())
        return;

    // Use all user content controllers in case the extension was briefly allowed in private browsing
    // and content was injected into any of those content controllers.
    for (Ref userContentController : extensionController()->allUserContentControllers()) {
        for (auto& entry : m_injectedScriptsPerPatternMap) {
            for (auto& userScript : entry.value)
                userContentController->removeUserScript(userScript);
        }

        for (auto& entry : m_injectedStyleSheetsPerPatternMap) {
            for (auto& userStyleSheet : entry.value)
                userContentController->removeUserStyleSheet(userStyleSheet);
        }
    }

    m_injectedScriptsPerPatternMap.clear();
    m_injectedStyleSheetsPerPatternMap.clear();
}

void WebExtensionContext::removeInjectedContent(const MatchPatternSet& removedMatchPatterns)
{
    if (!isLoaded())
        return;

    for (auto& removedPattern : removedMatchPatterns)
        removeInjectedContent(removedPattern);

    // If "all hosts" was removed, then we need to add back any individual granted hosts,
    // now that the catch all pattern has been removed.
    if (WebExtensionMatchPattern::patternsMatchAllHosts(removedMatchPatterns))
        addInjectedContent();
}

void WebExtensionContext::removeInjectedContent(WebExtensionMatchPattern& pattern)
{
    if (!isLoaded())
        return;

    auto originInjectedScripts = m_injectedScriptsPerPatternMap.take(pattern);
    auto originInjectedStyleSheets = m_injectedStyleSheetsPerPatternMap.take(pattern);

    if (originInjectedScripts.isEmpty() && originInjectedStyleSheets.isEmpty())
        return;

    // Use all user content controllers in case the extension was briefly allowed in private browsing
    // and content was injected into any of those content controllers.
    for (Ref userContentController : extensionController()->allUserContentControllers()) {
        for (auto& userScript : originInjectedScripts)
            userContentController->removeUserScript(userScript);

        for (auto& userStyleSheet : originInjectedStyleSheets)
            userContentController->removeUserStyleSheet(userStyleSheet);
    }
}

void WebExtensionContext::removeInjectedContent(WebUserContentControllerProxy& userContentController)
{
    for (auto& entry : m_injectedScriptsPerPatternMap) {
        for (auto& userScript : entry.value)
            userContentController.removeUserScript(userScript);
    }

    for (auto& entry : m_injectedStyleSheetsPerPatternMap) {
        for (auto& userStyleSheet : entry.value)
            userContentController.removeUserStyleSheet(userStyleSheet);
    }
}

#if ENABLE(DNR_ON_RULE_MATCHED_DEBUG)
void WebExtensionContext::handleContentRuleListMatchedRule(WebExtensionTab& tab, WebCore::ContentRuleListMatchedRule& matchedRule)
{
    // FIXME: <158147119> Figure out the permissions story for onRuleMatchedDebug
    if (!(hasPermission(WebExtensionPermission::declarativeNetRequestFeedback()) && hasPermission(WebExtensionPermission::declarativeNetRequest()) && hasPermission(URL { matchedRule.request.url }, &tab)))
        return;

    wakeUpBackgroundContentIfNecessaryToFireEvents({ WebExtensionEventListenerType::DeclarativeNetRequestOnRuleMatchedDebug }, [=, this, protectedThis = Ref { *this }] {
        sendToProcessesForEvent(WebExtensionEventListenerType::DeclarativeNetRequestOnRuleMatchedDebug, Messages::WebExtensionContextProxy::DispatchOnRuleMatchedDebugEvent(matchedRule));
    });
}
#endif

bool WebExtensionContext::handleContentRuleListNotificationForTab(WebExtensionTab& tab, const URL& url, WebCore::ContentRuleListResults::Result)
{
    incrementActionCountForTab(tab, 1);

    if (!hasPermission(WebExtensionPermission::declarativeNetRequestFeedback()) && !(hasPermission(WebExtensionPermission::declarativeNetRequest()) && hasPermission(url, &tab)))
        return false;

    m_matchedRules.append({
        url,
        WallTime::now(),
        tab.identifier()
    });

    return true;
}

bool WebExtensionContext::purgeMatchedRulesFromBefore(const WallTime& startTime)
{
    if (m_matchedRules.isEmpty())
        return false;

    DeclarativeNetRequestMatchedRuleVector filteredMatchedRules;
    for (auto& matchedRule : m_matchedRules) {
        if (matchedRule.timeStamp >= startTime)
            filteredMatchedRules.append(matchedRule);
    }

    m_matchedRules = WTFMove(filteredMatchedRules);
    return !m_matchedRules.isEmpty();
}

void WebExtensionContext::addDeclarativeNetRequestRulesToPrivateUserContentControllers()
{
    API::ContentRuleListStore::defaultStoreSingleton().lookupContentRuleListFile(declarativeNetRequestContentRuleListFilePath(), uniqueIdentifier().isolatedCopy(), [this, protectedThis = Ref { *this }](RefPtr<API::ContentRuleList> ruleList, std::error_code) {
        if (!ruleList)
            return;

        // The extension could have been unloaded before this was called.
        if (!isLoaded())
            return;

        for (Ref controller : extensionController()->allPrivateUserContentControllers())
            controller->addContentRuleList(*ruleList, m_baseURL);
    });
}

static HashMap<WebExtensionContextIdentifier, WeakRef<WebExtensionContext>>& webExtensionContexts()
{
    static NeverDestroyed<HashMap<WebExtensionContextIdentifier, WeakRef<WebExtensionContext>>> contexts;
    return contexts;
}

WebExtensionContext* WebExtensionContext::get(WebExtensionContextIdentifier identifier)
{
    return webExtensionContexts().get(identifier);
}

WebExtensionContext::WebExtensionContext()
{
    ASSERT(!get(identifier()));
    webExtensionContexts().add(identifier(), *this);
}

WebExtensionContextIdentifier WebExtensionContext::privilegedIdentifier() const
{
    if (!m_privilegedIdentifier)
        m_privilegedIdentifier = WebExtensionContextIdentifier::generate();
    return *m_privilegedIdentifier;
}

bool WebExtensionContext::isPrivilegedMessage(IPC::Decoder& message) const
{
    if (!m_privilegedIdentifier)
        return false;
    return m_privilegedIdentifier.value().toRawValue() == message.destinationID();
}

WebExtensionContextParameters WebExtensionContext::parameters(IncludePrivilegedIdentifier includePrivilegedIdentifier) const
{
    RefPtr extension = m_extension;

    return {
        identifier(),
        includePrivilegedIdentifier == IncludePrivilegedIdentifier::Yes ? std::optional(privilegedIdentifier()) : std::nullopt,
        baseURL(),
        uniqueIdentifier(),
        unsupportedAPIs(),
        m_grantedPermissions,
        extension->serializeLocalization(),
        extension->serializeManifest(),
        extension->manifestVersion(),
        isSessionStorageAllowedInContentScripts(),
        backgroundPageIdentifier(),
#if ENABLE(INSPECTOR_EXTENSIONS)
        inspectorPageIdentifiers(),
        inspectorBackgroundPageIdentifiers(),
#endif
        popupPageIdentifiers(),
        tabPageIdentifiers()
    };
}

bool WebExtensionContext::inTestingMode() const
{
    return m_extensionController && m_extensionController->inTestingMode();
}

const WebExtensionContext::UserContentControllerProxySet& WebExtensionContext::userContentControllers() const
{
    ASSERT(isLoaded());

    if (hasAccessToPrivateData())
        return extensionController()->allUserContentControllers();
    return extensionController()->allNonPrivateUserContentControllers();
}

WebExtensionContext::WebProcessProxySet WebExtensionContext::processes(EventListenerTypeSet&& typeSet, ContentWorldTypeSet&& contentWorldTypeSet, Function<bool(WebProcessProxy&, WebPageProxy&, WebFrameProxy&)>&& predicate) const
{
    if (!isLoaded())
        return { };

#if ENABLE(INSPECTOR_EXTENSIONS)
    // Inspector content world is a special alias of Main. Include it when Main is requested (and vice versa).
    if (contentWorldTypeSet.contains(WebExtensionContentWorldType::Main))
        contentWorldTypeSet.add(WebExtensionContentWorldType::Inspector);
    else if (contentWorldTypeSet.contains(WebExtensionContentWorldType::Inspector))
        contentWorldTypeSet.add(WebExtensionContentWorldType::Main);
#endif

    WebProcessProxySet result;

    for (auto type : typeSet) {
        for (auto contentWorldType : contentWorldTypeSet) {
            auto pagesEntry = m_eventListenerFrames.find({ type, contentWorldType });
            if (pagesEntry == m_eventListenerFrames.end())
                continue;

            for (auto entry : pagesEntry->value) {
                Ref frame = entry.key;
                RefPtr page = frame->page();
                if (!page)
                    continue;

                if (!hasAccessToPrivateData() && page->sessionID().isEphemeral())
                    continue;

                Ref webProcess = frame->process();
                if (predicate && !predicate(webProcess, *page, frame))
                    continue;

                if (webProcess->canSendMessage())
                    result.add(webProcess);
            }
        }
    }

    return result;
}

} // namespace WebKit

#endif // ENABLE(WK_WEB_EXTENSIONS)
