/*
 * Copyright (C) 2019 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "WebKitPointerLockPermissionRequest.h"

#include "WebKitPermissionRequest.h"
#include "WebKitPointerLockPermissionRequestPrivate.h"
#include "WebKitWebViewPrivate.h"
#include <wtf/glib/WTFGType.h>

/**
 * WebKitPointerLockPermissionRequest:
 * @See_also: #WebKitPermissionRequest, #WebKitWebView
 *
 * A permission request for locking the pointer.
 *
 * WebKitPointerLockPermissionRequest represents a request for
 * permission to decide whether WebKit can lock the pointer device when
 * requested by web content.
 *
 * When a WebKitPointerLockPermissionRequest is not handled by the user,
 * it is allowed by default.
 *
 * Since: 2.28
 */

#if !ENABLE(2022_GLIB_API)
typedef WebKitPermissionRequestIface WebKitPermissionRequestInterface;
#endif

static void webkit_permission_request_interface_init(WebKitPermissionRequestInterface*);

struct _WebKitPointerLockPermissionRequestPrivate {
    GRefPtr<WebKitWebView> webView;
    CompletionHandler<void(bool)> completionHandler;
};

WEBKIT_DEFINE_FINAL_TYPE_WITH_CODE(
    WebKitPointerLockPermissionRequest, webkit_pointer_lock_permission_request, G_TYPE_OBJECT, GObject,
    G_IMPLEMENT_INTERFACE(WEBKIT_TYPE_PERMISSION_REQUEST, webkit_permission_request_interface_init))

#if ENABLE(POINTER_LOCK)
static void webkitPointerLockPermissionRequestAllow(WebKitPermissionRequest* request)
{
    ASSERT(WEBKIT_IS_POINTER_LOCK_PERMISSION_REQUEST(request));

    WebKitPointerLockPermissionRequestPrivate* priv = WEBKIT_POINTER_LOCK_PERMISSION_REQUEST(request)->priv;

    // Only one decision at a time.
    if (!priv->completionHandler)
        return;

    webkitWebViewRequestPointerLock(priv->webView.get(), WTFMove(priv->completionHandler));
}

static void webkitPointerLockPermissionRequestDeny(WebKitPermissionRequest* request)
{
    ASSERT(WEBKIT_IS_POINTER_LOCK_PERMISSION_REQUEST(request));

    WebKitPointerLockPermissionRequestPrivate* priv = WEBKIT_POINTER_LOCK_PERMISSION_REQUEST(request)->priv;

    // Only one decision at a time.
    if (!priv->completionHandler)
        return;

    webkitWebViewDenyPointerLockRequest(WTFMove(priv->completionHandler));
}
#endif // ENABLE(POINTER_LOCK)

static void webkit_permission_request_interface_init(WebKitPermissionRequestInterface* iface)
{
#if ENABLE(POINTER_LOCK)
    iface->allow = webkitPointerLockPermissionRequestAllow;
    iface->deny = webkitPointerLockPermissionRequestDeny;
#else
    iface->allow = nullptr;
    iface->deny = nullptr;
#endif // ENABLE(POINTER_LOCK)
}

static void webkitPointerLockPermissionRequestDispose(GObject* object)
{
    // Default behaviour when no decision has been made is allowing the request.
#if ENABLE(POINTER_LOCK)
    webkitPointerLockPermissionRequestAllow(WEBKIT_PERMISSION_REQUEST(object));
#endif // ENABLE(POINTER_LOCK)
    G_OBJECT_CLASS(webkit_pointer_lock_permission_request_parent_class)->dispose(object);
}

static void webkit_pointer_lock_permission_request_class_init(WebKitPointerLockPermissionRequestClass* klass)
{
    GObjectClass* objectClass = G_OBJECT_CLASS(klass);
    objectClass->dispose = webkitPointerLockPermissionRequestDispose;
}

WebKitPointerLockPermissionRequest* webkitPointerLockPermissionRequestCreate(WebKitWebView* webView, CompletionHandler<void(bool)>&& completionHandler)
{
    WebKitPointerLockPermissionRequest* request = WEBKIT_POINTER_LOCK_PERMISSION_REQUEST(g_object_new(WEBKIT_TYPE_POINTER_LOCK_PERMISSION_REQUEST, nullptr));
    request->priv->webView = webView;
    request->priv->completionHandler = WTFMove(completionHandler);
    return request;
}

void webkitPointerLockPermissionRequestDidLosePointerLock(WebKitPointerLockPermissionRequest* request)
{
    request->priv->webView = nullptr;
    if (request->priv->completionHandler)
        request->priv->completionHandler(false);
}
