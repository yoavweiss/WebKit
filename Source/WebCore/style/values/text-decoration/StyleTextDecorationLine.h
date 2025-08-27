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
#include <wtf/Variant.h>
#include <wtf/text/TextStream.h>

namespace WebCore {
namespace Style {
struct TextDecorationLine;

// text-decoration-line = none | [ underline || overline || line-through || blink ] | spelling-error | grammar-error
// https://www.w3.org/TR/css-text-decor-4/#text-decoration-line-property
struct TextDecorationLine {
    TextDecorationLine(CSS::Keyword::None keyword)
        : m_value { keyword }
    {
    }

    TextDecorationLine(CSS::Keyword::SpellingError keyword)
        : m_value { keyword }
    {
    }

    TextDecorationLine(CSS::Keyword::GrammarError keyword)
        : m_value { keyword }
    {
    }

    TextDecorationLine(OptionSet<TextDecorationLineFlags> flags)
        : m_value { flags }
    {
        if (auto* flags = std::get_if<OptionSet<TextDecorationLineFlags>>(&m_value)) {
            if (flags->isEmpty())
                m_value = CSS::Keyword::None { };
        } else
            ASSERT_NOT_REACHED();
    }

    TextDecorationLine(TextDecorationLineFlags flag)
        : TextDecorationLine(OptionSet<TextDecorationLineFlags>(flag))
    {
    }

    constexpr bool isNone() const { return WTF::holdsAlternative<CSS::Keyword::None>(m_value); }
    bool isSpellingError() const { return WTF::holdsAlternative<CSS::Keyword::SpellingError>(m_value); }
    bool isGrammarError() const { return WTF::holdsAlternative<CSS::Keyword::GrammarError>(m_value); }
    bool isFlags() const { return WTF::holdsAlternative<OptionSet<TextDecorationLineFlags>>(m_value); }

    bool hasUnderline() const
    {
        if (auto* flags = std::get_if<OptionSet<TextDecorationLineFlags>>(&m_value))
            return flags->contains(TextDecorationLineFlags::Underline);
        return false;
    }

    bool hasOverline() const
    {
        if (auto* flags = std::get_if<OptionSet<TextDecorationLineFlags>>(&m_value))
            return flags->contains(TextDecorationLineFlags::Overline);
        return false;
    }

    bool hasLineThrough() const
    {
        if (auto* flags = std::get_if<OptionSet<TextDecorationLineFlags>>(&m_value))
            return flags->contains(TextDecorationLineFlags::LineThrough);
        return false;
    }

    bool hasBlink() const
    {
        if (auto* flags = std::get_if<OptionSet<TextDecorationLineFlags>>(&m_value))
            return flags->contains(TextDecorationLineFlags::Blink);
        return false;
    }

    bool containsAny(OptionSet<TextDecorationLineFlags> options) const
    {
        if (auto* flags = std::get_if<OptionSet<TextDecorationLineFlags>>(&m_value))
            return flags->containsAny(options);
        return false;
    }

    bool contains(TextDecorationLineFlags option) const
    {
        if (auto* flags = std::get_if<OptionSet<TextDecorationLineFlags>>(&m_value))
            return flags->contains(option);
        return false;
    }

    void remove(TextDecorationLineFlags option)
    {
        if (auto* flags = std::get_if<OptionSet<TextDecorationLineFlags>>(&m_value)) {
            flags->remove(option);
            if (flags->isEmpty())
                m_value = CSS::Keyword::None { };
        }
    }

    void addOrReplaceIfNotNone(const TextDecorationLine& value);

    template<typename... F> decltype(auto) switchOn(F&&... f) const
    {
        return WTF::switchOn(m_value, std::forward<F>(f)...);
    }

    constexpr explicit operator bool() const { return !isNone(); }
    bool operator==(const TextDecorationLine&) const = default;

private:
    Variant<CSS::Keyword::None, CSS::Keyword::SpellingError, CSS::Keyword::GrammarError, OptionSet<TextDecorationLineFlags>> m_value;
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
