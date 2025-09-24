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

#include "config.h"
#include "LargestContentfulPaintData.h"

#include "CachedImage.h"
#include "Element.h"
#include "FloatQuad.h"
#include "LargestContentfulPaint.h"
#include "LegacyRenderSVGImage.h"
#include "LocalDOMWindow.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "Page.h"
#include "Performance.h"
#include "RenderBlock.h"
#include "RenderBox.h"
#include "RenderElement.h"
#include "RenderInline.h"
#include "RenderLineBreak.h"
#include "RenderReplaced.h"
#include "RenderSVGImage.h"
#include "RenderText.h"
#include "RenderView.h"
#include "VisibleRectContext.h"

#include <wtf/Ref.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(LargestContentfulPaintData);

LargestContentfulPaintData::LargestContentfulPaintData() = default;
LargestContentfulPaintData::~LargestContentfulPaintData() = default;

// https://w3c.github.io/paint-timing/#exposed-for-paint-timing
bool LargestContentfulPaintData::isExposedForPaintTiming(const Element& element)
{
    if (!element.document().isFullyActive())
        return false;

    if (!element.isInDocumentTree()) // Also checks isConnected().
        return false;

    return true;
}

// https://w3c.github.io/largest-contentful-paint/#largest-contentful-paint-candidate
bool LargestContentfulPaintData::isEligibleForLargestContentfulPaint(const Element& element, float effectiveVisualArea)
{
    CheckedPtr renderer = element.renderer();
    if (!renderer)
        return false;

    // FIXME: Check isEffectivelyTransparent

    // FIXME: Need to implement the response length vs. image size logic.
    UNUSED_PARAM(effectiveVisualArea);
    return true;
}

// https://w3c.github.io/largest-contentful-paint/#sec-effective-visual-size
std::optional<float> LargestContentfulPaintData::effectiveVisualArea(const Element& element, CachedImage* image, FloatRect imageLocalRect, FloatRect intersectionRect)
{
    RefPtr frameView = element.document().view();
    if (!frameView)
        return { };

    auto visualViewportSize = FloatSize { frameView->visualViewportRect().size() };
    if (intersectionRect.area() >= visualViewportSize.area())
        return { };

    auto area = intersectionRect.area();
    if (image) {
        CheckedPtr renderer = element.renderer();
        if (!renderer)
            return { };

        auto absoluteContentRect = renderer->localToAbsoluteQuad(FloatRect(imageLocalRect)).boundingBox();

        auto intersectingContentRect = intersection(absoluteContentRect, intersectionRect);
        area = intersectingContentRect.area();

        auto naturalSize = image->imageSizeForRenderer(renderer.get(), 1);
        if (naturalSize.isEmpty())
            return { };

        auto scaleFactor = absoluteContentRect.area() / FloatSize { naturalSize }.area();
        if (scaleFactor > 1)
            area /= scaleFactor;

        return area;
    }

    return area;
}

// https://w3c.github.io/largest-contentful-paint/#sec-add-lcp-entry
void LargestContentfulPaintData::potentiallyAddLargestContentfulPaintEntry(Element&, CachedImage*, FloatRect imageLocalRect, FloatRect intersectionRect, DOMHighResTimeStamp paintTimestamp)
{
    UNUSED_PARAM(imageLocalRect);
    UNUSED_PARAM(intersectionRect);
    UNUSED_PARAM(paintTimestamp);
}

RefPtr<LargestContentfulPaint> LargestContentfulPaintData::takePendingEntry(DOMHighResTimeStamp)
{
    return nullptr;
}

// This is a simplified version of IntersectionObserver::computeIntersectionState(). Some code should be shared.
FloatRect LargestContentfulPaintData::computeViewportIntersectionRect(Element& element, FloatRect localRect)
{
    RefPtr frameView = element.document().view();
    if (!frameView)
        return { };

    CheckedPtr targetRenderer = element.renderer();
    if (!targetRenderer)
        return { };

    if (targetRenderer->isSkippedContent())
        return { };

    CheckedPtr rootRenderer = frameView->renderView();
    auto layoutViewport = frameView->layoutViewportRect();

    auto localTargetBounds = LayoutRect { localRect };

    // FIXME: This clips for ancestors, which maybe isn't what we want.
    auto absoluteRects = targetRenderer->computeVisibleRectsInContainer(
        { localTargetBounds },
        &targetRenderer->view(),
        {
            .hasPositionFixedDescendant = false,
            .dirtyRectIsFlipped = false,
            .options = {
                VisibleRectContext::Option::UseEdgeInclusiveIntersection,
                VisibleRectContext::Option::ApplyCompositedClips,
                VisibleRectContext::Option::ApplyCompositedContainerScrolls
            },
        }
    );

    if (!absoluteRects)
        return { };

    auto intersectionRect = layoutViewport;
    intersectionRect.edgeInclusiveIntersect(absoluteRects->clippedOverflowRect);
    return intersectionRect;
}

FloatRect LargestContentfulPaintData::computeViewportIntersectionRectForTextContainer(Element& element, const WeakHashSet<Text, WeakPtrImplWithEventTargetData>& textNodes)
{
    RefPtr frameView = element.document().view();
    if (!frameView)
        return { };

    CheckedPtr rootRenderer = frameView->renderView();
    auto layoutViewport = frameView->layoutViewportRect();

    IntRect absoluteTextBounds;
    for (RefPtr node : textNodes) {
        if (!node)
            continue;

        CheckedPtr renderer = node->renderer();
        if (!renderer)
            continue;

        if (renderer->isSkippedContent())
            continue;

        static constexpr bool useTransforms = true;
        auto absoluteBounds = renderer->absoluteBoundingBoxRect(useTransforms);
        absoluteTextBounds.unite(absoluteBounds);
    }

    auto intersectionRect = layoutViewport;
    intersectionRect.edgeInclusiveIntersect(absoluteTextBounds);

    return intersectionRect;
}

void LargestContentfulPaintData::didPaintImage(Element&, CachedImage*, FloatRect)
{
}

void LargestContentfulPaintData::didPaintText(const RenderText&, FloatRect)
{
}

} // namespace WebCore
