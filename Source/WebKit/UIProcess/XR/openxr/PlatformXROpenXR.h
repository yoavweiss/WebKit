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

#pragma once

#if ENABLE(WEBXR) && USE(OPENXR)

#include "PlatformXRCoordinator.h"
#include <openxr/openxr.h>

namespace WebKit {

class OpenXRExtensions;

class OpenXRCoordinator final : public PlatformXRCoordinator {
    WTF_MAKE_TZONE_ALLOCATED(OpenXRCoordinator);
public:
    OpenXRCoordinator();
    virtual ~OpenXRCoordinator();

    void getPrimaryDeviceInfo(WebPageProxy&, DeviceInfoCallback&&) override;

    void startSession(WebPageProxy&, WeakPtr<PlatformXRCoordinatorSessionEventClient>&&, const WebCore::SecurityOriginData&, PlatformXR::SessionMode, const PlatformXR::Device::FeatureList&) override { }
    void endSessionIfExists(WebPageProxy&) override { }

    void scheduleAnimationFrame(WebPageProxy&, std::optional<PlatformXR::RequestData>&&, PlatformXR::Device::RequestFrameCallback&& onFrameUpdateCallback) override { onFrameUpdateCallback({ }); }

private:
    void createInstance();
    void initializeDevice();
    void initializeSystem();
    void collectViewConfigurations();
    WebCore::IntSize recommendedResolution() const;

    XRDeviceIdentifier m_deviceIdentifier { XRDeviceIdentifier::generate() };
    XrInstance m_instance { XR_NULL_HANDLE };
    XrSystemId m_systemId { XR_NULL_SYSTEM_ID };
    Vector<XrViewConfigurationType> m_viewConfigurations;
    XrViewConfigurationType m_currentViewConfiguration;

    std::unique_ptr<OpenXRExtensions> m_extensions;
};

} // namespace WebKit

#endif // ENABLE(WEBXR) && USE(OPENXR)
