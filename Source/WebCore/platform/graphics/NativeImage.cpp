/*
 * Copyright (C) 2020-2025 Apple Inc. All rights reserved.
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
#include "NativeImage.h"

#include "FloatRect.h"
#include "GraphicsContext.h"
#include "ImageBuffer.h"
#include "RenderingMode.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(NativeImage);

#if !USE(CG)
RefPtr<NativeImage> NativeImage::create(PlatformImagePtr&& platformImage, RenderingResourceIdentifier identifier)
{
    if (!platformImage)
        return nullptr;
    return adoptRef(*new NativeImage(WTFMove(platformImage), identifier));
}

RefPtr<NativeImage> NativeImage::createTransient(PlatformImagePtr&& image, RenderingResourceIdentifier identifier)
{
    return create(WTFMove(image), identifier);
}
#endif

NativeImage::NativeImage(PlatformImagePtr&& platformImage, RenderingResourceIdentifier renderingResourceIdentifier)
    : m_platformImage(WTFMove(platformImage))
    , m_renderingResourceIdentifier(renderingResourceIdentifier)
{
}

NativeImage::~NativeImage()
{
    for (auto& observer : m_observers)
        observer.willDestroyNativeImage(*this);
}

const PlatformImagePtr& NativeImage::platformImage() const
{
    return m_platformImage;
}

bool NativeImage::hasHDRContent() const
{
    return colorSpace().usesITUR_2100TF();
}

void NativeImage::drawWithToneMapping(GraphicsContext& context, const FloatRect& destinationRect, const FloatRect& sourceRect, ImagePaintingOptions options)
{
    ASSERT(hasHDRContent());

    auto colorSpaceForToneMapping = [](GraphicsContext& context) {
#if PLATFORM(IOS_FAMILY)
        // iOS typically renders into extended range sRGB to preserve wide gamut colors, but here we want
        // a non-dynamic but extended-range colorspace such that the contents are tone mapped to SDR range.
        UNUSED_PARAM(context);
        return DestinationColorSpace::DisplayP3();
#else
        // Otherwise, match the colorSpace of the GraphicsContext even if it is dynamic-extended-range.
        // The BGRA8 pixel format of the intermediate ImageBuffer will force the tone-mapping.
        return context.colorSpace();
#endif
    };

    auto imageBuffer = context.createScaledImageBuffer(destinationRect, context.scaleFactor(), colorSpaceForToneMapping(context), RenderingMode::Unaccelerated, RenderingMethod::Local);
    if (!imageBuffer)
        return;

    imageBuffer->context().drawNativeImageInternal(*this, destinationRect, sourceRect, options);

    auto sourceRectScaled = FloatRect { { }, sourceRect.size() };
    auto scaleFactor = destinationRect.size() / sourceRect.size();
    sourceRectScaled.scale(scaleFactor * context.scaleFactor());

    context.drawImageBuffer(*imageBuffer, destinationRect, sourceRectScaled, { });
}

void NativeImage::replacePlatformImage(PlatformImagePtr&& platformImage)
{
    ASSERT(platformImage);
    m_platformImage = WTFMove(platformImage);
}

} // namespace WebCore
