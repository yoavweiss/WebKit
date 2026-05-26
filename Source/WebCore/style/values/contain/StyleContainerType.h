/*
 * Copyright (C) 2026 Igalia S.L. All rights reserved.
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

#include <WebCore/StyleValueTypes.h>

namespace WebCore {
namespace Style {

// <'container-type'> = normal | size | inline-size
// https://drafts.csswg.org/css-conditional-5/#container-type
struct ContainerType {
    constexpr ContainerType(CSS::Keyword::Normal)
        : m_type { Type::Normal }
    {
    }

    constexpr ContainerType(CSS::Keyword::Size)
        : m_type { Type::Size }
    {
    }

    constexpr ContainerType(CSS::Keyword::InlineSize)
        : m_type { Type::InlineSize }
    {
    }

    constexpr bool isNormal() const { return m_type == Type::Normal; }
    constexpr bool hasSize() const { return m_type == Type::Size; }
    constexpr bool hasInlineSize() const { return m_type == Type::InlineSize; }
    constexpr bool hasSizeContainment() const { return hasSize() || hasInlineSize(); }

    template<typename... F> decltype(auto) switchOn(F&&... f) const
    {
        auto visitor = WTF::makeVisitor(std::forward<F>(f)...);

        switch (m_type) {
        case Type::Normal:
            return visitor(CSS::Keyword::Normal { });
        case Type::Size:
            return visitor(CSS::Keyword::Size { });
        case Type::InlineSize:
            return visitor(CSS::Keyword::InlineSize { });
        }
        RELEASE_ASSERT_NOT_REACHED();
    }

    constexpr bool operator==(const ContainerType&) const = default;

private:
    enum Type : uint8_t { Normal, Size, InlineSize };
    Type m_type;
};

// MARK: - Conversion

template<> struct CSSValueConversion<ContainerType> {
    ContainerType NODELETE operator()(BuilderState&, const CSSValue&);
};

} // namespace Style
} // namespace WebCore

DEFINE_VARIANT_LIKE_CONFORMANCE(WebCore::Style::ContainerType)
