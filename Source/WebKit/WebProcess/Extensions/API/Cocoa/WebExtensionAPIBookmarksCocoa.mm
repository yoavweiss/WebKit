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


#if !__has_feature(objc_arc)
#error This file requires ARC. Add the "-fobjc-arc" compiler flag for this file.
#endif

#include "config.h"
#import "WebExtensionAPIBookmarks.h"
#import "CocoaHelpers.h"

#if ENABLE(WK_WEB_EXTENSIONS_BOOKMARKS)

namespace WebKit {

static NSString * const idKey = @"id";
static NSString * const urlKey = @"url";
static NSString * const titleKey = @"title";
static NSString * const dateAddedKey = @"dateAdded";
static NSString * const typeKey = @"type";
static NSString * const bookmarkKey = @"bookmark";

NSDictionary *WebExtensionAPIBookmarks::createDictionaryFromNode(const MockBookmarkNode& node)
{
    return @{
        idKey: node.id.createNSString().get(),
        titleKey: node.title.createNSString().get(),
        urlKey: node.url.createNSString().get(),
        dateAddedKey: @(node.dateAdded.secondsSinceEpoch().milliseconds()),
        typeKey: bookmarkKey
    };
}

void WebExtensionAPIBookmarks::createBookmark(NSDictionary *bookmark, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    NSArray *requiredKeys = @[ @"id", @"url", @"dateAdded" ];
    static NSDictionary<NSString *, id> *types= @{
        idKey: NSString.class,
        urlKey: NSString.class,
        dateAddedKey: NSNumber.class,
        titleKey: NSString.class
    };

    if (!validateDictionary(bookmark, @"bookmark", requiredKeys, types, outExceptionString))
        return;

    MockBookmarkNode newNode;
    newNode.id = bookmark[idKey];
    newNode.url = bookmark[urlKey];
    newNode.title = bookmark[titleKey];

    newNode.dateAdded = WallTime::fromRawSeconds(objectForKey<NSDate>(bookmark, dateAddedKey).timeIntervalSince1970);

    m_mockBookmarks.append(WTFMove(newNode));
    callback->call(createDictionaryFromNode(newNode));
}

void WebExtensionAPIBookmarks::getChildren(NSString *bookmarkIdentifier, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    callback->reportError(@"unimplemented");
}

void WebExtensionAPIBookmarks::getRecent(long long numberOfItems, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    if (numberOfItems < 1) {
        *outExceptionString = toErrorString(nullString(), @"numberOfItems", @"it must be at least 1").createNSString().autorelease();
        return;
    }

    auto sortedBookmarks = m_mockBookmarks;
    std::sort(sortedBookmarks.begin(), sortedBookmarks.end(), [](const MockBookmarkNode& a, const MockBookmarkNode& b) {
        return a.dateAdded > b.dateAdded;
    });

    size_t countToReturn = std::min(static_cast<size_t>(numberOfItems), sortedBookmarks.size());
    auto *resultArray = [NSMutableArray arrayWithCapacity:countToReturn];
    for (size_t i = 0; i < countToReturn; ++i)
        [resultArray addObject:createDictionaryFromNode(sortedBookmarks[i])];
    callback->call(resultArray);
}

void WebExtensionAPIBookmarks::getSubTree(NSString *bookmarkIdentifier, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    callback->reportError(@"unimplemented");
}

void WebExtensionAPIBookmarks::getTree(Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    callback->reportError(@"unimplemented");
}

void WebExtensionAPIBookmarks::get(NSObject *idOrIdList, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    callback->reportError(@"unimplemented");
}

void WebExtensionAPIBookmarks::move(NSString *bookmarkIdentifier, NSDictionary *destination, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    callback->reportError(@"unimplemented");
}

void WebExtensionAPIBookmarks::remove(NSString *bookmarkIdentifier, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    callback->reportError(@"unimplemented");
}

void WebExtensionAPIBookmarks::removeTree(NSString *bookmarkIdentifier, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    callback->reportError(@"unimplemented");
}

void WebExtensionAPIBookmarks::search(NSObject *query, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    callback->reportError(@"unimplemented");
}

void WebExtensionAPIBookmarks::update(NSString *bookmarkIdentifier, NSDictionary *changes, Ref<WebExtensionCallbackHandler>&& callback, NSString **outExceptionString)
{
    callback->reportError(@"unimplemented");
}
} // namespace WebKit

#endif
