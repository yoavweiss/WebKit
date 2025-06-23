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

#if PLATFORM(COCOA)

#import "PlatformUtilities.h"
#import "Test.h"
#import "TestCocoa.h"
#import "TestWKWebView.h"
#import "Utilities.h"
#import <wtf/RetainPtr.h>

@interface WKWebView (ScrollGeometryTesting)
- (void)_setNeedsScrollGeometryUpdates:(BOOL)needsScrollGeometryUpdates;
@end

@interface TestScrollGeometryDelegate : NSObject <WKUIDelegate>

- (void)_webView:(WKWebView *)webView geometryDidChange:(WKScrollGeometry *)geometry;

- (CGSize)currentContentSize;

@end

@implementation TestScrollGeometryDelegate {
    RetainPtr<WKScrollGeometry> _currentGeometry;
}

- (void)_webView:(WKWebView *)webView geometryDidChange:(WKScrollGeometry *)geometry
{
    _currentGeometry = geometry;
}

- (CGSize)currentContentSize
{
    return [_currentGeometry contentSize];
}

@end

static void runContentSizeTest(NSString *html, CGSize expectedSize, BOOL needsScrollGeometryUpdates = YES)
{
    RetainPtr webView = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600)]);
    [webView _setNeedsScrollGeometryUpdates:needsScrollGeometryUpdates];

    RetainPtr delegate = adoptNS([[TestScrollGeometryDelegate alloc] init]);
    [webView setUIDelegate:delegate.get()];

    [webView synchronouslyLoadHTMLString:[NSString stringWithFormat:@"<!DOCTYPE html>%@", html]];

    [webView waitForNextPresentationUpdate];

    EXPECT_EQ([delegate currentContentSize], expectedSize);

    [webView synchronouslyLoadHTMLString:html];
    [webView waitForNextPresentationUpdate];

    EXPECT_EQ([delegate currentContentSize], expectedSize);
}

TEST(WKScrollGeometry, ContentSizeTallerThanWebView)
{
#if PLATFORM(IOS_FAMILY)
    CGFloat expectedWidth = 980;
#elif (PLATFORM(MAC) && __MAC_OS_X_VERSION_MIN_REQUIRED <= 150000)
    CGFloat expectedWidth = 785;
#else
    CGFloat expectedWidth = 786;
#endif

    runContentSizeTest(@""
        "<html>"
        "<head>"
        "<style>"
        "    div { background: red; height: 10000px; }"
        "</style>"
        "</head>"
        "<body>"
        "<div>Test</div>"
        "</body>"
        "</html>", CGSizeMake(expectedWidth, 10016));
}

TEST(WKScrollGeometry, NoScrollGeometryUpdates)
{
    runContentSizeTest(@""
        "<html>"
        "<head>"
        "<style>"
        "    div { background: red; height: 10000px; }"
        "</style>"
        "</head>"
        "<body>"
        "<div>Test</div>"
        "</body>"
        "</html>", CGSizeZero, NO);
}

#endif
