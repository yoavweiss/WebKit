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


#include "config.h"
#include "StreamTeeUtilities.h"

#include "JSDOMPromise.h"
#include "JSDOMPromiseDeferred.h"
#include "JSReadableStreamReadResult.h"
#include "ReadableByteStreamController.h"
#include "ReadableStream.h"
#include "ReadableStreamBYOBReader.h"
#include "ReadableStreamBYOBRequest.h"
#include "ReadableStreamDefaultReader.h"

namespace WebCore {

class StreamTeeState : public RefCountedAndCanMakeWeakPtr<StreamTeeState> {
public:
    template<typename Reader>
    static Ref<StreamTeeState> create(JSDOMGlobalObject& globalObject, Ref<ReadableStream>&& stream, Ref<Reader>&& reader)
    {
        auto [cancelPromise, cancelDeferred] = createPromiseAndWrapper(globalObject);
        return adoptRef(*new StreamTeeState(WTFMove(stream), WTFMove(reader), WTFMove(cancelDeferred), WTFMove(cancelPromise)));
    }

    ~StreamTeeState();

    bool isReader(const ReadableStreamDefaultReader* thisReader) const { return m_defaultReader && m_defaultReader.get() == thisReader; }
    bool isReader(const ReadableStreamBYOBReader* thisReader) const { return m_byobReader && m_byobReader.get() == thisReader; }

    bool reading() const { return m_reading; }
    void setReading(bool value) { m_reading = value; }

    bool readAgainForBranch1() const { return m_readAgainForBranch1; }
    void setReadAgainForBranch1(bool value) { m_readAgainForBranch1 = value; }

    bool readAgainForBranch2() const { return m_readAgainForBranch2; }
    void setReadAgainForBranch2(bool value) { m_readAgainForBranch2 = value; }

    bool canceled1() const { return m_canceled1; }
    bool canceled2() const { return m_canceled2; }
    void setCanceled1() { m_canceled1 = true; }
    void setCanceled2() { m_canceled2 = true; }
    JSC::Strong<JSC::Unknown> takeReason1() { return std::exchange(m_branch1Reason, { }); }
    JSC::Strong<JSC::Unknown> takeReason2() { return std::exchange(m_branch2Reason, { }); }
    void setReason1(JSDOMGlobalObject& globalObject, JSC::JSValue value)
    {
        Ref vm = globalObject.vm();
        m_branch1Reason = { vm, value };
    }
    void setReason2(JSDOMGlobalObject& globalObject, JSC::JSValue value)
    {
        Ref vm = globalObject.vm();
        m_branch2Reason = { vm, value };
    }

    ReadableStream& stream() const { return m_stream; }
    ReadableStream* branch1() const { return m_branch1.get(); }
    ReadableStream* branch2() const { return m_branch2.get(); }
    void setBranch1(ReadableStream& stream) { m_branch1 = &stream; }
    void setBranch2(ReadableStream& stream) { m_branch2 = &stream; }

    DOMPromise* readPromise() const { return m_readPromise.get(); }
    void setReadPromise(Ref<DOMPromise>&& promise)
    {
        ASSERT(!m_readPromise || m_readPromise->status() != DOMPromise::Status::Pending);
        m_readPromise = WTFMove(promise);
    }

    ReadableStreamBYOBReader* byobReader() const { return m_byobReader.get(); }
    RefPtr<ReadableStreamBYOBReader> takeBYOBReader() { return std::exchange(m_byobReader, { }); }
    void setReader(Ref<ReadableStreamBYOBReader>&& reader)
    {
        ASSERT(!m_defaultReader);
        ASSERT(!m_byobReader);
        m_byobReader = WTFMove(reader);
    }

    ReadableStreamDefaultReader* defaultReader() const { return m_defaultReader.get(); }
    RefPtr<ReadableStreamDefaultReader> takeDefaultReader() { return std::exchange(m_defaultReader, { }); }
    void setReader(Ref<ReadableStreamDefaultReader>&& reader)
    {
        ASSERT(!m_defaultReader);
        ASSERT(!m_byobReader);
        m_defaultReader = WTFMove(reader);
    }

    DOMPromise& cancelPromise() { return m_cancelPromise; }

    void resolveCancelPromise()
    {
        Ref { m_cancelDeferredPromise }->resolve();
    }

    void rejectCancelPromise(JSC::JSValue value)
    {
        Ref { m_cancelDeferredPromise }->rejectWithCallback([&](auto&) {
            return value;
        });
    }

    template<typename Reader>
    void forwardReadError(Reader& thisReader)
    {
        thisReader.onClosedPromiseRejection([weakThis = WeakPtr { *this }, thisReader = WeakPtr { thisReader }](auto& globalObject, auto&& reason) {
            RefPtr protectedThis = weakThis.get();
            if (!protectedThis || !protectedThis->isReader(thisReader.get()))
                return;

            if (RefPtr branch1 = protectedThis->branch1())
                branch1->controller()->error(globalObject, reason);
            if (RefPtr branch2 = protectedThis->branch2())
                branch2->controller()->error(globalObject, reason);
            if (!protectedThis->canceled1() || !protectedThis->canceled2())
                protectedThis->resolveCancelPromise();
        });
    }

private:
    StreamTeeState(Ref<ReadableStream>&& stream, Ref<ReadableStreamDefaultReader>&& reader, Ref<DeferredPromise>&& cancelDeferred, Ref<DOMPromise>&& cancelPromise)
        : m_stream(WTFMove(stream))
        , m_defaultReader(WTFMove(reader))
        , m_cancelDeferredPromise(WTFMove(cancelDeferred))
        , m_cancelPromise(WTFMove(cancelPromise))
    {
    }

    StreamTeeState(Ref<ReadableStream>&& stream, Ref<ReadableStreamBYOBReader>&& reader, Ref<DeferredPromise>&& cancelDeferred, Ref<DOMPromise>&& cancelPromise)
        : m_stream(WTFMove(stream))
        , m_byobReader(WTFMove(reader))
        , m_cancelDeferredPromise(WTFMove(cancelDeferred))
        , m_cancelPromise(WTFMove(cancelPromise))
    {
    }

    const Ref<ReadableStream> m_stream;
    RefPtr<ReadableStreamDefaultReader> m_defaultReader;
    RefPtr<ReadableStreamBYOBReader> m_byobReader;
    bool m_reading = false;
    bool m_readAgainForBranch1 = false;
    bool m_readAgainForBranch2 = false;
    bool m_canceled1 = false;
    bool m_canceled2 = false;
    Ref<DeferredPromise> m_cancelDeferredPromise;
    Ref<DOMPromise> m_cancelPromise;
    RefPtr<ReadableStream> m_branch1;
    RefPtr<ReadableStream> m_branch2;

    // FIXME: we should probably have m_stream mark m_branch1Reason and m_branch2Reason instead of taking strong.
    JSC::Strong<JSC::Unknown> m_branch1Reason;
    JSC::Strong<JSC::Unknown> m_branch2Reason;

    RefPtr<DOMPromise> m_readPromise;
};

// https://streams.spec.whatwg.org/#abstract-opdef-readablebytestreamtee
ExceptionOr<Vector<Ref<ReadableStream>>> byteStreamTee(JSDOMGlobalObject& globalObject, ReadableStream& stream)
{
    ASSERT(stream.controller());

    auto readerOrException = ReadableStreamDefaultReader::create(globalObject, stream);
    if (readerOrException.hasException())
        return readerOrException.releaseException();

    Ref reader = readerOrException.releaseReturnValue();
    Ref state = StreamTeeState::create(globalObject, stream, reader.copyRef());

    ReadableByteStreamController::PullAlgorithm pull1Algorithm = [state = Ref { state }](auto& globalObject, auto&&) {
        return pull1Steps(globalObject, state, Ref { *state->branch1() });
    };

    ReadableByteStreamController::PullAlgorithm pull2Algorithm = [state = Ref { state }](auto& globalObject, auto&&) {
        return pull2Steps(globalObject, state, Ref { *state->branch2() });
    };

    ReadableByteStreamController::CancelAlgorithm cancel1Algorithm = [state = Ref { state }](auto& globalObject, auto&&, auto&& reason) {
        state->setCanceled1();
        state->setReason1(globalObject, reason.value_or(JSC::jsUndefined()));

        if (state->canceled2()) {
            // Create the array of reason1 and reason2.
            JSC::MarkedArgumentBuffer list;
            list.ensureCapacity(2);
            list.append(state->takeReason1().get());
            list.append(state->takeReason2().get());
            JSC::JSValue reason = JSC::constructArray(&globalObject, static_cast<JSC::ArrayAllocationProfile*>(nullptr), list);

            auto [promise, deferred] = createPromiseAndWrapper(globalObject);
            state->stream().cancel(globalObject, reason, WTFMove(deferred));
            promise->whenSettled([state, promise] {
                if (promise->status() == DOMPromise::Status::Rejected) {
                    state->rejectCancelPromise(promise->result());
                    return;
                }
                state->resolveCancelPromise();
            });
        }
        return Ref { state->cancelPromise() };
    };

    ReadableByteStreamController::CancelAlgorithm cancel2Algorithm = [state = Ref { state }](auto& globalObject, auto&&, auto&& reason) {
        state->setCanceled2();
        state->setReason2(globalObject, reason.value_or(JSC::jsUndefined()));

        if (state->canceled1()) {
            // Create the array of reason1 and reason2.
            JSC::MarkedArgumentBuffer list;
            list.ensureCapacity(2);
            list.append(state->takeReason1().get());
            list.append(state->takeReason2().get());
            JSC::JSValue reason = JSC::constructArray(&globalObject, static_cast<JSC::ArrayAllocationProfile*>(nullptr), list);

            auto [promise, deferred] = createPromiseAndWrapper(globalObject);
            state->stream().cancel(globalObject, reason, WTFMove(deferred));
            promise->whenSettled([state, promise] {
                if (promise->status() == DOMPromise::Status::Rejected) {
                    state->rejectCancelPromise(promise->result());
                    return;
                }
                state->resolveCancelPromise();
            });
        }
        return Ref { state->cancelPromise() };
    };

    Vector<Ref<ReadableStream>> branches;
    branches.append(ReadableStream::createReadableByteStream(globalObject, WTFMove(pull1Algorithm), WTFMove(cancel1Algorithm)));
    branches.append(ReadableStream::createReadableByteStream(globalObject, WTFMove(pull2Algorithm), WTFMove(cancel2Algorithm)));

    state->setBranch1(branches[0]);
    state->setBranch2(branches[1]);

    state->forwardReadError(reader.get());

    return branches;
}

StreamTeeState::~StreamTeeState() = default;

static ExceptionOr<Ref<JSC::ArrayBufferView>> cloneAsUInt8Array(JSC::ArrayBufferView& o)
{
    RefPtr buffer = JSC::ArrayBuffer::tryCreate(o.span());
    if (!buffer)
        return Exception { ExceptionCode::OutOfMemoryError };

    Ref<JSC::ArrayBufferView> clone = JSC::Uint8Array::create(WTFMove(buffer), 0, o.byteLength());

    return clone;
}

static void pullWithBYOBReader(JSDOMGlobalObject&, StreamTeeState&, ReadableStreamBYOBRequest&, bool);
static void pullWithDefaultReader(JSDOMGlobalObject&, StreamTeeState&);

static Ref<DOMPromise> pull1Steps(JSDOMGlobalObject& globalObject, StreamTeeState& state, ReadableStream& branch1)
{
    if (state.reading()) {
        state.setReadAgainForBranch1(true);
        auto [promise, deferred] = createPromiseAndWrapper(globalObject);
        deferred->resolve();
        return promise;
    }

    state.setReading(true);

    RefPtr byobRequest = branch1.protectedController()->getByobRequest();
    if (!byobRequest)
        pullWithDefaultReader(globalObject, state);
    else
        pullWithBYOBReader(globalObject, state, *byobRequest, false);

    auto [promise, deferred] = createPromiseAndWrapper(globalObject);
    deferred->resolve();
    return promise;
}

static Ref<DOMPromise> pull2Steps(JSDOMGlobalObject& globalObject, StreamTeeState& state, ReadableStream& branch2)
{
    if (state.reading()) {
        state.setReadAgainForBranch2(true);
        auto [promise, deferred] = createPromiseAndWrapper(globalObject);
        deferred->resolve();
        return promise;
    }

    state.setReading(true);

    RefPtr byobRequest = branch2.protectedController()->getByobRequest();
    if (!byobRequest)
        pullWithDefaultReader(globalObject, state);
    else
        pullWithBYOBReader(globalObject, state, *byobRequest, true);

    auto [promise, deferred] = createPromiseAndWrapper(globalObject);
    deferred->resolve();
    return promise;
}

static void pullWithDefaultReader(JSDOMGlobalObject& globalObject, StreamTeeState& state)
{
    if (RefPtr byobReader = state.takeBYOBReader()) {
        ASSERT(!byobReader->readIntoRequestsSize());
        byobReader->releaseLock(globalObject);

        auto readerOrException = ReadableStreamDefaultReader::create(globalObject, Ref { state.stream() }.get());
        if (readerOrException.hasException()) {
            ASSERT_NOT_REACHED();
            return;
        }
        Ref reader = readerOrException.releaseReturnValue();
        state.setReader(reader.get());
        state.forwardReadError(reader.get());
    }

    RefPtr reader = state.defaultReader();

    auto [promise, deferred] = createPromiseAndWrapper(globalObject);
    reader->read(globalObject, WTFMove(deferred));
    promise->whenSettled([state = Ref { state }, weakReader = WeakPtr { *reader }] {
        RefPtr readPromise = state->readPromise();
        RefPtr reader = weakReader.get();
        if (!readPromise || !reader)
            return;

        switch (readPromise->status()) {
        case DOMPromise::Status::Fulfilled: {
            auto& globalObject = *readPromise->globalObject();

            Ref vm = globalObject.vm();
            auto scope = DECLARE_THROW_SCOPE(vm);
            auto resultOrException = convertDictionary<ReadableStreamReadResult>(globalObject, readPromise->result());
            ASSERT(!resultOrException.hasException(scope));
            if (resultOrException.hasException(scope))
                return;

            RefPtr branch1 = state->branch1();
            RefPtr branch2 = state->branch2();

            auto result = resultOrException.releaseReturnValue();
            if (!result.done) {
                // chunk steps.
                state->setReadAgainForBranch1(false);
                state->setReadAgainForBranch2(false);

                auto chunkResult = convert<IDLArrayBufferView>(globalObject, result.value);
                if (chunkResult.hasException(scope)) [[unlikely]]
                    return;

                Ref chunk1 = chunkResult.releaseReturnValue();
                Ref chunk2 = chunk1;

                if (!state->canceled1() && !state->canceled2()) {
                    auto resultOrException = cloneAsUInt8Array(chunk1);
                    if (resultOrException.hasException()) {
                        if (branch1)
                            branch1->controller()->error(globalObject, resultOrException.exception());
                        if (branch2)
                            branch2->controller()->error(globalObject, resultOrException.exception());

                        state->stream().cancel(resultOrException.releaseException());
                        return;
                    }
                    chunk2 = resultOrException.releaseReturnValue();
                }
                if (!state->canceled1() && branch1)
                    branch1->protectedController()->enqueue(globalObject, chunk1);
                if (!state->canceled2() && branch2)
                    branch2->protectedController()->enqueue(globalObject, chunk2);

                state->setReading(false);
                if (state->readAgainForBranch1() && branch1)
                    pull1Steps(globalObject, state, *branch1);
                else if (state->readAgainForBranch2() && branch2)
                    pull2Steps(globalObject, state, *branch2);
                return;
            }

            // close steps.
            state->setReading(false);
            if (!state->canceled1() && branch1)
                branch1->controller()->close(globalObject);
            if (!state->canceled2() && branch2)
                branch2->controller()->close(globalObject);

            if (branch1 && branch1->protectedController()->hasPendingPullIntos())
                branch1->protectedController()->respond(globalObject, 0);
            if (branch2 && branch2->protectedController()->hasPendingPullIntos())
                branch2->protectedController()->respond(globalObject, 0);

            if (!state->canceled1() || !state->canceled2())
                state->resolveCancelPromise();
            return;
        }
        case DOMPromise::Status::Rejected:
            // error steps.
            state->setReading(false);
            return;
        case DOMPromise::Status::Pending:
            ASSERT_NOT_REACHED();
            break;
        }
    });
    state.setReadPromise(WTFMove(promise));
}

static void pullWithBYOBReader(JSDOMGlobalObject& globalObject, StreamTeeState& state, ReadableStreamBYOBRequest& request, bool forBranch2)
{
    if (RefPtr defaultReader = state.takeDefaultReader()) {
        ASSERT(!defaultReader->getNumReadRequests());
        defaultReader->releaseLock(globalObject);

        auto readerOrException = ReadableStreamBYOBReader::create(globalObject, Ref { state.stream() }.get());
        if (readerOrException.hasException()) {
            ASSERT_NOT_REACHED();
            return;
        }
        Ref reader = readerOrException.releaseReturnValue();
        state.setReader(reader.get());
        state.forwardReadError(reader.get());
    }

    RefPtr reader = state.byobReader();
    RefPtr byobBranch = forBranch2 ? state.branch2() : state.branch1();
    RefPtr otherBranch = forBranch2 ? state.branch1() : state.branch2();

    auto [promise, deferred] = createPromiseAndWrapper(globalObject);

    reader->read(globalObject, Ref { *request.view() }, 1, WTFMove(deferred));
    promise->whenSettled([state = Ref { state }, weakReader = WeakPtr { *reader }, forBranch2] {
        RefPtr readPromise = state->readPromise();
        RefPtr reader = weakReader.get();
        if (!readPromise || !reader)
            return;

        switch (readPromise->status()) {
        case DOMPromise::Status::Fulfilled: {
            auto& globalObject = *readPromise->globalObject();
            auto resultOrException = convertDictionary<ReadableStreamReadResult>(globalObject, readPromise->result());

            Ref vm = globalObject.vm();
            auto scope = DECLARE_THROW_SCOPE(vm);
            ASSERT(!resultOrException.hasException(scope));
            if (resultOrException.hasException(scope))
                return;

            RefPtr branch1 = state->branch1();
            RefPtr branch2 = state->branch2();

            auto result = resultOrException.releaseReturnValue();
            if (!result.done) {
                // Chunk steps.
                auto chunkResult = convert<IDLArrayBufferView>(globalObject, result.value);
                if (chunkResult.hasException(scope)) [[unlikely]]
                    return;

                Ref chunk = chunkResult.releaseReturnValue();

                state->setReadAgainForBranch1(false);
                state->setReadAgainForBranch2(false);

                bool byobCanceled = forBranch2 ? state->canceled2() : state->canceled1();
                bool otherCanceled = forBranch2 ? state->canceled1() : state->canceled2();

                RefPtr byobBranch = forBranch2 ? branch2 : branch1;
                RefPtr otherBranch = forBranch2 ? branch1 : branch2;
                if (!otherCanceled) {
                    auto resultOrException = cloneAsUInt8Array(chunk);
                    if (resultOrException.hasException()) {
                        if (byobBranch)
                            byobBranch->controller()->error(globalObject, resultOrException.exception());
                        if (otherBranch)
                            otherBranch->controller()->error(globalObject, resultOrException.exception());

                        state->stream().cancel(resultOrException.releaseException());
                        return;
                    }
                    Ref clonedChunk = resultOrException.releaseReturnValue();
                    if (!byobCanceled)
                        byobBranch->protectedController()->respondWithNewView(globalObject, chunk);
                    otherBranch->protectedController()->enqueue(globalObject, clonedChunk);
                } else if (!byobCanceled)
                    byobBranch->protectedController()->respondWithNewView(globalObject, chunk);

                state->setReading(false);
                if (state->readAgainForBranch1() && branch1)
                    pull1Steps(globalObject, state, *branch1);
                else if (state->readAgainForBranch2() && branch2)
                    pull2Steps(globalObject, state, *branch2);

                return;
            }

            // Close steps.
            state->setReading(false);
            bool byobCanceled = forBranch2 ? state->canceled2() : state->canceled1();
            bool otherCanceled = forBranch2 ? state->canceled1() : state->canceled2();
            if (!byobCanceled && branch1)
                branch1->controller()->close(globalObject);

            if (!otherCanceled && branch2)
                branch2->controller()->close(globalObject);

            if (result.value) {
                auto chunkResult = convert<IDLArrayBufferView>(globalObject, result.value);
                if (chunkResult.hasException(scope)) [[unlikely]]
                    return;

                Ref chunk = chunkResult.releaseReturnValue();
                ASSERT(!chunk->byteLength());

                if (!byobCanceled && branch1)
                    branch1->protectedController()->respondWithNewView(globalObject, chunk);
                if (!otherCanceled && branch2 && branch2->controller()->hasPendingPullIntos())
                    branch2->protectedController()->respond(globalObject, 0);
            }
            if (!byobCanceled || !otherCanceled)
                state->resolveCancelPromise();
            return;
        }
        case DOMPromise::Status::Rejected:
            // error steps.
            state->setReading(false);
            return;
        case DOMPromise::Status::Pending:
            ASSERT_NOT_REACHED();
            break;
        }
    });
    state.setReadPromise(WTFMove(promise));
}


} // namespace WebCore
