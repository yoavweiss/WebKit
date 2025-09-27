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
    if (!element.protectedDocument()->isFullyActive())
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

    if (renderer->style().isEffectivelyTransparent())
        return false;

    // FIXME: Need to implement the response length vs. image size logic: webkit.org/b/299558.
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
void LargestContentfulPaintData::potentiallyAddLargestContentfulPaintEntry(Element& element, CachedImage* image, FloatRect imageLocalRect, FloatRect intersectionRect, DOMHighResTimeStamp paintTimestamp)
{
    bool isNewCandidate = false;
    if (image) {
        isNewCandidate = m_imageContentSet.ensure(element, [] {
            return WeakHashSet<CachedImage> { };
        }).iterator->value.add(*image).isNewEntry;
    }

    LOG_WITH_STREAM(LargestContentfulPaint, stream << "LargestContentfulPaintData " << this << " potentiallyAddLargestContentfulPaintEntry() " << element << " image " << (image ? image->url().string() : emptyString()) << " rect " << intersectionRect << " - isNewCandidate " << isNewCandidate);

    if (!isNewCandidate)
        return;

    Ref document = element.document();
    RefPtr window = document->window();
    if (!window)
        return;

    RefPtr view = document->view();
    if (!view)
        return;

    // The spec talks about trusted scroll events, but the intent is to detect user scrolls: https://github.com/w3c/largest-contentful-paint/issues/105
    if ((view->wasEverScrolledExplicitlyByUser() || window->hasDispatchedInputEvent()))
        return;

    auto elementArea = effectiveVisualArea(element, image, imageLocalRect, intersectionRect);
    if (!elementArea)
        return;

    if (*elementArea <= m_largestPaintArea) {
        LOG_WITH_STREAM(LargestContentfulPaint, stream << " element area " << elementArea << " less than LCP " << m_largestPaintArea);
        return;
    }

    if (!isEligibleForLargestContentfulPaint(element, *elementArea))
        return;

    m_largestPaintArea = *elementArea;

    Ref pendingEntry = LargestContentfulPaint::create(0);
    pendingEntry->setElement(&element);
    pendingEntry->setSize(std::round<unsigned>(m_largestPaintArea));

    if (image) {
        pendingEntry->setURLString(image->url().string());
        // FIXME: Need to get resource loadTime: webkit.org/b/299556.
    }

    if (element.hasID())
        pendingEntry->setID(element.getIdAttribute().string());

    pendingEntry->setRenderTime(paintTimestamp);

    LOG_WITH_STREAM(LargestContentfulPaint, stream << " making new entry for " << element << " image " << (image ? image->url().string() : emptyString()) << " id " << pendingEntry->id() <<
        ": entry size " << pendingEntry->size() << ", loadTime " << pendingEntry->loadTime() << ", renderTime " << pendingEntry->renderTime());

    m_pendingEntry = RefPtr { WTFMove(pendingEntry) };
}

RefPtr<LargestContentfulPaint> LargestContentfulPaintData::takePendingEntry(DOMHighResTimeStamp paintTimestamp)
{
    auto imageRecords = std::exchange(m_pendingImageRecords, { });
    for (auto [weakElement, imageAndRects] : imageRecords) {
        RefPtr element = weakElement;
        if (!element)
            continue;

        // FIXME: This is doing multiple localToAbsolute on the same element.
        for (auto [image, rect] : imageAndRects) {
            auto intersectionRect = computeViewportIntersectionRect(*element, rect);
            potentiallyAddLargestContentfulPaintEntry(*element, &image, rect, intersectionRect, paintTimestamp);
        }
    }

    return std::exchange(m_pendingEntry, nullptr);
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
        &targetRenderer->checkedView().get(),
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

void LargestContentfulPaintData::didPaintImage(Element& element, CachedImage* image, FloatRect localRect)
{
    if (!image)
        return;

    if (localRect.isEmpty())
        return;

    if (!isExposedForPaintTiming(element))
        return;

    auto it = m_imageContentSet.find(element);
    if (it != m_imageContentSet.end()) {
        auto& imageSet = it->value;
        if (imageSet.contains(*image))
            return;
    }

    if (m_pendingImageRecords.isEmptyIgnoringNullReferences()) {
        if (RefPtr page = element.document().page())
            page->scheduleRenderingUpdate(RenderingUpdateStep::PaintTiming);
    }

    auto addResult = m_pendingImageRecords.ensure(element, [] {
        return WeakHashMap<CachedImage, FloatRect> { };
    });

    auto& imageRectMap = addResult.iterator->value;
    auto imageAddResult = imageRectMap.add(*image, localRect);
    if (!addResult.isNewEntry) {
        auto& existingRect = imageAddResult.iterator->value;
        if (localRect.area() > existingRect.area())
            imageAddResult.iterator->value = localRect;
    }
}

void LargestContentfulPaintData::didPaintText(const RenderText&, FloatRect)
{
}

} // namespace WebCore
