/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
 * Copyright (C) 2025 Samuel Weinig <sam@webkit.org>
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

#include "StyleColor.h"
#include <wtf/Markable.h>

namespace WebCore {
namespace Style {

// <'scrollbar-color'> = auto | <color>{2}
// https://www.w3.org/TR/css-scrollbars/#propdef-scrollbar-color
struct ScrollbarColor {
    struct Parts {
        Color thumb;
        Color track;

        bool operator==(const Parts&) const = default;
    };

    ScrollbarColor(CSS::Keyword::Auto) { }
    ScrollbarColor(Parts&& parts) : m_parts { WTFMove(parts) } { }

    bool isAuto() const { return !m_parts; }
    bool isParts() const { return !!m_parts; }

    template<typename... F> decltype(auto) switchOn(F&&... f) const
    {
        auto visitor = WTF::makeVisitor(std::forward<F>(f)...);

        if (isAuto())
            return visitor(CSS::Keyword::Auto { });
        return visitor(*m_parts);
    }

    bool operator==(const ScrollbarColor&) const = default;

private:
    friend struct Blending<ScrollbarColor>;

    struct PartsMarkableTraits {
        static bool isEmptyValue(const Parts& value)
        {
            return WTF::MarkableTraits<Color>::isEmptyValue(value.thumb);
        }
        static Parts emptyValue()
        {
            return { WTF::MarkableTraits<Color>::emptyValue(), WTF::MarkableTraits<Color>::emptyValue() };
        }
    };

    Markable<Parts, PartsMarkableTraits> m_parts { };
};

template<size_t I> const auto& get(const ScrollbarColor::Parts& value)
{
    if constexpr (!I)
        return value.thumb;
    else if constexpr (I == 1)
        return value.track;
}

// MARK: - Conversion

template<> struct CSSValueConversion<ScrollbarColor> { auto operator()(BuilderState&, const CSSValue&) -> ScrollbarColor; };

// MARK: - Blending

template<> struct Blending<ScrollbarColor> {
    auto equals(const ScrollbarColor&, const ScrollbarColor&, const RenderStyle&, const RenderStyle&) -> bool;
    auto canBlend(const ScrollbarColor&, const ScrollbarColor&) -> bool;
    auto blend(const ScrollbarColor&, const ScrollbarColor&, const RenderStyle&, const RenderStyle&, const BlendingContext&) -> ScrollbarColor;
};

} // namespace Style
} // namespace WebCore

DEFINE_SPACE_SEPARATED_TUPLE_LIKE_CONFORMANCE(WebCore::Style::ScrollbarColor::Parts, 2)
DEFINE_VARIANT_LIKE_CONFORMANCE(WebCore::Style::ScrollbarColor)
