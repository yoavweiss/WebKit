/*
 * Copyright (C) 2006-2025 Apple Inc. All rights reserved.
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

#import "config.h"
#import "EventHandler.h"

#if PLATFORM(IOS_FAMILY)

#import "AXObjectCache.h"
#import "AutoscrollController.h"
#import "Chrome.h"
#import "ChromeClient.h"
#import "ContainerNodeInlines.h"
#import "DataTransfer.h"
#import "DocumentInlines.h"
#import "DragState.h"
#import "EventNames.h"
#import "FocusController.h"
#import "FrameLoader.h"
#import "HandleUserInputEventResult.h"
#import "KeyboardEvent.h"
#import "LocalFrame.h"
#import "LocalFrameView.h"
#import "MouseEventTypes.h"
#import "MouseEventWithHitTestResults.h"
#import "NodeIdentifier.h"
#import "Page.h"
#import "Pasteboard.h"
#import "PlatformEventFactoryIOS.h"
#import "PlatformKeyboardEvent.h"
#import "RemoteFrame.h"
#import "RemoteFrameGeometryTransformer.h"
#import "RemoteFrameView.h"
#import "RenderWidgetInlines.h"
#import "RenderObjectInlines.h"
#import "ScrollingCoordinatorTypes.h"
#import "WAKView.h"
#import "WAKWindow.h"
#import "WebEvent.h"
#import <wtf/BlockObjCExceptions.h>
#import <wtf/NeverDestroyed.h>
#import <wtf/Noncopyable.h>
#import <wtf/SetForScope.h>

#if ENABLE(CONTENT_CHANGE_OBSERVER)
#import "ContentChangeObserver.h"
#endif

#if ENABLE(IOS_TOUCH_EVENTS)
#import <WebKitAdditions/EventHandlerIOSTouch.cpp>
#endif

#if ENABLE(MODEL_PROCESS)
#import "DOMMatrixReadOnly.h"
#import "DOMPointReadOnly.h"
#import "HTMLModelElement.h"
#endif

namespace WebCore {

static RetainPtr<WebEvent>& currentEventSlot()
{
    static NeverDestroyed<RetainPtr<WebEvent>> event;
    return event;
}

WebEvent *EventHandler::currentEvent()
{
    return currentEventSlot().get();
}

class CurrentEventScope {
    WTF_MAKE_NONCOPYABLE(CurrentEventScope);
public:
    CurrentEventScope(WebEvent *);
    ~CurrentEventScope();

private:
    const RetainPtr<WebEvent> m_savedCurrentEvent;
#if ASSERT_ENABLED
    const RetainPtr<WebEvent> m_event;
#endif
};

inline CurrentEventScope::CurrentEventScope(WebEvent *event)
    : m_savedCurrentEvent(currentEventSlot())
#if ASSERT_ENABLED
    , m_event(event)
#endif
{
    currentEventSlot() = event;
}

inline CurrentEventScope::~CurrentEventScope()
{
    ASSERT(currentEventSlot() == m_event);
    currentEventSlot() = m_savedCurrentEvent;
}

bool EventHandler::wheelEvent(WebEvent *event)
{
    RefPtr page = m_frame->page();
    if (!page)
        return false;

    CurrentEventScope scope(event);

    auto wheelEvent = PlatformEventFactory::createPlatformWheelEvent(event);
    OptionSet<WheelEventProcessingSteps> processingSteps = { WheelEventProcessingSteps::SynchronousScrolling, WheelEventProcessingSteps::BlockingDOMEventDispatch };

    if (wheelEvent.isGestureStart())
        m_wheelScrollGestureState = std::nullopt;
    else if (wheelEvent.phase() == PlatformWheelEventPhase::Changed || wheelEvent.momentumPhase() == PlatformWheelEventPhase::Changed) {
        if (m_wheelScrollGestureState && *m_wheelScrollGestureState == WheelScrollGestureState::NonBlocking)
            processingSteps = { WheelEventProcessingSteps::SynchronousScrolling, WheelEventProcessingSteps::NonBlockingDOMEventDispatch };
    }

    auto [result, _] = handleWheelEvent(wheelEvent, processingSteps);
    event.wasHandled = result.wasHandled();
    return event.wasHandled;
}

#if ENABLE(IOS_TOUCH_EVENTS)

bool EventHandler::dispatchSimulatedTouchEvent(IntPoint location)
{
    bool handled = handleTouchEvent(PlatformEventFactory::createPlatformSimulatedTouchEvent(PlatformEvent::Type::TouchStart, location)).value_or(false);
    handled |= handleTouchEvent(PlatformEventFactory::createPlatformSimulatedTouchEvent(PlatformEvent::Type::TouchEnd, location)).value_or(false);
    return handled;
}
    
void EventHandler::touchEvent(WebEvent *event)
{
    CurrentEventScope scope(event);

    event.wasHandled = handleTouchEvent(PlatformEventFactory::createPlatformTouchEvent(event)).value_or(false);
}
#endif

bool EventHandler::keyEvent(WebEvent *event)
{
    BEGIN_BLOCK_OBJC_EXCEPTIONS

    ASSERT(event.type == WebEventKeyDown || event.type == WebEventKeyUp);

    CurrentEventScope scope(event);
    bool eventWasHandled = keyEvent(PlatformEventFactory::createPlatformKeyboardEvent(event));
    event.wasHandled = eventWasHandled;
    return eventWasHandled;

    END_BLOCK_OBJC_EXCEPTIONS

    return false;
}

void EventHandler::focusDocumentView()
{
    RefPtr page = m_frame->page();
    if (!page)
        return;

    if (RefPtr frameView = m_frame->view()) {
        if (NSView *documentView = frameView->documentView()) {
            page->chrome().focusNSView(documentView);
            // Check page() again because focusNSView can cause reentrancy.
            if (!m_frame->page())
                return;
        }
    }

    RELEASE_ASSERT(page == m_frame->page());
    page->focusController().setFocusedFrame(protectedFrame().ptr());
}

bool EventHandler::passWidgetMouseDownEventToWidget(const MouseEventWithHitTestResults& event)
{
    // Figure out which view to send the event to.
    auto* target = event.targetNode() ? dynamicDowncast<RenderWidget>(event.targetNode()->renderer()) : nullptr;
    if (!target)
        return false;

    // Double-click events don't exist in Cocoa. Since passWidgetMouseDownEventToWidget() will
    // just pass currentEvent down to the widget, we don't want to call it for events that
    // don't correspond to Cocoa events. The mousedown/ups will have already been passed on as
    // part of the pressed/released handling.
    return passMouseDownEventToWidget(target->widget());
}

bool EventHandler::passWidgetMouseDownEventToWidget(RenderWidget* renderWidget)
{
    return passMouseDownEventToWidget(renderWidget->widget());
}

static bool lastEventIsMouseUp()
{
    // Many AppKit widgets run their own event loops and consume events while the mouse is down.
    // When they finish, currentEvent is the mouseUp that they exited on. We need to update
    // the WebCore state with this mouseUp, which we never saw. This method lets us detect
    // that state. Handling this was critical when we used AppKit widgets for form elements.
    // It's not clear in what cases this is helpful now -- it's possible it can be removed. 

    BEGIN_BLOCK_OBJC_EXCEPTIONS
    WebEvent *currentEventAfterHandlingMouseDown = [WAKWindow currentEvent];
    return currentEventAfterHandlingMouseDown
        && EventHandler::currentEvent() != currentEventAfterHandlingMouseDown
        && currentEventAfterHandlingMouseDown.type == WebEventMouseUp
        && currentEventAfterHandlingMouseDown.timestamp >= EventHandler::currentEvent().timestamp;
    END_BLOCK_OBJC_EXCEPTIONS

    return false;
}

bool EventHandler::passMouseDownEventToWidget(Widget* pWidget)
{
    // FIXME: This function always returns true. It should be changed either to return
    // false in some cases or the return value should be removed.

    RefPtr<Widget> widget = pWidget;

    if (!widget) {
        LOG_ERROR("hit a RenderWidget without a corresponding Widget, means a frame is half-constructed");
        return true;
    }

    // In WebKit2 we will never have a native widget. Just return early and let the regular event handler machinery take care of
    // dispatching the event.
    if (!widget->platformWidget())
        return false;

    BEGIN_BLOCK_OBJC_EXCEPTIONS

    NSView *nodeView = widget->platformWidget();
    ASSERT(nodeView);
    ASSERT([nodeView superview]);
    NSView *view = [nodeView hitTest:[[nodeView superview] convertPoint:currentEvent().locationInWindow fromView:nil]];
    if (!view) {
        // We probably hit the border of a RenderWidget
        return true;
    }

    RefPtr page = m_frame->page();
    if (!page)
        return true;

    if (page->chrome().client().firstResponder() != view) {
        // Normally [NSWindow sendEvent:] handles setting the first responder.
        // But in our case, the event was sent to the view representing the entire web page.
        if ([view acceptsFirstResponder] && [view needsPanelToBecomeKey])
            page->chrome().client().makeFirstResponder(view);
    }

    // We need to "defer loading" while tracking the mouse, because tearing down the
    // page while an AppKit control is tracking the mouse can cause a crash.

    // FIXME: In theory, WebCore now tolerates tear-down while tracking the
    // mouse. We should confirm that, and then remove the deferrsLoading
    // hack entirely.

    bool wasDeferringLoading = page->defersLoading();
    if (!wasDeferringLoading)
        page->setDefersLoading(true);

    ASSERT(!m_sendingEventToSubview);
    m_sendingEventToSubview = true;

    {
        WidgetHierarchyUpdatesSuspensionScope suspendWidgetHierarchyUpdates;
        [view mouseDown:currentEvent()];
    }

    m_sendingEventToSubview = false;
    
    if (!wasDeferringLoading)
        page->setDefersLoading(false);

    // Remember which view we sent the event to, so we can direct the release event properly.
    m_mouseDownView = view;
    m_mouseDownWasInSubframe = false;

    // Many AppKit widgets run their own event loops and consume events while the mouse is down.
    // When they finish, currentEvent is the mouseUp that they exited on.  We need to update
    // the EventHandler state with this mouseUp, which we never saw.
    // If this event isn't a mouseUp, we assume that the mouseUp will be coming later.  There
    // is a hole here if the widget consumes both the mouseUp and subsequent events.
    if (lastEventIsMouseUp())
        m_mousePressed = false;

    END_BLOCK_OBJC_EXCEPTIONS

    return true;
}

// Note that this does the same kind of check as [target isDescendantOf:superview].
// There are two differences: This is a lot slower because it has to walk the whole
// tree, and this works in cases where the target has already been deallocated.
static bool findViewInSubviews(NSView *superview, NSView *target)
{
    BEGIN_BLOCK_OBJC_EXCEPTIONS
    NSEnumerator *e = [[superview subviews] objectEnumerator];
    NSView *subview;
    while ((subview = [e nextObject])) {
        if (subview == target || findViewInSubviews(subview, target)) {
            return true;
        }
    }
    END_BLOCK_OBJC_EXCEPTIONS

    return false;
}

NSView *EventHandler::mouseDownViewIfStillGood()
{
    // Since we have no way of tracking the lifetime of m_mouseDownView, we have to assume that
    // it could be deallocated already. We search for it in our subview tree; if we don't find
    // it, we set it to nil.
    NSView *mouseDownView = m_mouseDownView;
    if (!mouseDownView) {
        return nil;
    }
    auto* topFrameView = m_frame->view();
    NSView *topView = topFrameView ? topFrameView->platformWidget() : nil;
    if (!topView || !findViewInSubviews(topView, mouseDownView)) {
        m_mouseDownView = nil;
        return nil;
    }
    return mouseDownView;
}

bool EventHandler::eventActivatedView(const PlatformMouseEvent&) const
{
    return false;
}

bool EventHandler::eventLoopHandleMouseUp(const MouseEventWithHitTestResults&)
{
    NSView *view = mouseDownViewIfStillGood();
    if (!view)
        return false;

    if (!m_mouseDownWasInSubframe) {
        ASSERT(!m_sendingEventToSubview);
        m_sendingEventToSubview = true;
        BEGIN_BLOCK_OBJC_EXCEPTIONS
        [view mouseUp:currentEvent()];
        END_BLOCK_OBJC_EXCEPTIONS
        m_sendingEventToSubview = false;
    }
 
    return true;
}
    
bool EventHandler::passSubframeEventToSubframe(MouseEventWithHitTestResults& event, LocalFrame& subframe, HitTestResult* hitTestResult)
{
    BEGIN_BLOCK_OBJC_EXCEPTIONS

    WebEventType currentEventType = currentEvent().type;
    switch (currentEventType) {
    case WebEventMouseMoved: {
        // Since we're passing in currentNSEvent() here, we can call
        // handleMouseMoveEvent() directly, since the save/restore of
        // currentNSEvent() that mouseMoved() does would have no effect.
        ASSERT(!m_sendingEventToSubview);
        m_sendingEventToSubview = true;
        subframe.eventHandler().handleMouseMoveEvent(currentPlatformMouseEvent(), hitTestResult);
        m_sendingEventToSubview = false;
        return true;
    }
    case WebEventMouseDown: {
        auto* node = event.targetNode();
        if (!node)
            return false;
        auto* renderer = dynamicDowncast<RenderWidget>(node->renderer());
        if (!renderer)
            return false;
        auto* widget = renderer->widget();
        if (!widget || !widget->isLocalFrameView())
            return false;
        if (!passWidgetMouseDownEventToWidget(renderer))
            return false;
        m_mouseDownWasInSubframe = true;
        return true;
    }
    case WebEventMouseUp: {
        if (!m_mouseDownWasInSubframe)
            return false;
        ASSERT(!m_sendingEventToSubview);
        m_sendingEventToSubview = true;
        subframe.eventHandler().handleMouseReleaseEvent(currentPlatformMouseEvent());
        m_sendingEventToSubview = false;
        return true;
    }
    case WebEventKeyDown:
    case WebEventKeyUp:
    case WebEventScrollWheel:
    case WebEventTouchBegin:
    case WebEventTouchCancel:
    case WebEventTouchChange:
    case WebEventTouchEnd:
        return false;
    }
    END_BLOCK_OBJC_EXCEPTIONS

    return false;
}

bool EventHandler::passWheelEventToWidget(const PlatformWheelEvent& wheelEvent, Widget& widget, OptionSet<WheelEventProcessingSteps> processingSteps)
{
    BEGIN_BLOCK_OBJC_EXCEPTIONS

    NSView* nodeView = widget.platformWidget();
    if (!nodeView) {
        // WebKit2 code path.
        RefPtr frameView = dynamicDowncast<LocalFrameView>(widget);
        if (!frameView)
            return false;
        auto [result, _] = frameView->frame().eventHandler().handleWheelEvent(wheelEvent, processingSteps);
        return result.wasHandled();
    }

    if (currentEvent().type != WebEventScrollWheel || m_sendingEventToSubview)
        return false;

    ASSERT(nodeView);
    ASSERT([nodeView superview]);
    NSView *view = [nodeView hitTest:[[nodeView superview] convertPoint:currentEvent().locationInWindow fromView:nil]];
    if (!view) {
        // We probably hit the border of a RenderWidget
        return false;
    }

    ASSERT(!m_sendingEventToSubview);
    m_sendingEventToSubview = true;
    [view scrollWheel:currentEvent()];
    m_sendingEventToSubview = false;
    return true;

    END_BLOCK_OBJC_EXCEPTIONS
    return false;
}

void EventHandler::mouseDown(WebEvent *event)
{
    auto* v = m_frame->view();
    if (!v || m_sendingEventToSubview)
        return;

    BEGIN_BLOCK_OBJC_EXCEPTIONS

    // FIXME: Why is this here? EventHandler::handleMousePressEvent() calls it.
    protectedFrame()->loader().resetMultipleFormSubmissionProtection();

    m_mouseDownView = nil;

    CurrentEventScope scope(event);

    event.wasHandled = handleMousePressEvent(currentPlatformMouseEvent()).wasHandled();

    END_BLOCK_OBJC_EXCEPTIONS
}

void EventHandler::mouseUp(WebEvent *event)
{
    auto* v = m_frame->view();
    if (!v || m_sendingEventToSubview)
        return;

    BEGIN_BLOCK_OBJC_EXCEPTIONS

    CurrentEventScope scope(event);

    event.wasHandled = handleMouseReleaseEvent(currentPlatformMouseEvent()).wasHandled();

    m_mouseDownView = nil;

    END_BLOCK_OBJC_EXCEPTIONS
}

void EventHandler::mouseMoved(WebEvent *event)
{
    // Reject a mouse moved if the button is down - screws up tracking during autoscroll
    // These happen because WebKit sometimes has to fake up moved events.
    if (!m_frame->document() || !m_frame->view() || m_mousePressed || m_sendingEventToSubview)
        return;

    BEGIN_BLOCK_OBJC_EXCEPTIONS
    Ref document = *m_frame->document();
    // Ensure we start mouse move event dispatching on a clear tree.
    document->updateStyleIfNeeded();
    CurrentEventScope scope(event);
    {
#if ENABLE(CONTENT_CHANGE_OBSERVER)
        ContentChangeObserver::MouseMovedScope observingScope(document);
#endif
        event.wasHandled = mouseMoved(currentPlatformMouseEvent()).wasHandled();
        // Run style recalc to be able to capture content changes as the result of the mouse move event.
        document->updateStyleIfNeeded();
#if ENABLE(CONTENT_CHANGE_OBSERVER)
        callOnMainThread([frame = protectedFrame()] {
            // This is called by WebKitLegacy only.
            if (RefPtr document = frame->document())
                document->contentChangeObserver().willNotProceedWithFixedObservationTimeWindow();
        });
#endif
    }

    END_BLOCK_OBJC_EXCEPTIONS
}

static bool frameHasPlatformWidget(const LocalFrame& frame)
{
    if (auto* frameView = frame.view()) {
        if (frameView->platformWidget())
            return true;
    }

    return false;
}

void EventHandler::dispatchSyntheticMouseOut(const PlatformMouseEvent& platformMouseEvent)
{
    updateMouseEventTargetNode(eventNames().mouseoutEvent, nullptr, platformMouseEvent, FireMouseOverOut::Yes);
}

void EventHandler::dispatchSyntheticMouseMove(const PlatformMouseEvent& platformMouseEvent)
{
    mouseMoved(platformMouseEvent);
}

HandleUserInputEventResult EventHandler::passMousePressEventToSubframe(MouseEventWithHitTestResults& mouseEventAndResult, LocalFrame& subframe)
{
    // WebKit1 code path.
    if (frameHasPlatformWidget(m_frame))
        return passSubframeEventToSubframe(mouseEventAndResult, subframe);

    // WebKit2 code path.
    subframe.eventHandler().handleMousePressEvent(mouseEventAndResult.event());
    return true;
}

HandleUserInputEventResult EventHandler::passMouseMoveEventToSubframe(MouseEventWithHitTestResults& mouseEventAndResult, LocalFrame& subframe, HitTestResult* hitTestResult)
{
    // WebKit1 code path.
    if (frameHasPlatformWidget(m_frame))
        return passSubframeEventToSubframe(mouseEventAndResult, subframe, hitTestResult);

    subframe.eventHandler().handleMouseMoveEvent(mouseEventAndResult.event(), hitTestResult);
    return true;
}

HandleUserInputEventResult EventHandler::passMouseReleaseEventToSubframe(MouseEventWithHitTestResults& mouseEventAndResult, LocalFrame& subframe)
{
    // WebKit1 code path.
    if (frameHasPlatformWidget(m_frame))
        return passSubframeEventToSubframe(mouseEventAndResult, subframe);

    // WebKit2 code path.
    subframe.eventHandler().handleMouseReleaseEvent(mouseEventAndResult.event());
    return true;
}

OptionSet<PlatformEvent::Modifier> EventHandler::accessKeyModifiers()
{
    // Control+Option key combinations are usually unused on macOS, but not when VoiceOver is enabled.
    // So, we use Control in this case, even though it conflicts with Emacs-style key bindings.
    // See <https://bugs.webkit.org/show_bug.cgi?id=21107> for more detail.
    if (AXObjectCache::accessibilityEnhancedUserInterfaceEnabled())
        return PlatformEvent::Modifier::ControlKey;

    return { PlatformEvent::Modifier::ControlKey, PlatformEvent::Modifier::AltKey };
}

PlatformMouseEvent EventHandler::currentPlatformMouseEvent() const
{
    return PlatformEventFactory::createPlatformMouseEvent(currentEvent());
}
    
void EventHandler::startSelectionAutoscroll(RenderObject* renderer, const FloatPoint& positionInWindow)
{
    Ref frame = m_frame.get();
    RefPtr frameView = frame->view();
    if (!frameView)
        return;

    m_targetAutoscrollPositionInRootView = roundedIntPoint(positionInWindow);
    m_targetAutoscrollPositionInUnscrolledRootView = m_targetAutoscrollPositionInRootView - toIntSize(frameView->documentScrollPositionRelativeToViewOrigin());

    if (!m_isAutoscrolling)
        m_initialAutoscrollPositionInUnscrolledRootView = m_targetAutoscrollPositionInUnscrolledRootView;

    m_isAutoscrolling = true;
    m_autoscrollController->startAutoscrollForSelection(renderer);
}

void EventHandler::cancelSelectionAutoscroll()
{
    m_isAutoscrolling = false;
    m_initialAutoscrollPositionInUnscrolledRootView = std::nullopt;
    m_targetAutoscrollPositionInRootView = { };
    m_targetAutoscrollPositionInUnscrolledRootView = { };
    m_autoscrollController->stopAutoscrollTimer();
}

static IntPoint adjustAutoscrollDestinationForInsetEdges(IntPoint autoscrollPoint, std::optional<IntPoint> initialAutoscrollPoint, FloatRect unobscuredRootViewRect, float zoomScale)
{
    IntPoint resultPoint = autoscrollPoint;

    const float edgeInset = 100 / zoomScale;
    const float maximumScrollingSpeed = 40 / zoomScale;
    const float insetDistanceThreshold = edgeInset / 2;

    // FIXME: Ideally we would only inset on edges that touch the edge of the screen,
    // like macOS, but we don't have enough information in WebCore to do that currently.
    FloatRect insetUnobscuredRootViewRect = unobscuredRootViewRect;

    if (initialAutoscrollPoint) {
        IntSize autoscrollDelta = autoscrollPoint - *initialAutoscrollPoint;

        // Inset edges in the direction of the autoscroll.
        // Do not apply insets until you drag in the direction of the edge at least `insetDistanceThreshold`,
        // to make it possible to select text that abuts the edge of `unobscuredRootViewRect` without causing
        // unwanted autoscrolling.
        if (autoscrollDelta.width() < insetDistanceThreshold)
            insetUnobscuredRootViewRect.shiftXEdgeTo(insetUnobscuredRootViewRect.x() + edgeInset);
        else if (autoscrollDelta.width() > insetDistanceThreshold)
            insetUnobscuredRootViewRect.shiftMaxXEdgeTo(insetUnobscuredRootViewRect.maxX() - edgeInset);

        if (autoscrollDelta.height() < insetDistanceThreshold)
            insetUnobscuredRootViewRect.shiftYEdgeTo(insetUnobscuredRootViewRect.y() + edgeInset);
        else if (autoscrollDelta.height() > insetDistanceThreshold)
            insetUnobscuredRootViewRect.shiftMaxYEdgeTo(insetUnobscuredRootViewRect.maxY() - edgeInset);
    }

    // If the current autoscroll point is beyond the edge of the view (respecting insets), shift it outside
    // of the view, so that autoscrolling will occur. The distance we move outside of the view is scaled from
    // `edgeInset` to `maximumScrollingSpeed` so that the inset's contribution to the speed is independent of its size.
    if (autoscrollPoint.x() < insetUnobscuredRootViewRect.x()) {
        float distanceFromEdge = autoscrollPoint.x() - insetUnobscuredRootViewRect.x();
        if (distanceFromEdge < 0)
            resultPoint.setX(unobscuredRootViewRect.x() + ((distanceFromEdge / edgeInset) * maximumScrollingSpeed));
    } else if (autoscrollPoint.x() >= insetUnobscuredRootViewRect.maxX()) {
        float distanceFromEdge = autoscrollPoint.x() - insetUnobscuredRootViewRect.maxX();
        if (distanceFromEdge > 0)
            resultPoint.setX(unobscuredRootViewRect.maxX() + ((distanceFromEdge / edgeInset) * maximumScrollingSpeed));
    }

    if (autoscrollPoint.y() < insetUnobscuredRootViewRect.y()) {
        float distanceFromEdge = autoscrollPoint.y() - insetUnobscuredRootViewRect.y();
        if (distanceFromEdge < 0)
            resultPoint.setY(unobscuredRootViewRect.y() + ((distanceFromEdge / edgeInset) * maximumScrollingSpeed));
    } else if (autoscrollPoint.y() >= insetUnobscuredRootViewRect.maxY()) {
        float distanceFromEdge = autoscrollPoint.y() - insetUnobscuredRootViewRect.maxY();
        if (distanceFromEdge > 0)
            resultPoint.setY(unobscuredRootViewRect.maxY() + ((distanceFromEdge / edgeInset) * maximumScrollingSpeed));
    }

    return resultPoint;
}

IntPoint EventHandler::targetPositionInWindowForSelectionAutoscroll() const
{
    Ref frame = m_frame.get();
    if (!frame->view())
        return { };

    if (!frame->isMainFrame())
        return m_targetAutoscrollPositionInRootView;

    RefPtr page = frame->page();
    if (!page)
        return m_targetAutoscrollPositionInRootView;

    Ref frameView = *frame->view();

    // All work is done in "unscrolled" root view coordinates (as if delegatesScrolling were off),
    // so that when the autoscrolling timer fires, it uses the new scroll position, so that it
    // can keep scrolling without the client pushing a new contents-space target position via startSelectionAutoscroll.
    auto scrollPosition = toIntSize(frameView->documentScrollPositionRelativeToViewOrigin());
    
    FloatRect unobscuredContentRectInUnscrolledRootView = frameView->contentsToRootView(frameView->unobscuredContentRect());
    unobscuredContentRectInUnscrolledRootView.move(-scrollPosition);

    return adjustAutoscrollDestinationForInsetEdges(m_targetAutoscrollPositionInUnscrolledRootView, m_initialAutoscrollPositionInUnscrolledRootView, unobscuredContentRectInUnscrolledRootView, page->pageScaleFactor()) + scrollPosition;
}

bool EventHandler::shouldUpdateAutoscroll()
{
    return m_isAutoscrolling;
}

#if ENABLE(MODEL_PROCESS)
std::optional<NodeIdentifier> EventHandler::requestInteractiveModelElementAtPoint(const IntPoint& clientPosition)
{
    Ref frame = m_frame.get();

    RefPtr document = frame->document();
    RefPtr frameView = frame->view();
    if (!document || !frameView)
        return std::nullopt;

    SetForScope shouldAllowMouseDownToStartDrag { m_shouldAllowMouseDownToStartDrag, true };

    document->updateLayoutIgnorePendingStylesheets();

    FloatPoint adjustedClientPositionAsFloatPoint(clientPosition);
    frame->nodeRespondingToClickEvents(clientPosition, adjustedClientPositionAsFloatPoint);
    auto adjustedClientPosition = roundedIntPoint(adjustedClientPositionAsFloatPoint);
    auto adjustedGlobalPosition = frameView->windowToContents(adjustedClientPosition);

    PlatformMouseEvent syntheticMousePressEvent(adjustedClientPosition, adjustedGlobalPosition, MouseButton::Left, PlatformEvent::Type::MousePressed, 1, { }, WallTime::now(), 0, SyntheticClickType::NoTap);
    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent };
    auto documentPoint = frameView->windowToContents(syntheticMousePressEvent.position());
    auto hitTestedMouseEvent = document->prepareMouseEvent(hitType, documentPoint, syntheticMousePressEvent);

    if (RefPtr subframe = dynamicDowncast<LocalFrame>(subframeForHitTestResult(hitTestedMouseEvent))) {
        if (std::optional<NodeIdentifier> nodeID = subframe->eventHandler().requestInteractiveModelElementAtPoint(adjustedClientPosition))
            return nodeID;
    }

    RefPtr targetElement = hitTestedMouseEvent.hitTestResult().targetElement();
    if (RefPtr modelElement = dynamicDowncast<HTMLModelElement>(targetElement)) {
        if (modelElement->supportsStageModeInteraction()) {
            auto transform = TransformationMatrix::identity;
            transform.translate(clientPosition.x(), clientPosition.y());

            modelElement->beginStageModeTransform(transform);
            return modelElement->nodeIdentifier();
        }
    }

    return std::nullopt;
}

void EventHandler::stageModeSessionDidUpdate(std::optional<NodeIdentifier> nodeID, const TransformationMatrix& transform)
{
    if (!nodeID)
        return;

    RefPtr node = Node::fromIdentifier(*nodeID);
    if (!node)
        return;

    RefPtr modelElement = dynamicDowncast<HTMLModelElement>(node);
    if (!modelElement)
        return;

    // It's possible that interactivity can change via JS during the session
    if (!modelElement->supportsStageModeInteraction())
        return;

    modelElement->updateStageModeTransform(transform);
}

void EventHandler::stageModeSessionDidEnd(std::optional<NodeIdentifier> nodeID)
{
    if (!nodeID)
        return;

    RefPtr node = Node::fromIdentifier(*nodeID);
    if (!node)
        return;

    RefPtr modelElement = dynamicDowncast<HTMLModelElement>(node);
    if (!modelElement)
        return;

    // It's possible that interactivity can change via JS during the session
    if (!modelElement->supportsStageModeInteraction())
        return;

    modelElement->endStageModeInteraction();
}
#endif

#if ENABLE(DRAG_SUPPORT)

bool EventHandler::eventLoopHandleMouseDragged(const MouseEventWithHitTestResults&)
{
    return false;
}

void EventHandler::tryToBeginDragAtPoint(const IntPoint& clientPosition, const IntPoint&, CompletionHandler<void(Expected<bool, RemoteFrameGeometryTransformer>)>&& completionHandler)
{
    Ref frame = m_frame.get();

    RefPtr document = frame->document();
    if (!document)
        return completionHandler(false);

    SetForScope shouldAllowMouseDownToStartDrag { m_shouldAllowMouseDownToStartDrag, true };

    document->updateLayoutIgnorePendingStylesheets();

    FloatPoint adjustedClientPositionAsFloatPoint(clientPosition);
    frame->nodeRespondingToClickEvents(clientPosition, adjustedClientPositionAsFloatPoint);
    IntPoint adjustedClientPosition = roundedIntPoint(adjustedClientPositionAsFloatPoint);
    IntPoint adjustedGlobalPosition = frame->view()->windowToContents(adjustedClientPosition);

    PlatformMouseEvent syntheticMousePressEvent(adjustedClientPosition, adjustedGlobalPosition, MouseButton::Left, PlatformEvent::Type::MousePressed, 1, { }, WallTime::now(), 0, SyntheticClickType::NoTap);
    PlatformMouseEvent syntheticMouseMoveEvent(adjustedClientPosition, adjustedGlobalPosition, MouseButton::Left, PlatformEvent::Type::MouseMoved, 0, { }, WallTime::now(), 0, SyntheticClickType::NoTap);

    constexpr OptionSet<HitTestRequest::Type> hitType { HitTestRequest::Type::Active, HitTestRequest::Type::DisallowUserAgentShadowContent };
    RefPtr frameView = frame->view();
    auto documentPoint = frameView ? frameView->windowToContents(syntheticMouseMoveEvent.position()) : syntheticMouseMoveEvent.position();
    auto hitTestedMouseEvent = document->prepareMouseEvent(hitType, documentPoint, syntheticMouseMoveEvent);

    if (RefPtr subframe = subframeForHitTestResult(hitTestedMouseEvent)) {
        if (RefPtr localSubframe = dynamicDowncast<LocalFrame>(subframe.get()))
            return localSubframe->eventHandler().tryToBeginDragAtPoint(adjustedClientPosition, adjustedGlobalPosition, WTFMove(completionHandler));
        if (RefPtr remoteSubframe = dynamicDowncast<RemoteFrame>(subframe.get())) {
            if (RefPtr remoteFrameView = remoteSubframe->view(); remoteFrameView && frameView)
                return completionHandler(makeUnexpected(RemoteFrameGeometryTransformer(remoteFrameView.releaseNonNull(), frameView.releaseNonNull(), remoteSubframe->frameID())));
        }
    }

    if (!eventMayStartDrag(syntheticMousePressEvent))
        return completionHandler(false);

    handleMousePressEvent(syntheticMousePressEvent);
    bool handledDrag = m_mouseDownMayStartDrag && handleMouseDraggedEvent(hitTestedMouseEvent, DontCheckDragHysteresis);
    // Reset this bit to prevent autoscrolling from updating the selection with the last mouse location.
    m_mouseDownMayStartSelect = false;
    // Reset this bit to ensure that WebChromeClientIOS::observedContentChange() is called by EventHandler::mousePressed()
    // when we would process the next tap after a drag interaction.
    m_mousePressed = false;

#if ENABLE(MODEL_PROCESS)
    RefPtr targetElement = hitTestedMouseEvent.hitTestResult().targetElement();
    if (RefPtr modelElement = dynamicDowncast<HTMLModelElement>(targetElement))
        return modelElement->tryAnimateModelToFitPortal(handledDrag, WTFMove(completionHandler));
#endif

    return completionHandler(handledDrag);
}

bool EventHandler::supportsSelectionUpdatesOnMouseDrag() const
{
    return false;
}

bool EventHandler::shouldAllowMouseDownToStartDrag() const
{
    return m_shouldAllowMouseDownToStartDrag;
}

#endif // ENABLE(DRAG_SUPPORT)

}

#endif // PLATFORM(IOS_FAMILY)
