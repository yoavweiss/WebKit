/*
 * Copyright (C) 2015-2017 Apple Inc. All rights reserved.
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

#include <span>
#include <wtf/Box.h>
#include <wtf/FileHandle.h>
#include <wtf/FileSystem.h>
#include <wtf/MappedFileData.h>
#include <wtf/SHA1.h>
#include <wtf/ThreadSafeRefCounted.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

#if PLATFORM(COCOA)
#include <wtf/OSObjectPtr.h>
#endif

#if USE(GLIB)
#include <wtf/glib/GRefPtr.h>
#endif

#if USE(CURL)
#include <wtf/Box.h>
#endif

namespace WebCore {
class SharedMemory;
}

namespace WebKit {

namespace NetworkCache {

class Data {
public:
    Data() { }
    Data(std::span<const uint8_t>);
    Data(Vector<uint8_t>&& data);

    ~Data() { }

    static Data empty();
    static Data adoptMap(FileSystem::MappedFileData&&, FileSystem::FileHandle&&);

#if PLATFORM(COCOA)
    enum class Backing { Buffer, Map };
    Data(OSObjectPtr<dispatch_data_t>&&, Backing = Backing::Buffer);
#endif
#if USE(GLIB)
    Data(GRefPtr<GBytes>&&, FileSystem::FileHandle&& = { });
#elif USE(CURL)
    Data(Variant<Vector<uint8_t>, FileSystem::MappedFileData>&&);
#endif
    bool isNull() const;
    bool isEmpty() const { return !size(); }

    std::span<const uint8_t> span() const LIFETIME_BOUND;
    size_t size() const;
    bool isMap() const { return m_isMap; }
    RefPtr<WebCore::SharedMemory> tryCreateSharedMemory() const;

    Data subrange(size_t offset, size_t) const;

    bool apply(NOESCAPE const Function<bool(std::span<const uint8_t>)>&) const;

    Data mapToFile(const String& path) const;

#if PLATFORM(COCOA)
    dispatch_data_t dispatchData() const { return m_dispatchData.get(); }
#endif

#if USE(GLIB)
    GBytes* bytes() const { return m_buffer.get(); }
#endif
private:
#if PLATFORM(COCOA)
    mutable OSObjectPtr<dispatch_data_t> m_dispatchData;
    mutable std::span<const uint8_t> m_data;
#endif
#if USE(GLIB)
    mutable GRefPtr<GBytes> m_buffer;
    Box<FileSystem::FileHandle> m_fileHandle;
#endif
#if USE(CURL)
    Box<Variant<Vector<uint8_t>, FileSystem::MappedFileData>> m_buffer;
#endif
    bool m_isMap { false };
};

Data concatenate(const Data&, const Data&);
bool bytesEqual(const Data&, const Data&);
Data mapFile(const String& path);

using Salt = FileSystem::Salt;
SHA1::Digest computeSHA1(const Data&, const Salt&);

}

}
