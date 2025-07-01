/*
 * Copyright (C) 2025 Igalia, S.L.
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
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "PlatformXROpenXR.h"

#if ENABLE(WEBXR) && USE(OPENXR)

#include "OpenXRExtensions.h"
#include "OpenXRUtils.h"
#include <wtf/RunLoop.h>

#if USE(LIBEPOXY)
#include <epoxy/egl.h>
#else
#include <EGL/egl.h>
#endif
#include <openxr/openxr_platform.h>

namespace WebKit {

OpenXRCoordinator::OpenXRCoordinator()
{
    ASSERT(RunLoop::isMain());
}

OpenXRCoordinator::~OpenXRCoordinator()
{
    if (m_instance != XR_NULL_HANDLE)
        xrDestroyInstance(m_instance);
}

void OpenXRCoordinator::getPrimaryDeviceInfo(WebPageProxy&, DeviceInfoCallback&& callback)
{
    ASSERT(RunLoop::isMain());

    initializeDevice();
    if (m_instance == XR_NULL_HANDLE || m_systemId == XR_NULL_SYSTEM_ID) {
        LOG(XR, "Failed to initialize OpenXR system");
        callback(std::nullopt);
        return;
    }

    auto supportsOrientationTracking = [instance = m_instance, system = m_systemId]() -> bool {
        XrSystemProperties systemProperties = createOpenXRStruct<XrSystemProperties, XR_TYPE_SYSTEM_PROPERTIES>();
        CHECK_XRCMD(xrGetSystemProperties(instance, system, &systemProperties));
        return systemProperties.trackingProperties.orientationTracking == XR_TRUE;
    };

    XRDeviceInfo deviceInfo { .identifier = m_deviceIdentifier, .vrFeatures = { }, .arFeatures = { } };
    deviceInfo.supportsOrientationTracking = supportsOrientationTracking();
    deviceInfo.supportsStereoRendering = m_currentViewConfiguration == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    deviceInfo.recommendedResolution = recommendedResolution();
    LOG(XR, "OpenXR device info:\n\tOrientation tracking: %s\n\tStereo rendering: %s\n\tRecommended resolution: %dx%d", deviceInfo.supportsOrientationTracking ? "yes" : "no", deviceInfo.supportsStereoRendering ? "yes" : "no", deviceInfo.recommendedResolution.width(), deviceInfo.recommendedResolution.height());

    // OpenXR runtimes MUST support VIEW and LOCAL reference spaces.
    deviceInfo.vrFeatures.append(PlatformXR::SessionFeature::ReferenceSpaceTypeViewer);
    deviceInfo.arFeatures.append(PlatformXR::SessionFeature::ReferenceSpaceTypeViewer);
    deviceInfo.vrFeatures.append(PlatformXR::SessionFeature::ReferenceSpaceTypeLocal);
    deviceInfo.arFeatures.append(PlatformXR::SessionFeature::ReferenceSpaceTypeLocal);

    if (m_extensions->isExtensionSupported(XR_MSFT_UNBOUNDED_REFERENCE_SPACE_EXTENSION_NAME ""_span)) {
        deviceInfo.vrFeatures.append(PlatformXR::SessionFeature::ReferenceSpaceTypeUnbounded);
        deviceInfo.arFeatures.append(PlatformXR::SessionFeature::ReferenceSpaceTypeUnbounded);
    }

    // In order to get the supported reference space types, we need the session to be created. However at this point we shouldn't do it.
    // Instead, we report ReferenceSpaceTypeLocalFloor as available, because we can supoport it via either the STAGE reference space, the
    // LOCAL_FLOOR reference space or even via an educated guess from the LOCAL reference space as a backup.
    deviceInfo.vrFeatures.append(PlatformXR::SessionFeature::ReferenceSpaceTypeLocalFloor);
    deviceInfo.arFeatures.append(PlatformXR::SessionFeature::ReferenceSpaceTypeLocalFloor);

    callback(WTFMove(deviceInfo));
}

void OpenXRCoordinator::createInstance()
{
    ASSERT(RunLoop::isMain());
    ASSERT(m_instance == XR_NULL_HANDLE);

    Vector<char *, 2> extensions;
#if defined(XR_USE_PLATFORM_EGL)
    extensions.append(const_cast<char*>(XR_MNDX_EGL_ENABLE_EXTENSION_NAME));
#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
    extensions.append(const_cast<char*>(XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME));
#endif
#endif

    XrInstanceCreateInfo createInfo = createOpenXRStruct<XrInstanceCreateInfo, XR_TYPE_INSTANCE_CREATE_INFO >();
    createInfo.applicationInfo = { "WebKit", 1, "WebKit", 1, XR_CURRENT_API_VERSION };
    createInfo.enabledApiLayerCount = 0;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.enabledExtensionNames = extensions.mutableSpan().data();

    CHECK_XRCMD(xrCreateInstance(&createInfo, &m_instance));
}

WebCore::IntSize OpenXRCoordinator::recommendedResolution() const
{
    ASSERT(RunLoop::isMain());
    ASSERT(m_viewConfigurations.size());

    uint32_t viewCount;
    CHECK_XRCMD(xrEnumerateViewConfigurationViews(m_instance, m_systemId, m_currentViewConfiguration, 0, &viewCount, nullptr));
    if (!viewCount) {
        LOG(XR, "No views available for configuration type %s", toString(m_currentViewConfiguration));
        return { 0, 0 };
    }

    Vector<XrViewConfigurationView> views(viewCount, createOpenXRStruct<XrViewConfigurationView, XR_TYPE_VIEW_CONFIGURATION_VIEW>());
    CHECK_XRCMD(xrEnumerateViewConfigurationViews(m_instance, m_systemId, m_currentViewConfiguration, viewCount, &viewCount, views.mutableSpan().data()));

    // OpenXR is very flexible wrt views resolution, but the current WebKit architecture expects a single resolution for all views.
    return { static_cast<int>(viewCount * views.first().recommendedImageRectWidth), static_cast<int>(views.first().recommendedImageRectHeight) };
}

void OpenXRCoordinator::collectViewConfigurations()
{
    ASSERT(RunLoop::isMain());
    ASSERT(m_instance != XR_NULL_HANDLE);

    uint32_t viewConfigurationCount;
    CHECK_XRCMD(xrEnumerateViewConfigurations(m_instance, m_systemId, 0, &viewConfigurationCount, nullptr));

    if (!viewConfigurationCount)
        return;

    m_viewConfigurations.resize(viewConfigurationCount);
    CHECK_XRCMD(xrEnumerateViewConfigurations(m_instance, m_systemId, viewConfigurationCount, &viewConfigurationCount, m_viewConfigurations.mutableSpan().data()));

    const XrViewConfigurationType preferredViewConfiguration = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    m_currentViewConfiguration = m_viewConfigurations.find(preferredViewConfiguration) != notFound ? preferredViewConfiguration : m_viewConfigurations.first();
    LOG(XR, "OpenXR selected view configurations: %s", toString(m_currentViewConfiguration));
}

void OpenXRCoordinator::initializeSystem()
{
    ASSERT(RunLoop::isMain());
    ASSERT(m_instance != XR_NULL_HANDLE);

    XrSystemGetInfo systemInfo = createOpenXRStruct<XrSystemGetInfo, XR_TYPE_SYSTEM_GET_INFO>();
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    CHECK_XRCMD(xrGetSystem(m_instance, &systemInfo, &m_systemId));
}

void OpenXRCoordinator::initializeDevice()
{
    ASSERT(RunLoop::isMain());

    if (m_instance != XR_NULL_HANDLE)
        return;

    m_extensions = OpenXRExtensions::create();
    if (!m_extensions) {
        LOG(XR, "Failed to create OpenXRExtensions.");
        return;
    }

    createInstance();
    if (m_instance == XR_NULL_HANDLE) {
        LOG(XR, "Failed to create OpenXR instance.");
        return;
    }

    initializeSystem();
    if (m_systemId == XR_NULL_SYSTEM_ID) {
        LOG(XR, "Failed to get OpenXR system ID.");
        return;
    }

    collectViewConfigurations();
}

} // namespace WebKit

#endif // ENABLE(WEBXR) && USE(OPENXR)
