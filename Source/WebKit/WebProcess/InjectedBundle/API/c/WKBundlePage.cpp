/*
 * Copyright (C) 2010, 2011, 2013, 2015 Apple Inc. All rights reserved.
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
#include "WKBundlePage.h"
#include "WKBundlePagePrivate.h"

#include "APIArray.h"
#include "APICaptionUserPreferencesTestingModeToken.h"
#include "APIDictionary.h"
#include "APIFrameHandle.h"
#include "APIInjectedBundlePageContextMenuClient.h"
#include "APINumber.h"
#include "APIString.h"
#include "APIURL.h"
#include "APIURLRequest.h"
#include "InjectedBundleNodeHandle.h"
#include "InjectedBundlePageContextMenuClient.h"
#include "InjectedBundlePageEditorClient.h"
#include "InjectedBundlePageFormClient.h"
#include "InjectedBundlePageLoaderClient.h"
#include "InjectedBundlePageResourceLoadClient.h"
#include "InjectedBundlePageUIClient.h"
#include "InjectedBundleScriptWorld.h"
#include "PageBanner.h"
#include "WKAPICast.h"
#include "WKArray.h"
#include "WKBundleAPICast.h"
#include "WKRetainPtr.h"
#include "WKString.h"
#include "WebContextMenu.h"
#include "WebContextMenuItem.h"
#include "WebFrame.h"
#include "WebImage.h"
#include "WebInspectorInternal.h"
#include "WebPage.h"
#include "WebPageGroupProxy.h"
#include "WebPageOverlay.h"
#include "WebProcess.h"
#include <WebCore/AXCoreObject.h>
#include <WebCore/AXObjectCache.h>
#include <WebCore/CSSPropertyParserConsumer+Color.h>
#include <WebCore/CaptionUserPreferences.h>
#include <WebCore/CompositionHighlight.h>
#include <WebCore/FocusController.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/Page.h>
#include <WebCore/PageGroup.h>
#include <WebCore/PageOverlay.h>
#include <WebCore/PageOverlayController.h>
#include <WebCore/RenderLayerCompositor.h>
#include <WebCore/ScriptExecutionContext.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/WheelEventTestMonitor.h>
#include <wtf/StdLibExtras.h>
#include <wtf/URL.h>

WKTypeID WKBundlePageGetTypeID()
{
    return WebKit::toAPI(WebKit::WebPage::APIType);
}

void WKBundlePageSetContextMenuClient(WKBundlePageRef pageRef, WKBundlePageContextMenuClientBase* wkClient)
{
#if ENABLE(CONTEXT_MENUS)
    WebKit::toImpl(pageRef)->setInjectedBundleContextMenuClient(makeUnique<WebKit::InjectedBundlePageContextMenuClient>(wkClient));
#else
    UNUSED_PARAM(pageRef);
    UNUSED_PARAM(wkClient);
#endif
}

void WKBundlePageSetEditorClient(WKBundlePageRef pageRef, WKBundlePageEditorClientBase* wkClient)
{
    WebKit::toImpl(pageRef)->setInjectedBundleEditorClient(wkClient ? makeUnique<WebKit::InjectedBundlePageEditorClient>(*wkClient) : makeUnique<API::InjectedBundle::EditorClient>());
}

void WKBundlePageSetFormClient(WKBundlePageRef pageRef, WKBundlePageFormClientBase* wkClient)
{
    WebKit::toImpl(pageRef)->setInjectedBundleFormClient(makeUnique<WebKit::InjectedBundlePageFormClient>(wkClient));
}

void WKBundlePageSetPageLoaderClient(WKBundlePageRef pageRef, WKBundlePageLoaderClientBase* wkClient)
{
    WebKit::toImpl(pageRef)->setInjectedBundlePageLoaderClient(makeUnique<WebKit::InjectedBundlePageLoaderClient>(wkClient));
}

void WKBundlePageSetResourceLoadClient(WKBundlePageRef pageRef, WKBundlePageResourceLoadClientBase* wkClient)
{
    WebKit::toImpl(pageRef)->setInjectedBundleResourceLoadClient(makeUnique<WebKit::InjectedBundlePageResourceLoadClient>(wkClient));
}

void WKBundlePageSetPolicyClient(WKBundlePageRef, WKBundlePagePolicyClientBase*)
{
}

void WKBundlePageSetUIClient(WKBundlePageRef pageRef, WKBundlePageUIClientBase* wkClient)
{
    WebKit::toImpl(pageRef)->setInjectedBundleUIClient(makeUnique<WebKit::InjectedBundlePageUIClient>(wkClient));
}

WKBundleFrameRef WKBundlePageGetMainFrame(WKBundlePageRef pageRef)
{
    return toAPI(&WebKit::toImpl(pageRef)->mainWebFrame());
}

WKFrameHandleRef WKBundleFrameCreateFrameHandle(WKBundleFrameRef bundleFrameRef)
{
    return WebKit::toAPI(&API::FrameHandle::create(WebKit::toImpl(bundleFrameRef)->frameID()).leakRef());
}

void WKBundlePageClickMenuItem(WKBundlePageRef pageRef, WKContextMenuItemRef item)
{
#if ENABLE(CONTEXT_MENUS)
    WebKit::toImpl(pageRef)->contextMenu().itemSelected(WebKit::toImpl(item)->data());
#else
    UNUSED_PARAM(pageRef);
    UNUSED_PARAM(item);
#endif
}

#if ENABLE(CONTEXT_MENUS)
static Ref<API::Array> contextMenuItems(const WebKit::WebContextMenu& contextMenu)
{
    auto menuItems = contextMenu.items().map([](auto& item) -> RefPtr<API::Object> {
        return WebKit::WebContextMenuItem::create(item);
    });
    return API::Array::create(WTFMove(menuItems));
}
#endif

WKArrayRef WKBundlePageCopyContextMenuItems(WKBundlePageRef pageRef)
{
#if ENABLE(CONTEXT_MENUS)
    Ref contextMenu = WebKit::toImpl(pageRef)->contextMenu();

    return WebKit::toAPI(&contextMenuItems(contextMenu.get()).leakRef());
#else
    UNUSED_PARAM(pageRef);
    return nullptr;
#endif
}

WKArrayRef WKBundlePageCopyContextMenuAtPointInWindow(WKBundlePageRef pageRef, WKPoint point)
{
#if ENABLE(CONTEXT_MENUS)
    RefPtr page = WebKit::toImpl(pageRef)->corePage();
    if (!page)
        return nullptr;

    RefPtr contextMenu = WebKit::toImpl(pageRef)->contextMenuAtPointInWindow(page->mainFrame().frameID(), WebKit::toIntPoint(point));
    if (!contextMenu)
        return nullptr;

    return WebKit::toAPI(&contextMenuItems(*contextMenu).leakRef());
#else
    UNUSED_PARAM(pageRef);
    UNUSED_PARAM(point);
    return nullptr;
#endif
}

void WKBundlePageInsertNewlineInQuotedContent(WKBundlePageRef pageRef)
{
    WebKit::toImpl(pageRef)->insertNewlineInQuotedContent();
}

void WKAccessibilityTestingInjectPreference(WKBundlePageRef pageRef, WKStringRef domain, WKStringRef key, WKStringRef encodedValue)
{
    if (!pageRef)
        return;
    
#if ENABLE(CFPREFS_DIRECT_MODE)
    WebKit::WebProcess::singleton().preferenceDidUpdate(WebKit::toWTFString(domain), WebKit::toWTFString(key), WebKit::toWTFString(encodedValue));
#endif
}

void WKAccessibilityEnable()
{
    WebCore::AXObjectCache::enableAccessibility();
}

void* WKAccessibilityFocusedObject(WKBundlePageRef pageRef)
{
    if (!pageRef)
        return nullptr;

    RefPtr page = WebKit::toImpl(pageRef)->corePage();
    if (!page)
        return nullptr;

    RefPtr focusedOrMainFrame = page->focusController().focusedOrMainFrame();
    if (!focusedOrMainFrame)
        return nullptr;
    RefPtr focusedDocument = focusedOrMainFrame->document();
    if (!focusedDocument)
        return nullptr;

    WebCore::AXObjectCache::enableAccessibility();

    auto* axObjectCache = focusedDocument->axObjectCache();
    if (!axObjectCache)
        return nullptr;

    RefPtr focus = axObjectCache->focusedObjectForPage(page.get());
    return focus ? focus->wrapper() : nullptr;
}

void* WKAccessibilityFocusedUIElement()
{
#if PLATFORM(COCOA)
    return WebKit::WebProcess::accessibilityFocusedUIElement();
#else
    return 0;
#endif
}

void WKAccessibilityAnnounce(WKBundlePageRef pageRef, WKStringRef message)
{
    if (!pageRef)
        return;

    RefPtr page = WebKit::toImpl(pageRef)->corePage();
    if (!page)
        return;

    RefPtr localMainFrame = dynamicDowncast<WebCore::LocalFrame>(page->mainFrame());
    if (!localMainFrame)
        return;

    Ref core = *localMainFrame;
    if (!core->document())
        return;

    if (auto* cache = core->document()->axObjectCache())
        cache->announce(WebKit::toWTFString(message));
}

void WKAccessibilitySetForceDeferredSpellChecking(bool shouldForce)
{
    WebCore::AXObjectCache::setForceDeferredSpellChecking(shouldForce);
}

void WKAccessibilityEnableEnhancedAccessibility(bool enable)
{
    WebCore::AXObjectCache::setEnhancedUserInterfaceAccessibility(enable);
}

bool WKAccessibilityEnhancedAccessibilityEnabled()
{
    return WebCore::AXObjectCache::accessibilityEnhancedUserInterfaceEnabled();
}

void WKAccessibilitySetForceInitialFrameCaching(bool shouldForce)
{
    WebCore::AXObjectCache::setForceInitialFrameCaching(shouldForce);
}

void WKBundlePageSetEditable(WKBundlePageRef pageRef, bool isEditable)
{
    WebKit::WebPage* webPage = WebKit::toImpl(pageRef);
    if (WebCore::Page* page = webPage ? webPage->corePage() : nullptr)
        page->setEditable(isEditable);
}

void WKBundlePageSetDefersLoading(WKBundlePageRef, bool)
{
}

WKStringRef WKBundlePageCopyRenderTreeExternalRepresentation(WKBundlePageRef pageRef, RenderTreeExternalRepresentationBehavior options)
{
    // Convert to webcore options.
    return WebKit::toCopiedAPI(WebKit::toImpl(pageRef)->renderTreeExternalRepresentation(options));
}

WKStringRef WKBundlePageCopyRenderTreeExternalRepresentationForPrinting(WKBundlePageRef pageRef)
{
    return WebKit::toCopiedAPI(WebKit::toImpl(pageRef)->renderTreeExternalRepresentationForPrinting());
}

void WKBundlePageClose(WKBundlePageRef pageRef)
{
    WebKit::toImpl(pageRef)->sendClose();
}

double WKBundlePageGetTextZoomFactor(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->textZoomFactor();
}

double WKBundlePageGetPageZoomFactor(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->pageZoomFactor();
}

WKStringRef WKBundlePageDumpHistoryForTesting(WKBundlePageRef page, WKStringRef directory)
{
    return WebKit::toCopiedAPI(WebKit::toImpl(page)->dumpHistoryForTesting(WebKit::toWTFString(directory)));
}

WKBundleBackForwardListRef WKBundlePageGetBackForwardList(WKBundlePageRef pageRef)
{
    return nullptr;
}

void WKBundlePageInstallPageOverlay(WKBundlePageRef pageRef, WKBundlePageOverlayRef pageOverlayRef)
{
    WebKit::toImpl(pageRef)->corePage()->pageOverlayController().installPageOverlay(*WebKit::toImpl(pageOverlayRef)->coreOverlay(), WebCore::PageOverlay::FadeMode::DoNotFade);
}

void WKBundlePageUninstallPageOverlay(WKBundlePageRef pageRef, WKBundlePageOverlayRef pageOverlayRef)
{
    WebKit::toImpl(pageRef)->corePage()->pageOverlayController().uninstallPageOverlay(*WebKit::toImpl(pageOverlayRef)->coreOverlay(), WebCore::PageOverlay::FadeMode::DoNotFade);
}

void WKBundlePageInstallPageOverlayWithAnimation(WKBundlePageRef pageRef, WKBundlePageOverlayRef pageOverlayRef)
{
    WebKit::toImpl(pageRef)->corePage()->pageOverlayController().installPageOverlay(*WebKit::toImpl(pageOverlayRef)->coreOverlay(), WebCore::PageOverlay::FadeMode::Fade);
}

void WKBundlePageUninstallPageOverlayWithAnimation(WKBundlePageRef pageRef, WKBundlePageOverlayRef pageOverlayRef)
{
    WebKit::toImpl(pageRef)->corePage()->pageOverlayController().uninstallPageOverlay(*WebKit::toImpl(pageOverlayRef)->coreOverlay(), WebCore::PageOverlay::FadeMode::Fade);
}

void WKBundlePageSetTopOverhangImage(WKBundlePageRef pageRef, WKImageRef imageRef)
{
#if PLATFORM(MAC)
    WebKit::toImpl(pageRef)->setTopOverhangImage(WebKit::toImpl(imageRef));
#else
    UNUSED_PARAM(pageRef);
    UNUSED_PARAM(imageRef);
#endif
}

void WKBundlePageSetBottomOverhangImage(WKBundlePageRef pageRef, WKImageRef imageRef)
{
#if PLATFORM(MAC)
    WebKit::toImpl(pageRef)->setBottomOverhangImage(WebKit::toImpl(imageRef));
#else
    UNUSED_PARAM(pageRef);
    UNUSED_PARAM(imageRef);
#endif
}

#if !PLATFORM(IOS_FAMILY)
void WKBundlePageSetHeaderBanner(WKBundlePageRef pageRef, WKBundlePageBannerRef bannerRef)
{
    WebKit::toImpl(pageRef)->setHeaderPageBanner(WebKit::toImpl(bannerRef));
}

void WKBundlePageSetFooterBanner(WKBundlePageRef pageRef, WKBundlePageBannerRef bannerRef)
{
    WebKit::toImpl(pageRef)->setFooterPageBanner(WebKit::toImpl(bannerRef));
}
#endif // !PLATFORM(IOS_FAMILY)

bool WKBundlePageHasLocalDataForURL(WKBundlePageRef pageRef, WKURLRef urlRef)
{
    return WebKit::toImpl(pageRef)->corePage()->hasLocalDataForURL(URL { WebKit::toWTFString(urlRef) });
}

bool WKBundlePageCanHandleRequest(WKURLRequestRef requestRef)
{
    if (!requestRef)
        return false;
    return WebKit::WebPage::canHandleRequest(WebKit::toImpl(requestRef)->resourceRequest());
}

void WKBundlePageReplaceStringMatches(WKBundlePageRef pageRef, WKArrayRef matchIndicesRef, WKStringRef replacementText, bool selectionOnly)
{
    RefPtr matchIndices = WebKit::toImpl(matchIndicesRef);

    Vector<uint32_t> indices;
    indices.reserveInitialCapacity(matchIndices->size());

    auto numberOfMatchIndices = matchIndices->size();
    for (size_t i = 0; i < numberOfMatchIndices; ++i) {
        if (RefPtr indexAsObject = matchIndices->at<API::UInt64>(i))
            indices.append(indexAsObject->value());
    }
    WebKit::toImpl(pageRef)->replaceStringMatchesFromInjectedBundle(indices, WebKit::toWTFString(replacementText), selectionOnly);
}

WKImageRef WKBundlePageCreateSnapshotWithOptions(WKBundlePageRef pageRef, WKRect rect, WKSnapshotOptions options)
{
    RefPtr<WebKit::WebImage> webImage = WebKit::toImpl(pageRef)->scaledSnapshotWithOptions(WebKit::toIntRect(rect), 1, WebKit::toSnapshotOptions(options));
    return toAPI(webImage.leakRef());
}

WKImageRef WKBundlePageCreateSnapshotInViewCoordinates(WKBundlePageRef pageRef, WKRect rect, WKImageOptions options)
{
    auto snapshotOptions = WebKit::snapshotOptionsFromImageOptions(options);
    snapshotOptions.add(WebKit::SnapshotOption::InViewCoordinates);
    RefPtr<WebKit::WebImage> webImage = WebKit::toImpl(pageRef)->scaledSnapshotWithOptions(WebKit::toIntRect(rect), 1, snapshotOptions);
    return toAPI(webImage.leakRef());
}

WKImageRef WKBundlePageCreateSnapshotInDocumentCoordinates(WKBundlePageRef pageRef, WKRect rect, WKImageOptions options)
{
    RefPtr<WebKit::WebImage> webImage = WebKit::toImpl(pageRef)->scaledSnapshotWithOptions(WebKit::toIntRect(rect), 1, WebKit::snapshotOptionsFromImageOptions(options));
    return toAPI(webImage.leakRef());
}

WKImageRef WKBundlePageCreateScaledSnapshotInDocumentCoordinates(WKBundlePageRef pageRef, WKRect rect, double scaleFactor, WKImageOptions options)
{
    RefPtr<WebKit::WebImage> webImage = WebKit::toImpl(pageRef)->scaledSnapshotWithOptions(WebKit::toIntRect(rect), scaleFactor, WebKit::snapshotOptionsFromImageOptions(options));
    return toAPI(webImage.leakRef());
}

double WKBundlePageGetBackingScaleFactor(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->deviceScaleFactor();
}

void WKBundlePageListenForLayoutMilestones(WKBundlePageRef pageRef, WKLayoutMilestones milestones)
{
    WebKit::toImpl(pageRef)->listenForLayoutMilestones(WebKit::toLayoutMilestones(milestones));
}

void WKBundlePageCloseInspectorForTest(WKBundlePageRef page)
{
    WebKit::toImpl(page)->inspector()->close();
}

void WKBundlePageEvaluateScriptInInspectorForTest(WKBundlePageRef page, WKStringRef script)
{
    WebKit::toImpl(page)->inspector()->evaluateScriptForTest(WebKit::toWTFString(script));
}

void WKBundlePageForceRepaint(WKBundlePageRef page)
{
    WebKit::toImpl(page)->updateRenderingWithForcedRepaintWithoutCallback();
}

void WKBundlePageFlushPendingEditorStateUpdate(WKBundlePageRef page)
{
    WebKit::toImpl(page)->flushPendingEditorStateUpdate();
}

uint64_t WKBundlePageGetRenderTreeSize(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->renderTreeSize();
}

// This function should be kept around for compatibility with SafariForWebKitDevelopment.
void WKBundlePageCopyRenderTree(WKBundlePageRef pageRef)
{
}

// This function should be kept around for compatibility with SafariForWebKitDevelopment.
void WKBundlePageCopyRenderLayerTree(WKBundlePageRef pageRef)
{
}

void WKBundlePageSetPaintedObjectsCounterThreshold(WKBundlePageRef, uint64_t)
{
    // FIXME: This function is only still here to keep open source Mac builds building.
    // We should remove it as soon as we can.
}

bool WKBundlePageIsTrackingRepaints(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->isTrackingRepaints();
}

WKArrayRef WKBundlePageCopyTrackedRepaintRects(WKBundlePageRef pageRef)
{
    return WebKit::toAPI(&WebKit::toImpl(pageRef)->trackedRepaintRects().leakRef());
}

void WKBundlePageSetComposition(WKBundlePageRef pageRef, WKStringRef text, int from, int length, bool suppressUnderline, WKArrayRef highlightData, WKArrayRef annotationData)
{
    Vector<WebCore::CompositionHighlight> highlights;
    if (highlightData) {
        RefPtr highlightDataArray = WebKit::toImpl(highlightData);
        highlights.reserveInitialCapacity(highlightDataArray->size());
        for (RefPtr dictionary : highlightDataArray->elementsOfType<API::Dictionary>()) {
            auto startOffset = downcast<API::UInt64>(dictionary->get("from"_s))->value();

            std::optional<WebCore::Color> backgroundHighlightColor;
            std::optional<WebCore::Color> foregroundHighlightColor;

            if (RefPtr backgroundColor = dictionary->get("color"_s))
                backgroundHighlightColor = WebCore::CSSPropertyParserHelpers::deprecatedParseColorRawWithoutContext(downcast<API::String>(backgroundColor)->string());

            if (RefPtr foregroundColor = dictionary->get("foregroundColor"_s))
                foregroundHighlightColor = WebCore::CSSPropertyParserHelpers::deprecatedParseColorRawWithoutContext(downcast<API::String>(foregroundColor)->string());

            highlights.append({
                static_cast<unsigned>(startOffset),
                static_cast<unsigned>(startOffset + downcast<API::UInt64>(dictionary->get("length"_s))->value()),
                backgroundHighlightColor,
                foregroundHighlightColor
            });
        }
    }

    HashMap<String, Vector<WebCore::CharacterRange>> annotations;
    if (annotationData) {
        if (RefPtr annotationDataArray = WebKit::toImpl(annotationData)) {
            for (RefPtr dictionary : annotationDataArray->elementsOfType<API::Dictionary>()) {
                auto location = downcast<API::UInt64>(dictionary->get("from"_s))->value();
                auto length = downcast<API::UInt64>(dictionary->get("length"_s))->value();
                auto name = downcast<API::String>(dictionary->get("annotation"_s))->string();

                auto it = annotations.find(name);
                if (it == annotations.end())
                    annotations.add(name, Vector<WebCore::CharacterRange> { { location, length } });
                else
                    it->value.append({ location, length });
            }
        }
    }

    WebKit::toImpl(pageRef)->setCompositionForTesting(WebKit::toWTFString(text), from, length, suppressUnderline, highlights, annotations);
}

bool WKBundlePageHasComposition(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->hasCompositionForTesting();
}

void WKBundlePageConfirmComposition(WKBundlePageRef pageRef)
{
    WebKit::toImpl(pageRef)->confirmCompositionForTesting(String());
}

void WKBundlePageConfirmCompositionWithText(WKBundlePageRef pageRef, WKStringRef text)
{
    WebKit::toImpl(pageRef)->confirmCompositionForTesting(WebKit::toWTFString(text));
}

void WKBundlePageSetUseDarkAppearance(WKBundlePageRef pageRef, bool useDarkAppearance)
{
    RefPtr webPage = WebKit::toImpl(pageRef);
    if (RefPtr page = webPage ? webPage->corePage() : nullptr)
        page->setUseColorAppearance(useDarkAppearance, page->useElevatedUserInterfaceLevel());
}

bool WKBundlePageIsUsingDarkAppearance(WKBundlePageRef pageRef)
{
    RefPtr webPage = WebKit::toImpl(pageRef);
    if (RefPtr page = webPage ? webPage->corePage() : nullptr)
        return page->useDarkAppearance();
    return false;
}

bool WKBundlePageCanShowMIMEType(WKBundlePageRef pageRef, WKStringRef mimeTypeRef)
{
    return WebKit::toImpl(pageRef)->canShowMIMEType(WebKit::toWTFString(mimeTypeRef));
}

WKRenderingSuppressionToken WKBundlePageExtendIncrementalRenderingSuppression(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->extendIncrementalRenderingSuppression();
}

void WKBundlePageStopExtendingIncrementalRenderingSuppression(WKBundlePageRef pageRef, WKRenderingSuppressionToken token)
{
    WebKit::toImpl(pageRef)->stopExtendingIncrementalRenderingSuppression(token);
}

bool WKBundlePageIsUsingEphemeralSession(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->usesEphemeralSession();
}

bool WKBundlePageIsControlledByAutomation(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->isControlledByAutomation();
}

#if TARGET_OS_IPHONE
void WKBundlePageSetUseTestingViewportConfiguration(WKBundlePageRef pageRef, bool useTestingViewportConfiguration)
{
    WebKit::toImpl(pageRef)->setUseTestingViewportConfiguration(useTestingViewportConfiguration);
}
#endif

void WKBundlePageStartMonitoringScrollOperations(WKBundlePageRef pageRef, bool clearLatchingState)
{
    RefPtr webPage = WebKit::toImpl(pageRef);
    RefPtr page = webPage ? webPage->corePage() : nullptr;
    
    if (!page)
        return;

    page->startMonitoringWheelEvents(clearLatchingState);
}

bool WKBundlePageRegisterScrollOperationCompletionCallback(WKBundlePageRef pageRef, WKBundlePageTestNotificationCallback callback, bool expectWheelEndOrCancel, bool expectMomentumEnd, void* context)
{
    if (!callback)
        return false;
    
    RefPtr webPage = WebKit::toImpl(pageRef);
    RefPtr page = webPage ? webPage->corePage() : nullptr;
    if (!page || !page->isMonitoringWheelEvents())
        return false;
    
    if (auto wheelEventTestMonitor = page->wheelEventTestMonitor()) {
        wheelEventTestMonitor->setTestCallbackAndStartMonitoring(expectWheelEndOrCancel, expectMomentumEnd, [=]() {
            callback(context);
        });
    }
    return true;
}

void WKBundlePageCallAfterTasksAndTimers(WKBundlePageRef pageRef, WKBundlePageTestNotificationCallback callback, void* context)
{
    if (!callback)
        return;
    
    RefPtr webPage = WebKit::toImpl(pageRef);
    RefPtr page = webPage ? webPage->corePage() : nullptr;
    if (!page)
        return;
    
    RefPtr localMainFrame = dynamicDowncast<WebCore::LocalFrame>(page->mainFrame());
    if (!localMainFrame)
        return;

    RefPtr document = localMainFrame->document();
    if (!document)
        return;
    
    document->postTask([=] (WebCore::ScriptExecutionContext&) {
        WebCore::Timer::schedule(0_s, [=] {
            callback(context);
        });
    });
}

void WKBundlePageFlushDeferredDidReceiveMouseEventForTesting(WKBundlePageRef page)
{
    WebKit::toImpl(page)->flushDeferredDidReceiveMouseEvent();
}

void WKBundlePagePostMessage(WKBundlePageRef pageRef, WKStringRef messageNameRef, WKTypeRef messageBodyRef)
{
    WebKit::toImpl(pageRef)->postMessage(WebKit::toWTFString(messageNameRef), WebKit::toImpl(messageBodyRef));
}

void WKBundlePagePostMessageWithAsyncReply(WKBundlePageRef page, WKStringRef messageName, WKTypeRef messageBody, WKBundlePageMessageReplyCallback replyCallback, void* context)
{
    WebKit::toImpl(page)->postMessageWithAsyncReply(WebKit::toWTFString(messageName), WebKit::toImpl(messageBody), [replyCallback, context] (API::Object* reply) mutable {
        replyCallback(WebKit::toAPI(reply), context);
    });
}

void WKBundlePagePostMessageIgnoringFullySynchronousMode(WKBundlePageRef pageRef, WKStringRef messageNameRef, WKTypeRef messageBodyRef)
{
    WebKit::toImpl(pageRef)->postMessageIgnoringFullySynchronousMode(WebKit::toWTFString(messageNameRef), WebKit::toImpl(messageBodyRef));
}

void WKBundlePagePostSynchronousMessageForTesting(WKBundlePageRef pageRef, WKStringRef messageNameRef, WKTypeRef messageBodyRef, WKTypeRef* returnRetainedDataRef)
{
    RefPtr<API::Object> returnData;
    WebKit::toImpl(pageRef)->postSynchronousMessageForTesting(WebKit::toWTFString(messageNameRef), WebKit::toImpl(messageBodyRef), returnData);
    if (returnRetainedDataRef)
        *returnRetainedDataRef = WebKit::toAPI(returnData.leakRef());
}

bool WKBundlePageIsSuspended(WKBundlePageRef pageRef)
{
    return WebKit::toImpl(pageRef)->isSuspended();
}

void WKBundlePageAddUserScript(WKBundlePageRef pageRef, WKStringRef source, _WKUserScriptInjectionTime injectionTime, WKUserContentInjectedFrames injectedFrames)
{
    WebKit::toImpl(pageRef)->addUserScript(WebKit::toWTFString(source), WebKit::InjectedBundleScriptWorld::normalWorldSingleton(), WebKit::toUserContentInjectedFrames(injectedFrames), WebKit::toUserScriptInjectionTime(injectionTime));
}

void WKBundlePageAddUserScriptInWorld(WKBundlePageRef page, WKStringRef source, WKBundleScriptWorldRef scriptWorld, _WKUserScriptInjectionTime injectionTime, WKUserContentInjectedFrames injectedFrames)
{
    WebKit::toImpl(page)->addUserScript(WebKit::toWTFString(source), *WebKit::toImpl(scriptWorld), WebKit::toUserContentInjectedFrames(injectedFrames), WebKit::toUserScriptInjectionTime(injectionTime));
}

void WKBundlePageAddUserStyleSheet(WKBundlePageRef pageRef, WKStringRef source, WKUserContentInjectedFrames injectedFrames)
{
    WebKit::toImpl(pageRef)->addUserStyleSheet(WebKit::toWTFString(source), WebKit::toUserContentInjectedFrames(injectedFrames));
}

void WKBundlePageRemoveAllUserContent(WKBundlePageRef pageRef)
{
    WebKit::toImpl(pageRef)->removeAllUserContent();
}

WKStringRef WKBundlePageCopyGroupIdentifier(WKBundlePageRef pageRef)
{
    return WebKit::toCopiedAPI(WebKit::toImpl(pageRef)->pageGroup()->identifier());
}

void WKBundlePageSetCaptionDisplayMode(WKBundlePageRef page, WKStringRef mode)
{
#if ENABLE(VIDEO)
    Ref captionPreferences = WebKit::toImpl(page)->corePage()->group().ensureCaptionPreferences();
    auto displayMode = WTF::EnumTraits<WebCore::CaptionUserPreferences::CaptionDisplayMode>::fromString(WebKit::toWTFString(mode));
    if (displayMode.has_value())
        captionPreferences->setCaptionDisplayMode(displayMode.value());
#else
    UNUSED_PARAM(page);
    UNUSED_PARAM(mode);
#endif
}

WKCaptionUserPreferencesTestingModeTokenRef WKBundlePageCreateCaptionUserPreferencesTestingModeToken(WKBundlePageRef page)
{
#if ENABLE(VIDEO)
    Ref captionPreferences = WebKit::toImpl(page)->corePage()->group().ensureCaptionPreferences();
    return WebKit::toAPI(&API::CaptionUserPreferencesTestingModeToken::create(captionPreferences.get()).leakRef());
#else
    UNUSED_PARAM(page);
    return { };
#endif
}

void WKBundlePageLayoutIfNeeded(WKBundlePageRef page)
{
    WebKit::toImpl(page)->layoutIfNeeded();
}

void WKBundlePageSetSkipDecidePolicyForResponseIfPossible(WKBundlePageRef page, bool skip)
{
    WebKit::toImpl(page)->setSkipDecidePolicyForResponseIfPossible(skip);
}

WKStringRef WKBundlePageCopyFrameTextForTesting(WKBundlePageRef page, bool includeSubframes)
{
    return WebKit::toAPI(&API::String::create(WebKit::toImpl(page)->frameTextForTestingIncludingSubframes(includeSubframes)).leakRef());
}
