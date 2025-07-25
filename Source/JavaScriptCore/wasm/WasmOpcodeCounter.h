/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
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

#if ENABLE(WEBASSEMBLY) && ENABLE(B3_JIT)

#include "WasmTypeDefinition.h"
#include <wtf/Atomics.h>

namespace JSC {
namespace Wasm {

class WasmOpcodeCounter {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(WasmOpcodeCounter);
    using NumberOfRegisteredOpcodes = size_t;
    using CounterSize = size_t;

public:
    static WasmOpcodeCounter& singleton();

    void registerDispatch();

    void increment(ExtSIMDOpType);
    void increment(ExtAtomicOpType);
    void increment(ExtGCOpType);
    void increment(OpType);

    void dump();
    template<typename OpcodeType, typename OpcodeTypeDump, typename IsRegisteredOpcodeFunctor>
    void dump(Atomic<uint64_t>* counter, NumberOfRegisteredOpcodes, CounterSize, const IsRegisteredOpcodeFunctor&, const char* prefix, const char* suffix);

private:
    constexpr static std::pair<NumberOfRegisteredOpcodes, CounterSize> m_extendedSIMDOpcodeInfo = countNumberOfWasmExtendedSIMDOpcodes();
    Atomic<uint64_t> m_extendedSIMDOpcodeCounter[m_extendedSIMDOpcodeInfo.second];

    constexpr static std::pair<NumberOfRegisteredOpcodes, CounterSize> m_extendedAtomicOpcodeInfo = countNumberOfWasmExtendedAtomicOpcodes();
    Atomic<uint64_t> m_extendedAtomicOpcodeCounter[m_extendedAtomicOpcodeInfo.second];

    constexpr static std::pair<NumberOfRegisteredOpcodes, CounterSize> m_GCOpcodeInfo = countNumberOfWasmGCOpcodes();
    Atomic<uint64_t> m_GCOpcodeCounter[m_GCOpcodeInfo.second];

    constexpr static std::pair<NumberOfRegisteredOpcodes, CounterSize> m_baseOpcodeInfo = countNumberOfWasmBaseOpcodes();
    Atomic<uint64_t> m_baseOpcodeCounter[m_baseOpcodeInfo.second];
};

} // namespace JSC
} // namespace JSC::Wasm

#endif // ENABLE(WEBASSEMBLY) && && ENABLE(B3_JIT)
