/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
 * Portions Copyright (c) 2010 Motorola Mobility, Inc. All rights reserved.
 * Copyright (C) 2011 Igalia S.L.
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
#include "WebPage.h"

#include "MessageSenderInlines.h"
#include "WebFrame.h"
#include "WebKeyboardEvent.h"
#include "WebPageProxyMessages.h"
#include "WebProcess.h"
#include <WebCore/BackForwardController.h>
#include <WebCore/Editor.h>
#include <WebCore/EventHandler.h>
#include <WebCore/FocusController.h>
#include <WebCore/KeyboardEvent.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameView.h>
#include <WebCore/NotImplemented.h>
#include <WebCore/Page.h>
#include <WebCore/PlatformKeyboardEvent.h>
#include <WebCore/PlatformScreen.h>
#include <WebCore/RenderTheme.h>
#include <WebCore/RenderThemeAdwaita.h>
#include <WebCore/Settings.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/WindowsKeyboardCodes.h>
#include <gtk/gtk.h>
#include <wtf/glib/GUniquePtr.h>

namespace WebKit {
using namespace WebCore;

void WebPage::platformReinitialize()
{
}

bool WebPage::platformCanHandleRequest(const ResourceRequest&)
{
    notImplemented();
    return false;
}

void WebPage::collapseSelectionInFrame(FrameIdentifier frameID)
{
    WebFrame* frame = WebProcess::singleton().webFrame(frameID);
    if (!frame || !frame->coreLocalFrame())
        return;

    // Collapse the selection without clearing it.
    const VisibleSelection& selection = frame->coreLocalFrame()->selection().selection();
    frame->coreLocalFrame()->selection().setBase(selection.extent(), selection.affinity());
}

void WebPage::showEmojiPicker(LocalFrame& frame)
{
    CompletionHandler<void(String)> completionHandler = [frame = Ref { frame }](String result) {
        if (!result.isEmpty())
            frame->editor().insertText(result, nullptr);
    };
    sendWithAsyncReply(Messages::WebPageProxy::ShowEmojiPicker(frame.view()->contentsToRootView(frame.selection().absoluteCaretBounds())), WTFMove(completionHandler));
}

void WebPage::setAccentColor(WebCore::Color color)
{
    static_cast<RenderThemeAdwaita&>(RenderTheme::singleton()).setAccentColor(color);
}

} // namespace WebKit
