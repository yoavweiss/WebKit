/*
 * Copyright (C) 2013-2025 Apple Inc. All rights reserved.
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
#import "WKWebProcessPlugInFrameInternal.h"

#import "WKNSArray.h"
#import "WKNSURLExtras.h"
#import "WKWebProcessPlugInBrowserContextControllerInternal.h"
#import "WKWebProcessPlugInCSSStyleDeclarationHandleInternal.h"
#import "WKWebProcessPlugInHitTestResultInternal.h"
#import "WKWebProcessPlugInNodeHandleInternal.h"
#import "WKWebProcessPlugInRangeHandleInternal.h"
#import "WKWebProcessPlugInScriptWorldInternal.h"
#import "WebProcess.h"
#import "_WKFrameHandleInternal.h"
#import <JavaScriptCore/JSValue.h>
#import <WebCore/CertificateInfo.h>
#import <WebCore/IntPoint.h>
#import <WebCore/LinkIconCollector.h>
#import <WebCore/LinkIconType.h>
#import <WebCore/LocalFrameInlines.h>
#import <WebCore/WebCoreObjCExtras.h>
#import <wtf/AlignedStorage.h>
#import <wtf/cocoa/VectorCocoa.h>

@implementation WKWebProcessPlugInFrame {
    AlignedStorage<WebKit::WebFrame> _frame;
}

+ (instancetype)lookUpFrameFromHandle:(_WKFrameHandle *)handle
{
    auto frameID = handle->_frameHandle->frameID();
    return wrapper(frameID ? WebKit::WebProcess::singleton().webFrame(*frameID) : nullptr);
}

+ (instancetype)lookUpFrameFromJSContext:(JSContext *)context
{
    return wrapper(WebKit::WebFrame::frameForContext(context.JSGlobalContextRef)).autorelease();
}

+ (instancetype)lookUpContentFrameFromWindowOrFrameElement:(JSValue *)value
{
    return wrapper(WebKit::WebFrame::contentFrameForWindowOrFrameElement(value.context.JSGlobalContextRef, value.JSValueRef)).autorelease();
}

- (void)dealloc
{
    if (WebCoreObjCScheduleDeallocateOnMainRunLoop(WKWebProcessPlugInFrame.class, self))
        return;
    _frame->~WebFrame();
    [super dealloc];
}

- (JSContext *)jsContextForWorld:(WKWebProcessPlugInScriptWorld *)world
{
    return [JSContext contextWithJSGlobalContextRef:_frame->jsContextForWorld(&[world _scriptWorld])];
}

- (JSContext *)jsContextForServiceWorkerWorld:(WKWebProcessPlugInScriptWorld *)world
{
    if (auto context = _frame->jsContextForServiceWorkerWorld(&[world _scriptWorld]))
        return [JSContext contextWithJSGlobalContextRef:context];
    return nil;
}

- (WKWebProcessPlugInHitTestResult *)hitTest:(CGPoint)point
{
    return wrapper(_frame->hitTest(WebCore::IntPoint(point))).autorelease();
}

- (WKWebProcessPlugInHitTestResult *)hitTest:(CGPoint)point options:(WKHitTestOptions)options
{
    auto types = WebKit::WebFrame::defaultHitTestRequestTypes();
    if (options & WKHitTestOptionAllowUserAgentShadowRootContent)
        types.remove(WebCore::HitTestRequest::Type::DisallowUserAgentShadowContent);
    return wrapper(_frame->hitTest(WebCore::IntPoint(point), types)).autorelease();
}

- (JSValue *)jsCSSStyleDeclarationForCSSStyleDeclarationHandle:(WKWebProcessPlugInCSSStyleDeclarationHandle *)cssStyleDeclarationHandle inWorld:(WKWebProcessPlugInScriptWorld *)world
{
    JSValueRef valueRef = _frame->jsWrapperForWorld(&[cssStyleDeclarationHandle _cssStyleDeclarationHandle], &[world _scriptWorld]);
    return [JSValue valueWithJSValueRef:valueRef inContext:[self jsContextForWorld:world]];
}

- (JSValue *)jsNodeForNodeHandle:(WKWebProcessPlugInNodeHandle *)nodeHandle inWorld:(WKWebProcessPlugInScriptWorld *)world
{
    JSValueRef valueRef = _frame->jsWrapperForWorld(&[nodeHandle _nodeHandle], &[world _scriptWorld]);
    return [JSValue valueWithJSValueRef:valueRef inContext:[self jsContextForWorld:world]];
}

- (JSValue *)jsRangeForRangeHandle:(WKWebProcessPlugInRangeHandle *)rangeHandle inWorld:(WKWebProcessPlugInScriptWorld *)world
{
    JSValueRef valueRef = _frame->jsWrapperForWorld(&[rangeHandle _rangeHandle], &[world _scriptWorld]);
    return [JSValue valueWithJSValueRef:valueRef inContext:[self jsContextForWorld:world]];
}

- (WKWebProcessPlugInBrowserContextController *)_browserContextController
{
    if (!_frame->page())
        return nil;
    return WebKit::wrapper(*_frame->page());
}

- (NSURL *)URL
{
    return _frame->url().createNSURL().autorelease();
}

- (NSArray *)childFrames
{
    return WebKit::wrapper(_frame->childFrames()).autorelease();
}

- (BOOL)containsAnyFormElements
{
    return !!_frame->containsAnyFormElements();
}

- (BOOL)isMainFrame
{
    return !!_frame->isMainFrame();
}

- (_WKFrameHandle *)handle
{
    return wrapper(API::FrameHandle::create(_frame->frameID())).autorelease();
}

- (NSString *)_securityOrigin
{
    RefPtr coreFrame = _frame->coreLocalFrame();
    if (!coreFrame)
        return nil;
    return coreFrame->document()->securityOrigin().toString().createNSString().autorelease();
}

static RetainPtr<NSArray> collectIcons(WebCore::LocalFrame* frame, OptionSet<WebCore::LinkIconType> iconTypes)
{
    if (!frame)
        return @[];
    RefPtr document = frame->document();
    if (!document)
        return @[];
    return createNSArray(WebCore::LinkIconCollector(*document).iconsOfTypes(iconTypes), [] (auto&& icon) {
        return icon.url.createNSURL();
    });
}

- (NSArray *)appleTouchIconURLs
{
    return collectIcons(_frame->coreLocalFrame(), { WebCore::LinkIconType::TouchIcon, WebCore::LinkIconType::TouchPrecomposedIcon }).autorelease();
}

- (NSArray *)faviconURLs
{
    return collectIcons(_frame->coreLocalFrame(), WebCore::LinkIconType::Favicon).autorelease();
}

- (WKWebProcessPlugInFrame *)_parentFrame
{
    return wrapper(_frame->parentFrame()).autorelease();
}

- (BOOL)_hasCustomContentProvider
{
    if (!_frame->isMainFrame())
        return false;

    return _frame->page()->mainFrameHasCustomContentProvider();
}

- (NSArray *)_certificateChain
{
    return (NSArray *)WebCore::CertificateInfo::certificateChainFromSecTrust(_frame->certificateInfo().trust().get()).autorelease();
}

- (SecTrustRef)_serverTrust
{
    return _frame->certificateInfo().trust().get();
}

- (NSURL *)_provisionalURL
{
    return [NSURL _web_URLWithWTFString:_frame->provisionalURL()];
}

#pragma mark WKObject protocol implementation

- (API::Object&)_apiObject
{
    return *_frame;
}

@end
