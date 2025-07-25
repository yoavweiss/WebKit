/*
 * Copyright (C) 2007 Apple Inc. All rights reserved.
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

#pragma once

#include "FloatSize.h"
#include "ImageOrientation.h"
#include "IntSize.h"
#include "Path.h"
#include "TextFlags.h"
#include "TextIndicator.h"
#include <wtf/Forward.h>

#if PLATFORM(IOS_FAMILY)
#include <wtf/RetainPtr.h>
typedef struct CGImage *CGImageRef;
#elif PLATFORM(MAC)
#include <wtf/RetainPtr.h>
OBJC_CLASS NSImage;
#elif USE(CAIRO)
#if PLATFORM(WIN)
typedef struct HBITMAP__* HBITMAP;
#endif
#include "RefPtrCairo.h"
#elif USE(SKIA)
#include <skia/core/SkImage.h>
#endif

namespace WebCore {

class Element;
class GraphicsClient;
class Image;
class IntRect;
class LocalFrame;
class Node;

#if PLATFORM(IOS_FAMILY)
typedef RetainPtr<CGImageRef> DragImageRef;
#elif PLATFORM(MAC)
typedef RetainPtr<NSImage> DragImageRef;
#elif USE(CAIRO) && PLATFORM(WIN)
typedef HBITMAP DragImageRef;
#elif USE(CAIRO)
typedef RefPtr<cairo_surface_t> DragImageRef;
#elif USE(SKIA)
typedef sk_sp<SkImage> DragImageRef;
#else
typedef void* DragImageRef;
#endif

#if PLATFORM(COCOA)
extern const float ColorSwatchCornerRadius;
extern const float ColorSwatchStrokeSize;
extern const float ColorSwatchWidth;
#endif

IntSize dragImageSize(DragImageRef);

// These functions should be memory neutral, eg. if they return a newly allocated image,
// they should release the input image. As a corollary these methods don't guarantee
// the input image ref will still be valid after they have been called.
DragImageRef fitDragImageToMaxSize(DragImageRef, const IntSize& srcSize, const IntSize& dstSize);
DragImageRef scaleDragImage(DragImageRef, FloatSize scale);
DragImageRef platformAdjustDragImageForDeviceScaleFactor(DragImageRef, float deviceScaleFactor);
DragImageRef dissolveDragImageToFraction(DragImageRef, float delta);

DragImageRef createDragImageFromImage(Image*, ImageOrientation, GraphicsClient* = nullptr, float deviceScaleFactor = 1);
DragImageRef createDragImageIconForCachedImageFilename(const String&);

struct DragImageData {
    DragImageRef dragImageRef;
    RefPtr<TextIndicator> textIndicator;
};
// FIXME: These platform helpers should be refactored to avoid using `LocalFrame` and `Node`.
WEBCORE_EXPORT DragImageRef createDragImageForNode(LocalFrame&, Node&);
WEBCORE_EXPORT DragImageData createDragImageForSelection(LocalFrame&, bool forceBlackText = false);
WEBCORE_EXPORT DragImageRef createDragImageForRange(LocalFrame&, const SimpleRange&, bool forceBlackText = false);
DragImageRef createDragImageForColor(const Color&, const FloatRect&, float, Path&);
DragImageRef createDragImageForImage(LocalFrame&, Node&, IntRect& imageRect, IntRect& elementRect);

DragImageData createDragImageForLink(Element&, URL&, const String& label, float deviceScaleFactor);
void deleteDragImage(DragImageRef);

IntPoint dragOffsetForLinkDragImage(DragImageRef);
FloatPoint anchorPointForLinkDragImage(DragImageRef);

class DragImage final {
public:
    WEBCORE_EXPORT DragImage();
    explicit DragImage(DragImageRef);
    WEBCORE_EXPORT DragImage(DragImage&&);
    WEBCORE_EXPORT ~DragImage();

    DragImage(RefPtr<TextIndicator> textIndicator, std::optional<Path>&& visiblePath)
        : m_textIndicator(WTFMove(textIndicator))
        , m_visiblePath(WTFMove(visiblePath))
    { }

    WEBCORE_EXPORT DragImage& operator=(DragImage&&);

    void setTextIndicator(const RefPtr<TextIndicator> textIndicator) { m_textIndicator = textIndicator; }
    bool hasTextIndicator() const { return !!m_textIndicator; }
    const RefPtr<TextIndicator> textIndicator() const { return m_textIndicator; }

    void setVisiblePath(const Path& path) { m_visiblePath = path; }
    bool hasVisiblePath() const { return !!m_visiblePath; }
    const std::optional<Path>& visiblePath() const { return m_visiblePath; }

    explicit operator bool() const { return !!m_dragImageRef; }
    DragImageRef get() const { return m_dragImageRef; }

private:
    DragImageRef m_dragImageRef;
    RefPtr<TextIndicator> m_textIndicator;
    std::optional<Path> m_visiblePath;
};

}
