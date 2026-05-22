// Copyright (C) 2025 Apple Inc. All rights reserved.
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

import Metal
import OSLog
import WebKit
import simd

#if ENABLE_GPU_PROCESS_MODEL && canImport(RealityCoreTextureProcessing, _version: 24) && canImport(_USDKit_RealityKit, _version: 42) && canImport(RealityCoreRenderer, _version: 22) && canImport(ShaderGraph, _version: 156) && arch(arm64)
@_spi(UsdLoaderAPI) import _USDKit_RealityKit
@_spi(RealityCoreRendererAPI) import RealityKit
import USDKit
@_spi(SwiftAPI) import DirectResource
import RealityKit
import ShaderGraph
import RealityCoreDeformation

extension _USDKit_RealityKit._Proto_MeshDataUpdate_v1 {
    @_silgen_name("$s18_USDKit_RealityKit24_Proto_MeshDataUpdate_v1V18instanceTransformsSaySo13simd_float4x4aGvg")
    internal func instanceTransformsCompat() -> [simd_float4x4]
}

extension MTLCaptureDescriptor {
    fileprivate convenience init(from device: (any MTLDevice)?) {
        self.init()

        captureObject = device
        destination = .gpuTraceDocument
        let now = Date()
        let dateFormatter = DateFormatter()
        dateFormatter.timeZone = .current
        dateFormatter.dateFormat = "yyyy-MM-dd-HH-mm-ss-SSSS"
        let dateString = dateFormatter.string(from: now)

        outputURL = URL.temporaryDirectory.appending(path: "capture_\(dateString).gputrace").standardizedFileURL
    }
}

private func makeMTLTextureFromImageAsset(
    _ imageAsset: WKBridgeImageAsset,
    device: any MTLDevice,
    generateMips: Bool,
    memoryOwner: task_id_token_t,
    layout: [WKBridgeTextureLevelInfo]
) -> ((any MTLTexture), Int)? {
    guard let imageAssetData = imageAsset.data else {
        logError("no image data")
        return nil
    }
    logInfo(
        "imageAssetData = \(imageAssetData)  -  width = \(imageAsset.width)  -  height = \(imageAsset.height)  - imageAsset.pixelFormat:  \(imageAsset.pixelFormat)"
    )

    let pixelFormat = imageAsset.pixelFormat

    let (textureDescriptor, sliceCount) =
        switch imageAsset.textureType {
        case .typeCube:
            (
                MTLTextureDescriptor.textureCubeDescriptor(
                    pixelFormat: pixelFormat,
                    size: imageAsset.width,
                    mipmapped: generateMips
                ), 6
            )
        default:
            (
                MTLTextureDescriptor.texture2DDescriptor(
                    pixelFormat: pixelFormat,
                    width: imageAsset.width,
                    height: imageAsset.height,
                    mipmapped: generateMips
                ), 1
            )
        }

    textureDescriptor.usage = .shaderRead
    textureDescriptor.storageMode = .shared

    guard let mtlTexture = device.makeTexture(descriptor: textureDescriptor) else {
        logError("failed to device.makeTexture")
        return nil
    }
    mtlTexture.__setOwnerWithIdentity(memoryOwner)

    func mipDimension(_ base: Int, level: Int) -> Int {
        max(1, base >> level)
    }

    guard let bytesPerPixel = imageAsset.pixelFormat.bytesPerPixel else {
        fatalError("unexpected pixel format \(imageAsset.pixelFormat)")
    }

    var mipLevelsInData = 0
    unsafe imageAssetData.bytes.withUnsafeBytes { textureBytes in
        guard let baseAddress = textureBytes.baseAddress else {
            logError("nil base address in imageAssetData")
            return
        }

        func uploadSlice(face: Int, mipLevel: Int, bytesOffset: Int, bytesPerRow: Int, bytesPerImage: Int) {
            let mipWidth = mipDimension(imageAsset.width, level: mipLevel)
            let mipHeight = mipDimension(imageAsset.height, level: mipLevel)
            unsafe mtlTexture.replace(
                region: MTLRegionMake2D(0, 0, mipWidth, mipHeight),
                mipmapLevel: mipLevel,
                slice: face,
                withBytes: unsafe baseAddress.advanced(by: bytesOffset),
                bytesPerRow: bytesPerRow,
                bytesPerImage: bytesPerImage
            )
        }

        if !layout.isEmpty {
            // Use the pre-computed per-mip layout transmitted over IPC.
            // layout is indexed by mip level; each entry covers all slices at that level.
            mipLevelsInData = min(layout.count, mtlTexture.mipmapLevelCount)
            logInfo("uploading \(mipLevelsInData) of \(mtlTexture.mipmapLevelCount) mip level(s) from imageAssetData (layout-driven)")
            for face in 0..<sliceCount {
                for mipLevel in 0..<mipLevelsInData {
                    let info = layout[mipLevel]
                    // dataOffset covers all slices; advance by bytesPerImage per face.
                    uploadSlice(
                        face: face,
                        mipLevel: mipLevel,
                        bytesOffset: info.dataOffset + face * info.byteCountPerImage,
                        bytesPerRow: info.byteCountPerRow,
                        bytesPerImage: info.byteCountPerImage
                    )
                }
            }
        } else {
            // ---------------------------------------------------------------
            // No layout provided: compute how many mip levels are present in
            // the data by accumulating expected byte counts level by level.
            // ---------------------------------------------------------------
            var bytesAccounted = 0
            for mipLevel in 0..<mtlTexture.mipmapLevelCount {
                let mipWidth = mipDimension(imageAsset.width, level: mipLevel)
                let mipHeight = mipDimension(imageAsset.height, level: mipLevel)
                let mipBytes = mipWidth * bytesPerPixel * mipHeight * sliceCount
                guard bytesAccounted + mipBytes <= textureBytes.count else { break }
                bytesAccounted += mipBytes
                mipLevelsInData += 1
            }

            guard mipLevelsInData > 0 else {
                logError(
                    "imageAssetData too small: have \(textureBytes.count) bytes, "
                        + "need at least \(mipDimension(imageAsset.width, level: 0) * bytesPerPixel * mipDimension(imageAsset.height, level: 0) * sliceCount) "
                        + "for mip level 0"
                )
                return
            }

            if bytesAccounted != textureBytes.count {
                logError(
                    "imageAssetData has \(textureBytes.count - bytesAccounted) unexpected trailing bytes "
                        + "after \(mipLevelsInData) mip level(s) — ignoring"
                )
            }

            logInfo("uploading \(mipLevelsInData) of \(mtlTexture.mipmapLevelCount) mip level(s) from imageAssetData")

            // Data is face-major: Face 0 [Mip 0, Mip 1, …], Face 1 [Mip 0, Mip 1, …], …
            var offset = 0
            for face in 0..<sliceCount {
                for mipLevel in 0..<mipLevelsInData {
                    let mipWidth = mipDimension(imageAsset.width, level: mipLevel)
                    let mipHeight = mipDimension(imageAsset.height, level: mipLevel)
                    let bytesPerRow = mipWidth * bytesPerPixel
                    let bytesPerImage = bytesPerRow * mipHeight
                    uploadSlice(
                        face: face,
                        mipLevel: mipLevel,
                        bytesOffset: offset,
                        bytesPerRow: bytesPerRow,
                        bytesPerImage: bytesPerImage
                    )
                    offset += bytesPerImage
                }
            }
        }
    }

    guard mipLevelsInData > 0 else {
        return nil
    }
    return (mtlTexture, mipLevelsInData)
}

private func makeTextureFromImageAsset(
    _ imageAsset: WKBridgeImageAsset,
    device: any MTLDevice,
    renderContext: any LowLevelRenderContext,
    commandQueue: any MTLCommandQueue,
    generateMips: Bool,
    memoryOwner: task_id_token_t,
    swizzle: MTLTextureSwizzleChannels,
    existingTexture: LowLevelTextureResource?,
    layout: [WKBridgeTextureLevelInfo]
) -> LowLevelTextureResource? {
    guard
        let (mtlTexture, mipLevelsInData) = makeMTLTextureFromImageAsset(
            imageAsset,
            device: device,
            generateMips: generateMips,
            memoryOwner: memoryOwner,
            layout: layout
        )
    else {
        logError("could not create metal texture")
        return nil
    }

    let descriptor = LowLevelTextureResource.Descriptor.from(mtlTexture, swizzle: swizzle)
    if let textureResource = existingTexture ?? (try? renderContext.makeTextureResource(descriptor: descriptor)) {
        guard let commandBuffer = commandQueue.makeCommandBuffer() else {
            fatalError("Could not create command buffer")
        }
        guard let blitEncoder = commandBuffer.makeBlitCommandEncoder() else {
            fatalError("Could not create blit encoder")
        }
        if generateMips && mtlTexture.mipmapLevelCount > mipLevelsInData {
            blitEncoder.generateMipmaps(for: mtlTexture)
        }

        let outTexture = textureResource.replace(commandBuffer: commandBuffer)
        blitEncoder.copy(from: mtlTexture, to: outTexture)

        blitEncoder.endEncoding()
        commandBuffer.commit()
        commandBuffer.waitUntilCompleted()
        return textureResource
    }

    return nil
}

private func makeTextureFromImageAsset(
    _ imageAsset: WKBridgeImageAsset,
    device: any MTLDevice,
    renderContext: any LowLevelRenderContext,
    commandQueue: any MTLCommandQueue,
    generateMips: Bool,
    memoryOwner: task_id_token_t,
    swizzle: MTLTextureSwizzleChannels = .init(red: .red, green: .green, blue: .blue, alpha: .alpha)
) -> LowLevelTextureResource? {
    makeTextureFromImageAsset(
        imageAsset,
        device: device,
        renderContext: renderContext,
        commandQueue: commandQueue,
        generateMips: generateMips,
        memoryOwner: memoryOwner,
        swizzle: swizzle,
        existingTexture: nil,
        layout: []
    )
}

private func makeTextureFromImageAsset(
    _ imageAsset: WKBridgeImageAsset,
    device: any MTLDevice,
    renderContext: any LowLevelRenderContext,
    commandQueue: any MTLCommandQueue,
    generateMips: Bool,
    memoryOwner: task_id_token_t,
    existingTexture: LowLevelTextureResource?,
    layout: [WKBridgeTextureLevelInfo]
) -> LowLevelTextureResource? {
    makeTextureFromImageAsset(
        imageAsset,
        device: device,
        renderContext: renderContext,
        commandQueue: commandQueue,
        generateMips: generateMips,
        memoryOwner: memoryOwner,
        swizzle: .init(red: .red, green: .green, blue: .blue, alpha: .alpha),
        existingTexture: existingTexture,
        layout: layout
    )
}

private func makeParameters(
    for function: (any LowLevelMaterialResource.Function)?,
    renderContext: any LowLevelRenderContext,
    textureHashesAndResources: [WKBridgeTypedResourceId: (String, LowLevelTextureResource)],
    fallbackTexture: LowLevelTextureResource
) throws -> LowLevelArgumentTable? {
    guard let function else { return nil }
    guard let argumentTableDescriptor = function.argumentTableDescriptor else { return nil }
    let parameterMapping = function.parameterMapping

    var optTextures: [LowLevelTextureResource?] = argumentTableDescriptor.textures.map({ _ in nil })
    for parameter in parameterMapping?.textures ?? [] {
        if let textureHashAndResource = textureHashesAndResources.values.first(where: { $0.0 == parameter.name }) {
            optTextures[parameter.textureIndex] = textureHashAndResource.1
        } else {
            // use fallback texture if no texture if found
            logInfo("Cannot find texture \(parameter.name), use fallback texture instead")
            optTextures[parameter.textureIndex] = fallbackTexture
        }
    }
    let textures = optTextures.map({ $0! })

    let buffers: [LowLevelBufferSlice] = try argumentTableDescriptor.buffers.map { bufferRequirements in
        let capacity = (bufferRequirements.size + 16 - 1) / 16 * 16
        let buffer = try renderContext.makeBufferResource(descriptor: .init(capacity: capacity))
        buffer.replace { span in
            for byteOffset in span.byteOffsets {
                span.storeBytes(of: 0, toByteOffset: byteOffset, as: UInt8.self)
            }
        }
        return try LowLevelBufferSlice(buffer: buffer, offset: 0, size: bufferRequirements.size)
    }

    return try renderContext.makeArgumentTable(
        descriptor: argumentTableDescriptor,
        buffers: buffers,
        textures: textures
    )
}

extension Logger {
    fileprivate static let modelGPU = Logger(subsystem: "com.apple.WebKit", category: "model")
}

internal func logError(_ error: String) {
    Logger.modelGPU.error("\(error)")
}

internal func logInfo(_ info: String) {
    Logger.modelGPU.info("\(info)")
}

@objc
@implementation
extension WKBridgeUSDConfiguration {
    @nonobjc
    fileprivate let device: any MTLDevice
    @nonobjc
    fileprivate let appRenderer: Renderer
    @nonobjc
    fileprivate final var commandQueue: any MTLCommandQueue {
        get { appRenderer.commandQueue }
    }
    @nonobjc
    fileprivate final var renderer: LowLevelRenderer {
        get {
            // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
            // swift-format-ignore: NeverForceUnwrap
            appRenderer.renderer!
        }
    }
    @nonobjc
    fileprivate final var renderContext: any LowLevelRenderContext {
        get {
            // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
            // swift-format-ignore: NeverForceUnwrap
            appRenderer.renderContext!
        }
    }

    @nonobjc
    fileprivate final var renderTarget: LowLevelRenderTarget.Descriptor {
        get { appRenderer.renderTargetDescriptor }
    }

    init(device: any MTLDevice, memoryOwner: task_id_token_t) {
        self.device = device
        do {
            self.appRenderer = try Renderer(device: device, memoryOwner: memoryOwner)
        } catch {
            fatalError("Exception creating renderer \(error)")
        }
    }

    @objc(createMaterialCompiler:)
    func createMaterialCompiler() async {
        do {
            try await self.appRenderer.createMaterialCompiler(colorPixelFormat: .rgba16Float, rasterSampleCount: 4)
        } catch {
            fatalError("Exception creating renderer \(error)")
        }
    }
}

@objc
@implementation
extension WKBridgeReceiver {
    @nonobjc
    fileprivate let device: any MTLDevice
    @nonobjc
    fileprivate let skyboxGenerator: SkyboxGenerator
    @nonobjc
    fileprivate let imageBasedLightTextureGenerator: ImageBasedLightTextureGenerator
    @nonobjc
    fileprivate let commandQueue: any MTLCommandQueue

    @nonobjc
    fileprivate let renderContext: any LowLevelRenderContext
    @nonobjc
    fileprivate let renderer: LowLevelRenderer
    @nonobjc
    fileprivate let appRenderer: Renderer
    @nonobjc
    fileprivate let lightingFunction: LowLevelMaterialResource.LightingFunction
    @nonobjc
    fileprivate let lightingArguments: LowLevelArgumentTable
    @nonobjc
    fileprivate var lightingArgumentBuffer: LowLevelArgumentTable?

    @nonobjc
    fileprivate final var renderTarget: LowLevelRenderTarget.Descriptor {
        get { appRenderer.renderTargetDescriptor }
    }
    @nonobjc
    fileprivate var meshInstancePool: MeshInstancePool

    @nonobjc
    fileprivate var meshResources: [WKBridgeTypedResourceId: LowLevelMeshResource] = [:]
    @nonobjc
    fileprivate var meshResourceToMaterials: [WKBridgeTypedResourceId: [WKBridgeTypedResourceId]] = [:]
    @nonobjc
    fileprivate var meshToMeshInstances: [WKBridgeTypedResourceId: [LowLevelMeshInstance]] = [:]
    @nonobjc
    fileprivate var rotationAngle: Float = 0

    @nonobjc
    fileprivate let deformationSystem: _Proto_LowLevelDeformationSystem_v1

    @nonobjc
    fileprivate var meshResourceToDeformationContext: [WKBridgeTypedResourceId: DeformationContext] = [:]

    struct Material {
        let resource: LowLevelMaterialResource
        let geometryArguments: LowLevelArgumentTable?
        let surfaceArguments: LowLevelArgumentTable?
        let blending: LowLevelMaterialResource.ShaderGraphOutput.Blending
    }
    @nonobjc
    fileprivate var materialsAndParams: [WKBridgeTypedResourceId: Material] = [:]

    @nonobjc
    fileprivate var textureHashesAndResources: [WKBridgeTypedResourceId: (String, LowLevelTextureResource)] = [:]

    @nonobjc
    fileprivate var dontCaptureAgain: Bool = false

    @nonobjc
    fileprivate final var memoryOwner: task_id_token_t {
        appRenderer.memoryOwner
    }

    @nonobjc
    fileprivate let fallbackTexture: LowLevelTextureResource

    struct DeferredMeshUpdate {
        enum UpdateType {
            // First time mesh update, should add mesh instances to the scene
            case newMesh
            // Transform update on existing mesh, should only update the transform on relevant mesh instances
            case transformUpdate([simd_float4x4])
        }

        let identifier: WKBridgeTypedResourceId
        let type: UpdateType
        var updatedInstances: [LowLevelMeshInstance]

        init(identifier: WKBridgeTypedResourceId, type: UpdateType, updatedInstances: [LowLevelMeshInstance]) {
            self.identifier = identifier
            self.type = type
            self.updatedInstances = updatedInstances

            if case .transformUpdate(let newTransforms) = type {
                assert(newTransforms.count == updatedInstances.count)
            }
        }
    }

    init(
        configuration: WKBridgeUSDConfiguration,
        diffuseAsset: WKBridgeImageAsset,
        specularAsset: WKBridgeImageAsset
    ) throws {
        self.renderContext = configuration.renderContext
        self.renderer = configuration.renderer
        self.appRenderer = configuration.appRenderer
        self.device = configuration.device
        self.skyboxGenerator = SkyboxGenerator(device: configuration.device)
        self.imageBasedLightTextureGenerator = ImageBasedLightTextureGenerator(device: configuration.device)
        self.commandQueue = configuration.commandQueue
        self.deformationSystem = try _Proto_LowLevelDeformationSystem_v1.make(configuration.device, configuration.commandQueue).get()
        let meshInstances = try configuration.renderContext.makeMeshInstanceArray(renderTargets: [configuration.renderTarget], count: 16)
        self.meshInstancePool = MeshInstancePool(renderContext: configuration.renderContext, meshInstances: meshInstances)
        let lightingFunction = configuration.renderContext.lighting.makeImageBasedLightingFunction()
        guard
            let diffuseTexture = makeTextureFromImageAsset(
                diffuseAsset,
                device: device,
                renderContext: renderContext,
                commandQueue: configuration.commandQueue,
                generateMips: false,
                memoryOwner: configuration.appRenderer.memoryOwner,
                swizzle: .init(red: .red, green: .red, blue: .red, alpha: .one)
            )
        else {
            fatalError("Could not create diffuseTexture")
        }
        guard
            let specularTexture = makeTextureFromImageAsset(
                specularAsset,
                device: device,
                renderContext: renderContext,
                commandQueue: configuration.commandQueue,
                generateMips: true,
                memoryOwner: configuration.appRenderer.memoryOwner,
                swizzle: .init(red: .red, green: .red, blue: .red, alpha: .one)
            )
        else {
            fatalError("Could not create specularTexture")
        }
        self.lightingFunction = lightingFunction
        guard let lightingFunctionArgumentTableDescriptor = lightingFunction.argumentTableDescriptor else {
            fatalError("Could not create lighting function")
        }
        self.lightingArguments = try configuration.renderContext.makeArgumentTable(
            descriptor: lightingFunctionArgumentTableDescriptor,
            buffers: [],
            textures: [
                diffuseTexture, specularTexture,
            ]
        )

        self.fallbackTexture = makeFallBackTextureResource(
            renderContext,
            commandQueue: configuration.commandQueue,
            device: configuration.device,
            memoryOwner: configuration.appRenderer.memoryOwner
        )
    }

    func commandBuffer() -> (any MTLCommandBuffer)? {
        // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
        // swift-format-ignore: NeverForceUnwrap
        commandQueue.makeCommandBuffer()!
    }

    @objc(renderWithTexture:commandBuffer:)
    func render(with texture: any MTLTexture, commandBuffer: any MTLCommandBuffer) {
        // animate
        if !meshResourceToDeformationContext.isEmpty {
            let commandBuffer = self.commandQueue.makeCommandBuffer()!

            for (identifier, deformationContext) in meshResourceToDeformationContext where deformationContext.dirty {
                let result = deformationContext.deformation.execute(
                    deformation: deformationContext.description,
                    commandBuffer: commandBuffer
                ) { (commandBuffer: any MTLCommandBuffer) in }
                meshResourceToDeformationContext[identifier]!.dirty = false

                if case .failure(let error) = result {
                    print(error)
                    fatalError("Failed to execute deformation work")
                }
            }

            commandBuffer.enqueue()
            commandBuffer.commit()
        }

        // render
        if dontCaptureAgain == false {
            let captureDescriptor = MTLCaptureDescriptor(from: device)
            let captureManager = MTLCaptureManager.shared()
            do {
                try captureManager.startCapture(with: captureDescriptor)
                print("Capture started at \(captureDescriptor.outputURL?.absoluteString ?? "")")
            } catch {
                logError("failed to start gpu capture \(error)")
                dontCaptureAgain = true
            }
        }

        do {
            try appRenderer.render(meshInstances: meshInstancePool.meshInstances, texture: texture, commandBuffer: commandBuffer)
        } catch {
            logError("failed to render \(error)")
        }

        let captureManager = MTLCaptureManager.shared()
        if captureManager.isCapturing {
            captureManager.stopCapture()
        }
    }

    @objc(updateTexture:)
    func updateTexture(_ datas: [WKBridgeUpdateTexture]) {
        for textureData in datas {
            let asset = textureData.imageAsset
            let existingTexture = textureHashesAndResources[textureData.identifier]?.1
            let needsNewTexture: Bool
            if let existingTexture {
                needsNewTexture = existingTexture.descriptor != LowLevelTextureResource.Descriptor(from: asset)
            } else {
                needsNewTexture = true
            }

            let commandQueue = appRenderer.commandQueue
            if let textureResource = makeTextureFromImageAsset(
                asset,
                device: device,
                renderContext: renderContext,
                commandQueue: commandQueue,
                generateMips: true,
                memoryOwner: self.memoryOwner,
                existingTexture: needsNewTexture ? nil : existingTexture,
                layout: textureData.layout
            ) {
                textureHashesAndResources[textureData.identifier] = (textureData.hashString, textureResource)
            }
        }
    }

    @objc(updateMaterial:completionHandler:)
    func updateMaterial(_ updates: [WKBridgeUpdateMaterial]) async {
        do {
            for data in updates {
                logInfo("updateMaterial (pre-dispatch) \(data.identifier)")

                let identifier = data.identifier
                logInfo("updateMaterial \(identifier)")

                guard let shaderGraph = ShaderGraph.fromWKDescriptor(data.materialGraph) else {
                    fatalError("No materialGraph data provided for material \(identifier)")
                }

                let shaderGraphOutput = try await renderContext.shaderGraph.makeShaderGraphFunctions(
                    shaderGraph: shaderGraph,
                    constantValues: .init()
                )

                let geometryArguments = try makeParameters(
                    for: shaderGraphOutput.geometryModifier,
                    renderContext: renderContext,
                    textureHashesAndResources: textureHashesAndResources,
                    fallbackTexture: self.fallbackTexture
                )
                let surfaceArguments = try makeParameters(
                    for: shaderGraphOutput.surfaceShader,
                    renderContext: renderContext,
                    textureHashesAndResources: textureHashesAndResources,
                    fallbackTexture: self.fallbackTexture
                )

                let geometryModifier = shaderGraphOutput.geometryModifier ?? renderContext.makeDefaultGeometryModifier()
                let surfaceShader = shaderGraphOutput.surfaceShader
                let materialResource = try await renderContext.makeMaterialResource(
                    descriptor: .init(
                        geometry: geometryModifier,
                        surface: surfaceShader,
                        lighting: lightingFunction
                    )
                )

                materialsAndParams[identifier] = .init(
                    resource: materialResource,
                    geometryArguments: geometryArguments,
                    surfaceArguments: surfaceArguments,
                    blending: shaderGraphOutput.blending
                )
            }
        } catch {
            logError("updateMaterial failed \(error)")
        }
    }

    @objc
    func processRemovals(
        _ meshRemovals: [WKBridgeTypedResourceId],
        materialRemovals: [WKBridgeTypedResourceId],
        textureRemovals: [WKBridgeTypedResourceId]
    ) -> Bool {
        do {
            for meshId in meshRemovals {
                logInfo("mesh destroyed: \(meshId)")
                if let meshInstancesToRemove = meshToMeshInstances.removeValue(forKey: meshId) {
                    for meshInstanceToRemove in meshInstancesToRemove {
                        try meshInstancePool.remove(meshInstanceToRemove)
                    }
                }
                meshResources.removeValue(forKey: meshId)
                meshResourceToMaterials.removeValue(forKey: meshId)
                meshResourceToDeformationContext.removeValue(forKey: meshId)
            }

            for materialId in materialRemovals {
                logInfo("material destroyed: \(materialId)")
                materialsAndParams.removeValue(forKey: materialId)
            }

            for textureId in textureRemovals {
                logInfo("texture destroyed: \(textureId)")
                textureHashesAndResources.removeValue(forKey: textureId)
            }
        } catch {
            logError("\(error)")
            return false
        }
        return true
    }

    @objc(updateMesh:completionHandler:)
    func updateMesh(_ updates: [WKBridgeUpdateMesh]) async {
        do {
            var deferredMeshUpdates: [DeferredMeshUpdate] = []

            for meshData in updates {
                let identifier = meshData.identifier

                let meshResource: LowLevelMeshResource
                if meshData.updateType == .initial || meshData.descriptor != nil {
                    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
                    // swift-format-ignore: NeverForceUnwrap
                    let meshDescriptor = meshData.descriptor!
                    let descriptor = LowLevelMeshResource.Descriptor.fromLlmDescriptor(meshDescriptor)
                    if let cachedMeshResource = meshResources[identifier] {
                        meshResource = cachedMeshResource
                    } else {
                        meshResource = try renderContext.makeMeshResource(descriptor: descriptor)
                    }
                    meshResource.replaceData(indexData: meshData.indexData, vertexData: meshData.vertexData)
                    meshResources[identifier] = meshResource
                    meshResourceToDeformationContext.removeValue(forKey: identifier)
                } else {
                    guard let cachedMeshResource = meshResources[identifier] else {
                        fatalError("Mesh resource should already be created from previous update")
                    }

                    if meshData.indexData != nil || !meshData.vertexData.isEmpty {
                        cachedMeshResource.replaceData(indexData: meshData.indexData, vertexData: meshData.vertexData)
                    }
                    meshResource = cachedMeshResource
                }

                if let deformationData = meshData.deformationData {
                    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
                    // swift-format-ignore: NeverForceUnwrap
                    let commandBuffer = commandQueue.makeCommandBuffer()!
                    // TODO: delta update
                    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
                    // swift-format-ignore: NeverForceUnwrap
                    configureDeformation(
                        identifier: identifier,
                        deformationData: deformationData,
                        commandBuffer: commandBuffer,
                        device: device,
                        meshResource: meshResources[identifier]!,
                        meshResourceToDeformationContext: &meshResourceToDeformationContext,
                        deformationSystem: deformationSystem,
                        memoryOwner: self.memoryOwner
                    )
                    commandBuffer.enqueue()
                    commandBuffer.commit()
                }

                if meshData.instanceTransformsCount > 0 {
                    if meshToMeshInstances[identifier] == nil {
                        meshToMeshInstances[identifier] = []

                        var deferredMeshUpdate = DeferredMeshUpdate(identifier: identifier, type: .newMesh, updatedInstances: [])

                        for (partIndex, part) in meshData.parts.enumerated() {
                            if part.materialIndex >= meshData.assignedMaterials.count {
                                fatalError(
                                    "index out of range: material index \(part.materialIndex) while only \(meshData.assignedMaterials.count) were found"
                                )
                            }
                            let materialIdentifier = meshData.assignedMaterials[part.materialIndex]
                            guard let material = materialsAndParams[materialIdentifier] else {
                                fatalError("Failed to get material instance \(materialIdentifier)")
                            }

                            let pipeline = try await renderContext.makeRenderPipelineState(
                                descriptor: .init(
                                    mesh: meshResource.descriptor,
                                    material: material.resource,
                                    renderTargets: [renderTarget],
                                    blending: material.blending == .transparent ? .sourceOver : nil
                                )
                            )

                            let meshPart = try renderContext.makeMeshPart(
                                resource: meshResource,
                                indexOffset: meshData.parts[partIndex].indexOffset,
                                indexCount: meshData.parts[partIndex].indexCount,
                                primitive: meshData.parts[partIndex].topology,
                                windingOrder: .counterClockwise,
                                bounds: .init(
                                    boxMin: meshData.parts[partIndex].boundsMin,
                                    boxMax: meshData.parts[partIndex].boundsMax
                                )
                            )

                            for instanceTransform in meshData.instanceTransforms {
                                let meshInstance = try renderContext.makeMeshInstance(
                                    meshPart: meshPart,
                                    pipeline: pipeline,
                                    geometryArguments: material.geometryArguments,
                                    surfaceArguments: material.surfaceArguments,
                                    lightingArguments: lightingArguments,
                                    transform: instanceTransform,
                                    sortCategory: material.blending == .transparent ? .transparent : .opaque
                                )

                                // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
                                // swift-format-ignore: NeverForceUnwrap
                                meshToMeshInstances[identifier]!.append(meshInstance)
                                deferredMeshUpdate.updatedInstances.append(meshInstance)
                            }
                        }

                        deferredMeshUpdates.append(deferredMeshUpdate)
                    } else {
                        // Update transforms otherwise
                        // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
                        // swift-format-ignore: NeverForceUnwrap
                        var newTransforms: [simd_float4x4] = []
                        var updatedInstances: [LowLevelMeshInstance] = []

                        let partCount = meshToMeshInstances[identifier]!.count / meshData.instanceTransforms.count
                        for (instanceIndex, instanceTransform) in meshData.instanceTransforms.enumerated() {
                            for partIndex in 0..<partCount {
                                // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
                                // swift-format-ignore: NeverForceUnwrap
                                let meshInstance = meshToMeshInstances[identifier]![instanceIndex * meshData.parts.count + partIndex]
                                updatedInstances.append(meshInstance)
                                newTransforms.append(instanceTransform)
                            }
                        }

                        let deferredMeshUpdate = DeferredMeshUpdate(
                            identifier: identifier,
                            type: .transformUpdate(newTransforms),
                            updatedInstances: updatedInstances
                        )
                        deferredMeshUpdates.append(deferredMeshUpdate)
                    }
                }

                if !meshData.assignedMaterials.isEmpty {
                    meshResourceToMaterials[identifier] = meshData.assignedMaterials
                }
            }

            // Process all deferred mesh updates at once to avoid mesh popping
            for deferredUpdate in deferredMeshUpdates {
                switch deferredUpdate.type {
                case .newMesh:
                    for newMeshInstance in deferredUpdate.updatedInstances {
                        try meshInstancePool.add(newMeshInstance)
                    }
                case .transformUpdate(let newTransforms):
                    for (instanceIndex, meshInstance) in deferredUpdate.updatedInstances.enumerated() {
                        meshInstance.transform = newTransforms[instanceIndex]
                    }
                }
            }
        } catch {
            logError("updateMesh \(error)")
        }
    }

    @objc(setTransform:)
    func setTransform(_ transform: simd_float4x4) {
        appRenderer.setCameraTransformForModelTransform(transform)
    }

    func setFOV(_ fovY: Float) {
        appRenderer.setFOV(fovY)
    }

    func setBackgroundColor(_ color: simd_float3) {
        appRenderer.setBackgroundColor(color)
    }

    func setPlaying(_ play: Bool) {
    }

    func setEnvironmentMap(_ textureData: WKBridgeUpdateTexture) {
        do {
            let imageAsset = textureData.imageAsset
            guard
                let (mtlTextureEquirectangular, _) = makeMTLTextureFromImageAsset(
                    imageAsset,
                    device: device,
                    generateMips: true,
                    memoryOwner: self.memoryOwner,
                    layout: textureData.layout
                )
            else {
                fatalError("Could not make metal texture from environment asset data")
            }

            let cubeMTLTextureDescriptor = try self.skyboxGenerator.makeDescriptor(
                fromEquirectangular: mtlTextureEquirectangular
            )
            // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
            // swift-format-ignore: NeverForceUnwrap
            let cubeMTLTexture = self.device.makeTexture(descriptor: cubeMTLTextureDescriptor)!
            cubeMTLTexture.__setOwnerWithIdentity(self.memoryOwner)

            let diffuseMTLTextureDescriptor = try self.imageBasedLightTextureGenerator.makeDiffuseDescriptor(
                fromCube: cubeMTLTexture
            )
            let diffuseTextureDescriptor = LowLevelTextureResource.Descriptor.from(diffuseMTLTextureDescriptor)
            let diffuseTexture = try self.renderContext.makeTextureResource(descriptor: diffuseTextureDescriptor)

            let specularMTLTextureDescriptor = try self.imageBasedLightTextureGenerator.makeSpecularDescriptor(
                fromCube: cubeMTLTexture
            )
            let specularTextureDescriptor = LowLevelTextureResource.Descriptor.from(specularMTLTextureDescriptor)
            let specularTexture = try self.renderContext.makeTextureResource(descriptor: specularTextureDescriptor)

            // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
            // swift-format-ignore: NeverForceUnwrap
            let commandBuffer = self.commandQueue.makeCommandBuffer()!

            try self.skyboxGenerator.generateSkybox(
                using: commandBuffer,
                fromEquirectangular: mtlTextureEquirectangular,
                into: cubeMTLTexture
            )

            let diffuseMTLTexture = diffuseTexture.replace(commandBuffer: commandBuffer)
            let specularMTLTexture = specularTexture.replace(commandBuffer: commandBuffer)

            try self.imageBasedLightTextureGenerator.generateDiffuse(
                using: commandBuffer,
                fromSkyboxCube: cubeMTLTexture,
                into: diffuseMTLTexture
            )
            try self.imageBasedLightTextureGenerator.generateSpecular(
                using: commandBuffer,
                fromSkyboxCube: cubeMTLTexture,
                into: specularMTLTexture
            )

            try self.lightingArguments.setTexture(diffuseTexture, at: 0)
            try self.lightingArguments.setTexture(specularTexture, at: 1)

            commandBuffer.commit()
        } catch {
            fatalError(error.localizedDescription)
        }
    }
}

private func webPartsFromParts(_ parts: [LowLevelMesh.Part]) -> [WKBridgeMeshPart] {
    parts.map({ a in
        WKBridgeMeshPart(
            indexOffset: a.indexOffset,
            indexCount: a.indexCount,
            topology: a.topology,
            materialIndex: a.materialIndex,
            boundsMin: a.bounds.min,
            boundsMax: a.bounds.max
        )
    })
}

private func convert(_ m: _Proto_DataUpdateType_v1) -> WKBridgeDataUpdateType {
    if m == .initial {
        return .initial
    }
    return .delta
}

private func convert<T>(_ ids: [_Proto_TypedResourceId<T>]) -> [WKBridgeTypedResourceId] {
    ids.map { id in
        WKBridgeTypedResourceId(
            value: id.value,
            path: id.path,
            hashValue: id.hashValue
        )
    }
}

private func webUpdateTextureRequestFromUpdateTextureRequest(_ request: _Proto_TextureDataUpdate_v1) -> WKBridgeUpdateTexture {
    // FIXME: remove placeholder code
    // FIXME: https://bugs.webkit.org/show_bug.cgi?id=305857
    // swift-format-ignore: NeverForceUnwrap
    let descriptor = request.descriptor!
    let data = request.data
    return WKBridgeUpdateTexture(
        imageAsset: .init(descriptor, data: data),
        identifier: .init(value: request.id.value, path: request.id.path, hashValue: request.id.hashValue),
        hashString: request.hashString,
        layout: request.layout.map {
            WKBridgeTextureLevelInfo(
                dataOffset: $0.dataOffset,
                byteCountPerRow: $0.byteCountPerRow,
                byteCountPerImage: $0.byteCountPerImage
            )
        }
    )
}

private func webUpdateMeshRequestFromUpdateMeshRequest(
    _ request: _Proto_MeshDataUpdate_v1
) -> WKBridgeUpdateMesh {
    var descriptor: WKBridgeMeshDescriptor?
    if let requestDescriptor = request.descriptor {
        descriptor = .init(request: requestDescriptor)
    }

    return WKBridgeUpdateMesh(
        identifier: .init(value: request.id.value, path: request.id.path, hashValue: request.id.hashValue),
        updateType: convert(request.updateType),
        descriptor: descriptor,
        parts: webPartsFromParts(request.parts),
        indexData: request.indexData,
        vertexData: request.vertexData,
        instanceTransforms: toData(request.instanceTransformsCompat()),
        instanceTransformsCount: request.instanceTransformsCompat().count,
        assignedMaterials: convert(request.assignedMaterials),
        deformationData: .init(request.deformationData)
    )
}

private func toWKBridgeNodeType(_ node: _Proto_ShaderNodeGraph.Node) -> WKBridgeNodeType {
    // Determine node type based on the node's name or data type
    let nodeName = node.name.lowercased()
    if nodeName == "arguments" {
        return .arguments
    } else if nodeName == "results" || nodeName == "result" {
        return .results
    }

    // Check the node data type
    switch node.data {
    case .constant:
        return .constant
    case .definition, .graph:
        return .builtin
    default: fatalError("toWKBridgeNodeType - unknown _Proto_ShaderNodeGraph.Node.data type")
    }
}

private func toWKBridgeBuiltin(_ node: _Proto_ShaderNodeGraph.Node) -> WKBridgeBuiltin {
    // Extract builtin information from the node
    switch node.data {
    case .definition(let definition):
        return WKBridgeBuiltin(definition: definition.name, name: node.name)
    case .graph:
        return WKBridgeBuiltin(definition: "graph", name: node.name)
    case .constant:
        return WKBridgeBuiltin(definition: "", name: node.name)
    default: fatalError("toWKBridgeBuiltin - unknown _Proto_ShaderNodeGraph.Node.data type")
    }
}

private func constantValues(_ constant: _Proto_ShaderGraphValue) -> ([WKBridgeValueString], WKBridgeConstant) {
    switch constant {
    case .bool(let bool):
        return ([WKBridgeValueString(number: NSNumber(booleanLiteral: bool))], .bool)
    case .uchar(let uchar):
        return ([WKBridgeValueString(number: NSNumber(value: uchar))], .uchar)
    case .int(let int):
        return ([WKBridgeValueString(number: NSNumber(value: int))], .int)
    case .uint(let uint):
        return ([WKBridgeValueString(number: NSNumber(value: uint))], .uint)
    case .half(let uint16):
        return ([WKBridgeValueString(number: NSNumber(value: uint16))], .half)
    case .float(let float):
        return ([WKBridgeValueString(number: NSNumber(value: float))], .float)
    case .string(let string):
        return ([WKBridgeValueString(string: string)], .string)
    case .float2(let vector2f):
        return (
            [WKBridgeValueString(number: NSNumber(value: vector2f.x)), WKBridgeValueString(number: NSNumber(value: vector2f.y))], .float2
        )
    case .float3(let float3):
        return (
            [
                WKBridgeValueString(number: NSNumber(value: float3.x)),
                WKBridgeValueString(number: NSNumber(value: float3.y)),
                WKBridgeValueString(number: NSNumber(value: float3.z)),
            ], .float3
        )
    case .float4(let vector4f):
        return (
            [
                WKBridgeValueString(number: NSNumber(value: vector4f.x)),
                WKBridgeValueString(number: NSNumber(value: vector4f.y)),
                WKBridgeValueString(number: NSNumber(value: vector4f.z)),
                WKBridgeValueString(number: NSNumber(value: vector4f.w)),
            ], .float4
        )
    case .half2(let vector2h):
        return (
            [
                WKBridgeValueString(number: NSNumber(value: vector2h.x)),
                WKBridgeValueString(number: NSNumber(value: vector2h.y)),
            ], .half2
        )
    case .half3(let half3):
        return (
            [
                WKBridgeValueString(number: NSNumber(value: half3.x)),
                WKBridgeValueString(number: NSNumber(value: half3.y)),
                WKBridgeValueString(number: NSNumber(value: half3.z)),
            ], .half3
        )
    case .half4(let vector4h):
        return (
            [
                WKBridgeValueString(number: NSNumber(value: vector4h.x)),
                WKBridgeValueString(number: NSNumber(value: vector4h.y)),
                WKBridgeValueString(number: NSNumber(value: vector4h.z)),
                WKBridgeValueString(number: NSNumber(value: vector4h.w)),
            ], .half4
        )
    case .int2(let vector2i):
        return (
            [WKBridgeValueString(number: NSNumber(value: vector2i.x)), WKBridgeValueString(number: NSNumber(value: vector2i.y))], .int2
        )
    case .int3(let vector3i):
        return (
            [
                WKBridgeValueString(number: NSNumber(value: vector3i.x)),
                WKBridgeValueString(number: NSNumber(value: vector3i.y)),
                WKBridgeValueString(number: NSNumber(value: vector3i.z)),
            ], .int3
        )
    case .int4(let vector4i):
        return (
            [
                WKBridgeValueString(number: NSNumber(value: vector4i.x)),
                WKBridgeValueString(number: NSNumber(value: vector4i.y)),
                WKBridgeValueString(number: NSNumber(value: vector4i.z)),
                WKBridgeValueString(number: NSNumber(value: vector4i.w)),
            ], .int4
        )
    case .cgColor3(let color3):
        guard let components = color3.components, components.count >= 3 else {
            fatalError("constantValues: CGColor3 has fewer than 3 components")
        }
        return (
            [
                WKBridgeValueString(number: NSNumber(value: Float(components[0]))),
                WKBridgeValueString(number: NSNumber(value: Float(components[1]))),
                WKBridgeValueString(number: NSNumber(value: Float(components[2]))),
            ], .cgColor3
        )
    case .cgColor4(let color4):
        guard let components = color4.components, components.count >= 4 else {
            fatalError("constantValues: CGColor4 has fewer than 4 components")
        }
        return (
            [
                WKBridgeValueString(number: NSNumber(value: Float(components[0]))),
                WKBridgeValueString(number: NSNumber(value: Float(components[1]))),
                WKBridgeValueString(number: NSNumber(value: Float(components[2]))),
                WKBridgeValueString(number: NSNumber(value: Float(components[3]))),
            ], .cgColor4
        )
    case .float3x3(let col0, let col1, let col2):
        // Extract 9 float values from the 3x3 matrix (3 columns of 3 rows each)
        return (
            [
                WKBridgeValueString(number: NSNumber(value: col0.x)),
                WKBridgeValueString(number: NSNumber(value: col0.y)),
                WKBridgeValueString(number: NSNumber(value: col0.z)),
                WKBridgeValueString(number: NSNumber(value: col1.x)),
                WKBridgeValueString(number: NSNumber(value: col1.y)),
                WKBridgeValueString(number: NSNumber(value: col1.z)),
                WKBridgeValueString(number: NSNumber(value: col2.x)),
                WKBridgeValueString(number: NSNumber(value: col2.y)),
                WKBridgeValueString(number: NSNumber(value: col2.z)),
            ], .matrix3f
        )
    case .float4x4(let col0, let col1, let col2, let col3):
        // Extract 16 float values from the 4x4 matrix (4 columns of 4 rows each)
        return (
            [
                WKBridgeValueString(number: NSNumber(value: col0.x)),
                WKBridgeValueString(number: NSNumber(value: col0.y)),
                WKBridgeValueString(number: NSNumber(value: col0.z)),
                WKBridgeValueString(number: NSNumber(value: col0.w)),
                WKBridgeValueString(number: NSNumber(value: col1.x)),
                WKBridgeValueString(number: NSNumber(value: col1.y)),
                WKBridgeValueString(number: NSNumber(value: col1.z)),
                WKBridgeValueString(number: NSNumber(value: col1.w)),
                WKBridgeValueString(number: NSNumber(value: col2.x)),
                WKBridgeValueString(number: NSNumber(value: col2.y)),
                WKBridgeValueString(number: NSNumber(value: col2.z)),
                WKBridgeValueString(number: NSNumber(value: col2.w)),
                WKBridgeValueString(number: NSNumber(value: col3.x)),
                WKBridgeValueString(number: NSNumber(value: col3.y)),
                WKBridgeValueString(number: NSNumber(value: col3.z)),
                WKBridgeValueString(number: NSNumber(value: col3.w)),
            ], .matrix4f
        )
    default:
        fatalError("constantValues: unhandled ShaderGraphValue type \(constant)")
    }
}

private func toWKBridgeConstantContainer(
    _ node: _Proto_ShaderNodeGraph.Node
) -> WKBridgeConstantContainer {
    // Extract constant value if this is a constant node
    switch node.data {
    case .constant(let value):
        let (values, defaultType) = constantValues(value)
        return WKBridgeConstantContainer(constant: defaultType, constantValues: values, name: node.name)
    case .definition, .graph:
        return WKBridgeConstantContainer(constant: .asset, constantValues: [], name: node.name)
    default: fatalError("toWKBridgeConstantContainer - unknown _Proto_ShaderNodeGraph.Node.data type")
    }
}

private func toWKBridgeDataType(_ dataType: _Proto_ShaderDataType) -> WKBridgeDataType {
    switch dataType {
    case .invalid: .asset // Map invalid to asset as fallback
    case .bool: .bool
    case .uchar: .uchar
    case .int: .int
    case .uint: .uint
    case .half: .half
    case .float: .float
    case .string: .string
    case .float2: .float2
    case .float3: .float3
    case .float4: .float4
    case .half2: .half2
    case .half3: .half3
    case .half4: .half4
    case .int2: .int2
    case .int3: .int3
    case .int4: .int4
    case .float2x2: .matrix2f
    case .float3x3: .matrix3f
    case .float4x4: .matrix4f
    case .half2x2: .matrix2h
    case .half3x3: .matrix3h
    case .half4x4: .matrix4h
    case .quaternion: .quat
    case .surfaceShader: .surfaceShader
    case .geometryModifier: .geometryModifier
    case .postLightingShader: .postLightingShader
    case .cgColor3: .cgColor3
    case .cgColor4: .cgColor4
    case .filename: .asset
    @unknown default: .asset
    }
}

private func createInputOutput(
    name: String,
    type: _Proto_ShaderDataType,
    semanticType: _Proto_ShaderGraphNodeDefinition.SemanticType?,
    defaultValue: _Proto_ShaderGraphValue?,
) -> WKBridgeInputOutput {
    let defaultValueContainer: WKBridgeConstantContainer? = defaultValue.map {
        let (values, constantType) = constantValues($0)
        return WKBridgeConstantContainer(constant: constantType, constantValues: values, name: "")
    }

    let actualType = toWKBridgeDataType(type)
    return WKBridgeInputOutput(
        type: actualType,
        name: name,
        semanticTypeName: semanticType?.name,
        defaultValue: defaultValueContainer
    )
}

private func toWebInputOutputs(_ inputs: [_Proto_ShaderGraphNodeDefinition.Input]) -> [WKBridgeInputOutput] {
    inputs.map { e in
        createInputOutput(
            name: e.name,
            type: e.type,
            semanticType: e.semanticType,
            defaultValue: e.defaultValue
        )
    }
}

private func toWebOutputs(_ outputs: [_Proto_ShaderGraphNodeDefinition.Output]) -> [WKBridgeInputOutput] {
    outputs.map { e in
        createInputOutput(
            name: e.name,
            type: e.type,
            semanticType: e.semanticType,
            defaultValue: e.defaultValue
        )
    }
}

private func toWebNode(_ e: _Proto_ShaderNodeGraph.Node) -> WKBridgeNode {
    WKBridgeNode(
        bridgeNodeType: toWKBridgeNodeType(e),
        builtin: toWKBridgeBuiltin(e),
        constant: toWKBridgeConstantContainer(e)
    )
}

private func toWebEdges(_ edges: [_Proto_ShaderNodeGraph.Edge]) -> [WKBridgeEdge] {
    edges.map { edge in
        WKBridgeEdge(
            outputNode: edge.outputNode,
            outputPort: edge.outputPort,
            inputNode: edge.inputNode,
            inputPort: edge.inputPort
        )
    }
}

private func texcoordName(for coord: _Proto_TextureCoordinate) -> String {
    switch coord {
    case .uv0: "UV0"
    case .uv1: "UV1"
    case .uv2: "UV2"
    case .uv3: "UV3"
    case .uv4: "UV4"
    case .uv5: "UV5"
    case .uv6: "UV6"
    case .uv7: "UV7"
    @unknown default: "UV0"
    }
}

private func textureCoordinate(from name: String) -> ShaderGraph.TextureCoordinate? {
    switch name {
    case "UV0": .uv0
    case "UV1": .uv1
    case "UV2": .uv2
    case "UV3": .uv3
    case "UV4": .uv4
    case "UV5": .uv5
    case "UV6": .uv6
    case "UV7": .uv7
    default: nil
    }
}

private func toWebMaterialGraph(_ material: _Proto_ShaderNodeGraph?) -> WKBridgeMaterialGraph {
    guard let material else {
        // Return empty material graph if nil
        return WKBridgeMaterialGraph(
            graphName: "",
            nodes: [],
            edges: [],
            arguments: WKBridgeNode(
                bridgeNodeType: .arguments,
                builtin: WKBridgeBuiltin(definition: "", name: "arguments"),
                constant: WKBridgeConstantContainer(constant: .asset, constantValues: [], name: "")
            ),
            results: WKBridgeNode(
                bridgeNodeType: .results,
                builtin: WKBridgeBuiltin(definition: "", name: "results"),
                constant: WKBridgeConstantContainer(constant: .asset, constantValues: [], name: "")
            ),
            inputs: [],
            outputs: [],
            primvarMappingPrimvarNames: [],
            primvarMappingTexcoordNames: [],
            functionConstantInputNames: []
        )
    }

    // Convert nodes dictionary to array
    let nodes = material.nodes.values.map { toWebNode($0) }

    // Convert edges
    let edges = toWebEdges(material.edges)

    // Get arguments and results nodes directly from the graph
    let argumentsNode = toWebNode(material.arguments)
    let resultsNode = toWebNode(material.results)

    // Convert inputs - filter out invalid material input types
    let inputs = toWebInputOutputs(material.inputs)

    // Convert outputs
    let outputs = toWebOutputs(material.outputs)

    // Serialize primvarMappings as parallel arrays (sorted for determinism).
    // _Proto_TextureCoordinate.name is internal so we map to the canonical UV name string.
    let sortedPrimvarKeys = material.primvarMappings.keys.sorted()
    let primvarPrimvarNames = sortedPrimvarKeys
    let primvarTexcoordNames = sortedPrimvarKeys.map { texcoordName(for: material.primvarMappings[$0]!) }

    return WKBridgeMaterialGraph(
        graphName: "MaterialGraph",
        nodes: nodes,
        edges: edges,
        arguments: argumentsNode,
        results: resultsNode,
        inputs: inputs,
        outputs: outputs,
        primvarMappingPrimvarNames: primvarPrimvarNames,
        primvarMappingTexcoordNames: primvarTexcoordNames,
        functionConstantInputNames: material.functionConstantInputs
    )
}

func webUpdateMaterialRequestFromUpdateMaterialRequest(
    _ request: _Proto_MaterialDataUpdate_v1
) -> WKBridgeUpdateMaterial {
    let bridgeMaterialGraph = toWebMaterialGraph(request.shaderGraph)

    return WKBridgeUpdateMaterial(
        materialGraph: bridgeMaterialGraph,
        identifier: .init(value: request.id.value, path: request.id.path, hashValue: request.id.hashValue)
    )
}

extension ShaderGraph {
    // Reconstructs the graph from the IPC bridge representation using the ShaderGraph API.
    static func fromWKDescriptor(_ descriptor: WKBridgeMaterialGraph?) -> ShaderGraph? {
        guard let descriptor else { return nil }

        do {
            let graph = try ShaderGraph(
                named: descriptor.graphName.isEmpty ? "MaterialGraph" : descriptor.graphName,
                inputs: descriptor.inputs.map {
                    ShaderGraph.NodeDefinition.Input(
                        name: $0.name,
                        type: fromWKBridgeDataType($0.type),
                        semanticType: $0.semanticTypeName.map { .init(name: $0) }
                    )
                },
                outputs: descriptor.outputs.map {
                    ShaderGraph.NodeDefinition.Output(
                        name: $0.name,
                        type: fromWKBridgeDataType($0.type),
                        semanticType: $0.semanticTypeName.map { .init(name: $0) }
                    )
                }
            )

            let library = ShaderGraph.NodeLibrary(version: .materialX138)

            for bridgeNode in descriptor.nodes {
                switch bridgeNode.bridgeNodeType {
                case .constant:
                    if let constant = bridgeNode.constant {
                        try graph.addConstant(fromWKBridgeConstant(constant), named: constant.name)
                    }
                case .builtin:
                    if let builtin = bridgeNode.builtin, !builtin.definition.isEmpty,
                        let definition = library.definition(named: builtin.definition)
                    {
                        try graph.addNode(.init(name: builtin.name, data: .definition(definition)))
                    }
                case .arguments, .results:
                    break
                @unknown default:
                    let name = bridgeNode.builtin?.name ?? bridgeNode.constant?.name ?? "unknown"
                    fatalError("Unknown node type '\(name)'")
                }
            }

            for bridgeEdge in descriptor.edges {
                do {
                    try graph.connect(
                        bridgeEdge.outputNode,
                        outputPort: bridgeEdge.outputPort,
                        to: bridgeEdge.inputNode,
                        inputPort: bridgeEdge.inputPort
                    )
                } catch {
                    logError(
                        "Failed to connect \(bridgeEdge.outputNode):\(bridgeEdge.outputPort) -> \(bridgeEdge.inputNode):\(bridgeEdge.inputPort): \(error)"
                    )
                }
            }

            // Restore primvarMappings so ShaderGraph knows how to resolve UV primvar names
            // (e.g. "UV0") to actual mesh texture coordinates. Without this the ND_geompropvalue
            // node can't find UV data and all texture sampling breaks.
            for (primvarName, texcoordName) in zip(descriptor.primvarMappingPrimvarNames, descriptor.primvarMappingTexcoordNames) {
                if let texcoord = textureCoordinate(from: texcoordName) {
                    graph.primvarMappings[primvarName] = texcoord
                }
            }

            // Restore functionConstantInputs so runtime-driven inputs are declared correctly.
            graph.functionConstantInputs = descriptor.functionConstantInputNames

            return graph
        } catch {
            logError("Failed to reconstruct ShaderNodeGraph: \(error)")
            return nil
        }
    }
}

private func fromWKBridgeDataType(_ dataType: WKBridgeDataType) -> ShaderGraph.DataType {
    switch dataType {
    case .bool: .bool
    case .uchar: .uchar
    case .int: .int
    case .uint: .uint
    case .int2: .int2
    case .int3: .int3
    case .int4: .int4
    case .float: .float
    case .cgColor3: .cgColor3
    case .cgColor4: .cgColor4
    case .color3h: .cgColor3
    case .color4h: .cgColor4
    case .float2: .float2
    case .float3: .float3
    case .float4: .float4
    case .half: .half
    case .half2: .half2
    case .half3: .half3
    case .half4: .half4
    case .matrix2f: .float2x2
    case .matrix3f: .float3x3
    case .matrix4f: .float4x4
    case .matrix2h: .half2x2
    case .matrix3h: .half3x3
    case .matrix4h: .half4x4
    case .surfaceShader: .surfaceShader
    case .geometryModifier: .geometryModifier
    case .postLightingShader: .postLightingShader
    case .string: .string
    case .asset: .texture
    case .token, .quat: .string
    @unknown default: fatalError("fromWKBridgeDataType: unknown WKBridgeDataType")
    }
}

private func fromWKBridgeConstant(_ constant: WKBridgeConstantContainer) -> ShaderGraph.Value {
    let values = constant.constantValues

    switch constant.constant {
    case .bool:
        guard let v = values.first else { fatalError("fromWKBridgeConstant: missing value for bool constant '\(constant.name)'") }
        return .bool(v.number.boolValue)
    case .uchar:
        guard let v = values.first else { fatalError("fromWKBridgeConstant: missing value for uchar constant '\(constant.name)'") }
        return .uchar(v.number.uint8Value)
    case .int:
        guard let v = values.first else { fatalError("fromWKBridgeConstant: missing value for int constant '\(constant.name)'") }
        return .int(Int32(v.number.intValue))
    case .uint:
        guard let v = values.first else { fatalError("fromWKBridgeConstant: missing value for uint constant '\(constant.name)'") }
        return .uint(UInt32(v.number.uintValue))
    case .half:
        guard let v = values.first else { fatalError("fromWKBridgeConstant: missing value for half constant '\(constant.name)'") }
        return .half(.init(bitPattern: v.number.uint16Value))
    case .float:
        guard let v = values.first else { fatalError("fromWKBridgeConstant: missing value for float constant '\(constant.name)'") }
        return .float(v.number.floatValue)
    case .string, .token, .asset:
        guard let v = values.first else { fatalError("fromWKBridgeConstant: missing value for string constant '\(constant.name)'") }
        return .string(v.string)
    case .timecode:
        guard let v = values.first else { fatalError("fromWKBridgeConstant: missing value for timecode constant '\(constant.name)'") }
        return .float(Float(v.number.doubleValue))
    case .float2, .texCoord2f:
        guard values.count >= 2 else {
            fatalError("fromWKBridgeConstant: expected 2 values for float2 constant '\(constant.name)', got \(values.count)")
        }
        return .float2(
            SIMD2<Float>(
                values[0].number.floatValue,
                values[1].number.floatValue
            )
        )
    case .vector3f, .float3, .point3f, .normal3f, .texCoord3f:
        // All float3-based semantic types map to float3
        guard values.count >= 3 else {
            fatalError("fromWKBridgeConstant: expected 3 values for float3 constant '\(constant.name)', got \(values.count)")
        }
        return .float3(
            SIMD3<Float>(
                values[0].number.floatValue,
                values[1].number.floatValue,
                values[2].number.floatValue
            )
        )
    case .float4, .matrix2f:
        guard values.count >= 4 else {
            fatalError("fromWKBridgeConstant: expected 4 values for float4 constant '\(constant.name)', got \(values.count)")
        }
        return .float4(
            SIMD4<Float>(
                values[0].number.floatValue,
                values[1].number.floatValue,
                values[2].number.floatValue,
                values[3].number.floatValue
            )
        )
    case .half2, .texCoord2h:
        // half2-based semantic types map to half2
        guard values.count >= 2 else {
            fatalError("fromWKBridgeConstant: expected 2 values for half2 constant '\(constant.name)', got \(values.count)")
        }
        return .half2(
            SIMD2<Float16>(
                .init(bitPattern: values[0].number.uint16Value),
                .init(bitPattern: values[1].number.uint16Value)
            )
        )
    case .vector3h, .half3, .point3h, .normal3h, .texCoord3h:
        // All half3-based semantic types map to half3
        guard values.count >= 3 else {
            fatalError("fromWKBridgeConstant: expected 3 values for half3 constant '\(constant.name)', got \(values.count)")
        }
        return .half3(
            SIMD3<Float16>(
                .init(bitPattern: values[0].number.uint16Value),
                .init(bitPattern: values[1].number.uint16Value),
                .init(bitPattern: values[2].number.uint16Value)
            )
        )
    case .half4:
        guard values.count >= 4 else {
            fatalError("fromWKBridgeConstant: expected 4 values for half4 constant '\(constant.name)', got \(values.count)")
        }
        return .half4(
            SIMD4<Float16>(
                .init(bitPattern: values[0].number.uint16Value),
                .init(bitPattern: values[1].number.uint16Value),
                .init(bitPattern: values[2].number.uint16Value),
                .init(bitPattern: values[3].number.uint16Value)
            )
        )
    case .int2:
        guard values.count >= 2 else {
            fatalError("fromWKBridgeConstant: expected 2 values for int2 constant '\(constant.name)', got \(values.count)")
        }
        return .int2(
            SIMD2<Int32>(
                values[0].number.int32Value,
                values[1].number.int32Value
            )
        )
    case .int3:
        guard values.count >= 3 else {
            fatalError("fromWKBridgeConstant: expected 3 values for int3 constant '\(constant.name)', got \(values.count)")
        }
        return .int3(
            SIMD3<Int32>(
                values[0].number.int32Value,
                values[1].number.int32Value,
                values[2].number.int32Value
            )
        )
    case .int4:
        guard values.count >= 4 else {
            fatalError("fromWKBridgeConstant: expected 4 values for int4 constant '\(constant.name)', got \(values.count)")
        }
        return .int4(
            SIMD4<Int32>(
                values[0].number.int32Value,
                values[1].number.int32Value,
                values[2].number.int32Value,
                values[3].number.int32Value
            )
        )
    case .matrix3f:
        // matrix3f maps to float3x3 - needs 9 values (3 columns of 3 rows each)
        guard values.count >= 9 else {
            fatalError("fromWKBridgeConstant: expected 9 values for matrix3f constant '\(constant.name)', got \(values.count)")
        }
        return .float3x3(
            .init(
                SIMD3<Float>(values[0].number.floatValue, values[1].number.floatValue, values[2].number.floatValue),
                SIMD3<Float>(values[3].number.floatValue, values[4].number.floatValue, values[5].number.floatValue),
                SIMD3<Float>(values[6].number.floatValue, values[7].number.floatValue, values[8].number.floatValue)
            )
        )
    case .matrix4f:
        // matrix4f maps to float4x4 - needs 16 values (4 columns of 4 rows each)
        guard values.count >= 16 else {
            fatalError("fromWKBridgeConstant: expected 16 values for matrix4f constant '\(constant.name)', got \(values.count)")
        }
        return .float4x4(
            .init(
                SIMD4<Float>(
                    values[0].number.floatValue,
                    values[1].number.floatValue,
                    values[2].number.floatValue,
                    values[3].number.floatValue
                ),
                SIMD4<Float>(
                    values[4].number.floatValue,
                    values[5].number.floatValue,
                    values[6].number.floatValue,
                    values[7].number.floatValue
                ),
                SIMD4<Float>(
                    values[8].number.floatValue,
                    values[9].number.floatValue,
                    values[10].number.floatValue,
                    values[11].number.floatValue
                ),
                SIMD4<Float>(
                    values[12].number.floatValue,
                    values[13].number.floatValue,
                    values[14].number.floatValue,
                    values[15].number.floatValue
                )
            )
        )
    case .quatf, .quath:
        // quath/quatf don't exist in the enum - map to float4
        guard values.count >= 4 else {
            fatalError("fromWKBridgeConstant: expected 4 values for quat constant '\(constant.name)', got \(values.count)")
        }
        return .float4(
            SIMD4<Float>(
                values[0].number.floatValue,
                values[1].number.floatValue,
                values[2].number.floatValue,
                values[3].number.floatValue
            )
        )
    case .cgColor3:
        guard values.count >= 3 else {
            fatalError("fromWKBridgeConstant: expected 3 values for color3 constant '\(constant.name)', got \(values.count)")
        }
        // Use extendedLinearSRGB to preserve values outside [0,1] (e.g. negative bias, scale > 1).
        // CGColor(red:green:blue:alpha:) clamps to device RGB [0,1] which corrupts shader constants.
        let components3: [CGFloat] = [
            CGFloat(values[0].number.floatValue),
            CGFloat(values[1].number.floatValue),
            CGFloat(values[2].number.floatValue),
            1.0,
        ]
        return .cgColor3(CGColor(red: components3[0], green: components3[1], blue: components3[2], alpha: 1.0))
    case .cgColor4:
        guard values.count >= 4 else {
            fatalError("fromWKBridgeConstant: expected 4 values for color4 constant '\(constant.name)', got \(values.count)")
        }
        // Use extendedLinearSRGB to preserve values outside [0,1] (e.g. negative bias, scale > 1).
        // CGColor(red:green:blue:alpha:) clamps to device RGB [0,1] which corrupts shader constants.
        let components4: [CGFloat] = [
            CGFloat(values[0].number.floatValue),
            CGFloat(values[1].number.floatValue),
            CGFloat(values[2].number.floatValue),
            CGFloat(values[3].number.floatValue),
        ]
        return .cgColor4(CGColor(red: components4[0], green: components4[1], blue: components4[2], alpha: components4[3]))
    case .color4f:
        // USD/MaterialX color4f or color4h — encoded as 4 raw floats without CGColor clamping.
        // Decoded as cgColor4 via extendedLinearSRGB to preserve color semantics for MaterialX.
        guard values.count >= 4 else {
            fatalError("fromWKBridgeConstant: expected 4 values for color4f constant '\(constant.name)', got \(values.count)")
        }
        let components4f: [CGFloat] = [
            CGFloat(values[0].number.floatValue),
            CGFloat(values[1].number.floatValue),
            CGFloat(values[2].number.floatValue),
            CGFloat(values[3].number.floatValue),
        ]
        return .cgColor4(CGColor(red: components4f[0], green: components4f[1], blue: components4f[2], alpha: components4f[3]))
    case .color3f:
        // USD/MaterialX color3f — encoded as 3 raw floats without CGColor clamping.
        guard values.count >= 3 else {
            fatalError("fromWKBridgeConstant: expected 3 values for color3f constant '\(constant.name)', got \(values.count)")
        }
        let components3f: [CGFloat] = [
            CGFloat(values[0].number.floatValue),
            CGFloat(values[1].number.floatValue),
            CGFloat(values[2].number.floatValue),
            1.0,
        ]
        return .cgColor3(CGColor(red: components3f[0], green: components3f[1], blue: components3f[2], alpha: 1.0))
    case .color3h:
        // USD/MaterialX color3h — half-precision; represented as cgColor3 in ShaderGraph.Value.
        guard values.count >= 3 else {
            fatalError("fromWKBridgeConstant: expected 3 values for color3h constant '\(constant.name)', got \(values.count)")
        }
        return .cgColor3(
            CGColor(
                red: CGFloat(values[0].number.floatValue),
                green: CGFloat(values[1].number.floatValue),
                blue: CGFloat(values[2].number.floatValue),
                alpha: 1.0
            )
        )
    case .color4h:
        // USD/MaterialX color4h — half-precision; represented as cgColor4 in ShaderGraph.Value.
        guard values.count >= 4 else {
            fatalError("fromWKBridgeConstant: expected 4 values for color4h constant '\(constant.name)', got \(values.count)")
        }
        return .cgColor4(
            CGColor(
                red: CGFloat(values[0].number.floatValue),
                green: CGFloat(values[1].number.floatValue),
                blue: CGFloat(values[2].number.floatValue),
                alpha: CGFloat(values[3].number.floatValue)
            )
        )
    @unknown default:
        fatalError("fromWKBridgeConstant: unhandled constant type \(constant.constant) for '\(constant.name)'")
    }
}

final class USDModelLoader {
    fileprivate let usdStageSession: _Proto_UsdStageSession_v1
    fileprivate var stage: UsdStage?
    fileprivate var data: Data?
    private let objcLoader: WKBridgeModelLoader

    @nonobjc
    fileprivate var time: TimeInterval = 0

    @nonobjc
    fileprivate var startTime: TimeInterval = 0
    @nonobjc
    fileprivate var endTime: TimeInterval = 1
    @nonobjc
    fileprivate var timeCodePerSecond: TimeInterval = 1
    @nonobjc
    fileprivate var loop: Bool = false

    init(objcInstance: WKBridgeModelLoader, gpuFamily: MTLGPUFamily) {
        objcLoader = objcInstance
        usdStageSession = _Proto_UsdStageSession_v1.noMetalSessionWithSynchronizedUpdate(gpuFamily: gpuFamily)
    }

    func loadModel(from url: Foundation.URL) {
        do {
            let stage = try UsdStage.open(url)
            self.setupTimes(from: stage)
            self.usdStageSession.loadStage(stage)
        } catch {
            fatalError(error.localizedDescription)
        }
    }

    func loadModel(data: Foundation.Data) -> Bool {
        do {
            self.stage = try UsdStage.open(buffer: data)
            guard let stage = self.stage else {
                logError("model data is corrupted")
                return false
            }
            self.data = data
            self.setupTimes(from: stage)
            self.usdStageSession.loadStage(stage)
            return true
        } catch {
            logError(error.localizedDescription)
            return false
        }
    }

    func loadEnvironmentMap(_ data: Foundation.Data) -> WKBridgeUpdateTexture? {
        guard
            let textureData = self.usdStageSession.importCustomIBLTexture(
                identifier: .init(UUID().uuidString),
                data: data
            )
        else { return nil }

        return webUpdateTextureRequestFromUpdateTextureRequest(textureData)
    }

    func setupTimes(from stage: UsdStage) {
        timeCodePerSecond = stage.timeCodesPerSecond > 0 ? stage.timeCodesPerSecond : 1
        startTime = stage.startTimeCode / timeCodePerSecond
        endTime = stage.endTimeCode / timeCodePerSecond
        time = 0
    }

    func duration() -> Double {
        endTime - startTime
    }

    func currentTime() -> Double {
        time - startTime
    }

    func setCurrentTime(_ newTime: Double) {
        time = startTime + newTime
    }

    func loadModel(from data: Data) -> Bool {
        false
    }

    func update(deltaTime: TimeInterval) {
        let frameUpdate = usdStageSession.updateAndFetchFrameData(time: time * timeCodePerSecond)

        let newTime = currentTime() + deltaTime
        let adjustedTime: TimeInterval
        if loop {
            let modValue = max(duration(), 1)
            var wrappedTime = fmod(newTime, modValue)
            if wrappedTime < 0 {
                wrappedTime += modValue
            }
            adjustedTime = wrappedTime
        } else {
            adjustedTime = max(0, min(newTime, duration()))
        }
        time = startTime + adjustedTime

        if frameUpdate.isEmpty {
            return
        }

        // Extract arrays from the ~Copyable FrameUpdate before capturing in the Task.
        let meshUpdates = frameUpdate.meshUpdates
        let materialUpdates = frameUpdate.materialUpdates
        let textureUpdates = frameUpdate.textureUpdates
        let meshRemovals = frameUpdate.meshRemovals
        let materialRemovals = frameUpdate.materialRemovals
        let textureRemovals = frameUpdate.textureRemovals

        if let errors = frameUpdate.errors {
            processErrors(errors)
        }

        // Process in dependency order: textures → materials → meshes.
        // Each phase completes before the next begins so that:
        //   - textureHashesAndResources is fully populated before makeParameters reads it inside material Tasks
        //   - materialsAndParams has Task entries for all new materials before mesh processing looks them up
        processRemovals(meshRemovals: meshRemovals, materialRemovals: materialRemovals, textureRemovals: textureRemovals)
        processTextureUpdates(textureUpdates)
        processMaterialUpdates(materialUpdates)
        processMeshUpdates(meshUpdates)
    }

    private func processErrors(_ errors: _Proto_UsdStageSession_v1.FrameUpdate.Errors) {
        for (id, error) in errors.meshErrors {
            print("mesh error with id \(id): \(error.localizedDescription)")
        }
        for (id, error) in errors.materialErrors {
            print("material error with id \(id): \(error.localizedDescription)")
        }
        for (id, error) in errors.textureErrors {
            print("texture error with id \(id): \(error.localizedDescription)")
        }
    }

    private func processRemovals(
        meshRemovals: [_Proto_MeshId],
        materialRemovals: [_Proto_MaterialId],
        textureRemovals: [_Proto_TextureId]
    ) {
        self.objcLoader.processRemovals(
            removals:
                WKBridgeRemovals(
                    meshRemovals: meshRemovals.map { .init(value: $0.value, path: $0.path, hashValue: $0.hashValue) },
                    materialRemovals: materialRemovals.map { .init(value: $0.value, path: $0.path, hashValue: $0.hashValue) },
                    textureRemovals: textureRemovals.map { .init(value: $0.value, path: $0.path, hashValue: $0.hashValue) }
                )
        )
    }

    private func processTextureUpdates(_ updates: [_Proto_TextureDataUpdate_v1]) {
        self.objcLoader.updateTexture(webRequest: updates.map { webUpdateTextureRequestFromUpdateTextureRequest($0) })
    }

    private func processMaterialUpdates(_ updates: [_Proto_MaterialDataUpdate_v1]) {
        self.objcLoader.updateMaterial(webRequest: updates.map { webUpdateMaterialRequestFromUpdateMaterialRequest($0) })
    }

    private func processMeshUpdates(_ updates: [_Proto_MeshDataUpdate_v1]) {
        self.objcLoader.updateMesh(webRequest: updates.map { webUpdateMeshRequestFromUpdateMeshRequest($0) })
    }
}

@objc
@implementation
extension WKBridgeModelLoader {
    @nonobjc
    var loader: USDModelLoader?
    @nonobjc
    var modelUpdated: (([WKBridgeUpdateMesh]) -> (Void))?
    @nonobjc
    var textureUpdatedCallback: (([WKBridgeUpdateTexture]) -> (Void))?
    @nonobjc
    var materialUpdatedCallback: (([WKBridgeUpdateMaterial]) -> (Void))?
    @nonobjc
    var processRemovalsCallback: ((WKBridgeRemovals) -> (Void))?

    @nonobjc
    fileprivate var retainedRequests: Set<NSObject> = []

    @objc(initWithGPUFamily:)
    init(gpuFamily: MTLGPUFamily) {
        super.init()

        self.loader = USDModelLoader(objcInstance: self, gpuFamily: gpuFamily)
    }

    @objc(
        setCallbacksWithModelUpdatedCallback:
        textureUpdatedCallback:
        materialUpdatedCallback:
        processRemovalsCallback:
    )
    func setCallbacksWithModelUpdatedCallback(
        _ modelUpdatedCallback: @escaping (([WKBridgeUpdateMesh]) -> (Void)),
        textureUpdatedCallback: @escaping (([WKBridgeUpdateTexture]) -> (Void)),
        materialUpdatedCallback: @escaping (([WKBridgeUpdateMaterial]) -> (Void)),
        processRemovalsCallback: @escaping ((WKBridgeRemovals) -> (Void))
    ) {
        self.modelUpdated = modelUpdatedCallback
        self.textureUpdatedCallback = textureUpdatedCallback
        self.materialUpdatedCallback = materialUpdatedCallback
        self.processRemovalsCallback = processRemovalsCallback
    }

    func loadModel(from url: Foundation.URL) {
        self.loader?.loadModel(from: url)
    }

    func loadModel(_ data: Foundation.Data) -> Bool {
        self.loader?.loadModel(data: data) ?? false
    }

    func loadEnvironmentMap(_ data: Foundation.Data) -> WKBridgeUpdateTexture? {
        self.loader?.loadEnvironmentMap(data)
    }

    func update(_ deltaTime: Double) {
        self.loader?.update(deltaTime: deltaTime)
    }

    func setLoop(_ loop: Bool) {
        self.loader?.loop = loop
    }

    func requestCompleted(_ request: NSObject) {
        retainedRequests.remove(request)
    }

    func duration() -> Double {
        guard let loader else {
            return 0.0
        }
        return loader.duration()
    }

    func currentTime() -> Double {
        guard let loader else {
            return 0.0
        }
        return loader.currentTime()
    }

    func setCurrentTime(_ newTime: Double) {
        loader?.setCurrentTime(newTime)
    }

    fileprivate func updateMesh(webRequest: [WKBridgeUpdateMesh]) {
        if webRequest.isEmpty {
            return
        }
        if let modelUpdated {
            retainedRequests.insert(webRequest as NSArray)
            modelUpdated(webRequest)
        }
    }

    fileprivate func updateTexture(webRequest: [WKBridgeUpdateTexture]) {
        if webRequest.isEmpty {
            return
        }
        if let textureUpdatedCallback {
            retainedRequests.insert(webRequest as NSArray)
            textureUpdatedCallback(webRequest)
        }
    }

    fileprivate func updateMaterial(webRequest: [WKBridgeUpdateMaterial]) {
        if webRequest.isEmpty {
            return
        }
        if let materialUpdatedCallback {
            retainedRequests.insert(webRequest as NSArray)
            materialUpdatedCallback(webRequest)
        }
    }
    fileprivate func processRemovals(removals: WKBridgeRemovals) {
        if removals.isEmpty() {
            return
        }

        if let processRemovalsCallback {
            retainedRequests.insert(removals)
            processRemovalsCallback(removals)
        }
    }
}

private func makeFallBackTextureResource(
    _ renderContext: any LowLevelRenderContext,
    commandQueue: any MTLCommandQueue,
    device: any MTLDevice,
    memoryOwner: task_id_token_t
) -> LowLevelTextureResource {
    // Create 1x1 white fallback texture
    let fallbackDescriptor = LowLevelTextureResource.Descriptor(
        textureType: .type2D,
        pixelFormat: .rgba8Unorm,
        width: 1,
        height: 1,
        mipmapLevelCount: 1,
        textureUsage: .shaderRead
    )
    // swift-format-ignore: NeverUseForceTry
    let fallbackTexture = try! renderContext.makeTextureResource(descriptor: fallbackDescriptor)

    // White color: RGBA = (255, 255, 255, 255)
    let whitePixel: [UInt8] = [255, 255, 255, 255]

    let stagingBuffer = device.makeBuffer(fromSpan: whitePixel.span, length: whitePixel.count, memoryOwner: memoryOwner)

    // Create command buffer to upload white pixel data
    // swift-format-ignore: NeverForceUnwrap
    let fallbackCommandBuffer = commandQueue.makeCommandBuffer()!
    let fallbackMTLTexture = fallbackTexture.replace(commandBuffer: fallbackCommandBuffer)

    // Use blit encoder to copy from buffer to texture
    // swift-format-ignore: NeverForceUnwrap
    let blitEncoder = fallbackCommandBuffer.makeBlitCommandEncoder()!
    blitEncoder.copy(
        from: stagingBuffer,
        sourceOffset: 0,
        sourceBytesPerRow: 4,
        sourceBytesPerImage: 4,
        sourceSize: MTLSize(width: 1, height: 1, depth: 1),
        to: fallbackMTLTexture,
        destinationSlice: 0,
        destinationLevel: 0,
        destinationOrigin: MTLOrigin(x: 0, y: 0, z: 0)
    )
    blitEncoder.endEncoding()

    fallbackCommandBuffer.commit()

    return fallbackTexture
}

#else
@objc
@implementation
extension WKBridgeUSDConfiguration {
    init(device: any MTLDevice, memoryOwner: task_id_token_t) {
    }

    @objc(createMaterialCompiler:)
    func createMaterialCompiler() async {
    }
}

@objc
@implementation
extension WKBridgeReceiver {
    init(
        configuration: WKBridgeUSDConfiguration,
        diffuseAsset: WKBridgeImageAsset,
        specularAsset: WKBridgeImageAsset
    ) throws {
    }

    func commandBuffer() -> (any MTLCommandBuffer)? {
        nil
    }

    @objc(renderWithTexture:commandBuffer:)
    func render(with texture: any MTLTexture, commandBuffer: any MTLCommandBuffer) {
    }

    @objc(updateTexture:)
    func updateTexture(_ data: [WKBridgeUpdateTexture]) {
    }

    @objc(updateMaterial:completionHandler:)
    func updateMaterial(_ data: [WKBridgeUpdateMaterial]) async {
    }

    @objc(updateMesh:completionHandler:)
    func updateMesh(_ data: [WKBridgeUpdateMesh]) async {
    }

    @objc
    func processRemovals(
        _ meshRemovals: [WKBridgeTypedResourceId],
        materialRemovals: [WKBridgeTypedResourceId],
        textureRemovals: [WKBridgeTypedResourceId]
    ) -> Bool {
        false
    }

    @objc(setTransform:)
    func setTransform(_ transform: simd_float4x4) {
    }

    func setFOV(_ fovY: Float) {
    }

    func setBackgroundColor(_ color: simd_float3) {
    }

    func setPlaying(_ play: Bool) {
    }

    func setEnvironmentMap(_ imageAsset: WKBridgeUpdateTexture) {
    }
}

@objc
@implementation
extension WKBridgeModelLoader {
    @objc(initWithGPUFamily:)
    init(gpuFamily: MTLGPUFamily) {
        super.init()
    }

    @objc(
        setCallbacksWithModelUpdatedCallback:
        textureUpdatedCallback:
        materialUpdatedCallback:
        processRemovalsCallback:
    )
    func setCallbacksWithModelUpdatedCallback(
        _ modelUpdatedCallback: @escaping (([WKBridgeUpdateMesh]) -> (Void)),
        textureUpdatedCallback: @escaping (([WKBridgeUpdateTexture]) -> (Void)),
        materialUpdatedCallback: @escaping (([WKBridgeUpdateMaterial]) -> (Void)),
        processRemovalsCallback: @escaping ((WKBridgeRemovals) -> (Void))
    ) {
    }

    func loadModel(from url: Foundation.URL) {
    }

    func loadModel(_ data: Foundation.Data) -> Bool {
        false
    }

    func loadEnvironmentMap(_ data: Foundation.Data) -> WKBridgeUpdateTexture? {
        nil
    }

    func update(_ deltaTime: Double) {
    }

    func setLoop(_ loop: Bool) {
    }

    func requestCompleted(_ request: NSObject) {
    }

    func duration() -> Double {
        0.0
    }

    func currentTime() -> Double {
        0.0
    }

    func setCurrentTime(_ newTime: Double) {
    }
}
#endif
