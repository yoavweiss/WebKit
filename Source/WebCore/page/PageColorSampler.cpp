/*
 * Copyright (C) 2021-2023 Apple Inc. All rights reserved.
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

#include "config.h"
#include "PageColorSampler.h"

#include "ColorHash.h"
#include "ColorSerialization.h"
#include "ContentfulPaintChecker.h"
#include "Document.h"
#include "DocumentInlines.h"
#include "Element.h"
#include "FixedContainerEdges.h"
#include "FrameSnapshotting.h"
#include "HTMLCanvasElement.h"
#include "HTMLIFrameElement.h"
#include "HitTestRequest.h"
#include "HitTestResult.h"
#include "ImageBuffer.h"
#include "IntPoint.h"
#include "IntRect.h"
#include "IntSize.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "Node.h"
#include "Page.h"
#include "PixelBuffer.h"
#include "RegistrableDomain.h"
#include "RenderImage.h"
#include "RenderObjectInlines.h"
#include "RenderStyleInlines.h"
#include "Settings.h"
#include "Styleable.h"
#include "WebAnimation.h"
#include <ranges>
#include <wtf/HashCountedSet.h>
#include <wtf/ListHashSet.h>
#include <wtf/OptionSet.h>
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>
#include <wtf/URL.h>

namespace WebCore {

static bool isValidSampleLocation(Document& document, const IntPoint& location)
{
    // FIXME: <https://webkit.org/b/225167> (Sampled Page Top Color: hook into painting logic instead of taking snapshots)

    constexpr OptionSet<HitTestRequest::Type> hitTestRequestTypes { HitTestRequest::Type::ReadOnly, HitTestRequest::Type::IgnoreCSSPointerEventsProperty, HitTestRequest::Type::DisallowUserAgentShadowContent, HitTestRequest::Type::CollectMultipleElements, HitTestRequest::Type::IncludeAllElementsUnderPoint };
    HitTestResult hitTestResult(location);
    document.hitTest(hitTestRequestTypes, hitTestResult);

    for (auto& hitTestNode : hitTestResult.listBasedTestResult()) {
        auto& node = hitTestNode.get();

        auto* renderer = node.renderer();
        if (!renderer)
            return false;

        // Skip images (both `<img>` and CSS `background-image`) as they're likely not a solid color.
        if (is<RenderImage>(renderer) || renderer->style().hasBackgroundImage())
            return false;

        RefPtr element = dynamicDowncast<Element>(node);
        if (!element)
            continue;

        auto styleable = Styleable::fromElement(*element);

        // Skip nodes with animations as the sample may get an odd color if the animation is in-progress.
        if (styleable.hasRunningTransitions())
            return false;
        if (auto* animations = styleable.animations()) {
            for (auto& animation : *animations) {
                if (animation->playState() == WebAnimation::PlayState::Running)
                    return false;
            }
        }

        // Skip `<canvas>` but only if they've been drawn into. Guess this by seeing if there's already
        // a `CanvasRenderingContext`, which is only created by JavaScript.
        if (RefPtr canvas = dynamicDowncast<HTMLCanvasElement>(*element); canvas && canvas->renderingContext())
            return false;

        // Skip 3rd-party `<iframe>` as the content likely won't match the rest of the page.
        if (is<HTMLIFrameElement>(*element))
            return false;
    }

    return true;
}

static std::optional<Lab<float>> sampleColor(Document& document, IntPoint&& location)
{
    // FIXME: <https://webkit.org/b/225167> (Sampled Page Top Color: hook into painting logic instead of taking snapshots)

    if (!isValidSampleLocation(document, location))
        return std::nullopt;

    // FIXME: <https://webkit.org/b/225942> (Sampled Page Top Color: support sampling non-RGB values like P3)
    auto colorSpace = DestinationColorSpace::SRGB();

    ASSERT(document.view());
    auto snapshot = snapshotFrameRect(document.view()->protectedFrame(), IntRect(location, IntSize(1, 1)), { { SnapshotFlags::ExcludeSelectionHighlighting, SnapshotFlags::PaintEverythingExcludingSelection }, ImageBufferPixelFormat::BGRA8, colorSpace });
    if (!snapshot)
        return std::nullopt;

    auto pixelBuffer = snapshot->getPixelBuffer({ AlphaPremultiplication::Unpremultiplied, PixelFormat::BGRA8, colorSpace }, { { }, snapshot->truncatedLogicalSize() });
    if (!pixelBuffer)
        return std::nullopt;

    if (pixelBuffer->bytes().size() < 4)
        return std::nullopt;

    auto snapshotData = pixelBuffer->bytes();
    return convertColor<Lab<float>>(SRGBA<uint8_t> { snapshotData[2], snapshotData[1], snapshotData[0], snapshotData[3] });
}

static double colorDifference(const Lab<float>& lhs, const Lab<float>& rhs)
{
    // FIXME: This should use a formal color difference metric (deltaE2000, deltaEOK) as this current one is not perceptually uniform (see https://en.wikipedia.org/wiki/Color_difference).

    auto resolvedLeftHandSide = lhs.resolved();
    auto resolvedRightHandSide = rhs.resolved();

    return sqrt(pow(resolvedRightHandSide.lightness - resolvedLeftHandSide.lightness, 2) + pow(resolvedRightHandSide.a - resolvedLeftHandSide.a, 2) + pow(resolvedRightHandSide.b - resolvedLeftHandSide.b, 2));
}

static Lab<float> averageColor(std::span<Lab<float>> colors)
{
    ColorComponents<float, 3> totals { };
    for (auto color : colors)
        totals += asColorComponents(color.resolved()).subset<0, 3>();

    totals /= colors.size();

    return { totals[0], totals[1], totals[2] };
}

std::optional<Color> PageColorSampler::sampleTop(Page& page)
{
    // If `std::nullopt` is returned then that means that no samples were taken (i.e. the `Page` is not ready yet).
    // If an invalid `Color` is returned then that means that samples Were taken but they were too different.

    auto maxDifference = page.settings().sampledPageTopColorMaxDifference();
    if (maxDifference <= 0) {
        // Pretend that the samples are too different so that this function is not called again.
        return Color();
    }

    RefPtr localMainFrame = page.localMainFrame();
    if (!localMainFrame)
        return std::nullopt;

    RefPtr mainDocument = localMainFrame->document();
    if (!mainDocument)
        return std::nullopt;

    RefPtr frameView = localMainFrame->view();
    if (!frameView)
        return std::nullopt;

    // Don't take samples if the layer tree is still frozen.
    if (frameView->needsLayout())
        return std::nullopt;

    // Don't attempt to hit test or sample if we don't have any content yet.
    if (!frameView->isVisuallyNonEmpty() || !frameView->hasContentfulDescendants() || !ContentfulPaintChecker::qualifiesForContentfulPaint(*frameView))
        return std::nullopt;

    // Decrease the width by one pixel so that the last sample is within bounds and not off-by-one.
    auto frameWidth = frameView->contentsWidth() - 1;

    static constexpr auto numSamples = 5;
    size_t nonMatchingColorIndex = numSamples;

    std::array<Lab<float>, numSamples> samples;
    std::array<double, numSamples - 1> differences;

    auto shouldStopAfterFindingNonMatchingColor = [&] (size_t i) -> bool {
        // Bail if the non-matching color is not the first or last sample, or there already is an non-matching color.
        if ((i && i < numSamples - 1) || nonMatchingColorIndex != numSamples)
            return true;

        nonMatchingColorIndex = i;
        return false;
    };

    for (size_t i = 0; i < numSamples; ++i) {
        auto sample = sampleColor(*mainDocument, IntPoint(frameWidth * i / (numSamples - 1), 0));
        if (!sample) {
            if (shouldStopAfterFindingNonMatchingColor(i))
                return Color();
            continue;
        }

        samples[i] = *sample;

        if (i) {
            // Each `difference` item compares `i` with `i - 1` so if the first comparison (`i == 1`)
            // is too large of a difference, we should treat `i - 1` (i.e. `0`) as the problem since
            // we only allow for non-matching colors being the first or last sampled color.
            auto effectiveNonMatchingColorIndex = i == 1 ? 0 : i;

            differences[i - 1] = colorDifference(samples[i - 1], samples[i]);
            if (differences[i - 1] > maxDifference) {
                if (shouldStopAfterFindingNonMatchingColor(effectiveNonMatchingColorIndex))
                    return Color();
                continue;
            }

            double cumuluativeDifference = 0;
            for (size_t j = 0; j < i; ++j) {
                if (j == nonMatchingColorIndex)
                    continue;
                cumuluativeDifference += differences[j];
            }
            if (cumuluativeDifference > maxDifference) {
                if (shouldStopAfterFindingNonMatchingColor(effectiveNonMatchingColorIndex)) {
                    // If we haven't already identified a non-matching sample and the difference between the first
                    // and second samples or the second-to-last and last samples is less than the maximum, mark
                    // the first/last sample as non-matching to give a chance for the rest of the samples to match.
                    if (nonMatchingColorIndex == numSamples && (!i || i == numSamples - 1) && cumuluativeDifference - differences[i - 1] <= maxDifference) {
                        nonMatchingColorIndex = effectiveNonMatchingColorIndex;
                        continue;
                    }
                    return Color();
                }
                continue;
            }
        }
    }

    // Decrease the height by one pixel so that the last sample is within bounds and not off-by-one.
    auto minHeight = page.settings().sampledPageTopColorMinHeight() - 1;
    if (minHeight > 0) {
        if (nonMatchingColorIndex) {
            if (auto leftMiddleSample = sampleColor(*mainDocument, IntPoint(0, minHeight))) {
                if (colorDifference(*leftMiddleSample, samples[0]) > maxDifference)
                    return Color();
            }
        }

        if (nonMatchingColorIndex != numSamples - 1) {
            if (auto rightMiddleSample = sampleColor(*mainDocument, IntPoint(frameWidth, minHeight))) {
                if (colorDifference(*rightMiddleSample, samples[numSamples - 1]) > maxDifference)
                    return Color();
            }
        }
    }

    if (!nonMatchingColorIndex)
        return averageColor(std::span(samples).subspan<1, numSamples - 1>());
    else if (nonMatchingColorIndex == numSamples - 1)
        return averageColor(std::span(samples).subspan<0, numSamples - 1>());
    else
        return averageColor(std::span(samples));
}

bool PageColorSampler::colorsAreSimilar(const Color& a, const Color& b)
{
    static constexpr auto maxDistanceSquaredForSimilarColors = 36;
    auto [redA, greenA, blueA, alphaA] = a.toResolvedColorComponentsInColorSpace(DestinationColorSpace::SRGB());
    auto [redB, greenB, blueB, alphaB] = b.toResolvedColorComponentsInColorSpace(DestinationColorSpace::SRGB());
    auto distance = pow(255 * (redA - redB), 2) + pow(255 * (greenA - greenB), 2) + pow(255 * (blueA - blueB), 2);
    return distance <= maxDistanceSquaredForSimilarColors;
}

Variant<PredominantColorType, Color> PageColorSampler::predominantColor(Page& page, const LayoutRect& absoluteRect)
{
    RefPtr frame = page.localMainFrame();
    if (!frame)
        return PredominantColorType::None;

    RefPtr view = frame->view();
    if (!view)
        return PredominantColorType::None;

    RefPtr document = frame->document();
    if (!document)
        return PredominantColorType::None;

    static constexpr OptionSet snapshotFlags {
        SnapshotFlags::ExcludeSelectionHighlighting,
        SnapshotFlags::PaintEverythingExcludingSelection,
        SnapshotFlags::ExcludeReplacedContentExceptForIFrames,
        SnapshotFlags::ExcludeText,
        SnapshotFlags::FixedAndStickyLayersOnly,
    };

    auto colorSpace = DestinationColorSpace::SRGB();
    auto snapshot = snapshotFrameRect(*frame, snappedIntRect(absoluteRect), { snapshotFlags, ImageBufferPixelFormat::BGRA8, colorSpace });
    if (!snapshot)
        return PredominantColorType::None;

    auto pixelBuffer = snapshot->getPixelBuffer({ AlphaPremultiplication::Unpremultiplied, PixelFormat::BGRA8, colorSpace }, { { }, snapshot->truncatedLogicalSize() });
    if (!pixelBuffer)
        return PredominantColorType::None;

    static constexpr auto sampleCount = 29;
    static constexpr auto minimumSampleCountForPredominantColor = 0.67 * sampleCount;
    static constexpr auto bytesPerPixel = 4;

    auto isNearlyTransparent = [](const Color& color) {
        return color.alphaAsFloat() < nearlyTransparentAlphaThreshold;
    };

    auto numberOfBytes = pixelBuffer->bytes().size();
    auto numberOfPixels = numberOfBytes / bytesPerPixel;
    if (numberOfPixels <= sampleCount)
        return PredominantColorType::None;

    auto byteSamplingInterval = bytesPerPixel * (numberOfPixels / (sampleCount - 1));
    auto pixels = pixelBuffer->bytes();
    HashCountedSet<Color> colorDistribution;
    for (uint64_t i = 0; i < numberOfBytes; i += byteSamplingInterval) {
        auto color = Color { SRGBA<uint8_t> { pixels[i + 2], pixels[i + 1], pixels[i], pixels[i + 3] } };
        if (!color.isVisible())
            continue;

        colorDistribution.add(color);
    }

    if (colorDistribution.isEmpty())
        return PredominantColorType::None;

    for (auto& [color, count] : colorDistribution) {
        if (count > minimumSampleCountForPredominantColor) {
            if (isNearlyTransparent(color))
                return PredominantColorType::None;

            return { WTFMove(color) };
        }
    }

    using PairType = std::pair<Color, unsigned>;
    Vector<PairType> colorsByDescendingFrequency;
    colorsByDescendingFrequency.reserveInitialCapacity(colorDistribution.size());
    for (auto& [color, count] : colorDistribution)
        colorsByDescendingFrequency.append({ color, count });

    std::ranges::stable_sort(colorsByDescendingFrequency, std::ranges::greater { }, &PairType::second);

    std::optional<Color> mostFrequentColor;
    unsigned mostFrequentColorCount = 0;

    // FIXME: This doesn't account for the case where a predominant color is not similar to the color with the highest frequency.
    for (auto& [color, count] : colorsByDescendingFrequency) {
        if (!mostFrequentColor) {
            mostFrequentColor = color;
            mostFrequentColorCount = count;
            continue;
        }

        if (!colorsAreSimilar(*mostFrequentColor, color))
            continue;

        mostFrequentColorCount += count;

        if (mostFrequentColorCount > minimumSampleCountForPredominantColor) {
            if (isNearlyTransparent(*mostFrequentColor))
                return PredominantColorType::None;

            return { WTFMove(*mostFrequentColor) };
        }
    }

    return PredominantColorType::Multiple;
}

} // namespace WebCore
