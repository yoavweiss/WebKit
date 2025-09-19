/*
 * Copyright (C) 2017-2020 Apple Inc. All rights reserved.
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

#import "PlatformUtilities.h"
#import "TestCocoa.h"
#import "TestNavigationDelegate.h"
#import "TestWKWebView.h"
#import <WebKit/WKProcessPoolPrivate.h>
#import <WebKit/WKWebViewPrivate.h>
#import <WebKit/WKWebViewPrivateForTesting.h>
#import <WebKit/_WKProcessPoolConfiguration.h>
#import <wtf/Vector.h>

namespace TestWebKitAPI {

static bool isEnhancedSecurityEnabled(WKWebView *webView)
{
    __block bool gotResponse = false;
    __block bool isEnhancedSecurityEnabledResult = false;
    [webView _isEnhancedSecurityEnabled:^(BOOL isEnhancedSecurityEnabled) {
        isEnhancedSecurityEnabledResult = isEnhancedSecurityEnabled;
        gotResponse = true;
    }];
    TestWebKitAPI::Util::run(&gotResponse);
    EXPECT_NE([webView _webProcessIdentifier], 0);
    return isEnhancedSecurityEnabledResult;
}

static bool isJITEnabled(WKWebView *webView)
{
    __block bool gotResponse = false;
    __block bool isJITEnabledResult = false;
    [webView _isJITEnabled:^(BOOL isJITEnabled) {
        isJITEnabledResult = isJITEnabled;
        gotResponse = true;
    }];
    TestWebKitAPI::Util::run(&gotResponse);
    EXPECT_NE([webView _webProcessIdentifier], 0);
    return isJITEnabledResult;
}


TEST(EnhancedSecurity, EnhancedSecurityEnablesTrue)
{
    auto webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    webViewConfiguration.get().defaultWebpagePreferences._enhancedSecurityEnabled = YES;
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);
    NSURL *url = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url]];
    [webView _test_waitForDidFinishNavigation];
    EXPECT_EQ(true, isEnhancedSecurityEnabled(webView.get()));
}

TEST(EnhancedSecurity, EnhancedSecurityEnableFalse)
{
    auto webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    webViewConfiguration.get().defaultWebpagePreferences._enhancedSecurityEnabled = NO;
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);
    NSURL *url = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url]];
    [webView _test_waitForDidFinishNavigation];
    EXPECT_EQ(false, isEnhancedSecurityEnabled(webView.get()));
}

TEST(EnhancedSecurity, EnhancedSecurityDisablesJIT)
{
    auto webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    webViewConfiguration.get().defaultWebpagePreferences._enhancedSecurityEnabled = YES;
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);
    NSURL *url = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url]];
    [webView _test_waitForDidFinishNavigation];
    EXPECT_EQ(false, isJITEnabled(webView.get()));
}

TEST(EnhancedSecurity, EnhancedSecurityNavigationStaysEnabledAfterNavigation)
{
    auto webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    webViewConfiguration.get().defaultWebpagePreferences._enhancedSecurityEnabled = YES;
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);
    auto delegate = adoptNS([TestNavigationDelegate new]);
    [webView setNavigationDelegate:delegate.get()];

    __block bool finishedNavigation = false;
    delegate.get().didFinishNavigation = ^(WKWebView *, WKNavigation *) {
        finishedNavigation = true;
    };

    NSURL *url = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url]];
    TestWebKitAPI::Util::run(&finishedNavigation);
    EXPECT_EQ(true, isEnhancedSecurityEnabled(webView.get()));

    finishedNavigation = false;
    NSURL *url2 = [NSBundle.test_resourcesBundle URLForResource:@"simple2" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url2]];
    TestWebKitAPI::Util::run(&finishedNavigation);

    EXPECT_EQ(true, isEnhancedSecurityEnabled(webView.get()));
}

TEST(EnhancedSecurity, PSONToEnhancedSecurity)
{
    auto webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    webViewConfiguration.get().defaultWebpagePreferences._enhancedSecurityEnabled = NO;
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);
    auto delegate = adoptNS([TestNavigationDelegate new]);
    [webView setNavigationDelegate:delegate.get()];

    __block bool finishedNavigation = false;
    delegate.get().didFinishNavigation = ^(WKWebView *, WKNavigation *) {
        finishedNavigation = true;
    };

    NSURL *url = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url]];
    TestWebKitAPI::Util::run(&finishedNavigation);
    EXPECT_EQ(false, isEnhancedSecurityEnabled(webView.get()));
    pid_t pid1 = [webView _webProcessIdentifier];
    EXPECT_NE(pid1, 0);

    finishedNavigation = false;

    delegate.get().decidePolicyForNavigationActionWithPreferences = ^(WKNavigationAction *action, WKWebpagePreferences *preferences, void (^completionHandler)(WKNavigationActionPolicy, WKWebpagePreferences *)) {
        EXPECT_FALSE(preferences._enhancedSecurityEnabled);
        preferences._enhancedSecurityEnabled = YES;
        completionHandler(WKNavigationActionPolicyAllow, preferences);
    };

    NSURL *url2 = [NSBundle.test_resourcesBundle URLForResource:@"simple2" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url2]];
    TestWebKitAPI::Util::run(&finishedNavigation);

    EXPECT_EQ(true, isEnhancedSecurityEnabled(webView.get()));
    EXPECT_NE(pid1, [webView _webProcessIdentifier]);
}

TEST(EnhancedSecurity, PSONToEnhancedSecuritySamePage)
{
    auto webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    webViewConfiguration.get().defaultWebpagePreferences._enhancedSecurityEnabled = NO;
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);
    auto delegate = adoptNS([TestNavigationDelegate new]);
    [webView setNavigationDelegate:delegate.get()];

    __block bool finishedNavigation = false;
    delegate.get().didFinishNavigation = ^(WKWebView *, WKNavigation *) {
        finishedNavigation = true;
    };

    NSURL *url = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url]];
    TestWebKitAPI::Util::run(&finishedNavigation);
    EXPECT_EQ(false, isEnhancedSecurityEnabled(webView.get()));
    pid_t pid1 = [webView _webProcessIdentifier];
    EXPECT_NE(pid1, 0);

    finishedNavigation = false;

    delegate.get().decidePolicyForNavigationActionWithPreferences = ^(WKNavigationAction *action, WKWebpagePreferences *preferences, void (^completionHandler)(WKNavigationActionPolicy, WKWebpagePreferences *)) {
        EXPECT_FALSE(preferences._enhancedSecurityEnabled);
        preferences._enhancedSecurityEnabled = YES;
        completionHandler(WKNavigationActionPolicyAllow, preferences);
    };

    NSURL *url2 = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url2]];
    TestWebKitAPI::Util::run(&finishedNavigation);

    EXPECT_EQ(true, isEnhancedSecurityEnabled(webView.get()));
    EXPECT_NE(pid1, [webView _webProcessIdentifier]);
}

static RetainPtr<_WKProcessPoolConfiguration> psonProcessPoolConfiguration()
{
    auto processPoolConfiguration = adoptNS([[_WKProcessPoolConfiguration alloc] init]);
    processPoolConfiguration.get().processSwapsOnNavigation = YES;
    processPoolConfiguration.get().usesWebProcessCache = YES;
    processPoolConfiguration.get().prewarmsProcessesAutomatically = YES;
    processPoolConfiguration.get().processSwapsOnNavigationWithinSameNonHTTPFamilyProtocol = YES;
    return processPoolConfiguration;
}

TEST(EnhancedSecurity, PSONToEnhancedSecuritySharedProcessPool)
{
    auto processPoolConfiguration = psonProcessPoolConfiguration();
    auto processPool = adoptNS([[WKProcessPool alloc] _initWithConfiguration:processPoolConfiguration.get()]);

    auto webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    [webViewConfiguration setProcessPool:processPool.get()];
    webViewConfiguration.get().defaultWebpagePreferences._enhancedSecurityEnabled = NO;

    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);

    auto delegate = adoptNS([TestNavigationDelegate new]);
    [webView setNavigationDelegate:delegate.get()];

    __block bool finishedNavigation = false;
    delegate.get().didFinishNavigation = ^(WKWebView *, WKNavigation *) {
        finishedNavigation = true;
    };

    NSURL *url = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url]];
    TestWebKitAPI::Util::run(&finishedNavigation);
    EXPECT_EQ(false, isEnhancedSecurityEnabled(webView.get()));
    pid_t pid1 = [webView _webProcessIdentifier];
    EXPECT_NE(pid1, 0);

    finishedNavigation = false;
    auto webView2 = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);
    [webView2 setNavigationDelegate:delegate.get()];

    delegate.get().decidePolicyForNavigationActionWithPreferences = ^(WKNavigationAction *action, WKWebpagePreferences *preferences, void (^completionHandler)(WKNavigationActionPolicy, WKWebpagePreferences *)) {
        EXPECT_FALSE(preferences._enhancedSecurityEnabled);
        preferences._enhancedSecurityEnabled = YES;
        completionHandler(WKNavigationActionPolicyAllow, preferences);
    };

    NSURL *url2 = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView2 loadRequest:[NSURLRequest requestWithURL:url2]];
    TestWebKitAPI::Util::run(&finishedNavigation);

    EXPECT_EQ(true, isEnhancedSecurityEnabled(webView2.get()));
    EXPECT_NE(pid1, [webView2 _webProcessIdentifier]);
}

TEST(EnhancedSecurity, PSONToEnhancedSecuritySharedProcessPoolReverse)
{
    auto processPoolConfiguration = psonProcessPoolConfiguration();
    auto processPool = adoptNS([[WKProcessPool alloc] _initWithConfiguration:processPoolConfiguration.get()]);

    auto webViewConfiguration = adoptNS([WKWebViewConfiguration new]);
    [webViewConfiguration setProcessPool:processPool.get()];
    webViewConfiguration.get().defaultWebpagePreferences._enhancedSecurityEnabled = YES;

    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);

    auto delegate = adoptNS([TestNavigationDelegate new]);
    [webView setNavigationDelegate:delegate.get()];

    __block bool finishedNavigation = false;
    delegate.get().didFinishNavigation = ^(WKWebView *, WKNavigation *) {
        finishedNavigation = true;
    };

    NSURL *url = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView loadRequest:[NSURLRequest requestWithURL:url]];
    TestWebKitAPI::Util::run(&finishedNavigation);
    EXPECT_EQ(true, isEnhancedSecurityEnabled(webView.get()));
    pid_t pid1 = [webView _webProcessIdentifier];
    EXPECT_NE(pid1, 0);

    finishedNavigation = false;
    auto webView2 = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:webViewConfiguration.get()]);
    [webView2 setNavigationDelegate:delegate.get()];

    delegate.get().decidePolicyForNavigationActionWithPreferences = ^(WKNavigationAction *action, WKWebpagePreferences *preferences, void (^completionHandler)(WKNavigationActionPolicy, WKWebpagePreferences *)) {
        EXPECT_TRUE(preferences._enhancedSecurityEnabled);
        preferences._enhancedSecurityEnabled = NO;
        completionHandler(WKNavigationActionPolicyAllow, preferences);
    };

    NSURL *url2 = [NSBundle.test_resourcesBundle URLForResource:@"simple" withExtension:@"html"];
    [webView2 loadRequest:[NSURLRequest requestWithURL:url2]];
    TestWebKitAPI::Util::run(&finishedNavigation);

    EXPECT_EQ(false, isEnhancedSecurityEnabled(webView2.get()));
    EXPECT_NE(pid1, [webView2 _webProcessIdentifier]);
}

} // namespace TestWebKitAPI
