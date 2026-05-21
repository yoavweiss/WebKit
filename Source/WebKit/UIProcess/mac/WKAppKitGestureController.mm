/*
 * Copyright (C) 2025 Apple Inc. All rights reserved.
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

#import "config.h"
#import "WKAppKitGestureController.h"

#if HAVE(APPKIT_GESTURES_SUPPORT)

#import "AppKitSPI.h"
#import "IdentifierTypes.h"
#import "InteractionInformationAtPosition.h"
#import "InteractionInformationRequest.h"
#import "NativeWebWheelEvent.h"
#import "RemoteLayerTreeDrawingAreaProxy.h"
#import "ScrollingAccelerationCurve.h"
#import "ViewGestureController.h"
#import "WKDeferringGestureRecognizer.h"
#import "WKWebView.h"
#import "WebEventModifier.h"
#import "WebEventType.h"
#import "WebMouseEvent.h"
#import "WebPageProxy.h"
#import "WebPreferences.h"
#import "WebViewImpl.h"
#import "WebWheelEvent.h"
#import <Carbon/Carbon.h>
#import <WebCore/Color.h>
#import <WebCore/FloatPoint.h>
#import <WebCore/FloatQuad.h>
#import <WebCore/FloatSize.h>
#import <WebCore/IntPoint.h>
#import <WebCore/IntSize.h>
#import <WebCore/PlatformEventFactoryMac.h>
#import <WebCore/PointerID.h>
#import <WebCore/Scrollbar.h>
#import <source_location>
#import <wtf/BlockPtr.h>
#import <wtf/CheckedPtr.h>
#import <wtf/Markable.h>
#import <wtf/MonotonicTime.h>
#import <wtf/RefPtr.h>
#import <wtf/RetainPtr.h>
#import <wtf/UUID.h>
#import <wtf/WeakPtr.h>

#define WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(pageID, fmt, ...) RELEASE_LOG(ViewGestures, "[pageProxyID=%llu] %s: " fmt, pageID, std::source_location::current().function_name(), ##__VA_ARGS__)

@interface WKPanGestureRecognizer : NSPanGestureRecognizer

@end

@implementation WKPanGestureRecognizer {
    WeakPtr<WebKit::WebPageProxy> _page;
}

- (instancetype)initWithPage:(WebKit::WebPageProxy&)page target:(id)target action:(SEL)action
{
    if (!(self = [super initWithTarget:target action:action]))
        return nil;

    _page = page;

    return self;
}

- (BOOL)shouldRecognizeForDelta:(NSPoint)delta
{
    RefPtr page = _page.get();
    if (!page)
        return NO;

    if (![page->cocoaView() enclosingScrollView])
        return YES;

    auto pinnedState = page->pinnedStateIncludingAncestorsAtPoint([self locationInView:page->cocoaView().get()]);

    if (std::fabs(delta.x) > std::fabs(delta.y)) {
        if (delta.x < 0 && pinnedState.right())
            return NO;
        if (delta.x > 0 && pinnedState.left())
            return NO;
    } else {
        if (delta.y < 0 && pinnedState.top())
            return NO;
        if (delta.y > 0 && pinnedState.bottom())
            return NO;
    }

    return YES;
}

@end

static WebCore::FloatSize translationInView(NSPanGestureRecognizer *gesture, WKWebView *view)
{
    auto translation = WebCore::toFloatSize(WebCore::FloatPoint { [gesture translationInView:view] });
    [gesture setTranslation:NSZeroPoint inView:view];
    return translation;
}

static WebKit::WebWheelEvent::Phase toWheelEventPhase(NSGestureRecognizerState state)
{
    using enum WebKit::WebWheelEvent::Phase;
    switch (state) {
    case NSGestureRecognizerStatePossible:
        return MayBegin;
    case NSGestureRecognizerStateBegan:
        return Began;
    case NSGestureRecognizerStateChanged:
        return Changed;
    case NSGestureRecognizerStateEnded:
        return Ended;
    case NSGestureRecognizerStateCancelled:
    case NSGestureRecognizerStateFailed:
        return Cancelled;
    default:
        ASSERT_NOT_REACHED();
        return None;
    }
}

static WebCore::FloatSize velocityInView(NSPanGestureRecognizer *gesture, WKWebView *view)
{
    return WebCore::toFloatSize(WebCore::FloatPoint { [gesture velocityInView:view] });
}

static WebCore::FloatSize toRawPlatformDelta(WebCore::FloatSize delta)
{
    // rawPlatformDelta uses IOHIDEvent coordinate conventions, which have the opposite
    // sign from WebKit's delta field. This matches WebEventFactory.mm which negates
    // IOHIDEventFieldScrollX/Y values when extracting rawPlatformDelta from real events.
    return -delta;
}

using InteractionInformationCallback = Function<void(const WebKit::InteractionInformationAtPosition&)>;

struct InteractionInformationRequestAndCallback {
    WebKit::InteractionInformationRequest request;
    InteractionInformationCallback callback;
};

static bool representsDraggableElement(const WebKit::InteractionInformationAtPosition& info)
{
    return info.isLink || info.isImage || info.isAttachment || info.isDHTMLDraggable || info.isColorInput || info.prefersDraggingOverTextSelection;
}

@interface WKAppKitGestureController () <NSGestureRecognizerDelegatePrivate, WKDeferringGestureRecognizerDelegate>
@end

@implementation WKAppKitGestureController {
    WeakPtr<WebKit::WebPageProxy> _page;
    WeakPtr<WebKit::WebViewImpl> _viewImpl;

    RetainPtr<NSPanGestureRecognizer> _panGestureRecognizer;
    RetainPtr<NSPressGestureRecognizer> _mouseTrackingGestureRecognizer;
    RetainPtr<NSPressGestureRecognizer> _singleClickGestureRecognizer;
    RetainPtr<NSClickGestureRecognizer> _doubleClickGestureRecognizer;
    RetainPtr<NSPressGestureRecognizer> _secondaryClickGestureRecognizer;

    // Auxiliary gesture recognizers to support drag-and-drop.
    RetainPtr<NSGestureRecognizer> _textSelectionDragGesture;
    RetainPtr<WKDeferringGestureRecognizer> _dragDeferringGestureRecognizer;

    bool _isMomentumActive;

    bool _potentialClickInProgress;
    bool _isClickHighlightIDValid;
    bool _hasHighlightForPotentialClick;
    bool _isExpectingFastClickCommit;
    bool _isSuppressingSingleClickGestureForTextSelection;

    std::optional<WebKit::TransactionID> _layerTreeTransactionIdAtLastInteractionStart;
    Markable<WebKit::ClickIdentifier> _latestClickID;
    WebCore::PointerID _commitPotentialClickPointerId;
    WebCore::FloatPoint _lastInteractionLocationInWebView;

    bool _mouseTrackingHasSentMouseDown;
    WebCore::FloatPoint _mouseTrackingStartLocationInWindow;

    RetainPtr<NSPressGestureRecognizer> _dragPressGestureRecognizer;
    RetainPtr<NSDraggingSession> _gestureDraggingSession;
    BlockPtr<void(NSDraggingSession *)> _textSelectionDragCompletionHandler;
    bool _dragGestureHasSentMouseDown;

    WebKit::InteractionInformationAtPosition _positionInformation;
    std::optional<WebKit::InteractionInformationRequest> _lastOutstandingPositionInformationRequest;
    Vector<std::optional<InteractionInformationRequestAndCallback>> _pendingPositionInformationHandlers;
    uint64_t _positionInformationCallbackDepth;
    bool _hasValidPositionInformation;
}

#if __has_include(<WebKitAdditions/WKAppKitGestureControllerAdditionsImpl.mm>)
#import <WebKitAdditions/WKAppKitGestureControllerAdditionsImpl.mm>
#elif __has_include(<WebKitAdditions/WKAppKitGestureControllerAdditions.mm>)
#import <WebKitAdditions/WKAppKitGestureControllerAdditions.mm>
#endif

- (instancetype)initWithPage:(std::reference_wrapper<WebKit::WebPageProxy>)page viewImpl:(std::reference_wrapper<WebKit::WebViewImpl>)viewImpl
{
    if (!(self = [super init]))
        return nil;

    _page = page.get();
    _viewImpl = viewImpl.get();

    [self setUpGestureRecognizers];
    [self addGesturesToWebView];
    [self enableGesturesIfNeeded];

    return self;
}

- (void)setUpGestureRecognizers
{
    [self setUpPanGestureRecognizer];
    [self setUpMouseTrackingGestureRecognizer];
    [self setUpSingleClickGestureRecognizer];
    [self setUpDoubleClickGestureRecognizer];
    [self setUpSecondaryClickGestureRecognizer];
    [self setUpDragPressGestureRecognizer];
    [self setUpDragDeferringGestureRecognizer];
}

- (void)setUpPanGestureRecognizer
{
    RefPtr page = _page.get();
    if (!page)
        return;

    _panGestureRecognizer = adoptNS([[WKPanGestureRecognizer alloc] initWithPage:*page target:self action:@selector(panGestureRecognized:)]);
    [self configureForScrolling:_panGestureRecognizer.get()];
    [_panGestureRecognizer setDelegate:self];
    [_panGestureRecognizer setName:@"WKPanGesture"];
}

- (void)setUpMouseTrackingGestureRecognizer
{
    _mouseTrackingGestureRecognizer = adoptNS([[NSPressGestureRecognizer alloc] initWithTarget:self action:@selector(mouseTrackingGestureRecognized:)]);
    [self configureForMouseTracking:_mouseTrackingGestureRecognizer.get()];
    [_mouseTrackingGestureRecognizer setDelegate:self];
    [_mouseTrackingGestureRecognizer setName:@"WKMouseTrackingGesture"];
}

- (void)setUpSingleClickGestureRecognizer
{
    _singleClickGestureRecognizer = adoptNS([[NSPressGestureRecognizer alloc] initWithTarget:self action:@selector(singleClickGestureRecognized:)]);
    [self configureForSingleClick:_singleClickGestureRecognizer.get()];
    [_singleClickGestureRecognizer setDelegate:self];
    [_singleClickGestureRecognizer setName:@"WKSingleClickGesture"];
}

- (void)setUpDoubleClickGestureRecognizer
{
    _doubleClickGestureRecognizer = adoptNS([[NSClickGestureRecognizer alloc] initWithTarget:self action:@selector(doubleClickGestureRecognized:)]);
    [self configureForDoubleClick:_doubleClickGestureRecognizer.get()];
    [_doubleClickGestureRecognizer setDelegate:self];
    [_doubleClickGestureRecognizer setName:@"WKDoubleClickGesture"];
}

- (void)setUpSecondaryClickGestureRecognizer
{
    _secondaryClickGestureRecognizer = adoptNS([[NSPressGestureRecognizer alloc] initWithTarget:self action:@selector(secondaryClickGestureRecognized:)]);
    [self configureForSecondaryClick:_secondaryClickGestureRecognizer.get()];
    [_secondaryClickGestureRecognizer setDelegate:self];
    [_secondaryClickGestureRecognizer setName:@"WKSecondaryClickGesture"];
}

- (void)setUpDragPressGestureRecognizer
{
    _dragPressGestureRecognizer = adoptNS([[NSPressGestureRecognizer alloc] initWithTarget:self action:@selector(dragPressGestureRecognized:)]);
    [self configureForDragPress:_dragPressGestureRecognizer];
    [_dragPressGestureRecognizer setDelegate:self];
    [_dragPressGestureRecognizer setName:@"WKDragPressGesture"];
}

- (void)setUpDragDeferringGestureRecognizer
{
    _dragDeferringGestureRecognizer = adoptNS([[WKDeferringGestureRecognizer alloc] initWithDeferringGestureDelegate:self]);
    [self configureForDragDeferral:_dragDeferringGestureRecognizer];
    [_dragDeferringGestureRecognizer setDelegate:self];
    [_dragDeferringGestureRecognizer setName:@"WKDragDeferringGesture"];
}

- (void)addGesturesToWebView
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return;

    [webView addGestureRecognizer:_panGestureRecognizer.get()];
    [webView addGestureRecognizer:_mouseTrackingGestureRecognizer.get()];
    [webView addGestureRecognizer:_singleClickGestureRecognizer.get()];
    [webView addGestureRecognizer:_doubleClickGestureRecognizer.get()];
    [webView addGestureRecognizer:_secondaryClickGestureRecognizer.get()];
    [webView addGestureRecognizer:_dragPressGestureRecognizer.get()];
    [webView addGestureRecognizer:_dragDeferringGestureRecognizer.get()];
}

- (void)enableGesturesIfNeeded
{
    [self enableGestureIfNeeded:_panGestureRecognizer.get()];
    [self enableGestureIfNeeded:_mouseTrackingGestureRecognizer.get()];
    [self enableGestureIfNeeded:_singleClickGestureRecognizer.get()];
    [self enableGestureIfNeeded:_doubleClickGestureRecognizer.get()];
    [self enableGestureIfNeeded:_secondaryClickGestureRecognizer.get()];
    [self enableGestureIfNeeded:_dragPressGestureRecognizer.get()];
}

- (void)enableGestureIfNeeded:(NSGestureRecognizer *)gesture
{
    RefPtr page = _page.get();
    if (!page)
        return;
    bool gestureEnabled = protect(page->preferences())->useAppKitGestures();
    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "%@ setEnabled:%d", gesture, static_cast<int>(gestureEnabled));
    [gesture setEnabled:gestureEnabled];
}

- (void)beginSuppressingSingleClickGestureForTextSelection
{
    RefPtr page = _page.get();
    if (!page) {
        ASSERT_NOT_REACHED();
        return;
    }

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Begin suppressing single-click gesture for text selection");

    _isSuppressingSingleClickGestureForTextSelection = true;

    [self _handleClickCancelled];
    page->cancelPotentialClick();
}

- (void)endSuppressingSingleClickGestureForTextSelection
{
    RefPtr page = _page.get();
    if (!page) {
        ASSERT_NOT_REACHED();
        return;
    }

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "End suppressing single-click gesture for text selection");

    _isSuppressingSingleClickGestureForTextSelection = false;
}

#pragma mark - Gesture Recognition

- (void)panGestureRecognized:(NSGestureRecognizer *)gesture
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "%@", gesture);

    RetainPtr panGesture = dynamic_objc_cast<NSPanGestureRecognizer>(gesture);
    if (!panGesture || _panGestureRecognizer != panGesture)
        return;

    if (viewImpl->ignoresAllEvents()) {
        WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Ignored gesture");
        return;
    }

    if ([panGesture state] == NSGestureRecognizerStateBegan)
        viewImpl->dismissContentRelativeChildWindowsWithAnimation(false);

#if ENABLE(TOP_BANNER_VIEW_OVERLAYS)
    viewImpl->updateBannerViewForPanGesture([panGesture state]);
#endif

    [self sendWheelEventForGesture:panGesture.get()];
    [self startMomentumIfNeededForGesture:panGesture.get()];
}

- (void)singleClickGestureRecognized:(NSGestureRecognizer *)gesture
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "%@", gesture);

    if (_singleClickGestureRecognizer != gesture)
        return;

    // Clicks aren't delivered to NSButton's built-in click gesture
    // recognizer when a parent view's GR recognizes first, so we
    // forward the click manually.
    if (gesture.state == NSGestureRecognizerStateBegan) {
        WebCore::FloatPoint location { [gesture locationInView:webView.get()] };
        if (RetainPtr hitView = viewImpl->hitTestPDFHUD(location); hitView && viewImpl->isViewVisible(hitView.get())) {
            if (RetainPtr hudButton = dynamic_objc_cast<NSButton>(hitView))
                [hudButton performClick:nil];
            gesture.state = NSGestureRecognizerStateCancelled;
            return;
        }
    }

    switch (gesture.state) {
    case NSGestureRecognizerStateBegan:
        [self _handleClickBegan:gesture];
        break;
    case NSGestureRecognizerStateEnded:
        [self _handleClickEnded:gesture];
        break;
    case NSGestureRecognizerStateCancelled:
    case NSGestureRecognizerStateFailed:
        [self _handleClickCancelled];
        break;
    default:
        break;
    }
}

- (void)doubleClickGestureRecognized:(NSGestureRecognizer *)gesture
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "%@", gesture);

    RetainPtr clickGesture = dynamic_objc_cast<NSClickGestureRecognizer>(gesture);
    if (!clickGesture || _doubleClickGestureRecognizer != clickGesture)
        return;

    viewImpl->dismissContentRelativeChildWindowsWithAnimation(false);

    auto magnificationOrigin = [webView convertPoint:[gesture locationInView:nil] fromView:nil];
    protect(viewImpl->ensureGestureController())->handleSmartMagnificationGesture(magnificationOrigin);
}

- (void)secondaryClickGestureRecognized:(NSGestureRecognizer *)gesture
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "%@", gesture);

    if (_secondaryClickGestureRecognizer != gesture)
        return;

ALLOW_NEW_API_WITHOUT_GUARDS_BEGIN
    auto modifierFlags = [gesture modifierFlags];
ALLOW_NEW_API_WITHOUT_GUARDS_END
    auto location = [gesture locationInView:nil];
    auto windowNumber = viewImpl->windowNumber();

    RetainPtr mouseDown = [NSEvent mouseEventWithType:NSEventTypeRightMouseDown location:location modifierFlags:modifierFlags timestamp:GetCurrentEventTime() windowNumber:windowNumber context:NULL eventNumber:0 clickCount:1 pressure:1.0];
    viewImpl->mouseDown(mouseDown.get(), WebKit::WebEventInputSource::Automation);

    RetainPtr mouseUp = [NSEvent mouseEventWithType:NSEventTypeRightMouseUp location:location modifierFlags:modifierFlags timestamp:GetCurrentEventTime() windowNumber:windowNumber context:NULL eventNumber:0 clickCount:1 pressure:0.0];
    viewImpl->mouseUp(mouseUp.get(), WebKit::WebEventInputSource::Automation);
}

- (void)mouseTrackingGestureRecognized:(NSGestureRecognizer *)gesture
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl) {
        ASSERT_NOT_REACHED();
        return;
    }

    RetainPtr webView = viewImpl->view();
    if (!webView) {
        ASSERT_NOT_REACHED();
        return;
    }

    RefPtr page = _page.get();
    if (!page) {
        ASSERT_NOT_REACHED();
        return;
    }

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "%@", gesture);

    if (_dragGestureHasSentMouseDown) {
        WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Exiting early because _dragGestureHasSentMouseDown is true");
        return;
    }

    if (_isSuppressingSingleClickGestureForTextSelection) {
        WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Exiting early because _isSuppressingSingleClickGestureForTextSelection is true");
        return;
    }

    if (_mouseTrackingGestureRecognizer != gesture)
        return;

    if (viewImpl->ignoresAllEvents())
        return;

ALLOW_NEW_API_WITHOUT_GUARDS_BEGIN
    auto modifierFlags = [gesture modifierFlags];
ALLOW_NEW_API_WITHOUT_GUARDS_END
    WebCore::FloatPoint locationInWindow { [gesture locationInView:nil] };
    auto windowNumber = viewImpl->windowNumber();
    auto timestamp = GetCurrentEventTime();

    switch (gesture.state) {
    case NSGestureRecognizerStateBegan:
        _mouseTrackingHasSentMouseDown = false;
        _mouseTrackingStartLocationInWindow = locationInWindow;
        break;

    case NSGestureRecognizerStateChanged: {
        if (!_mouseTrackingHasSentMouseDown) {
            RetainPtr mouseDown = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                location:_mouseTrackingStartLocationInWindow
                modifierFlags:modifierFlags
                timestamp:timestamp
                windowNumber:windowNumber
                context:nil
                eventNumber:0
                clickCount:1
                pressure:1.0];
            viewImpl->mouseDown(mouseDown.get(), WebKit::WebEventInputSource::Automation, WebCore::PlatformMouseEvent::CanInitiateDrag::No);
            _mouseTrackingHasSentMouseDown = true;
        }

        RetainPtr mouseDragged = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged
            location:locationInWindow
            modifierFlags:modifierFlags
            timestamp:timestamp
            windowNumber:windowNumber
            context:nil
            eventNumber:0
            clickCount:1
            pressure:1.0];
        viewImpl->mouseDragged(mouseDragged.get(), WebKit::WebEventInputSource::Automation, WebCore::PlatformMouseEvent::CanInitiateDrag::No);
        break;
    }

    case NSGestureRecognizerStateEnded: {
        if (_mouseTrackingHasSentMouseDown) {
            RetainPtr mouseUp = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp
                location:locationInWindow
                modifierFlags:modifierFlags
                timestamp:timestamp
                windowNumber:windowNumber
                context:nil
                eventNumber:0
                clickCount:1
                pressure:0.0];
            viewImpl->mouseUp(mouseUp.get(), WebKit::WebEventInputSource::Automation, WebCore::PlatformMouseEvent::CanInitiateDrag::No);
        }
        [[fallthrough]];
    }

    case NSGestureRecognizerStateCancelled:
    case NSGestureRecognizerStateFailed:
        _mouseTrackingHasSentMouseDown = false;
        break;

    default:
        break;
    }
}

#pragma mark - WKDeferringGestureRecognizerDelegate

- (BOOL)deferringGestureRecognizer:(WKDeferringGestureRecognizer *)deferringGestureRecognizer shouldDeferOtherGestureRecognizer:(NSGestureRecognizer *)gestureRecognizer
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return NO;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return NO;

    for (NSGestureRecognizer *textSelectionGesture in [[webView textSelectionManager] gesturesForFailureRequirements]) {
        if (gestureRecognizer == textSelectionGesture)
            return deferringGestureRecognizer == _dragDeferringGestureRecognizer;
    }

    return NO;
}

- (BOOL)deferringGestureRecognizer:(WKDeferringGestureRecognizer *)deferringGestureRecognizer shouldDeferGesturesForEventThatWillBeginAction:(NSEvent *)event
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return NO;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return NO;

    if (deferringGestureRecognizer == _dragDeferringGestureRecognizer) {
        NSPoint locationInView = [webView convertPoint:[event locationInWindow] fromView:nil];
        if (viewImpl->isTextSelectedAtPoint(locationInView)) {
            WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(), "Drag deferral: not deferring; text already selected at %@", NSStringFromPoint(locationInView));
            return NO;
        }

        WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(), "Drag deferral: deferring; awaiting position info at %@", NSStringFromPoint(locationInView));

        WebKit::InteractionInformationRequest request { WebCore::IntPoint { locationInView } };
        [self doAfterPositionInformationUpdate:[weakSelf = WeakObjCPtr<WKAppKitGestureController>(self), weakDeferring = WeakObjCPtr<WKDeferringGestureRecognizer>(deferringGestureRecognizer)](const auto& info) {
            RetainPtr strongSelf = weakSelf.get();
            RetainPtr strongDeferring = weakDeferring.get();
            if (!strongSelf || !strongDeferring)
                return;

            auto deferralState = [strongDeferring state];
            if (deferralState != NSGestureRecognizerStatePossible) {
                WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { strongSelf->_page.get() }->logIdentifier(),
                    "Drag deferral: position info arrived after deferring gesture exited Possible (state=%ld); skipping resolution", static_cast<long>(deferralState));
                return;
            }
            bool isDraggable = representsDraggableElement(info);
            WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { strongSelf->_page.get() }->logIdentifier(),
                "Drag deferral resolved: isDraggable=%d (link=%d image=%d attachment=%d dhtml=%d color=%d prefersDrag=%d)",
                isDraggable, info.isLink, info.isImage, info.isAttachment, info.isDHTMLDraggable, info.isColorInput, info.prefersDraggingOverTextSelection);
            [strongDeferring endDeferralShouldPreventGestures:isDraggable];
        } forRequest:request];

        return YES;
    }

    return NO;
}

- (void)deferringGestureRecognizer:(WKDeferringGestureRecognizer *)deferringGestureRecognizer didEndActionWithEvent:(NSEvent *)event
{
    if (deferringGestureRecognizer.state != NSGestureRecognizerStatePossible)
        return;

    if (deferringGestureRecognizer == _dragDeferringGestureRecognizer) {
        WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(), "Drag deferral: press ended before position info arrived; unblocking text selection");
        if (auto abandonedRequest = std::exchange(_lastOutstandingPositionInformationRequest, std::nullopt)) {
            for (auto& slot : _pendingPositionInformationHandlers) {
                if (slot && slot->request.isValidForRequest(*abandonedRequest))
                    slot.reset();
            }
        }
    }

    [deferringGestureRecognizer endDeferralShouldPreventGestures:NO];
}

- (void)deferringGestureRecognizer:(WKDeferringGestureRecognizer *)deferringGestureRecognizer didTransitionToState:(NSGestureRecognizerState)state
{
}

#pragma mark - Position Information

- (void)_invalidateCurrentPositionInformation
{
    _hasValidPositionInformation = false;
    _positionInformation = { };
}

- (void)didCommitLoadForMainFrame
{
    [self _invalidateCurrentPositionInformation];
}

- (void)requestAsynchronousPositionInformationUpdate:(WebKit::InteractionInformationRequest)request
{
    if ([self _currentPositionInformationIsValidForRequest:request])
        return;

    RefPtr page = _page.get();
    if (!page || !page->hasRunningProcess())
        return;

    _lastOutstandingPositionInformationRequest = request;
    page->requestPositionInformation(request);
}

- (void)doAfterPositionInformationUpdate:(Function<void(const WebKit::InteractionInformationAtPosition&)>&&)handler forRequest:(WebKit::InteractionInformationRequest)request
{
    if ([self _currentPositionInformationIsValidForRequest:request]) {
        handler(_positionInformation);
        return;
    }

    _pendingPositionInformationHandlers.constructAndAppend(std::in_place, request, WTF::move(handler));

    if (![self _hasValidOutstandingPositionInformationRequest:request])
        [self requestAsynchronousPositionInformationUpdate:request];
}

- (BOOL)_currentPositionInformationIsValidForRequest:(const WebKit::InteractionInformationRequest&)request
{
    return _hasValidPositionInformation && _positionInformation.request.isValidForRequest(request);
}

- (BOOL)_hasValidOutstandingPositionInformationRequest:(const WebKit::InteractionInformationRequest&)request
{
    return _lastOutstandingPositionInformationRequest.and_then([&request](const auto& outstandingRequest) -> std::optional<bool> {
        return outstandingRequest.isValidForRequest(request);
    }).value_or(false);
}

- (void)_invokeAndRemovePendingHandlersValidForCurrentPositionInformation
{
    ASSERT(_hasValidPositionInformation);

    ++_positionInformationCallbackDepth;
    auto updatedPositionInformation = _positionInformation;

    for (size_t index = 0; index < _pendingPositionInformationHandlers.size(); ++index) {
        auto& slot = _pendingPositionInformationHandlers[index];
        if (!slot)
            continue;
        if (![self _currentPositionInformationIsValidForRequest:slot->request])
            continue;

        auto requestAndHandler = std::exchange(slot, std::nullopt);
        if (requestAndHandler->callback)
            requestAndHandler->callback(updatedPositionInformation);
    }

    if (--_positionInformationCallbackDepth)
        return;

    for (int index = _pendingPositionInformationHandlers.size() - 1; index >= 0; --index) {
        if (!_pendingPositionInformationHandlers[index])
            _pendingPositionInformationHandlers.removeAt(index);
    }
}

- (void)positionInformationDidChange:(const WebKit::InteractionInformationAtPosition&)info
{
    // Handlers run in `_invokeAndRemovePendingHandlersValidForCurrentPositionInformation`
    // can re-enter into client code (e.g. UI delegate callbacks), which may release
    // the WKWebView and tear `self` down before this method returns.
    RetainPtr protectedSelf = self;

    if (_lastOutstandingPositionInformationRequest && info.request.isValidForRequest(*_lastOutstandingPositionInformationRequest))
        _lastOutstandingPositionInformationRequest = std::nullopt;

    auto newInfo = info;
    newInfo.mergeCompatibleOptionalInformation(_positionInformation);

    _positionInformation = newInfo;
    _hasValidPositionInformation = _positionInformation.canBeValid;

    [self _invokeAndRemovePendingHandlersValidForCurrentPositionInformation];
}

- (BOOL)_dragPressShouldBeginAtLocation:(NSPoint)locationInViewCoordinates
{
    WebKit::InteractionInformationRequest request { WebCore::IntPoint { locationInViewCoordinates } };
    int radius = static_cast<int>(std::ceil([_dragPressGestureRecognizer allowableMovement]));
    // FIXME: Migrate to requestDragStart: IPC for an authoritative decision.
    // The heuristic below approximates DragController::draggableElement() by consulting the same element-type and style signals.
    bool isDraggable = representsDraggableElement(_positionInformation);
    bool requestIsValid = _hasValidPositionInformation && _positionInformation.request.isApproximatelyValidForRequest(request, radius);
    bool shouldDrag = requestIsValid && isDraggable;
    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(),
        "Drag-press shouldBegin → %d (hasInfo=%d link=%d image=%d attachment=%d dhtml=%d color=%d prefersDrag=%d radius=%d)",
        shouldDrag, _hasValidPositionInformation,
        _positionInformation.isLink, _positionInformation.isImage, _positionInformation.isAttachment,
        _positionInformation.isDHTMLDraggable, _positionInformation.isColorInput, _positionInformation.prefersDraggingOverTextSelection, radius);
    if (!requestIsValid)
        [self _invalidateCurrentPositionInformation];
    return shouldDrag;
}

#pragma mark - Drag Handling

- (void)dragPressGestureRecognized:(NSGestureRecognizer *)gesture
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "%@", gesture);

    if (_dragPressGestureRecognizer != gesture)
        return;

    if (viewImpl->ignoresAllEvents()) {
        WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Ignored gesture");
        return;
    }

    ALLOW_NEW_API_WITHOUT_GUARDS_BEGIN
    auto modifierFlags = [gesture modifierFlags];
    ALLOW_NEW_API_WITHOUT_GUARDS_END
    NSPoint locationInWindow = [gesture locationInView:nil];
    auto windowNumber = viewImpl->windowNumber();
    auto timestamp = GetCurrentEventTime();

    switch (gesture.state) {
    case NSGestureRecognizerStateBegan: {
        [self _handleClickCancelled];
        _dragGestureHasSentMouseDown = false;

        RetainPtr mouseDown = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:locationInWindow modifierFlags:modifierFlags timestamp:timestamp windowNumber:windowNumber context:nil eventNumber:0 clickCount:1 pressure:1.0];
        viewImpl->mouseDown(mouseDown.get(), WebKit::WebEventInputSource::Automation, WebCore::PlatformMouseEvent::CanInitiateDrag::Yes);
        _dragGestureHasSentMouseDown = true;
        break;
    }
    case NSGestureRecognizerStateChanged: {
        if (!_dragGestureHasSentMouseDown)
            break;
        if (_gestureDraggingSession)
            [_gestureDraggingSession updateDragWithGesture:gesture];
        else {
            RetainPtr mouseDragged = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDragged location:locationInWindow modifierFlags:modifierFlags timestamp:timestamp windowNumber:windowNumber context:nil eventNumber:0 clickCount:1 pressure:1.0];
            viewImpl->mouseDragged(mouseDragged.get(), WebKit::WebEventInputSource::Automation, WebCore::PlatformMouseEvent::CanInitiateDrag::Yes);
        }
        break;
    }
    case NSGestureRecognizerStateEnded:
    case NSGestureRecognizerStateCancelled:
    case NSGestureRecognizerStateFailed: {
        if (!_dragGestureHasSentMouseDown)
            break;
        if (_gestureDraggingSession)
            [_gestureDraggingSession updateDragWithGesture:gesture];

        RetainPtr mouseUp = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:locationInWindow modifierFlags:modifierFlags timestamp:timestamp windowNumber:windowNumber context:nil eventNumber:0 clickCount:1 pressure:0.0];
        viewImpl->mouseUp(mouseUp.get(), WebKit::WebEventInputSource::Automation, WebCore::PlatformMouseEvent::CanInitiateDrag::Yes);

        // We do not clear gesture drag state here since startDrag() may still be in flight via IPC.
        // State is cleared in draggingSessionEnded: (normal completion) or in startDrag() when
        // beginDraggingSessionWithItems:gesture: returns nil (gesture ended before session started).
        break;
    }
    default:
        break;
    }
}

#pragma mark - Click Handling

- (void)_handleClickBegan:(NSGestureRecognizer *)gesture
{
    if (_isSuppressingSingleClickGestureForTextSelection)
        return;

    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    WebCore::FloatPoint position = [gesture locationInView:webView.get()];
    _lastInteractionLocationInWebView = position;

    if (RefPtr drawingArea = page->drawingArea()) {
        if (RefPtr remoteDrawingArea = dynamicDowncast<WebKit::RemoteLayerTreeDrawingAreaProxy>(*drawingArea))
            _layerTreeTransactionIdAtLastInteractionStart = remoteDrawingArea->lastCommittedMainFrameLayerTreeTransactionID();
    }

    _latestClickID = WebKit::ClickIdentifier::generate();
    _potentialClickInProgress = true;
    _isClickHighlightIDValid = true;
    _isExpectingFastClickCommit = ![_doubleClickGestureRecognizer isEnabled];

    page->potentialClickAtPosition(std::nullopt, WebCore::FloatPoint(position), false, *_latestClickID, WebKit::WebEventInputSource::Automation);
}

- (void)_handleClickEnded:(NSGestureRecognizer *)gesture
{
    if (!_potentialClickInProgress)
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    [self _endPotentialClickAndEnableDoubleClickGesturesIfNecessary];

    _commitPotentialClickPointerId = WebCore::mousePointerID;

    if (!_layerTreeTransactionIdAtLastInteractionStart) {
        [self _handleClickCancelled];
        return;
    }

    page->commitPotentialClick(std::nullopt, { }, *_layerTreeTransactionIdAtLastInteractionStart, _commitPotentialClickPointerId);
}

- (void)_handleClickCancelled
{
    if (!_potentialClickInProgress)
        return;

    _potentialClickInProgress = false;
    _isClickHighlightIDValid = false;

    if (RefPtr page = _page.get())
        page->cancelPotentialClick();
}

- (void)_endPotentialClickAndEnableDoubleClickGesturesIfNecessary
{
    _potentialClickInProgress = false;
    [self _setDoubleClickGesturesEnabled:YES];
}

- (void)_setDoubleClickGesturesEnabled:(BOOL)enabled
{
    [_doubleClickGestureRecognizer setEnabled:enabled];
}

#if ENABLE(TWO_PHASE_CLICKS)

#pragma mark - Two-Phase Click Response Handlers

- (BOOL)isPotentialClickInProgress
{
    return _potentialClickInProgress;
}

- (void)didGetClickHighlightForRequest:(WebKit::ClickIdentifier)requestID color:(const WebCore::Color&)color quads:(const Vector<WebCore::FloatQuad>&)highlightedQuads topLeftRadius:(const WebCore::IntSize&)topLeftRadius topRightRadius:(const WebCore::IntSize&)topRightRadius bottomLeftRadius:(const WebCore::IntSize&)bottomLeftRadius bottomRightRadius:(const WebCore::IntSize&)bottomRightRadius nodeHasBuiltInClickHandling:(BOOL)nodeHasBuiltInClickHandling
{
    RefPtr page = _page.get();
    if (!page)
        return;

    if (!_isClickHighlightIDValid || _latestClickID != requestID)
        return;

    _isClickHighlightIDValid = false;
    _hasHighlightForPotentialClick = _potentialClickInProgress;

    // FIXME: Bring up support for click highlighting here.

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Received click highlight for request %llu, nodeHasBuiltInClickHandling=%d", requestID.toUInt64(), nodeHasBuiltInClickHandling);
}

- (void)disableDoubleClickGesturesDuringClickIfNecessary:(WebKit::ClickIdentifier)requestID
{
    if (_latestClickID != requestID)
        return;

    [self _setDoubleClickGesturesEnabled:NO];
}

- (void)commitPotentialClickFailed
{
    RefPtr page = _page.get();
    if (!page)
        return;

    _commitPotentialClickPointerId = 0;
    [self _handleClickCancelled];

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Commit potential click failed");
}

- (void)didCompleteSyntheticClick
{
    RefPtr page = _page.get();
    if (!page)
        return;

    _commitPotentialClickPointerId = 0;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Synthetic click completed");
}

- (void)didHandleClickAsHover
{
    RefPtr page = _page.get();
    if (!page)
        return;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Click was handled as hover");
}

- (void)didNotHandleClickAsClick:(const WebCore::IntPoint&)point
{
    RefPtr page = _page.get();
    if (!page)
        return;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Click at (%d, %d) was not handled as click", point.x(), point.y());

    // FIXME: Consider smart magnification here if a double-click is pending and the point hasn't moved significantly.
}

#endif

#pragma mark - Wheel Event Handling

- (void)sendWheelEventForGesture:(NSPanGestureRecognizer *)gesture
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    auto timestamp = MonotonicTime::fromRawSeconds([gesture timestamp]);
    WebCore::IntPoint position { [gesture locationInView:webView.get()] };
    auto globalPosition { WebCore::globalPoint([gesture locationInView:nil], [webView window]) };
    auto gestureDelta { translationInView(gesture, webView.get()) };
    auto wheelTicks { gestureDelta.scaled(1. / static_cast<float>(WebCore::Scrollbar::pixelsPerLineStep())) };
    auto granularity = WebKit::WebWheelEvent::Granularity::ScrollByPixelWheelEvent;
    bool directionInvertedFromDevice = false;
    auto phase = toWheelEventPhase(gesture.state);
    auto momentumPhase = WebKit::WebWheelEvent::Phase::None;
    bool hasPreciseScrollingDeltas = true;
    uint32_t scrollCount = 1;
    auto unacceleratedScrollingDelta = gestureDelta;
    auto ioHIDEventTimestamp = timestamp;
    std::optional<WebCore::FloatSize> rawPlatformDelta;
    auto momentumEndType = WebKit::WebWheelEvent::MomentumEndType::Unknown;

    WebKit::WebWheelEvent wheelEvent {
        { WebKit::WebEventType::Wheel, { }, timestamp, WTF::UUID::createVersion4() },
        WebCore::IntPoint { position },
        WebCore::IntPoint { globalPosition },
        gestureDelta,
        wheelTicks,
        granularity,
        directionInvertedFromDevice,
        phase,
        momentumPhase,
        hasPreciseScrollingDeltas,
        scrollCount,
        unacceleratedScrollingDelta,
        ioHIDEventTimestamp,
        rawPlatformDelta,
        momentumEndType,
        WebKit::WebEventInputSource::Automation
    };

    WebKit::NativeWebWheelEvent nativeEvent { wheelEvent };

    if (viewImpl->allowsBackForwardNavigationGestures() && protect(viewImpl->ensureGestureController())->handleScrollWheelEvent(nativeEvent)) {
        WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "View gesture controller handled gesture");
        return;
    }

    page->handleNativeWheelEvent(nativeEvent);
}

#pragma mark - Momentum Handling

- (void)startMomentumIfNeededForGesture:(NSPanGestureRecognizer *)gesture
{
    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    if (gesture.state != NSGestureRecognizerStateEnded)
        return;

    auto velocity = velocityInView(gesture, webView.get());
    auto velocityMagnitude = std::max(std::abs(velocity.width()), std::abs(velocity.height()));
    static constexpr float minimumVelocityForMomentum = 20;
    if (velocityMagnitude < minimumVelocityForMomentum)
        return;

    auto timestamp = MonotonicTime::fromRawSeconds([gesture timestamp]);
    WebCore::IntPoint position { [gesture locationInView:webView.get()] };
    auto globalPosition = WebCore::globalPoint([gesture locationInView:nil], [webView window]);

    WebKit::WebWheelEvent momentumEvent {
        { WebKit::WebEventType::Wheel, { }, timestamp, WTF::UUID::createVersion4() },
        position,
        WebCore::IntPoint { globalPosition },
        WebCore::FloatSize { },
        WebCore::FloatSize { },
        WebKit::WebWheelEvent::Granularity::ScrollByPixelWheelEvent,
        false,
        WebKit::WebWheelEvent::Phase::None,
        WebKit::WebWheelEvent::Phase::Began,
        true,
        1,
        WebCore::FloatSize { },
        timestamp,
        std::nullopt,
        WebKit::WebWheelEvent::MomentumEndType::Unknown,
        WebKit::WebEventInputSource::Automation
    };
    WebKit::NativeWebWheelEvent nativeMomentumEvent { momentumEvent };

    nativeMomentumEvent.setRawPlatformDelta([&nativeMomentumEvent, velocity] {
        static constexpr WebCore::FramesPerSecond fallbackMomentumFrameRate { 60 };
        auto momentumFrameRate = WebKit::ScrollingAccelerationCurve::fromNativeWheelEvent(nativeMomentumEvent)
            .or_else([] {
                return WebKit::ScrollingAccelerationCurve::fallbackCurve();
            }).transform([](const auto& curve) {
                return curve.frameRate();
            }).value_or(fallbackMomentumFrameRate);
        auto initialMomentumDelta = velocity / momentumFrameRate;
        return toRawPlatformDelta(initialMomentumDelta);
    }());

    page->handleNativeWheelEvent(nativeMomentumEvent);
    _isMomentumActive = true;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Started momentum scrolling with velocity %.2f pts/s", velocityMagnitude);
}

- (void)interruptMomentumIfNeeded
{
    if (!std::exchange(_isMomentumActive, false))
        return;

    RefPtr page = _page.get();
    if (!page)
        return;

    page->interruptSyntheticMomentumScrolling();
    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(page->logIdentifier(), "Interrupted momentum scrolling");
}

#pragma mark - Drag Gesture State

- (NSGestureRecognizer *)activeDragGestureRecognizer
{
    if (_textSelectionDragGesture)
        return _textSelectionDragGesture.get();
    if (_dragGestureHasSentMouseDown)
        return _dragPressGestureRecognizer.get();
    return nil;
}

- (void)setTextSelectionDragGesture:(NSGestureRecognizer *)gesture completionHandler:(void (^)(NSDraggingSession *))completionHandler
{
    if (_textSelectionDragGesture) {
        WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(),
            "Replacing prior text-selection drag gesture %@ (completion handler set: %d); prior drag never reached setGestureDraggingSession:",
            _textSelectionDragGesture.get(), !!_textSelectionDragCompletionHandler);
        ASSERT_NOT_REACHED();
    }

    _textSelectionDragGesture = gesture;
    _textSelectionDragCompletionHandler = makeBlockPtr(completionHandler);
}

- (void)setGestureDraggingSession:(NSDraggingSession *)session
{
    _gestureDraggingSession = session;
    if (!_gestureDraggingSession)
        return;

    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(), "Drag session began");
    if (auto handler = std::exchange(_textSelectionDragCompletionHandler, nullptr))
        handler(_gestureDraggingSession);
}

- (void)clearGestureDragState
{
    _gestureDraggingSession = nil;
    _textSelectionDragGesture = nil;
    _textSelectionDragCompletionHandler = nullptr;
    _dragGestureHasSentMouseDown = false;
}

- (void)reset
{
    [self clearGestureDragState];
    [self _handleClickCancelled];
    _mouseTrackingHasSentMouseDown = false;
    _isMomentumActive = false;
    _isSuppressingSingleClickGestureForTextSelection = false;
    _latestClickID.reset();
    _layerTreeTransactionIdAtLastInteractionStart.reset();
    [self _invalidateCurrentPositionInformation];
    _lastOutstandingPositionInformationRequest.reset();
    _pendingPositionInformationHandlers.clear();
}

#pragma mark - NSGestureRecognizerDelegate

static BOOL isBuiltInScrollViewPanGestureRecognizer(NSGestureRecognizer *recognizer)
{
    static Class scrollViewPanGestureClass = NSClassFromString(@"NSScrollViewPanGestureRecognizer");
    return [recognizer isKindOfClass:scrollViewPanGestureClass];
}

static inline bool isSamePair(NSGestureRecognizer *a, NSGestureRecognizer *b, NSGestureRecognizer *x, NSGestureRecognizer *y)
{
    return (a == x && b == y) || (b == x && a == y);
}

- (BOOL)gestureRecognizer:(NSGestureRecognizer *)gestureRecognizer shouldRecognizeSimultaneouslyWithGestureRecognizer:(NSGestureRecognizer *)otherGestureRecognizer
{
    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(), "Gesture: %@, Other gesture: %@", gestureRecognizer, otherGestureRecognizer);

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _singleClickGestureRecognizer.get(), _panGestureRecognizer.get()))
        return YES;

    if ([gestureRecognizer isKindOfClass:WKDeferringGestureRecognizer.class] || [otherGestureRecognizer isKindOfClass:WKDeferringGestureRecognizer.class])
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _mouseTrackingGestureRecognizer.get(), _singleClickGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _mouseTrackingGestureRecognizer.get(), _panGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _dragPressGestureRecognizer.get(), _singleClickGestureRecognizer.get()))
        return YES;

    if (isSamePair(gestureRecognizer, otherGestureRecognizer, _dragPressGestureRecognizer.get(), _mouseTrackingGestureRecognizer.get()))
        return YES;

    if (gestureRecognizer == _singleClickGestureRecognizer
        && isBuiltInScrollViewPanGestureRecognizer(otherGestureRecognizer)
        && [otherGestureRecognizer.view isKindOfClass:NSScrollView.class])
        return YES;

    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return NO;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return NO;

    // Allow the single click or mouse tracking GRs to be simultaneously
    // recognized with any of those from the text selection manager.
    for (NSGestureRecognizer *gestureForFailureRequirements in [[webView textSelectionManager] gesturesForFailureRequirements]) {
        if (isSamePair(gestureRecognizer, otherGestureRecognizer, _singleClickGestureRecognizer.get(), gestureForFailureRequirements))
            return YES;
        if (isSamePair(gestureRecognizer, otherGestureRecognizer, _mouseTrackingGestureRecognizer.get(), gestureForFailureRequirements))
            return YES;
    }

    return NO;
}

- (BOOL)gestureRecognizer:(NSGestureRecognizer *)gestureRecognizer shouldBeRequiredToFailByGestureRecognizer:(NSGestureRecognizer *)otherGestureRecognizer
{
    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(), "Gesture: %@, Other gesture: %@", gestureRecognizer, otherGestureRecognizer);

    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return NO;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return NO;

    if (gestureRecognizer == _dragDeferringGestureRecognizer)
        return [_dragDeferringGestureRecognizer shouldDeferGestureRecognizer:otherGestureRecognizer];

    // Fail any gestures from the text selection manager if the secondary click GR handles them.
    for (NSGestureRecognizer *gestureForFailureRequirements in [[webView textSelectionManager] gesturesForFailureRequirements]) {
        if (gestureRecognizer == _secondaryClickGestureRecognizer && otherGestureRecognizer == gestureForFailureRequirements)
            return YES;
    }

    return NO;
}

- (BOOL)gestureRecognizer:(NSGestureRecognizer *)gestureRecognizer shouldRequireFailureOfGestureRecognizer:(NSGestureRecognizer *)otherGestureRecognizer
{
    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(), "Gesture: %@, Other gesture: %@", gestureRecognizer, otherGestureRecognizer);

    if (gestureRecognizer == _singleClickGestureRecognizer && otherGestureRecognizer == _doubleClickGestureRecognizer)
        return YES;

    return NO;
}

- (BOOL)gestureRecognizerShouldBegin:(NSGestureRecognizer *)gestureRecognizer
{
    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(), "Gesture: %@", gestureRecognizer);

    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return NO;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return NO;

    if (gestureRecognizer == _doubleClickGestureRecognizer) {
        if (!viewImpl->allowsMagnification())
            return NO;
    }

    if (gestureRecognizer == _secondaryClickGestureRecognizer) {
        // FIXME: Implement logic for determining if the clicked node is not text.
        return NO;
    }

    if (gestureRecognizer == _singleClickGestureRecognizer || gestureRecognizer == _mouseTrackingGestureRecognizer || gestureRecognizer == _dragPressGestureRecognizer) {
        NSPoint locationInViewCoordinates = [gestureRecognizer locationInView:webView];

        if (gestureRecognizer == _dragPressGestureRecognizer)
            return [self _dragPressShouldBeginAtLocation:locationInViewCoordinates];

        return !viewImpl->isTextSelectedAtPoint(locationInViewCoordinates);
    }

    return YES;
}

- (BOOL)_isScrollOrZoomGestureRecognizer:(NSGestureRecognizer *)gesture
{
    return gesture == _panGestureRecognizer || isBuiltInScrollViewPanGestureRecognizer(gesture) || [gesture isKindOfClass:[NSMagnificationGestureRecognizer class]];
}

- (BOOL)_gestureRecognizer:(NSGestureRecognizer *)preventingGestureRecognizer canPreventGestureRecognizer:(NSGestureRecognizer *)preventedGestureRecognizer
{
    WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG(RefPtr { _page.get() }->logIdentifier(), "Preventing gesture: %@, Prevented gesture: %@", preventingGestureRecognizer, preventedGestureRecognizer);

    CheckedPtr viewImpl = _viewImpl.get();
    if (!viewImpl)
        return NO;

    RetainPtr webView = viewImpl->view();
    if (!webView)
        return NO;

    bool isOurClickGesture = preventingGestureRecognizer == _singleClickGestureRecognizer
        || preventingGestureRecognizer == _secondaryClickGestureRecognizer
        || preventingGestureRecognizer == _mouseTrackingGestureRecognizer
        || preventingGestureRecognizer == _dragPressGestureRecognizer;

    if (!isOurClickGesture)
        return YES;

    if ([self _isScrollOrZoomGestureRecognizer:preventedGestureRecognizer])
        return NO;

    // Don't let our click gestures prevent text selection manager gestures;
    // they should be allowed to recognize simultaneously (per shouldRecognizeSimultaneouslyWithGestureRecognizer:).
    for (NSGestureRecognizer *textSelectionGesture in [[webView textSelectionManager] gesturesForFailureRequirements]) {
        if (preventedGestureRecognizer == textSelectionGesture)
            return NO;
    }

    return YES;
}

@end

#undef WK_APPKIT_GESTURE_CONTROLLER_RELEASE_LOG

#endif // HAVE(APPKIT_GESTURES_SUPPORT)
