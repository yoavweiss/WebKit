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
#include "RemoteSampleBufferDisplayLayerManager.h"

#if PLATFORM(COCOA) && ENABLE(GPU_PROCESS) && ENABLE(MEDIA_STREAM)

#include "Decoder.h"
#include "GPUConnectionToWebProcess.h"
#include "GPUProcess.h"
#include "RemoteSampleBufferDisplayLayer.h"
#include "RemoteSampleBufferDisplayLayerManagerMessages.h"
#include "RemoteSampleBufferDisplayLayerMessages.h"
#include <WebCore/IntSize.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(RemoteSampleBufferDisplayLayerManager);

RemoteSampleBufferDisplayLayerManager::RemoteSampleBufferDisplayLayerManager(GPUConnectionToWebProcess& gpuConnectionToWebProcess, SharedPreferencesForWebProcess& sharedPreferencesForWebProcess)
    : m_connectionToWebProcess(gpuConnectionToWebProcess)
    , m_connection(gpuConnectionToWebProcess.connection())
    , m_sharedPreferencesForWebProcess(sharedPreferencesForWebProcess)
    , m_queue(gpuConnectionToWebProcess.gpuProcess().videoMediaStreamTrackRendererQueue())
{
    m_queue->dispatch([this, protectedThis = Ref { *this }, sharedPreferencesForWebProcess] {
        m_sharedPreferencesForWebProcess = sharedPreferencesForWebProcess;
    });
}

void RemoteSampleBufferDisplayLayerManager::startListeningForIPC()
{
    auto connection = m_connectionToWebProcess.get();
    if (!connection)
        return;
    Ref ipcConnection = connection->connection();
    ipcConnection->addWorkQueueMessageReceiver(Messages::RemoteSampleBufferDisplayLayer::messageReceiverName(), m_queue, *this);
    ipcConnection->addWorkQueueMessageReceiver(Messages::RemoteSampleBufferDisplayLayerManager::messageReceiverName(), m_queue, *this);
}

RemoteSampleBufferDisplayLayerManager::~RemoteSampleBufferDisplayLayerManager() = default;

void RemoteSampleBufferDisplayLayerManager::close()
{
    auto connection = m_connectionToWebProcess.get();
    if (!connection)
        return;
    Ref ipcConnection = connection->connection();
    ipcConnection->removeWorkQueueMessageReceiver(Messages::RemoteSampleBufferDisplayLayer::messageReceiverName());
    ipcConnection->removeWorkQueueMessageReceiver(Messages::RemoteSampleBufferDisplayLayerManager::messageReceiverName());
    m_queue->dispatch([this, protectedThis = Ref { *this }] {
        Locker lock(m_layersLock);
        callOnMainRunLoop([layers = WTFMove(m_layers)] { });
    });
}

bool RemoteSampleBufferDisplayLayerManager::dispatchMessage(IPC::Connection& connection, IPC::Decoder& decoder)
{
    if (!ObjectIdentifier<SampleBufferDisplayLayerIdentifierType>::isValidIdentifier(decoder.destinationID()))
        return false;

    auto identifier = ObjectIdentifier<SampleBufferDisplayLayerIdentifierType>(decoder.destinationID());
    Locker lock(m_layersLock);
    if (RefPtr layer = m_layers.get(identifier))
        layer->didReceiveMessage(connection, decoder);
    return true;
}

void RemoteSampleBufferDisplayLayerManager::createLayer(SampleBufferDisplayLayerIdentifier identifier, bool hideRootLayer, WebCore::IntSize size, bool shouldMaintainAspectRatio, bool canShowWhileLocked, LayerCreationCallback&& callback)
{
    callOnMainRunLoop([this, protectedThis = Ref { *this }, identifier, hideRootLayer, size, shouldMaintainAspectRatio, canShowWhileLocked, callback = WTFMove(callback)]() mutable {
        auto connection = m_connectionToWebProcess.get();
        if (!connection)
            return callback({ });
        auto layer = RemoteSampleBufferDisplayLayer::create(*connection, identifier, m_connection.copyRef(), protectedThis);
        if (!layer) {
            callback({ });
            return;
        }
        layer->initialize(hideRootLayer, size, shouldMaintainAspectRatio, canShowWhileLocked, [this, protectedThis = Ref { *this }, callback = WTFMove(callback), identifier, layer = Ref { *layer }](auto hostingContext) mutable {
            m_queue->dispatch([protectedThis = Ref { *this }, callback = WTFMove(callback), identifier, layer = WTFMove(layer), hostingContext = WTFMove(hostingContext)]() mutable {
                Locker lock(protectedThis->m_layersLock);
                ASSERT(!protectedThis->m_layers.contains(identifier));
                protectedThis->m_layers.add(identifier, WTFMove(layer));
                callback(WTFMove(hostingContext));
            });
        });
    });
}

void RemoteSampleBufferDisplayLayerManager::releaseLayer(SampleBufferDisplayLayerIdentifier identifier)
{
    callOnMainRunLoop([this, protectedThis = Ref { *this }, identifier]() mutable {
        m_queue->dispatch([protectedThis = Ref { *this }, identifier] {
            Locker lock(protectedThis->m_layersLock);
            ASSERT(protectedThis->m_layers.contains(identifier));
            callOnMainRunLoop([layer = protectedThis->m_layers.take(identifier)] { });
        });
    });
}

bool RemoteSampleBufferDisplayLayerManager::allowsExitUnderMemoryPressure() const
{
    Locker lock(m_layersLock);
    return m_layers.isEmpty();
}

void RemoteSampleBufferDisplayLayerManager::updateSampleBufferDisplayLayerBoundsAndPosition(SampleBufferDisplayLayerIdentifier identifier, WebCore::FloatRect bounds, std::optional<MachSendRightAnnotated>&& sendRight)
{
    Locker lock(m_layersLock);
    if (RefPtr layer = m_layers.get(identifier))
        layer->updateBoundsAndPosition(bounds, WTFMove(sendRight));
}

void RemoteSampleBufferDisplayLayerManager::updateSharedPreferencesForWebProcess(SharedPreferencesForWebProcess sharedPreferencesForWebProcess)
{
    m_queue->dispatch([this, protectedThis = Ref { *this }, sharedPreferencesForWebProcess = WTFMove(sharedPreferencesForWebProcess)] {
        m_sharedPreferencesForWebProcess = sharedPreferencesForWebProcess;
    });
}

}

#endif // PLATFORM(COCOA) && ENABLE(GPU_PROCESS) && ENABLE(MEDIA_STREAM)
