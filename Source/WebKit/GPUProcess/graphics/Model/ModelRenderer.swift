// Copyright (C) 2024 Apple Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
// BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.

#if ENABLE_GPU_PROCESS_MODEL && canImport(RealityCoreTextureProcessing, _version: 24) && canImport(_USDKit_RealityKit, _version: 42) && canImport(RealityCoreRenderer, _version: 22) && canImport(ShaderGraph, _version: 156) && arch(arm64)

import QuartzCore
import USDKit
@_spi(UsdLoaderAPI) import _USDKit_RealityKit
@_spi(Private) import RealityKit
import simd

final class Renderer {
    private let device: any MTLDevice
    let commandQueue: any MTLCommandQueue
    var renderContext: (any LowLevelRenderContext)?
    var renderer: LowLevelRenderer?
    var pendingStandaloneResources: LowLevelRenderContextStandalone.Resources?
    var pendingRendererResources: LowLevelRenderer.Resources?
    var renderTargetDescriptor: LowLevelRenderTarget.Descriptor {
        // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
        // swift-format-ignore: NeverForceUnwrap
        renderer!.renderTargetDescriptor
    }

    private var cameraPosition: SIMD3<Float>
    private var cameraRotation: simd_quatf
    private static let cameraDistance: Float = 0.5
    private var effectiveCameraDistance: Float = Renderer.cameraDistance
    private var fovY: Float = 60 * .pi / 180
    private var clearColor: MTLClearColor = .init(red: 1, green: 1, blue: 1, alpha: 1)
    let memoryOwner: task_id_token_t

    init(device: any MTLDevice, memoryOwner: task_id_token_t) throws {
        guard let commandQueue = device.makeCommandQueue() else {
            fatalError("Failed to create command queue.")
        }
        commandQueue.label = "LowLevelRenderer Command Queue"

        self.device = device
        self.commandQueue = commandQueue
        self.cameraPosition = [0, 0, Renderer.cameraDistance]
        self.cameraRotation = .init(ix: 0, iy: 0, iz: 0, r: 1)
        self.memoryOwner = memoryOwner
    }

    func createMaterialCompiler(resources: LowLevelRenderContextStandalone.Resources) throws {
        var configuration = LowLevelRenderContextStandalone.Configuration(device: device)
        configuration.memoryOwner = self.memoryOwner
        configuration.residencySetBehavior = LowLevelRenderContextStandalone.Configuration.ResidencySetBehavior.disable
        let renderContext = try LowLevelRenderContextStandalone(configuration: configuration, resources: resources)
        self.renderContext = renderContext
    }

    func createRenderer(resources: LowLevelRenderer.Resources) throws {
        let renderer = try LowLevelRenderer(resources: resources)
        self.renderer = renderer
        renderer.meshInstancesArrayCount = 1
    }

    private func newCommandBuffer() -> any MTLCommandBuffer {
        guard let renderCommandBuffer = commandQueue.makeCommandBuffer() else {
            fatalError("Failed to make command buffer")
        }

        return renderCommandBuffer
    }

    func render(
        meshInstances: LowLevelMeshInstanceArray,
        texture: any MTLTexture,
        commandBuffer: any MTLCommandBuffer
    ) throws {
        guard let renderer else {
            commandBuffer.commit()
            return
        }

        let time = CACurrentMediaTime()

        let aspect = Float(texture.width) / Float(texture.height)
        let d = effectiveCameraDistance
        let projection = LowLevelRenderer.Camera.Projection.perspective(
            fovYRadians: fovY,
            aspectRatio: aspect,
            nearZ: d * 0.01,
            farZ: d * 100,
            reverseZ: true
        )

        renderer.cameras[0].position = cameraPosition
        renderer.cameras[0].rotation = cameraRotation
        renderer.cameras[0].projection = projection
        renderer.output.clearColor = clearColor
        renderer.output.color = .init(texture: texture)
        try renderer.setMeshInstances(meshInstances, at: 0)

        commandBuffer.label = "Render Camera"
        var indices = Array(meshInstances.indices)
        indices = LowLevelRenderer.cullMeshInstances(
            meshInstances,
            indices: indices.span,
            configuration: .init(frustum: .init(from: renderer.cameras[0]))
        )
        var span = indices.mutableSpan
        LowLevelRenderer.sortMeshInstances(
            meshInstances,
            indices: &span,
            configuration: .init(cameraPosition: renderer.cameras[0].position)
        )
        renderer.render(using: commandBuffer) { state in
            for index in indices {
                state.render(meshInstancesArrayIndex: 0, meshInstanceIndex: index)
            }
        }
        commandBuffer.commit()
    }

    func setFOV(_ fovYRadians: Float) {
        fovY = fovYRadians
    }

    func setBackgroundColor(_ color: simd_float3) {
        clearColor = MTLClearColor(red: Double(color.x), green: Double(color.y), blue: Double(color.z), alpha: 1.0)
    }

    func setCameraTransformForModelTransform(_ modelTransform: simd_float4x4) {
        // To keep the model stationary while achieving the same visual result as applying
        // modelTransform to the model, derive the equivalent camera pose by composing the
        // inverse model transform with the default camera matrix.
        var defaultCameraMatrix = matrix_identity_float4x4
        defaultCameraMatrix.columns.3 = [0, 0, Renderer.cameraDistance, 1]
        let cameraMatrix = simd_inverse(modelTransform) * defaultCameraMatrix

        let col0 = simd_float3(cameraMatrix.columns.0.x, cameraMatrix.columns.0.y, cameraMatrix.columns.0.z)
        let col1 = simd_float3(cameraMatrix.columns.1.x, cameraMatrix.columns.1.y, cameraMatrix.columns.1.z)
        let col2 = simd_float3(cameraMatrix.columns.2.x, cameraMatrix.columns.2.y, cameraMatrix.columns.2.z)
        let scale = simd_float3(simd_length(col0), simd_length(col1), simd_length(col2))
        let rotation = simd_quatf(simd_float3x3(col0 / scale.x, col1 / scale.y, col2 / scale.z))
        let position = simd_float3(cameraMatrix.columns.3.x, cameraMatrix.columns.3.y, cameraMatrix.columns.3.z)
        cameraPosition = position
        cameraRotation = rotation
        // Derive the near/far distance from the camera's world-space position.
        // The model lives at its USD-space coordinates, so the camera can be far from
        // origin for large or offset models; simd_length gives the actual view distance.
        effectiveCameraDistance = max(simd_length(position), Self.cameraDistance)
    }
}

#endif
