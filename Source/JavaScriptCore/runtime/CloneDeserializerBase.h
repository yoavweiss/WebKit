/*
 * Copyright (C) 2009-2026 Apple Inc. All rights reserved.
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

#include <JavaScriptCore/CloneBase.h>
#include <wtf/StdLibExtras.h>

namespace JSC {

template<typename Derived>
concept StructuredCloneDeserializerHandler = requires(Derived& d, SerializationTag t) {
    { d.readDerivedTerminal(t) } -> std::same_as<JSValue>;
    { d.isTagExposed(t) } -> std::same_as<bool>;
};

namespace StructuredCloneInternal {

#if JSC_ASSUME_LITTLE_ENDIAN
template<typename T> inline bool readLittleEndian(std::span<const uint8_t>& span, T& value)
{
    if (span.size() < sizeof(value))
        return false;
    value = consumeAndReinterpretCastTo<const T>(span);
    return true;
}
#else
template<typename T> inline bool readLittleEndian(std::span<const uint8_t>& span, T& value)
{
    if (span.size() < sizeof(value))
        return false;

    if constexpr (sizeof(T) == 1)
        value = consume(span);
    else {
        value = 0;
        for (size_t i = 0; i < sizeof(T); ++i)
            value += static_cast<T>(span[i]) << (i * 8);
        skip(span, sizeof(T));
    }
    return true;
}
#endif

} // namespace StructuredCloneInternal

template<typename Derived>
class CloneDeserializerBase : public CloneBase {
protected:
    CloneDeserializerBase(JSGlobalObject* lexicalGlobalObject, std::span<const uint8_t> data)
        : CloneBase(lexicalGlobalObject)
        , m_data(data)
    {
    }

    template<typename T> bool readLittleEndian(T& value)
    {
        if (m_failed || !StructuredCloneInternal::readLittleEndian(m_data, value)) {
            SERIALIZE_TRACE("FAIL deserialize");
            fail();
            return false;
        }
        return true;
    }

    bool read(uint8_t& i)  { return readLittleEndian(i); }
    bool read(uint16_t& i) { return readLittleEndian(i); }
    bool read(uint32_t& i) { return readLittleEndian(i); }
    bool read(uint64_t& i) { return readLittleEndian(i); }
    bool read(int32_t& i)  { return readLittleEndian(*reinterpret_cast<uint32_t*>(&i)); }

    bool read(double& d)
    {
        uint64_t bits;
        if (!readLittleEndian(bits))
            return false;
        d = purifyNaN(std::bit_cast<double>(bits));
        return true;
    }

    SerializationTag readTag()
    {
        if (m_data.empty()) {
            SERIALIZE_TRACE("FAIL deserialize");
            return ErrorTag;
        }
        auto tag = static_cast<SerializationTag>(consume(m_data));
        SERIALIZE_TRACE("deserialize ", tag);
        return tag;
    }

    ALWAYS_INLINE JSValue readTerminalImpl(SerializationTag tag)
    {
        switch (tag) {
        case UndefinedTag:
            return jsUndefined();
        case NullTag:
            return jsNull();
        case IntTag: {
            int32_t i;
            if (!read(i))
                return JSValue();
            return jsNumber(i);
        }
        case ZeroTag:
            return jsNumber(0);
        case OneTag:
            return jsNumber(1);
        case FalseTag:
            return jsBoolean(false);
        case TrueTag:
            return jsBoolean(true);
        case DoubleTag: {
            double d;
            if (!read(d))
                return JSValue();
            return jsNumber(d);
        }
        default:
            return static_cast<Derived*>(this)->readDerivedTerminal(tag);
        }
    }

    ALWAYS_INLINE JSValue readTerminal()
    {
        // Note: This can't be a requirement on the template as, in the common usage,
        // Derived will still be an incomplete type
        static_assert(StructuredCloneDeserializerHandler<Derived>,
            "Derived class must satisfy StructuredCloneDeserializerHandler");
        if (!isSafeToRecurse()) {
            fail();
            return JSValue();
        }
        auto originalData = m_data;
        SerializationTag tag = readTag();
        if (!static_cast<Derived*>(this)->isTagExposed(tag)) {
            fail();
            return JSValue();
        }
        JSValue result = readTerminalImpl(tag);
        if (!result) {
            SERIALIZE_TRACE("push back ", tag);
            m_data = originalData;
        }
        return result;
    }

    std::span<const uint8_t> m_data;
};

} // namespace JSC
