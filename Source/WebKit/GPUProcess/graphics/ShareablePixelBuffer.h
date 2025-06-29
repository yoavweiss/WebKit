/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <WebCore/PixelBuffer.h>

namespace WebCore {
class SharedMemory;
}

namespace WebKit {

class ShareablePixelBuffer : public WebCore::PixelBuffer {
public:
    static RefPtr<ShareablePixelBuffer> tryCreate(const WebCore::PixelBufferFormat&, const WebCore::IntSize&);

    WebCore::SharedMemory& data() const { return m_data.get(); }
    Ref<WebCore::SharedMemory> protectedData() const;

    RefPtr<WebCore::PixelBuffer> createScratchPixelBuffer(const WebCore::IntSize&) const override;

private:
    ShareablePixelBuffer(const WebCore::PixelBufferFormat&, const WebCore::IntSize&, Ref<WebCore::SharedMemory>&&);

    Ref<WebCore::SharedMemory> m_data;
};

} // namespace WebKit
