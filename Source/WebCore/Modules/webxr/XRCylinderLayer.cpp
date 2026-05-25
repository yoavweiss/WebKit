/*
 * Copyright (C) 2025 Apple, Inc. All rights reserved.
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
#include "XRCylinderLayer.h"

#if ENABLE(WEBXR_LAYERS)
#include "EventTargetInlines.h"
#include "Logging.h"
#include "TransformationMatrix.h"
#include "WebXRRigidTransform.h"
#include "WebXRSession.h"
#include "XRLayerBacking.h"
#include "XRWebGLBinding.h"
#include <wtf/Scope.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(XRCylinderLayer);

XRCylinderLayer::XRCylinderLayer(ScriptExecutionContext& scriptExecutionContext, WebXRSession& session, Ref<XRLayerBacking>&& backing, const XRCylinderLayerInit& init)
    : XRCompositionLayer(&scriptExecutionContext, session, WTF::move(backing), init)
    , m_space(init.space)
    , m_transform((init.transform) ? init.transform : WebXRRigidTransform::create())
{
    // Explicitly call setters to add validation.
    setRadius(init.radius);
    setCentralAngle(init.centralAngle);
    setAspectRatio(init.aspectRatio);

    setIsStatic(init.isStatic);
}

XRCylinderLayer::~XRCylinderLayer() = default;

const WebXRSpace& XRCylinderLayer::space() const
{
    ASSERT(m_space);
    return *m_space;
}

void XRCylinderLayer::setSpace(WebXRSpace& space)
{
    if (m_space == &space)
        return;

    m_space = space;
    setNeedsRedraw(true);
}

const WebXRRigidTransform& XRCylinderLayer::transform() const
{
    ASSERT(m_transform);
    return *m_transform;
}

void XRCylinderLayer::setTransform(WebXRRigidTransform& transform)
{
    if (m_transform == &transform)
        return;

    m_transform = transform;
    setNeedsRedraw(true);
}

void XRCylinderLayer::setRadius(float radius)
{
    m_radius = std::max(std::numeric_limits<float>::epsilon(), radius);
    setNeedsRedraw(true);
}

void XRCylinderLayer::setCentralAngle(float angle)
{
    // (0, 2 * pi) although specs recommend 1.9 * pi as a practical limit.
    static constexpr float MaxCentralAngle = 1.9 * static_cast<float>(M_PI);

    m_centralAngle = std::clamp(angle, std::numeric_limits<float>::epsilon(), MaxCentralAngle);
    setNeedsRedraw(true);
}

void XRCylinderLayer::setAspectRatio(float ratio)
{
    m_aspectRatio = std::max(std::numeric_limits<float>::epsilon(), ratio);
    setNeedsRedraw(true);
}

void XRCylinderLayer::startFrame(PlatformXR::FrameData& frameData)
{
#if PLATFORM(GTK) || PLATFORM(WPE)
    auto it = frameData.layers.find(m_backing->handle());
    if (it == frameData.layers.end())
        return;

    if (needsRedraw())
        m_backing->startFrame(frameData);
#else
    UNUSED_PARAM(frameData);
#endif
}

void XRCylinderLayer::recomputePose()
{
    auto scopeExit = makeScopeExit([&]() {
        RELEASE_LOG_ERROR(XR, "Failed to decompose space transform, using identity transform for layer pose");
        m_poseInLocalSpace = PlatformXR::FrameData::Pose { .position = { 0, 0, 0 }, .orientation = { 0, 0, 0, 1 } };
    });

    std::optional<TransformationMatrix> spaceTransform;
    if (m_space)
        spaceTransform = m_space->nativeOrigin();
    if (!spaceTransform)
        spaceTransform = TransformationMatrix();

    auto transformInLocalSpace = m_transform ? *spaceTransform * m_transform->rawTransform() : *spaceTransform;
    TransformationMatrix::Decomposed4Type decomposed;
    if (!transformInLocalSpace.decompose4(decomposed))
        return;

    scopeExit.release();
    m_poseInLocalSpace.position = { static_cast<float>(decomposed.translateX), static_cast<float>(decomposed.translateY), static_cast<float>(decomposed.translateZ) };
    m_poseInLocalSpace.orientation = { static_cast<float>(decomposed.quaternion.x), static_cast<float>(decomposed.quaternion.y), static_cast<float>(decomposed.quaternion.z), static_cast<float>(decomposed.quaternion.w) };
}

PlatformXR::DeviceLayer XRCylinderLayer::endFrame()
{
#if PLATFORM(GTK) || PLATFORM(WPE)
    PlatformXR::DeviceLayer layerData;
    if (needsRedraw())
        m_backing->endFrame(layerData);

    layerData.handle = m_backing->handle();
    layerData.visible = true;

    if (needsRedraw())
        recomputePose();

    fillInCommonDeviceLayerData(layerData);

    layerData.cylinderLayerData = {
        .radius = m_radius,
        .centralAngle = m_centralAngle,
        .aspectRatio = m_aspectRatio,
        .poseInLocalSpace = m_poseInLocalSpace,
    };
    return layerData;
#else
    return PlatformXR::DeviceLayer { };
#endif
}

} // namespace WebCore

#endif // ENABLE(WEBXR_LAYERS)
