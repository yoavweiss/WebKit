/*
 * Copyright (C) 2026 Igalia, S.L.
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
#include <wtf/AutomaticThread.h>

#include <wtf/Condition.h>
#include <wtf/DataMutex.h>
#include <wtf/Deque.h>
#include <wtf/ThreadSpecific.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/Variant.h>

struct Invalid { };
struct Trivial { };
struct Exit { };
struct WaitOn {
    WaitOn(Box<Lock> lock, Condition* condition)
        : m_lock(WTF::move(lock))
        , m_condition(condition)
    {
    }
    Box<Lock> m_lock;
    Condition* m_condition;
};
using WorkItem = WTF::Variant<Invalid, Trivial, Exit, struct WaitOn*>;

struct CachedData {
    CachedData()
    {
        Locker locker { s_lock };
        s_numberOfCachedData++;
        s_condition.notifyAll();
    }
    ~CachedData()
    {
        Locker locker { s_lock };
        --s_numberOfCachedData;
        s_condition.notifyAll();
    }
    static void waitUntil(size_t count)
    {
        Locker locker { s_lock };
        while (s_numberOfCachedData != count)
            s_condition.wait(s_lock);
    }
    int m_value;
    static Lock s_lock;
    static size_t s_numberOfCachedData WTF_GUARDED_BY_LOCK(s_lock);
    static Condition s_condition;
};
using ThreadSpecificCachedData = ThreadSpecific<CachedData>;

size_t CachedData::s_numberOfCachedData;
Lock CachedData::s_lock;
Condition CachedData::s_condition { };

ThreadSpecificCachedData& threadSpecificCachedData()
{
    static ThreadSpecificCachedData* data;
    static std::once_flag flag;
    std::call_once(
        flag,
        []() {
            data = new ThreadSpecificCachedData();
        });
    return *data;
}

class WorkItemConsumerThread : public AutomaticThread {
    WTF_MAKE_TZONE_ALLOCATED(WorkItemConsumerThread);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(WorkItemConsumerThread);
public:
    WorkItemConsumerThread(const AbstractLocker& locker, Box<Lock> lock, Ref<AutomaticThreadCondition>&& condition, Seconds timeout)
        : AutomaticThread(locker, WTF::move(lock), WTF::move(condition), timeout)
        , m_started(0)
        , m_stopped(0)
        , m_lock()
        , m_runningCondition()
        , m_currentItem(Invalid { })
        , m_numberOfSubmittedItems(0)
        , m_numberOfProcessedItems(0)
    {
    }
    void waitUntilHasStopped(unsigned times)
    {
        for (;;) {
            Locker locker { m_lock };
            if (m_stopped >= times)
                return;
            m_runningCondition.wait(m_lock);
        }
    }
    void waitUntilHasStarted(unsigned times)
    {
        for (;;) {
            Locker locker { m_lock };
            if (m_started >= times)
                return;
            m_runningCondition.wait(m_lock);
        }
    }
    void appendWorkItem(WorkItem item)
    {
        DataMutexLocker workItems { m_workItems };
        workItems->append(item);
        ++m_numberOfSubmittedItems;
    }
private:
    PollResult poll(const AbstractLocker&)
    {
        DataMutexLocker workItems { m_workItems };
        if (workItems->isEmpty())
            return PollResult::Wait;
        m_currentItem = workItems->takeFirst();
        return PollResult::Work;
    }
    WorkResult work()
    {
        threadSpecificCachedData()->m_value++;
        RELEASE_ASSERT(!std::get_if<Invalid>(&m_currentItem));
        ++m_numberOfProcessedItems;
        if (std::get_if<Trivial>(&m_currentItem)) {
            m_currentItem = Invalid { };
            return WorkResult::Continue;
        }
        if (std::get_if<Exit>(&m_currentItem))
            return WorkResult::Stop;
        if (auto waitOn = std::get_if<WaitOn*>(&m_currentItem)) {
            Locker locker { *(*waitOn)->m_lock };
            (*waitOn)->m_condition->wait(*(*waitOn)->m_lock);
            // Wait twice. This gives outsiders the chance to do things knowing
            // we're waiting here.
            (*waitOn)->m_condition->wait(*(*waitOn)->m_lock);
            return WorkResult::Continue;
        }
        RELEASE_ASSERT_NOT_REACHED();
    }
    void threadDidStart()
    {
        Locker locker { m_lock };

        ++m_started;
        m_runningCondition.notifyAll();
    }
    void threadIsStopping(const AbstractLocker&)
    {
        Locker locker { m_lock };
        ++m_stopped;
        RELEASE_ASSERT(m_started == m_stopped);
        m_runningCondition.notifyAll();
        ASSERT_EQ(m_numberOfSubmittedItems, m_numberOfProcessedItems);
    }
    unsigned m_started;
    unsigned m_stopped;
    DataMutex<Deque<WorkItem>> m_workItems;
    Lock m_lock;
    Condition m_runningCondition;
    WorkItem m_currentItem;
    size_t m_numberOfSubmittedItems;
    size_t m_numberOfProcessedItems;

};

WTF_MAKE_TZONE_ALLOCATED_IMPL(WorkItemConsumerThread);

TEST(WTF, AutomaticThreadStopsWhenNotGivenWork)
{
    auto lock = Box<Lock>::create();
    auto condition = AutomaticThreadCondition::create();
    RefPtr<WorkItemConsumerThread> thread;
    {
        Locker locker { *lock };
        thread = adoptRef(new WorkItemConsumerThread(locker, lock, condition.copyRef(), 1_s));
        condition->notifyOne(locker);
    }
    // Test that the thread stops when it hasn't ever been given work.
    thread->waitUntilHasStopped(1);
    CachedData::waitUntil(0);

    // Test that the thread stops after processing work.
    thread->appendWorkItem(Trivial { });
    {
        Locker locker { *lock };
        condition->notifyOne(locker);
    }
    thread->waitUntilHasStopped(2);

    thread->appendWorkItem(Exit { });
    {
        Locker locker { *lock };
        condition->notifyOne(locker);
    }
    thread->waitUntilHasStopped(3);
    CachedData::waitUntil(0);
    thread = nullptr;
}

TEST(WTF, AutomaticThreadTemporaryStop)
{
    auto lock = Box<Lock>::create();
    auto condition = AutomaticThreadCondition::create();
    RefPtr<WorkItemConsumerThread> thread;
    CachedData::waitUntil(0);

    {
        Locker locker { *lock };
        // Note the timeout: will never stop on its own.
        thread = adoptRef(new WorkItemConsumerThread(locker, lock, condition.copyRef(), Seconds::infinity()));
        thread->appendWorkItem(Trivial { });
        condition->notifyOne(locker);
    }
    thread->waitUntilHasStarted(1);
    CachedData::waitUntil(1); // ::work has run, i.e. we should have an underlying thread.
    {
        Locker locker { *lock };
        ASSERT_TRUE(thread->hasUnderlyingThread(locker));
        thread->requestTemporaryStop(locker);
    }
    thread->waitUntilHasStopped(1);
    CachedData::waitUntil(0);

    {
        Locker locker { *lock };
        ASSERT_FALSE(thread->hasUnderlyingThread(locker));
    }

    // Does it start again?
    thread->appendWorkItem(Trivial { });
    {
        Locker locker { *lock };
        condition->notifyOne(locker);
    }
    thread->waitUntilHasStarted(2);

    {
        Locker locker { *lock };
        // Shut it down.
        thread->appendWorkItem(Exit { });
        condition->notifyOne(locker);
    }
    thread->waitUntilHasStopped(2);
    CachedData::waitUntil(0);
    thread = nullptr;
}

TEST(WTF, AutomaticThreadTemporaryStopWhileRunning)
{
    auto lock = Box<Lock>::create();
    auto condition = AutomaticThreadCondition::create();
    auto itemLock = Box<Lock>::create();
    Condition* itemCondition = new Condition();
    RefPtr<WorkItemConsumerThread> thread;
    CachedData::waitUntil(0);
    {
        Locker locker { *lock };
        // Note the timeout: will never stop on its own.
        thread = adoptRef(new WorkItemConsumerThread(locker, lock, condition.copyRef(), Seconds::infinity()));
        Locker itemLocker { *itemLock };
        thread->appendWorkItem(new WaitOn { itemLock, itemCondition });
        condition->notifyOne(locker);
    }
    thread->waitUntilHasStarted(1);

    // The thread should block on the WaitOn item in its ::work soon.
    while (!itemCondition->notifyOne())
        Thread::yield();

    // OK, now we know the thread is in ::work.

    // It must have created thread-specific data.
    {
        Locker locker { CachedData::s_lock };
        ASSERT_EQ(CachedData::s_numberOfCachedData, 1);
    }
    // Request the temporary stop.
    {
        Locker locker { *lock };
        thread->requestTemporaryStop(locker);
    }

    // Allow the thread to complete its work.
    while (!itemCondition->notifyOne())
        Thread::yield();

    // Now the thread should stop. This can only happen as a result of our
    // request (timeout is set to infinity).
    thread->waitUntilHasStopped(1);
    CachedData::waitUntil(0);

    {
        Locker locker { *lock };
        // Shut it down.
        thread->appendWorkItem(Exit { });
        condition->notifyOne(locker);
    }
    thread->waitUntilHasStopped(2);
    CachedData::waitUntil(0);
    thread = nullptr;
}
