/*
 * Copyright (C) 2011-2025 Apple Inc. All rights reserved.
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
#import "WebDragClient.h"

#if ENABLE(DRAG_SUPPORT)

#import "MessageSenderInlines.h"
#import "PasteboardTypes.h"
#import "WebPage.h"
#import "WebPageProxyMessages.h"
#import <WebCore/CachedImage.h>
#import <WebCore/Document.h>
#import <WebCore/DragController.h>
#import <WebCore/Editor.h>
#import <WebCore/ElementInlines.h>
#import <WebCore/FrameDestructionObserver.h>
#import <WebCore/FrameView.h>
#import <WebCore/GraphicsContextCG.h>
#import <WebCore/LegacyWebArchive.h>
#import <WebCore/LocalCurrentGraphicsContext.h>
#import <WebCore/LocalFrame.h>
#import <WebCore/NotImplemented.h>
#import <WebCore/Page.h>
#import <WebCore/PagePasteboardContext.h>
#import <WebCore/Pasteboard.h>
#import <WebCore/RenderImage.h>
#import <WebCore/ShareableBitmap.h>
#import <WebCore/StringTruncator.h>
#import <wtf/StdLibExtras.h>
#import <wtf/cocoa/NSURLExtras.h>

#if PLATFORM(IOS_FAMILY)
#import "UIKitSPI.h"
#endif

namespace WebKit {
using namespace WebCore;

#if USE(APPKIT)
using DragImage = NSImage *;
#else
using DragImage = CGImageRef;
#endif

static RefPtr<ShareableBitmap> convertDragImageToBitmap(DragImage image, const IntSize& size, Frame& frame)
{
    auto bitmap = ShareableBitmap::create({ size, screenColorSpace(frame.protectedMainFrame()->virtualView()) });
    if (!bitmap)
        return nullptr;

    auto graphicsContext = bitmap->createGraphicsContext();
    if (!graphicsContext)
        return nullptr;

    LocalCurrentGraphicsContext savedContext(*graphicsContext);
#if USE(APPKIT)
    [image drawInRect:NSMakeRect(0, 0, bitmap->size().width(), bitmap->size().height()) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
#else
    CGContextDrawImage(graphicsContext->platformContext(), CGRectMake(0, 0, size.width(), size.height()), image);
#endif

    return bitmap;
}

void WebDragClient::startDrag(DragItem dragItem, DataTransfer&, Frame& frame, const std::optional<NodeIdentifier>& nodeID)
{
    auto& image = dragItem.image;

#if USE(APPKIT)
    IntSize bitmapSize([image.get() size]);
#else
    IntSize bitmapSize(CGImageGetWidth(image.get().get()), CGImageGetHeight(image.get().get()));
#endif
    auto bitmap = convertDragImageToBitmap(image.get().get(), bitmapSize, frame);
    if (!bitmap)
        return;
    auto handle = bitmap->createHandle();
    if (!handle)
        return;

    m_page->willStartDrag();
    m_page->send(Messages::WebPageProxy::StartDrag(dragItem, WTFMove(*handle), nodeID));
}

void WebDragClient::didConcludeEditDrag()
{
#if PLATFORM(IOS_FAMILY)
    m_page->didConcludeEditDrag();
#endif
}

#if USE(APPKIT)

static WebCore::CachedImage* cachedImage(Element& element)
{
    CheckedPtr renderImage = dynamicDowncast<WebCore::RenderImage>(element.renderer());
    if (!renderImage)
        return nullptr;
    auto* image = renderImage->cachedImage();
    if (!image || image->errorOccurred()) 
        return nullptr;
    return image;
}

void WebDragClient::declareAndWriteDragImage(const String& pasteboardName, Element& element, const URL& url, const String& label, LocalFrame*)
{
    ASSERT(pasteboardName == String(NSPasteboardNameDrag));

    WebCore::CachedImage* image = cachedImage(element);

    String extension;
    if (image) {
        extension = image->protectedImage()->filenameExtension();
        if (extension.isEmpty())
            return;
    }

    String title = label;
    if (title.isEmpty()) {
        title = url.lastPathComponent().toString();
        if (title.isEmpty())
            title = WTF::userVisibleString(url.createNSURL().get());
    }

    auto archive = LegacyWebArchive::create(element);

    RetainPtr response = image->response().nsURLResponse();
    
    RefPtr imageBuffer = image->image()->data();

    std::optional<SharedMemory::Handle> imageHandle;
    {
        auto sharedMemoryBuffer = SharedMemory::copyBuffer(imageBuffer.releaseNonNull());
        if (!sharedMemoryBuffer)
            return;
        imageHandle = sharedMemoryBuffer->createHandle(SharedMemory::Protection::ReadOnly);
        if (!imageHandle)
            return;
    }
    RetainPtr<CFDataRef> data = archive ? archive->rawDataRepresentation() : 0;
    std::optional<SharedMemory::Handle> archiveHandle;
    if (data) {
        auto archiveBuffer = SharedBuffer::create((__bridge NSData *)data.get());
        auto archiveSharedMemoryBuffer = SharedMemory::copyBuffer(archiveBuffer.get());
        if (!archiveSharedMemoryBuffer)
            return;
        archiveHandle = archiveSharedMemoryBuffer->createHandle(SharedMemory::Protection::ReadOnly);
        if (!archiveHandle)
            return;
    }

    String filename = String([response suggestedFilename]);
    if (m_page->isInspectorPage()) {
        String downloadFilename = ResourceResponseBase::sanitizeSuggestedFilename(element.attributeWithoutSynchronization(HTMLNames::filenameAttr));
        if (!downloadFilename.isEmpty())
            filename = downloadFilename;
    }

    m_page->send(Messages::WebPageProxy::SetPromisedDataForImage(pasteboardName, WTFMove(*imageHandle), filename, extension, title, String([[response URL] absoluteString]), WTF::userVisibleString(url.createNSURL().get()), WTFMove(*archiveHandle), element.protectedDocument()->originIdentifierForPasteboard()));
}

#else

void WebDragClient::declareAndWriteDragImage(const String& pasteboardName, Element& element, const URL& url, const String& label, LocalFrame*)
{
    if (RefPtr frame = element.document().frame())
        frame->editor().writeImageToPasteboard(*Pasteboard::createForDragAndDrop(PagePasteboardContext::create(frame->pageID())), element, url, label);
}

#endif // USE(APPKIT)

} // namespace WebKit

#endif // ENABLE(DRAG_SUPPORT)
