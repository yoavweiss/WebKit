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

#include "config.h"
#include "ReadableByteStreamController.h"

#include "JSDOMPromise.h"
#include "JSDOMPromiseDeferred.h"
#include "ReadableStream.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(ReadableByteStreamController);

ReadableByteStreamController::ReadableByteStreamController(ReadableStream& stream)
    : m_stream(stream)
{
}

ReadableByteStreamController::~ReadableByteStreamController() = default;

void ReadableByteStreamController::ref()
{
    m_stream->ref();
}

void ReadableByteStreamController::deref()
{
    m_stream->deref();
}

ReadableStream& ReadableByteStreamController::stream()
{
    return m_stream;
}

ReadableStreamBYOBRequest* ReadableByteStreamController::byobRequestForBindings() const
{
    return nullptr;
}

std::optional<double> ReadableByteStreamController::desiredSize() const
{
    return { };
}

ExceptionOr<void> ReadableByteStreamController::closeForBindings(JSDOMGlobalObject& globalObject)
{
    UNUSED_PARAM(globalObject);
    return { };
}

ExceptionOr<void> ReadableByteStreamController::enqueueForBindings(JSDOMGlobalObject& globalObject, JSC::ArrayBufferView& chunk)
{
    UNUSED_PARAM(globalObject);
    UNUSED_PARAM(chunk);
    return { };
}

ExceptionOr<void> ReadableByteStreamController::errorForBindings(JSDOMGlobalObject& globalObject, JSC::JSValue value)
{
    UNUSED_PARAM(globalObject);
    UNUSED_PARAM(value);
    return { };
}

ExceptionOr<void> ReadableByteStreamController::start(JSDOMGlobalObject& globalObject, UnderlyingSourceStartCallback* startAlgorithm)
{
    UNUSED_PARAM(globalObject);
    UNUSED_PARAM(startAlgorithm);
    return { };
}

// https://streams.spec.whatwg.org/#readable-byte-stream-controller-close
void ReadableByteStreamController::close(JSDOMGlobalObject& globalObject)
{
    UNUSED_PARAM(globalObject);
}

// https://streams.spec.whatwg.org/#readable-byte-stream-controller-enqueue
ExceptionOr<void> ReadableByteStreamController::enqueue(JSDOMGlobalObject& globalObject, JSC::ArrayBufferView& chunk)
{
    UNUSED_PARAM(globalObject);
    UNUSED_PARAM(chunk);
    return { };
}

// https://streams.spec.whatwg.org/#readable-byte-stream-controller-error
void ReadableByteStreamController::error(JSDOMGlobalObject&, JSC::JSValue)
{
}

// https://streams.spec.whatwg.org/#readable-byte-stream-controller-pull-into
void ReadableByteStreamController::pullInto(JSDOMGlobalObject& globalObject, JSC::ArrayBufferView& view, size_t min, Ref<DeferredPromise>&& readIntoRequest)
{
    UNUSED_PARAM(globalObject);
    UNUSED_PARAM(view);
    UNUSED_PARAM(min);
    readIntoRequest->reject(Exception { ExceptionCode::NotSupportedError, "Reading a byte stream is not yet supported"_s }, RejectAsHandled::Yes);
}

// https://streams.spec.whatwg.org/#rbs-controller-private-cancel
void ReadableByteStreamController::runCancelSteps(JSDOMGlobalObject& globalObject, JSC::JSValue reason, Function<void(std::optional<JSC::JSValue>&&)>&& callback)
{
    UNUSED_PARAM(globalObject);
    UNUSED_PARAM(reason);
    UNUSED_PARAM(callback);
}

// https://streams.spec.whatwg.org/#rbs-controller-private-pull
void ReadableByteStreamController::runPullSteps(JSDOMGlobalObject& globalObject, Ref<DeferredPromise>&& readRequest)
{
    UNUSED_PARAM(globalObject);
    UNUSED_PARAM(readRequest);
}

// https://streams.spec.whatwg.org/#abstract-opdef-readablebytestreamcontroller-releasesteps
void ReadableByteStreamController::runReleaseSteps()
{
}

// https://streams.spec.whatwg.org/#readable-byte-stream-controller-respond
ExceptionOr<void> ReadableByteStreamController::respond(JSDOMGlobalObject& globalObject, size_t bytesWritten)
{
    UNUSED_PARAM(globalObject);
    UNUSED_PARAM(bytesWritten);

    return { };
}

// https://streams.spec.whatwg.org/#readable-byte-stream-controller-respond-with-new-view
ExceptionOr<void> ReadableByteStreamController::respondWithNewView(JSDOMGlobalObject& globalObject, JSC::ArrayBufferView& view)
{
    UNUSED_PARAM(globalObject);
    UNUSED_PARAM(view);

    return { };
}

} // namespace WebCore
