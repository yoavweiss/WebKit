/*
 * Copyright (C) 2025 Apple Inc. All rights reserved.
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

#pragma once

#include <WebCore/FloatSize.h>
#include <WebCore/MediaTimeUpdateData.h>
#include <wtf/MediaTime.h>
#include <wtf/Seconds.h>

namespace WebKit {

// Cadence at which the GPU-side proxy throttles outgoing TimeObserverUpdate
// IPCs to the WebContent process during steady-state playback. The
// WebContent-side TimeProgressEstimator also caps its between-anchor
// extrapolation to this interval.
constexpr Seconds remoteAudioVideoRendererUpdateInterval = 250_ms;

// Underlying GPU-side time observer cadence. Set shorter than the steady-state
// interval so the first tick after each play() lands quickly to confirm that
// playback has started; subsequent ticks are throttled by the proxy back to
// remoteAudioVideoRendererUpdateInterval.
constexpr Seconds remoteAudioVideoRendererFirstTickInterval = 100_ms;

struct RemoteAudioVideoRendererState {
    WebCore::MediaTimeUpdateData timeUpdateData { };
    bool paused { false };
};

}
