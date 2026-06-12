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
#include <JavaScriptCore/JSCJSValue.h>
#include <wtf/StdLibExtras.h>
#include <wtf/Vector.h>

namespace JSC {

template<typename Derived>
concept StructuredCloneSerializerHandler = requires(Derived& d, JSValue v, SerializationReturnCode& code) {
    { d.dumpDerivedTerminal(v, code) } -> std::same_as<bool>;
};

namespace StructuredCloneInternal {

#if JSC_ASSUME_LITTLE_ENDIAN
template<typename T> inline void writeLittleEndian(Vector<uint8_t>& buffer, T value)
{
    buffer.append(asByteSpan(value));
}
#else
template<typename T> inline void writeLittleEndian(Vector<uint8_t>& buffer, T value)
{
    for (unsigned i = 0; i < sizeof(T); i++) {
        buffer.append(value & 0xFF);
        value >>= 8;
    }
}
#endif

template<> inline void writeLittleEndian<uint8_t>(Vector<uint8_t>& buffer, uint8_t value)
{
    buffer.append(value);
}

template<typename T> inline bool writeLittleEndian(Vector<uint8_t>& buffer, std::span<const T> values)
{
    if (values.size() > std::numeric_limits<uint32_t>::max() / sizeof(T))
        return false;

#if JSC_ASSUME_LITTLE_ENDIAN
    buffer.append(asBytes(values));
#else
    for (unsigned i = 0; i < values.size(); i++) {
        T value = values[i];
        for (unsigned j = 0; j < sizeof(T); j++) {
            buffer.append(static_cast<uint8_t>(value & 0xFF));
            value >>= 8;
        }
    }
#endif
    return true;
}

template<> inline bool writeLittleEndian<uint8_t>(Vector<uint8_t>& buffer, std::span<const uint8_t> values)
{
    buffer.append(values);
    return true;
}

} // namespace StructuredCloneInternal

template<typename Derived>
class CloneSerializerBase : public CloneBase {
protected:
    CloneSerializerBase(JSGlobalObject* lexicalGlobalObject, Vector<uint8_t>& buffer)
        : CloneBase(lexicalGlobalObject)
        , m_buffer(buffer)
    {
    }

    template<typename T> requires std::is_enum_v<T>
    void write(T tag)
    {
        if constexpr (std::is_same_v<T, SerializationTag>)
            SERIALIZE_TRACE("serialize ", tag);
        StructuredCloneInternal::writeLittleEndian<uint8_t>(m_buffer, static_cast<uint8_t>(tag));
    }

    void write(bool b)        { write(static_cast<uint8_t>(b)); }
    void write(uint8_t c)     { StructuredCloneInternal::writeLittleEndian(m_buffer, c); }
    void write(uint16_t i)    { StructuredCloneInternal::writeLittleEndian(m_buffer, i); }
    void write(uint32_t i)    { StructuredCloneInternal::writeLittleEndian(m_buffer, i); }
    void write(int32_t i)     { StructuredCloneInternal::writeLittleEndian(m_buffer, i); }
    void write(uint64_t i)    { StructuredCloneInternal::writeLittleEndian(m_buffer, i); }

    void write(double d)
    {
        StructuredCloneInternal::writeLittleEndian(m_buffer, std::bit_cast<int64_t>(d));
    }

    ALWAYS_INLINE bool dumpIfTerminal(JSValue value, SerializationReturnCode& code)
    {
        // Note: This can't be a requirement on the template as, in the common usage,
        // Derived will still be an incomplete type
        static_assert(StructuredCloneSerializerHandler<Derived>,
            "Derived class must satisfy StructuredCloneSerializerHandler");
        if (value.isNull()) {
            write(NullTag);
            return true;
        }
        if (value.isUndefined()) {
            write(UndefinedTag);
            return true;
        }
        if (value.isInt32()) {
            int32_t i = value.asInt32();
            if (!i)
                write(ZeroTag);
            else if (i == 1)
                write(OneTag);
            else {
                write(IntTag);
                write(static_cast<uint32_t>(i));
            }
            return true;
        }
        if (value.isNumber()) {
            write(DoubleTag);
            write(value.asDouble());
            return true;
        }
        if (value.isBoolean()) {
            write(value.isTrue() ? TrueTag : FalseTag);
            return true;
        }
        return static_cast<Derived*>(this)->dumpDerivedTerminal(value, code);
    }

    Vector<uint8_t>& m_buffer;
};

} // namespace JSC
