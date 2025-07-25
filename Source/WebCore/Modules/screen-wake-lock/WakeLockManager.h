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

#include "PageIdentifier.h"
#include "VisibilityChangeClient.h"
#include "WakeLockType.h"
#include <wtf/HashMap.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/Vector.h>
#include <wtf/WeakPtr.h>

namespace WebCore {

class Document;
class SleepDisabler;
class WakeLockSentinel;

class WakeLockManager final : public VisibilityChangeClient {
    WTF_MAKE_TZONE_ALLOCATED(WakeLockManager);
public:
    explicit WakeLockManager(Document&);
    ~WakeLockManager();

    void ref() const final;
    void deref() const final;

    void addWakeLock(Ref<WakeLockSentinel>&&, std::optional<PageIdentifier>);
    void removeWakeLock(WakeLockSentinel&);

    void releaseAllLocks(WakeLockType);

private:
    void visibilityStateChanged() final;

    const CheckedRef<Document> m_document;
    HashMap<WakeLockType, Vector<RefPtr<WakeLockSentinel>>, WTF::IntHash<WakeLockType>, WTF::StrongEnumHashTraits<WakeLockType>> m_wakeLocks;
    std::unique_ptr<SleepDisabler> m_screenLockDisabler;
};

} // namespace WebCore
