/*
 * Copyright (C) 2017-2025 Apple Inc. All rights reserved.
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
#include "LibWebRTCProvider.h"

#if USE(LIBWEBRTC)

#include "ContentType.h"
#include "LibWebRTCAudioModule.h"
#include "LibWebRTCLogSink.h"
#include "LibWebRTCUtils.h"
#include "Logging.h"
#include "MediaCapabilitiesDecodingInfo.h"
#include "MediaCapabilitiesEncodingInfo.h"
#include "MediaDecodingConfiguration.h"
#include "MediaEncodingConfiguration.h"
#include "ProcessQualified.h"
#include <dlfcn.h>
#include <wtf/TZoneMallocInlines.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN

#include <webrtc/api/audio_codecs/builtin_audio_decoder_factory.h>
#include <webrtc/api/audio_codecs/builtin_audio_encoder_factory.h>
#include <webrtc/api/create_peerconnection_factory.h>
#include <webrtc/modules/audio_processing/include/audio_processing.h>
#include <webrtc/p2p/base/basic_packet_socket_factory.h>
#include <webrtc/p2p/client/basic_port_allocator.h>
// See Bug 274508: Disable thread-safety-reference-return warnings in libwebrtc
IGNORE_CLANG_WARNINGS_BEGIN("thread-safety-reference-return")
#include <webrtc/pc/peer_connection_factory.h>
IGNORE_CLANG_WARNINGS_END
#include <webrtc/pc/peer_connection_factory_proxy.h>
#include <webrtc/rtc_base/physical_socket_server.h>
#include <webrtc/rtc_base/task_queue_gcd.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

#include <wtf/Function.h>
#include <wtf/NeverDestroyed.h>

#if PLATFORM(COCOA)
#include "VP9UtilitiesCocoa.h"
#endif

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(LibWebRTCProvider);

LibWebRTCProvider::LibWebRTCProvider()
{
}

LibWebRTCProvider::~LibWebRTCProvider()
{
}

#if !PLATFORM(COCOA)
void LibWebRTCProvider::registerWebKitVP9Decoder()
{
}
#endif

static inline rtc::SocketAddress prepareSocketAddress(const rtc::SocketAddress& address, bool disableNonLocalhostConnections)
{
    auto result = address;
    if (disableNonLocalhostConnections)
        result.SetIP("127.0.0.1");
    return result;
}

class BasicPacketSocketFactory : public rtc::PacketSocketFactory {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(BasicPacketSocketFactory);
public:
    explicit BasicPacketSocketFactory(rtc::Thread& networkThread)
        : m_socketFactory(makeUniqueRefWithoutFastMallocCheck<rtc::BasicPacketSocketFactory>(networkThread.socketserver()))
    {
    }

    void setDisableNonLocalhostConnections(bool disableNonLocalhostConnections) { m_disableNonLocalhostConnections = disableNonLocalhostConnections; }

    rtc::AsyncPacketSocket* CreateUdpSocket(const rtc::SocketAddress& address, uint16_t minPort, uint16_t maxPort) final
    {
        return m_socketFactory->CreateUdpSocket(prepareSocketAddress(address, m_disableNonLocalhostConnections), minPort, maxPort);
    }

    rtc::AsyncListenSocket* CreateServerTcpSocket(const rtc::SocketAddress&, uint16_t, uint16_t, int) final
    {
        return nullptr;
    }

    rtc::AsyncPacketSocket* CreateClientTcpSocket(const rtc::SocketAddress& localAddress, const rtc::SocketAddress& remoteAddress, const rtc::PacketSocketTcpOptions& options) final
    {
        return m_socketFactory->CreateClientTcpSocket(prepareSocketAddress(localAddress, m_disableNonLocalhostConnections), remoteAddress, options);
    }

    std::unique_ptr<webrtc::AsyncDnsResolverInterface> CreateAsyncDnsResolver() final { return m_socketFactory->CreateAsyncDnsResolver(); }

private:
    bool m_disableNonLocalhostConnections { false };
    const UniqueRef<rtc::BasicPacketSocketFactory> m_socketFactory;
};

struct PeerConnectionFactoryAndThreads {
    std::unique_ptr<rtc::Thread> networkThread;
    std::unique_ptr<rtc::Thread> signalingThread;
    bool networkThreadWithSocketServer { false };
    std::unique_ptr<rtc::NetworkManager> networkManager;
    std::unique_ptr<BasicPacketSocketFactory> packetSocketFactory;
    std::unique_ptr<rtc::RTCCertificateGenerator> certificateGenerator;
};

static void doReleaseLogging(rtc::LoggingSeverity severity, const char* message)
{
#if RELEASE_LOG_DISABLED
    UNUSED_PARAM(severity);
    UNUSED_PARAM(message);
#else
    if (severity == rtc::LS_ERROR)
        RELEASE_LOG_ERROR(WebRTC, "LibWebRTC error: %" PUBLIC_LOG_STRING, message);
    else
        RELEASE_LOG(WebRTC, "LibWebRTC message: %" PUBLIC_LOG_STRING, message);
#endif
}

static rtc::LoggingSeverity computeLogLevel(WTFLogLevel level)
{
#if !RELEASE_LOG_DISABLED
    switch (level) {
    case WTFLogLevel::Always:
    case WTFLogLevel::Error:
        return rtc::LS_ERROR;
    case WTFLogLevel::Warning:
        return rtc::LS_WARNING;
    case WTFLogLevel::Info:
        return rtc::LS_INFO;
    case WTFLogLevel::Debug:
        return rtc::LS_VERBOSE;
    }
    RELEASE_ASSERT_NOT_REACHED();
#else
    UNUSED_PARAM(level);
#endif
    return rtc::LS_NONE;
}

static LibWebRTCLogSink& getRTCLogSink()
{
    static LazyNeverDestroyed<LibWebRTCLogSink> logSink;
    static std::once_flag onceKey;
    std::call_once(onceKey, [&] {
        LibWebRTCLogSink::LogCallback callback = [] (auto&& severity, auto&& message) {
            doReleaseLogging(severity, message.c_str());
        };
        logSink.construct(WTFMove(callback));
    });
    return logSink.get();
}

void LibWebRTCProvider::setRTCLogging(WTFLogLevel level)
{
    getRTCLogSink().start(computeLogLevel(level));
}

static void initializePeerConnectionFactoryAndThreads(PeerConnectionFactoryAndThreads& factoryAndThreads)
{
    ASSERT(!factoryAndThreads.networkThread);

    factoryAndThreads.networkThread = factoryAndThreads.networkThreadWithSocketServer ? rtc::Thread::CreateWithSocketServer() : rtc::Thread::Create();
    factoryAndThreads.networkThread->SetName("WebKitWebRTCNetwork", nullptr);
    bool result = factoryAndThreads.networkThread->Start();
    ASSERT_UNUSED(result, result);

    factoryAndThreads.signalingThread = rtc::Thread::Create();
    factoryAndThreads.signalingThread->SetName("WebKitWebRTCSignaling", nullptr);

    result = factoryAndThreads.signalingThread->Start();
    ASSERT(result);
}

static inline PeerConnectionFactoryAndThreads& staticFactoryAndThreads()
{
    static NeverDestroyed<PeerConnectionFactoryAndThreads> factoryAndThreads;
    return factoryAndThreads.get();
}

PeerConnectionFactoryAndThreads& LibWebRTCProvider::getStaticFactoryAndThreads(bool useNetworkThreadWithSocketServer)
{
    auto& factoryAndThreads = staticFactoryAndThreads();

    ASSERT(!factoryAndThreads.networkThread || factoryAndThreads.networkThreadWithSocketServer == useNetworkThreadWithSocketServer);

    if (!factoryAndThreads.networkThread) {
        factoryAndThreads.networkThreadWithSocketServer = useNetworkThreadWithSocketServer;
        initializePeerConnectionFactoryAndThreads(factoryAndThreads);
        startedNetworkThread();
    }
    return factoryAndThreads;
}

bool LibWebRTCProvider::hasWebRTCThreads()
{
    return !!staticFactoryAndThreads().networkThread;
}

void LibWebRTCProvider::callOnWebRTCNetworkThread(Function<void()>&& callback)
{
    PeerConnectionFactoryAndThreads& threads = staticFactoryAndThreads();
    threads.networkThread->PostTask(WTFMove(callback));
}

void LibWebRTCProvider::callOnWebRTCSignalingThread(Function<void()>&& callback)
{
    PeerConnectionFactoryAndThreads& threads = staticFactoryAndThreads();
    threads.signalingThread->PostTask(WTFMove(callback));
}

rtc::Thread& LibWebRTCProvider::signalingThread()
{
    PeerConnectionFactoryAndThreads& threads = staticFactoryAndThreads();
    return *threads.signalingThread;
}

void LibWebRTCProvider::setLoggingLevel(WTFLogLevel level)
{
    setRTCLogging(level);
}

bool LibWebRTCProvider::isEnumeratingAllNetworkInterfacesEnabled() const
{
    return m_enableEnumeratingAllNetworkInterfaces;
}

void LibWebRTCProvider::disableEnumeratingAllNetworkInterfaces()
{
    m_enableEnumeratingAllNetworkInterfaces = false;
}

void LibWebRTCProvider::enableEnumeratingAllNetworkInterfaces()
{
    m_enableEnumeratingAllNetworkInterfaces = true;
}

void LibWebRTCProvider::enableEnumeratingVisibleNetworkInterfaces()
{
    m_enableEnumeratingVisibleNetworkInterfaces = true;
}

void LibWebRTCProvider::disableNonLocalhostConnections()
{
    m_disableNonLocalhostConnections = true;
}

std::unique_ptr<LibWebRTCProvider::SuspendableSocketFactory> LibWebRTCProvider::createSocketFactory(String&& /* userAgent */, ScriptExecutionContextIdentifier, bool /* isFirstParty */, RegistrableDomain&&)
{
    return nullptr;
}

webrtc::PeerConnectionFactoryInterface* LibWebRTCProvider::factory()
{
    if (m_factory)
        return m_factory.get();

    if (!webRTCAvailable()) {
        RELEASE_LOG_ERROR(WebRTC, "LibWebRTC is not available to create a factory");
        return nullptr;
    }

    auto& factoryAndThreads = getStaticFactoryAndThreads(m_useNetworkThreadWithSocketServer);

    m_factory = createPeerConnectionFactory(factoryAndThreads.networkThread.get(), factoryAndThreads.signalingThread.get());

    return m_factory.get();
}

void LibWebRTCProvider::clearFactory()
{
    m_audioModule = nullptr;
    m_factory = nullptr;

    m_videoDecodingCapabilities = { };
    m_videoEncodingCapabilities = { };
}

rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> LibWebRTCProvider::createPeerConnectionFactory(rtc::Thread* networkThread, rtc::Thread* signalingThread)
{
    willCreatePeerConnectionFactory();

    ASSERT(!m_audioModule);
    m_audioModule = LibWebRTCAudioModule::create();

    return webrtc::CreatePeerConnectionFactory(networkThread, signalingThread, signalingThread, rtc::scoped_refptr<webrtc::AudioDeviceModule>(m_audioModule.get()), webrtc::CreateBuiltinAudioEncoderFactory(), webrtc::CreateBuiltinAudioDecoderFactory(), createEncoderFactory(), createDecoderFactory(), nullptr, nullptr, nullptr, nullptr
#if PLATFORM(COCOA)
        , webrtc::CreateTaskQueueGcdFactory()
#endif
    );
}

std::unique_ptr<webrtc::VideoDecoderFactory> LibWebRTCProvider::createDecoderFactory()
{
    return nullptr;
}

std::unique_ptr<webrtc::VideoEncoderFactory> LibWebRTCProvider::createEncoderFactory()
{
    return nullptr;
}

void LibWebRTCProvider::startedNetworkThread()
{

}

void LibWebRTCProvider::setPeerConnectionFactory(rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>&& factory)
{
    auto* thread = getStaticFactoryAndThreads(m_useNetworkThreadWithSocketServer).signalingThread.get();
    m_factory = webrtc::PeerConnectionFactoryProxy::Create(thread, thread, WTFMove(factory));
}

rtc::scoped_refptr<webrtc::PeerConnectionInterface> LibWebRTCProvider::createPeerConnection(ScriptExecutionContextIdentifier, webrtc::PeerConnectionObserver& observer, rtc::PacketSocketFactory*, webrtc::PeerConnectionInterface::RTCConfiguration&& configuration)
{
    // Default WK1 implementation.
    ASSERT(m_useNetworkThreadWithSocketServer);
    auto& factoryAndThreads = getStaticFactoryAndThreads(m_useNetworkThreadWithSocketServer);

    if (!factoryAndThreads.networkManager)
        factoryAndThreads.networkManager = makeUniqueWithoutFastMallocCheck<rtc::BasicNetworkManager>(factoryAndThreads.networkThread->socketserver());

    if (!factoryAndThreads.packetSocketFactory)
        factoryAndThreads.packetSocketFactory = makeUnique<BasicPacketSocketFactory>(*factoryAndThreads.networkThread);
    factoryAndThreads.packetSocketFactory->setDisableNonLocalhostConnections(m_disableNonLocalhostConnections);

    return createPeerConnection(observer, *factoryAndThreads.networkManager, *factoryAndThreads.packetSocketFactory, WTFMove(configuration), nullptr);
}

void LibWebRTCProvider::setEnableWebRTCEncryption(bool enableWebRTCEncryption)
{
    auto* factory = this->factory();
    if (!factory)
        return;

    webrtc::PeerConnectionFactoryInterface::Options options;
    options.disable_encryption = !enableWebRTCEncryption;
    m_factory->SetOptions(options);
}

rtc::scoped_refptr<webrtc::PeerConnectionInterface> LibWebRTCProvider::createPeerConnection(webrtc::PeerConnectionObserver& observer, rtc::NetworkManager& networkManager, rtc::PacketSocketFactory& packetSocketFactory, webrtc::PeerConnectionInterface::RTCConfiguration&& configuration, std::unique_ptr<webrtc::AsyncDnsResolverFactoryInterface>&& asyncDnsResolverFactory)
{
    auto& factoryAndThreads = getStaticFactoryAndThreads(m_useNetworkThreadWithSocketServer);

    std::unique_ptr<cricket::BasicPortAllocator> portAllocator;
    factoryAndThreads.signalingThread->BlockingCall([&]() {
        auto basicPortAllocator = makeUniqueWithoutFastMallocCheck<cricket::BasicPortAllocator>(&networkManager, &packetSocketFactory);

        basicPortAllocator->set_allow_tcp_listen(false);
        portAllocator = WTFMove(basicPortAllocator);
    });

    auto* factory = this->factory();
    if (!factory)
        return nullptr;

    if (auto portRange = portAllocatorRange())
        portAllocator->SetPortRange(portRange->first, portRange->second);

    webrtc::PeerConnectionDependencies dependencies { &observer };
    dependencies.allocator = WTFMove(portAllocator);
    dependencies.async_dns_resolver_factory = WTFMove(asyncDnsResolverFactory);

    auto peerConnectionOrError = m_factory->CreatePeerConnectionOrError(configuration, WTFMove(dependencies));
    if (!peerConnectionOrError.ok())
        return nullptr;

    return peerConnectionOrError.MoveValue();
}

void LibWebRTCProvider::prepareCertificateGenerator(Function<void(rtc::RTCCertificateGenerator&)>&& callback)
{
    auto& factoryAndThreads = getStaticFactoryAndThreads(m_useNetworkThreadWithSocketServer);
    if (!factoryAndThreads.certificateGenerator)
        factoryAndThreads.certificateGenerator = makeUniqueWithoutFastMallocCheck<rtc::RTCCertificateGenerator>(factoryAndThreads.signalingThread.get(), factoryAndThreads.networkThread.get());

    auto& generator = *factoryAndThreads.certificateGenerator;
    callOnWebRTCSignalingThread([&generator, callback = WTFMove(callback)]() mutable {
        callback(generator);
    });
}

static inline std::optional<cricket::MediaType> typeFromKind(const String& kind)
{
    if (kind == "audio"_s)
        return cricket::MediaType::MEDIA_TYPE_AUDIO;
    if (kind == "video"_s)
        return cricket::MediaType::MEDIA_TYPE_VIDEO;
    return { };
}

static inline RTCRtpCapabilities toRTCRtpCapabilities(const webrtc::RtpCapabilities& rtpCapabilities)
{
    RTCRtpCapabilities capabilities;

    capabilities.codecs = WTF::map(rtpCapabilities.codecs, [](auto& codec) {
        StringBuilder sdpFmtpLineBuilder;
        bool hasParameter = false;
        for (auto& parameter : codec.parameters) {
            sdpFmtpLineBuilder.append(hasParameter ? ";"_s : ""_s, std::span(parameter.first), '=', std::span(parameter.second));
            hasParameter = true;
        }
        String sdpFmtpLine;
        if (sdpFmtpLineBuilder.length())
            sdpFmtpLine = sdpFmtpLineBuilder.toString();
        return RTCRtpCodecCapability { fromStdString(codec.mime_type()), static_cast<uint32_t>(codec.clock_rate ? *codec.clock_rate : 0), codec.num_channels, WTFMove(sdpFmtpLine) };

    });

    capabilities.headerExtensions = WTF::map(rtpCapabilities.header_extensions, [](auto& header) {
        return RTCRtpCapabilities::HeaderExtensionCapability { fromStdString(header.uri) };
    });

    return capabilities;
}

std::optional<RTCRtpCapabilities> LibWebRTCProvider::receiverCapabilities(const String& kind)
{
    auto mediaType = typeFromKind(kind);
    if (!mediaType)
        return { };

    switch (*mediaType) {
    case cricket::MediaType::MEDIA_TYPE_AUDIO:
        return audioDecodingCapabilities();
    case cricket::MediaType::MEDIA_TYPE_VIDEO:
        return videoDecodingCapabilities();
    case cricket::MediaType::MEDIA_TYPE_DATA:
        ASSERT_NOT_REACHED();
        return { };
    case cricket::MediaType::MEDIA_TYPE_UNSUPPORTED:
        ASSERT_NOT_REACHED();
        return { };
    }

    ASSERT_NOT_REACHED();
    return { };
}

void LibWebRTCProvider::initializeAudioDecodingCapabilities()
{
    if (auto* factory = this->factory())
        m_audioDecodingCapabilities = toRTCRtpCapabilities(factory->GetRtpReceiverCapabilities(cricket::MediaType::MEDIA_TYPE_AUDIO));
}

void LibWebRTCProvider::initializeVideoDecodingCapabilities()
{
    if (auto* factory = this->factory())
        m_videoDecodingCapabilities = toRTCRtpCapabilities(factory->GetRtpReceiverCapabilities(cricket::MediaType::MEDIA_TYPE_VIDEO));
}

std::optional<RTCRtpCapabilities> LibWebRTCProvider::senderCapabilities(const String& kind)
{
    auto mediaType = typeFromKind(kind);
    if (!mediaType)
        return { };

    switch (*mediaType) {
    case cricket::MediaType::MEDIA_TYPE_AUDIO:
        return audioEncodingCapabilities();
    case cricket::MediaType::MEDIA_TYPE_VIDEO:
        return videoEncodingCapabilities();
    case cricket::MediaType::MEDIA_TYPE_DATA:
        ASSERT_NOT_REACHED();
        return { };
    case cricket::MediaType::MEDIA_TYPE_UNSUPPORTED:
        ASSERT_NOT_REACHED();
        return { };
    }
    ASSERT_NOT_REACHED();
    return { };
}

void LibWebRTCProvider::initializeAudioEncodingCapabilities()
{
    if (auto* factory = this->factory())
        m_audioEncodingCapabilities = toRTCRtpCapabilities(factory->GetRtpSenderCapabilities(cricket::MediaType::MEDIA_TYPE_AUDIO));
}

void LibWebRTCProvider::initializeVideoEncodingCapabilities()
{
    if (auto* factory = this->factory())
        m_videoEncodingCapabilities = toRTCRtpCapabilities(factory->GetRtpSenderCapabilities(cricket::MediaType::MEDIA_TYPE_VIDEO));
}

std::optional<MediaCapabilitiesDecodingInfo> LibWebRTCProvider::videoDecodingCapabilitiesOverride(const VideoConfiguration& configuration)
{
    MediaCapabilitiesDecodingInfo info;
    ContentType contentType { configuration.contentType };
    auto containerType = contentType.containerType();
    if (equalLettersIgnoringASCIICase(containerType, "video/vp8"_s)) {
        info.powerEfficient = false;
        info.smooth = isVPSoftwareDecoderSmooth(configuration);
    } else if (equalLettersIgnoringASCIICase(containerType, "video/vp9"_s)) {
        auto decodingInfo = computeVPParameters(configuration);
        if (decodingInfo && !decodingInfo->supported && isSupportingVP9HardwareDecoder()) {
            info.supported = false;
            return { info };
        }
        info.powerEfficient = decodingInfo ? decodingInfo->powerEfficient : isSupportingVP9HardwareDecoder();
        info.smooth = decodingInfo ? decodingInfo->smooth : isVPSoftwareDecoderSmooth(configuration);
    } else if (equalLettersIgnoringASCIICase(containerType, "video/h264"_s))
        info.powerEfficient = info.smooth = true;
    else if (equalLettersIgnoringASCIICase(containerType, "video/h265"_s))
        info.powerEfficient = info.smooth = true;
    else if (equalLettersIgnoringASCIICase(containerType, "video/av1"_s)) {
        // FIXME: Set value to true if AV1 is only enabled when HW decoder support is enabled.
        info.powerEfficient = false;
    }

    info.supported = true;
    return { info };
}

std::optional<MediaCapabilitiesEncodingInfo> LibWebRTCProvider::videoEncodingCapabilitiesOverride(const VideoConfiguration& configuration)
{
    MediaCapabilitiesEncodingInfo info;
    ContentType contentType { configuration.contentType };
    auto containerType = contentType.containerType();
    if (equalLettersIgnoringASCIICase(containerType, "video/vp8"_s) || equalLettersIgnoringASCIICase(containerType, "video/vp9"_s))
        info.powerEfficient = info.smooth = isVPXEncoderSmooth(configuration);
    else if (equalLettersIgnoringASCIICase(containerType, "video/h264"_s))
        info.powerEfficient = info.smooth = isH264EncoderSmooth(configuration);
    else if (equalLettersIgnoringASCIICase(containerType, "video/h265"_s))
        info.powerEfficient = info.smooth = true;
    else if (equalLettersIgnoringASCIICase(containerType, "video/av1"_s))
        info.powerEfficient = info.smooth = false;

    info.supported = true;
    info.configuration.type = MediaEncodingType::WebRTC;
    return { info };
}

} // namespace WebCore

#endif // USE(LIBWEBRTC)
