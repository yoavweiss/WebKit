/*
 * Copyright (C) 2026 Apple Inc. All rights reserved.
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
#include "SharedTimebase.h"

#include <WebCore/SharedMemory.h>
#include <wtf/MediaTime.h>
#include <wtf/MonotonicTime.h>
#include <wtf/SequenceLocked.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

namespace {
using SnapshotBuffer = SequenceLocked<SharedTimebase::Snapshot>;

SnapshotBuffer& snapshotBufferIn(SharedMemory& storage)
{
    return spanReinterpretCast<SnapshotBuffer>(storage.mutableSpan().first(sizeof(SnapshotBuffer))).front();
}
}

struct SharedTimebase::Impl {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(Impl);
public:
    Ref<SharedMemory> storage;
};

void SharedTimebaseHandle::takeOwnershipOfMemory(MemoryLedger ledger)
{
    memory.takeOwnershipOfMemory(ledger);
}

WTF_MAKE_TZONE_ALLOCATED_IMPL(SharedTimebase);
std::unique_ptr<SharedTimebase> SharedTimebase::create()
{
    RefPtr sharedMemory = SharedMemory::allocate(sizeof(SnapshotBuffer));
    if (!sharedMemory || sharedMemory->size() < sizeof(SnapshotBuffer))
        return nullptr;
    auto timebase = makeUnique<SharedTimebase>(sharedMemory.releaseNonNull());
    timebase->storeSnapshot({ });
    return timebase;
}

SharedTimebase::SharedTimebase(Ref<SharedMemory>&& memory)
    : m_impl { makeUniqueRef<SharedTimebase::Impl>(SharedTimebase::Impl {
        .storage = WTF::move(memory)
    }) }
{
}

SharedTimebase::~SharedTimebase() = default;

auto SharedTimebase::createHandle() const -> std::optional<Handle>
{
    if (auto handle = protect(m_impl->storage)->createHandle(SharedMemoryProtection::ReadOnly)) {
        return Handle {
            .memory = WTF::move(*handle)
        };
    }
    return std::nullopt;
}

void SharedTimebase::storeSnapshot(Snapshot snapshot)
{
    snapshotBufferIn(m_impl->storage).store(snapshot);
}

struct SharedTimebaseReader::Impl {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(Impl);
public:
    Ref<SharedMemory> storage;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(SharedTimebaseReader);

std::unique_ptr<SharedTimebaseReader> SharedTimebaseReader::create(SharedTimebase::Handle&& handle, Seconds maxExtrapolation, Function<MonotonicTime()>&& clock)
{
    RefPtr sharedMemory = SharedMemory::map(WTF::move(handle.memory), SharedMemoryProtection::ReadOnly);
    if (!sharedMemory || sharedMemory->size() < sizeof(SnapshotBuffer))
        return nullptr;
    return std::unique_ptr<SharedTimebaseReader>(new SharedTimebaseReader(sharedMemory.releaseNonNull(), maxExtrapolation, WTF::move(clock)));
}

SharedTimebaseReader::SharedTimebaseReader(Ref<SharedMemory>&& storage, Seconds maxExtrapolation, Function<MonotonicTime()>&& clock)
    : m_impl { makeUniqueRef<SharedTimebaseReader::Impl>(SharedTimebaseReader::Impl {
        .storage = WTF::move(storage)
    }) }
    , m_maxExtrapolation(maxExtrapolation)
    , m_clock(clock ? WTF::move(clock) : Function<MonotonicTime()> { [] { return MonotonicTime::now(); } })
{
}

SharedTimebaseReader::~SharedTimebaseReader() = default;

MediaTime SharedTimebaseReader::currentTime() const
{
    auto snapshot = snapshotBufferIn(m_impl->storage).load();

    auto rate = snapshot.playbackRate;
    MediaTime calculated;
    if (!rate)
        calculated = snapshot.currentTime;
    else {
        auto elapsed = std::min(m_clock() - snapshot.hostTime, m_maxExtrapolation);
        calculated = snapshot.currentTime + MediaTime::createWithDouble(rate * elapsed.seconds());
    }

    if (rate >= 0)
        calculated = std::max(m_lastReturnedTime.value_or(calculated), calculated);
    else
        calculated = std::min(m_lastReturnedTime.value_or(calculated), calculated);

    m_lastReturnedTime = calculated;
    return calculated;
}

double SharedTimebaseReader::currentRate() const
{
    return snapshotBufferIn(m_impl->storage).load().playbackRate;
}

void SharedTimebaseReader::resetForTimeDiscontinuity()
{
    m_lastReturnedTime.reset();
}

}
