/*
 * Copyright (C) 2025-2026 Igalia, S.L.
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

#if ENABLE(WEBXR) && USE(OPENXR)
#include "OpenXRSwapchain.h"

#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(OpenXRSwapchain);

std::unique_ptr<OpenXRSwapchain> OpenXRSwapchain::create(XrSession session, const XrSwapchainCreateInfo& info, HasAlpha hasAlpha)
{
    ASSERT(session != XR_NULL_HANDLE);
    // faceCount is 1 for regular swapchains and 6 for cube (cubemap) swapchains.
    ASSERT(info.faceCount == 1 || info.faceCount == 6);

    XrSwapchain swapchain { XR_NULL_HANDLE };
    CHECK_XRCMD(xrCreateSwapchain(session, &info, &swapchain));
    if (swapchain == XR_NULL_HANDLE) {
        LOG(XR, "xrCreateSwapchain() failed: swapchain is null");
        return nullptr;
    }

    uint32_t imageCount;
    CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr));
    if (!imageCount) {
        LOG(XR, "xrEnumerateSwapchainImages(): no images\n");
        return nullptr;
    }

    Vector<XrSwapchainImageOpenGLESKHR> imageBuffers(FillWith { }, imageCount, [] {
        return createOpenXRStruct<XrSwapchainImageOpenGLESKHR, XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR>();
    }());

    Vector<XrSwapchainImageBaseHeader*> imageHeaders = imageBuffers.map([](auto& image) {
        return (XrSwapchainImageBaseHeader*) &image;
    });

    // Get images from an XrSwapchain
    CHECK_XRCMD(xrEnumerateSwapchainImages(swapchain, imageCount, &imageCount, imageHeaders[0]));

    return std::unique_ptr<OpenXRSwapchain>(new OpenXRSwapchain(swapchain, info, WTF::move(imageBuffers), hasAlpha));
}

OpenXRSwapchain::OpenXRSwapchain(XrSwapchain swapchain, const XrSwapchainCreateInfo& info, Vector<XrSwapchainImageOpenGLESKHR>&& imageBuffers, HasAlpha hasAlpha)
    : m_swapchain(swapchain)
    , m_createInfo(info)
    , m_imageBuffers(WTF::move(imageBuffers))
    , m_hasAlpha(hasAlpha)
{
}

OpenXRSwapchain::~OpenXRSwapchain()
{
    if (m_acquiredTexture)
        releaseImage();
    if (m_swapchain != XR_NULL_HANDLE)
        xrDestroySwapchain(m_swapchain);
}

std::optional<PlatformGLObject> OpenXRSwapchain::acquireImage()
{
    RELEASE_ASSERT_WITH_MESSAGE(!m_acquiredTexture , "Expected no acquired images. ReleaseImage not called?");

    auto acquireInfo = createOpenXRStruct<XrSwapchainImageAcquireInfo, XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO>();
    uint32_t swapchainImageIndex = 0;
    CHECK_XRCMD(xrAcquireSwapchainImage(m_swapchain, &acquireInfo, &swapchainImageIndex));
    ASSERT(swapchainImageIndex < m_imageBuffers.size());

    auto waitInfo = createOpenXRStruct<XrSwapchainImageWaitInfo, XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO>();
    waitInfo.timeout = XR_INFINITE_DURATION;
    CHECK_XRCMD(xrWaitSwapchainImage(m_swapchain, &waitInfo));

    m_acquiredTexture = m_imageBuffers[swapchainImageIndex].image;

    return m_acquiredTexture;
}

void OpenXRSwapchain::releaseImage()
{
    RELEASE_ASSERT_WITH_MESSAGE(m_acquiredTexture, "Expected a valid acquired image. AcquireImage not called?");

    auto releaseInfo = createOpenXRStruct<XrSwapchainImageReleaseInfo, XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO>();
    CHECK_XRCMD(xrReleaseSwapchainImage(m_swapchain, &releaseInfo));

    m_acquiredTexture = 0;
}

} // namespace WebKit

#endif // ENABLE(WEBXR) && USE(OPENXR)
