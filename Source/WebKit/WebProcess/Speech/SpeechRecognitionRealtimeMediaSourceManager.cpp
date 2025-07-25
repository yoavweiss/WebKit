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
#include "SpeechRecognitionRealtimeMediaSourceManager.h"

#if ENABLE(MEDIA_STREAM)

#include "Logging.h"
#include "MessageSenderInlines.h"
#include "SpeechRecognitionRealtimeMediaSourceManagerMessages.h"
#include "SpeechRecognitionRemoteRealtimeMediaSourceManagerMessages.h"
#include "WebProcess.h"
#include <WebCore/RealtimeMediaSource.h>
#include <WebCore/SpeechRecognitionCaptureSource.h>
#include <wtf/CheckedRef.h>
#include <wtf/TZoneMallocInlines.h>

#if PLATFORM(COCOA)
#include "SharedCARingBuffer.h"
#include <WebCore/CAAudioStreamDescription.h>
#include <WebCore/WebAudioBufferList.h>
#else
#include <WebCore/AudioStreamDescription.h>
#include <WebCore/PlatformAudioData.h>
#endif

#if USE(AUDIO_SESSION)
#include <WebCore/AudioSession.h>
#endif

namespace WebKit {

using namespace WebCore;

class SpeechRecognitionRealtimeMediaSourceManager::Source final
    : private RealtimeMediaSourceObserver
    , private RealtimeMediaSource::AudioSampleObserver
    , public CanMakeCheckedPtr<SpeechRecognitionRealtimeMediaSourceManager::Source>
{
    WTF_MAKE_TZONE_ALLOCATED_INLINE(SpeechRecognitionRealtimeMediaSourceManager);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(Source);
public:
    Source(RealtimeMediaSourceIdentifier identifier, Ref<RealtimeMediaSource>&& source, Ref<IPC::Connection>&& connection)
        : m_identifier(identifier)
        , m_source(WTFMove(source))
        , m_connection(WTFMove(connection))
    {
        m_source->addObserver(*this);
        m_source->addAudioSampleObserver(*this);
    }

    ~Source()
    {
        m_source->removeAudioSampleObserver(*this);
        m_source->removeObserver(*this);
    }

    void start()
    {
        m_source->start();
    }

    void stop()
    {
        m_source->stop();
    }

private:
    // CheckedPtr interface
    uint32_t checkedPtrCount() const final { return CanMakeCheckedPtr::checkedPtrCount(); }
    uint32_t checkedPtrCountWithoutThreadCheck() const final { return CanMakeCheckedPtr::checkedPtrCountWithoutThreadCheck(); }
    void incrementCheckedPtrCount() const final { CanMakeCheckedPtr::incrementCheckedPtrCount(); }
    void decrementCheckedPtrCount() const final { CanMakeCheckedPtr::decrementCheckedPtrCount(); }

    void sourceStopped() final
    {
        if (m_source->captureDidFail()) {
            m_connection->send(Messages::SpeechRecognitionRemoteRealtimeMediaSourceManager::RemoteCaptureFailed(m_identifier), 0);
            return;
        }
        m_connection->send(Messages::SpeechRecognitionRemoteRealtimeMediaSourceManager::RemoteSourceStopped(m_identifier), 0);
    }

    void audioSamplesAvailable(const MediaTime& time, const PlatformAudioData& audioData, const AudioStreamDescription& description, size_t numberOfFrames) final
    {
#if PLATFORM(COCOA)
        DisableMallocRestrictionsForCurrentThreadScope scope;
        if (m_description != description) {
            ASSERT(description.platformDescription().type == PlatformDescription::CAAudioStreamBasicType);
            m_description = *std::get<const AudioStreamBasicDescription*>(description.platformDescription().description);
            size_t numberOfFrames = m_description->sampleRate() * 2;
            auto& format = m_description->streamDescription();
            auto result = ProducerSharedCARingBuffer::allocate(format, numberOfFrames);
            RELEASE_ASSERT(result); // FIXME(https://bugs.webkit.org/show_bug.cgi?id=262690): Handle allocation failure.
            auto [ringBuffer, handle] = WTFMove(*result);
            m_ringBuffer = WTFMove(ringBuffer);
            m_connection->send(Messages::SpeechRecognitionRemoteRealtimeMediaSourceManager::SetStorage(m_identifier, WTFMove(handle), format), 0);
        }

        m_ringBuffer->store(downcast<WebAudioBufferList>(audioData).list(), numberOfFrames, time.timeValue());
        m_connection->send(Messages::SpeechRecognitionRemoteRealtimeMediaSourceManager::RemoteAudioSamplesAvailable(m_identifier, time, numberOfFrames), 0);
#else
        UNUSED_PARAM(time);
        UNUSED_PARAM(audioData);
        UNUSED_PARAM(description);
        UNUSED_PARAM(numberOfFrames);
#endif
    }

    void audioUnitWillStart() final
    {
#if USE(AUDIO_SESSION)
        auto bufferSize = AudioSession::singleton().sampleRate() / 50;
        if (AudioSession::singleton().preferredBufferSize() > bufferSize)
            AudioSession::singleton().setPreferredBufferSize(bufferSize);
        AudioSession::singleton().setCategory(AudioSession::CategoryType::PlayAndRecord, AudioSession::Mode::Default, RouteSharingPolicy::Default);
#endif
    }

    RealtimeMediaSourceIdentifier m_identifier;
    const Ref<RealtimeMediaSource> m_source;
    const Ref<IPC::Connection> m_connection;

#if PLATFORM(COCOA)
    std::unique_ptr<ProducerSharedCARingBuffer> m_ringBuffer;
    std::optional<CAAudioStreamDescription> m_description { };
#endif
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(SpeechRecognitionRealtimeMediaSourceManager);

SpeechRecognitionRealtimeMediaSourceManager::SpeechRecognitionRealtimeMediaSourceManager(WebProcess& process)
    : m_process(process)
{
    process.addMessageReceiver(Messages::SpeechRecognitionRealtimeMediaSourceManager::messageReceiverName(), *this);
}

SpeechRecognitionRealtimeMediaSourceManager::~SpeechRecognitionRealtimeMediaSourceManager()
{
    m_process->removeMessageReceiver(*this);
}

IPC::Connection& SpeechRecognitionRealtimeMediaSourceManager::connection() const
{
    return *m_process->parentProcessConnection();
}

Ref<IPC::Connection> SpeechRecognitionRealtimeMediaSourceManager::protectedConnection() const
{
    return *m_process->parentProcessConnection();
}

void SpeechRecognitionRealtimeMediaSourceManager::ref() const
{
    m_process->ref();
}

void SpeechRecognitionRealtimeMediaSourceManager::deref() const
{
    m_process->deref();
}

void SpeechRecognitionRealtimeMediaSourceManager::createSource(RealtimeMediaSourceIdentifier identifier, const CaptureDevice& device, PageIdentifier pageIdentifier)
{
    auto result = SpeechRecognitionCaptureSource::createRealtimeMediaSource(device, pageIdentifier);
    if (!result) {
        RELEASE_LOG_ERROR(Media, "Failed to create realtime source");
        send(Messages::SpeechRecognitionRemoteRealtimeMediaSourceManager::RemoteCaptureFailed(identifier), 0);
        return;
    }

    ASSERT(!m_sources.contains(identifier));
    m_sources.add(identifier, makeUnique<Source>(identifier, result.source(), protectedConnection()));
}

void SpeechRecognitionRealtimeMediaSourceManager::deleteSource(RealtimeMediaSourceIdentifier identifier)
{
    m_sources.remove(identifier);
}

void SpeechRecognitionRealtimeMediaSourceManager::start(RealtimeMediaSourceIdentifier identifier)
{
    if (auto source = m_sources.get(identifier))
        source->start();
}

void SpeechRecognitionRealtimeMediaSourceManager::stop(RealtimeMediaSourceIdentifier identifier)
{
    if (auto source = m_sources.get(identifier))
        source->stop();
}

IPC::Connection* SpeechRecognitionRealtimeMediaSourceManager::messageSenderConnection() const
{
    return &connection();
}

uint64_t SpeechRecognitionRealtimeMediaSourceManager::messageSenderDestinationID() const
{
    return 0;
}

} // namespace WebKit

#endif
