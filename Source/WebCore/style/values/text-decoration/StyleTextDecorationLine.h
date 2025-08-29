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

#pragma once

#include "RenderStyleConstants.h"
#include "StyleValueTypes.h"
#include <wtf/OptionSet.h>
#include <wtf/text/TextStream.h>

namespace WebCore {
namespace Style {

// text-decoration-line = none | [ underline || overline || line-through || blink ] | spelling-error | grammar-error
// https://www.w3.org/TR/css-text-decor-4/#text-decoration-line-property

// We are representing TextDecorationLine in 5 bits.
// 1 bit is used for defining the Type (SingleValue or Flags)
// 4 bits are used for defining the Value
// Values for SingleValue: None, SpellingError, GrammarError
// Values for Flags: Any combination of Underline, Overline, LineThrough, Blink
// Therefore, we are packing its content with the following layout:
// Bits 7-5 : Reserved
// Bit 4    : Type (SingleValue or Flags)
// Bits 3-0 : When Type=1 (Underline=0x1, Overline=0x2, LineThrough=0x4, Blink=0x8)
//          : When Type=0 (None = 0, SpellingError = 1, GrammarError = 2)
struct TextDecorationLine {
    enum class Type : uint8_t {
        SingleValue   = 0,
        Flags         = 1 << 4
    };

    static constexpr uint8_t TypeMask = 1 << 4; // 0001 0000
    static constexpr uint8_t ValuesMask = 0x0F;

    // Values when Type is SingleValue
    enum class SingleValue : uint8_t {
        None  = 0,
        SpellingError,
        GrammarError
    };

    // Values when Type is Flags
    static constexpr uint8_t UnderlineBit   = static_cast<uint8_t>(TextDecorationLineFlags::Underline);
    static constexpr uint8_t OverlineBit    = static_cast<uint8_t>(TextDecorationLineFlags::Overline);
    static constexpr uint8_t LineThroughBit = static_cast<uint8_t>(TextDecorationLineFlags::LineThrough);
    static constexpr uint8_t BlinkBit       = static_cast<uint8_t>(TextDecorationLineFlags::Blink);

    static constexpr uint8_t SingleValueNone          = static_cast<uint8_t>(Type::SingleValue) | static_cast<uint8_t>(SingleValue::None);
    static constexpr uint8_t SingleValueSpellingError = static_cast<uint8_t>(Type::SingleValue) | static_cast<uint8_t>(SingleValue::SpellingError);
    static constexpr uint8_t SingleValueGrammarError  = static_cast<uint8_t>(Type::SingleValue) | static_cast<uint8_t>(SingleValue::GrammarError);

    TextDecorationLine() = default;

    TextDecorationLine(uint8_t rawValue)
        : m_packed(rawValue)
    {
    }

    TextDecorationLine(CSS::Keyword::None)
        : m_packed(SingleValueNone)
    {
    }

    TextDecorationLine(CSS::Keyword::SpellingError)
        : m_packed(SingleValueSpellingError)
    {
    }

    TextDecorationLine(CSS::Keyword::GrammarError)
        : m_packed(SingleValueGrammarError)
    {
    }

    TextDecorationLine(OptionSet<TextDecorationLineFlags> flags)
        : m_packed(flags.isEmpty() ? SingleValueNone : packFlags(flags))
    {
    }

    TextDecorationLine(TextDecorationLineFlags flag)
        : TextDecorationLine(OptionSet<TextDecorationLineFlags>(flag))
    {
    }

    inline Type type() const { return static_cast<Type>(m_packed & TypeMask); }
    bool isNone() const { return m_packed == SingleValueNone; }
    bool isSpellingError() const { return m_packed == SingleValueSpellingError; }
    bool isGrammarError() const { return m_packed == SingleValueGrammarError; }
    bool isFlags() const { return type() == Type::Flags; }

    bool hasUnderline() const
    {
        return (isFlags()) && (m_packed & UnderlineBit);
    }

    bool hasOverline() const
    {
        return (isFlags()) && (m_packed & OverlineBit);
    }

    bool hasLineThrough() const
    {
        return (isFlags()) && (m_packed & LineThroughBit);
    }

    bool hasBlink() const
    {
        return (isFlags()) && (m_packed & BlinkBit);
    }

    bool containsAny(OptionSet<TextDecorationLineFlags> options) const
    {
        if (!isFlags())
            return false;
        return (m_packed & packFlags(options));
    }

    bool contains(TextDecorationLineFlags option) const
    {
        if (!isFlags())
            return false;
        return (m_packed & packFlagValue(option));
    }

    void remove(TextDecorationLineFlags option)
    {
        if (type() == Type::Flags) {
            m_packed &= ~packFlagValue(option);
            // If none flags are set we should represent this as Type::None
            if (!(m_packed & ValuesMask))
                setNone();
        }
    }

    uint8_t addOrReplaceIfNotNone(const TextDecorationLine& value);

    template<typename... F> decltype(auto) switchOn(F&&... f) const
    {
        auto visitor = WTF::makeVisitor(std::forward<F>(f)...);

        switch (type()) {
        case Type::Flags:
            return visitor(unpackFlags());
        case Type::SingleValue: {
            if (isNone())
                return visitor(CSS::Keyword::None { });
            if (isSpellingError())
                return visitor(CSS::Keyword::SpellingError { });
            ASSERT(isGrammarError());
            return visitor(CSS::Keyword::GrammarError { });
            }
        }
        ASSERT_NOT_REACHED();
        return visitor(CSS::Keyword::None { });
    }

    void setNone() { m_packed = SingleValueNone; }
    void setSpellingError() { m_packed = SingleValueSpellingError; }
    void setGrammarError() { m_packed = SingleValueGrammarError; }
    void setFlags(OptionSet<TextDecorationLineFlags> flags)
    {
        if (isFlags())
            m_packed |= packFlags(flags);
        else
            m_packed = packFlags(flags);
    }

    constexpr explicit operator bool() const { return !isNone(); }
    bool operator==(const TextDecorationLine& other) const { return m_packed == other.m_packed; }

    uint8_t toRaw() const { return m_packed; }
    static constexpr uint8_t packFlags(OptionSet<TextDecorationLineFlags> flags)
    {
        uint8_t result = static_cast<uint8_t>(Type::Flags);
        if (flags.contains(TextDecorationLineFlags::Underline))
            result |= UnderlineBit;
        if (flags.contains(TextDecorationLineFlags::Overline))
            result |= OverlineBit;
        if (flags.contains(TextDecorationLineFlags::LineThrough))
            result |= LineThroughBit;
        if (flags.contains(TextDecorationLineFlags::Blink))
            result |= BlinkBit;
        return result;
    }

private:
    // Returns only the value bits, not to be confused with "toRaw", which returns the whole packed raw representation
    inline uint8_t rawValue() const { return m_packed & ValuesMask; }

    // Note that this function packs only the 'Value' bit, ignoring the Type. This is useful for bitwise operations.
    static constexpr uint8_t packFlagValue(TextDecorationLineFlags flag)
    {
        switch (flag) {
        case TextDecorationLineFlags::Underline:
            return UnderlineBit;
        case TextDecorationLineFlags::Overline:
            return OverlineBit;
        case TextDecorationLineFlags::LineThrough:
            return LineThroughBit;
        case TextDecorationLineFlags::Blink:
            return BlinkBit;
        }
        ASSERT_NOT_REACHED();
        return 0;
    }

    OptionSet<TextDecorationLineFlags> unpackFlags() const
    {
        ASSERT(isFlags());
        OptionSet<TextDecorationLineFlags> flags;
        if (m_packed & UnderlineBit)
            flags.add(TextDecorationLineFlags::Underline);
        if (m_packed & OverlineBit)
            flags.add(TextDecorationLineFlags::Overline);
        if (m_packed & LineThroughBit)
            flags.add(TextDecorationLineFlags::LineThrough);
        if (m_packed & BlinkBit)
            flags.add(TextDecorationLineFlags::Blink);
        return flags;
    }

    uint8_t m_packed { 0 };
};

// MARK: - Conversion

template<> struct CSSValueConversion<TextDecorationLine> {
    auto operator()(BuilderState&, const CSSValue&) -> TextDecorationLine;
};

template<> struct CSSValueCreation<OptionSet<TextDecorationLineFlags>> {
    auto operator()(CSSValuePool&, const RenderStyle&, const  OptionSet<TextDecorationLineFlags>&) -> Ref<CSSValue>;
};

// MARK: Serialization

template<> struct Serialize<OptionSet<TextDecorationLineFlags>> {
    void operator()(StringBuilder&, const CSS::SerializationContext&, const RenderStyle&, const OptionSet<TextDecorationLineFlags>&);
};

WTF::TextStream& operator<<(WTF::TextStream&, const TextDecorationLine&);

} // namespace Style
} // namespace WebCore

DEFINE_VARIANT_LIKE_CONFORMANCE(WebCore::Style::TextDecorationLine)
