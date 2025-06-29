/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
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

#include <wtf/Function.h>
#include <wtf/RefCountedAndCanMakeWeakPtr.h>
#include <wtf/UniqueRef.h>
#include <wtf/WeakPtr.h>

namespace WTF {

class CancellableTask;

class TaskCancellationGroupImpl final : public RefCountedAndCanMakeWeakPtr<TaskCancellationGroupImpl> {
public:
    static Ref<TaskCancellationGroupImpl> create()
    {
        return adoptRef(*new TaskCancellationGroupImpl);
    }
    void cancel() { weakPtrFactory().revokeAll(); }
    bool hasPendingTask() const { return weakPtrFactory().weakPtrCount(); }

private:
    TaskCancellationGroupImpl() = default;
};

class TaskCancellationGroupHandle {
public:
    bool isCancelled() const { return !m_impl; }
    void clear() { m_impl = nullptr; }
private:
    friend class TaskCancellationGroup;
    explicit TaskCancellationGroupHandle(TaskCancellationGroupImpl& impl)
        : m_impl(impl)
    {
    }
    WeakPtr<TaskCancellationGroupImpl> m_impl;
};

class TaskCancellationGroup {
public:
    TaskCancellationGroup()
        : m_impl(TaskCancellationGroupImpl::create())
    {
    }
    void cancel() { m_impl->cancel(); }
    bool hasPendingTask() const { return m_impl->hasPendingTask(); }

private:
    friend class CancellableTask;
    TaskCancellationGroupHandle createHandle() { return TaskCancellationGroupHandle { m_impl }; }

    const Ref<TaskCancellationGroupImpl> m_impl;
};

class CancellableTask {
public:
    CancellableTask(TaskCancellationGroup&, Function<void()>&&);
    void operator()();

private:
    TaskCancellationGroupHandle m_cancellationGroup;
    Function<void()> m_task;
};

inline CancellableTask::CancellableTask(TaskCancellationGroup& cancellationGroup, Function<void()>&& task)
    : m_cancellationGroup(cancellationGroup.createHandle())
    , m_task(WTFMove(task))
{ }

inline void CancellableTask::operator()()
{
    if (m_cancellationGroup.isCancelled())
        return;

    m_cancellationGroup.clear();
    m_task();
}

} // namespace WTF

using WTF::CancellableTask;
using WTF::TaskCancellationGroup;
