/*
 * Copyright (C) 2016-2024 Apple Inc. All rights reserved.
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

//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.9.5
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2024 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <algorithm>
#include <span>
#include <type_traits>
#include <wtf/Compiler.h>

namespace WTF {

template<typename> struct EnumTraits;
template<typename> struct EnumTraitsForPersistence;

template<typename E, E...> struct EnumValues;

template<typename E, E... values>
struct EnumValues {
    static constexpr E max = std::max({ values... });
    static constexpr E min = std::min({ values... });
    static constexpr size_t count = sizeof...(values);

    template <typename Callable>
    static void forEach(Callable&& c)
    {
        for (auto value : { values... })
            c(value);
    }
};

template<typename T, typename E> struct EnumValueChecker;

template<typename T, typename E, E e, E... es>
struct EnumValueChecker<T, EnumValues<E, e, es...>> {
    static constexpr bool isValidEnumForPersistence(T t)
    {
        return (static_cast<T>(e) == t) ? true : EnumValueChecker<T, EnumValues<E, es...>>::isValidEnumForPersistence(t);
    }
};

template<typename T, typename E>
struct EnumValueChecker<T, EnumValues<E>> {
    static constexpr bool isValidEnumForPersistence(T)
    {
        return false;
    }
};

template<typename E> bool isValidEnum(std::underlying_type_t<E>);

template<typename E, typename = std::enable_if_t<!std::is_same_v<std::underlying_type_t<E>, bool>>>
bool isValidEnumForPersistence(std::underlying_type_t<E> t)
{
    return EnumValueChecker<std::underlying_type_t<E>, typename EnumTraitsForPersistence<E>::values>::isValidEnumForPersistence(t);
}

template<typename E, typename = std::enable_if_t<std::is_same_v<std::underlying_type_t<E>, bool>>>
constexpr bool isValidEnumForPersistence(bool t)
{
    return !t || t == 1;
}

template<typename E>
constexpr auto enumToUnderlyingType(E e)
{
    return static_cast<std::underlying_type_t<E>>(e);
}

template<typename T, typename E> struct ZeroBasedContiguousEnumChecker;

template<typename T, typename E, E e, E... es>
struct ZeroBasedContiguousEnumChecker<T, EnumValues<E, e, es...>> {
    template<size_t INDEX = 0>
    static constexpr bool isZeroBasedContiguousEnum()
    {
        return (enumToUnderlyingType(e) == INDEX) ? ZeroBasedContiguousEnumChecker<T, EnumValues<E, es...>>::template isZeroBasedContiguousEnum<INDEX + 1>() : false;
    }
};

template<typename T, typename E>
struct ZeroBasedContiguousEnumChecker<T, EnumValues<E>> {
    template<size_t>
    static constexpr bool isZeroBasedContiguousEnum()
    {
        return true;
    }
};

template<typename E, typename = std::enable_if_t<!std::is_same_v<std::underlying_type_t<E>, bool>>>
constexpr bool isZeroBasedContiguousEnum()
{
    return ZeroBasedContiguousEnumChecker<std::underlying_type_t<E>, typename EnumTraits<E>::values>::isZeroBasedContiguousEnum();
}

#if COMPILER(CLANG)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wenum-constexpr-conversion"
#endif

#if COMPILER(CLANG) && __clang_major__ >= 16
template <typename E, auto V, typename = void>
inline constexpr bool isEnumConstexprStaticCastValid = false;
template <typename E, auto V>
inline constexpr bool isEnumConstexprStaticCastValid<E, V, std::void_t<std::integral_constant<E, static_cast<E>(V)>>> = true;
#else
template <typename, auto>
inline constexpr bool isEnumConstexprStaticCastValid = true;
#endif

template<typename E>
constexpr std::span<const char> enumTypeNameImpl()
{
    static_assert(std::is_enum_v<E>, "enumTypeName requires an enum type.");

#if COMPILER(CLANG)
    const size_t prefix = sizeof("std::span<const char> WTF::enumTypeNameImpl() [E = ") - 1;
    const size_t suffix = sizeof("]") - 1;
    std::span<const char> name = std::span { __PRETTY_FUNCTION__ }.subspan(prefix, sizeof(__PRETTY_FUNCTION__) - prefix - suffix - 1);
#elif COMPILER(GCC)
    const size_t prefix = sizeof("constexpr std::span<const char> WTF::enumTypeNameImpl() [with auto V = ") - 1;
    const size_t suffix = sizeof("]") - 1;
    std::span<const char> name = std::span { __PRETTY_FUNCTION__ }.subspan(prefix, sizeof(__PRETTY_FUNCTION__) - prefix - suffix - 1);
#else
#error "Unsupported compiler"
#endif
    for (size_t i = name.size(); i--;) {
        if (name[i] == ':')
            return name.subspan(i + 1);
    }
    return name;
}
template<typename E>
constexpr std::span<const char> enumTypeName()
{
    constexpr auto result = enumTypeNameImpl<std::decay_t<E>>(); // Force the span to be generated at compile time.
    return result;
}

template<auto V>
constexpr std::span<const char> enumNameImpl()
{
    static_assert(std::is_enum_v<decltype(V)>, "enumName requires an enum type.");

#if COMPILER(CLANG)
    const size_t prefix = sizeof("std::span<const char> WTF::enumNameImpl() [V = ") - 1;
    const size_t suffix = sizeof("]") - 1;
    auto name = std::span { __PRETTY_FUNCTION__ }.subspan(prefix, sizeof(__PRETTY_FUNCTION__) - prefix - suffix - 1);
    if (name[0] == '(' || name[0] == '-' || ('0' <= name[0] && name[0] <= '9'))
        return { };
#elif COMPILER(GCC)
    const size_t prefix = sizeof("constexpr std::span<const char> WTF::enumNameImpl() [with auto V = ") - 1;
    const size_t suffix = sizeof("]") - 1;
    std::span<const char> name = std::span { __PRETTY_FUNCTION__ }.subspan(prefix, sizeof(__PRETTY_FUNCTION__) - prefix - suffix - 1);
    if (name[0] == '(')
        name = { };
#else
#error "Unsupported compiler"
#endif
    for (std::size_t i = name.size(); i--;) {
        if (name[i] == ':')
            return name.subspan(i + 1);
    }
    return name;
}

template<auto V>
constexpr std::span<const char> enumName()
{
    constexpr auto result = enumNameImpl<V>(); // Force the span to be generated at compile time.
    return result;
}

template<typename E, auto V>
constexpr std::span<const char> enumName()
{
    if constexpr (isEnumConstexprStaticCastValid<E, V>)
        return enumName<static_cast<E>(V)>();
    else
        return { };
}

template<typename E>
constexpr std::underlying_type_t<E> enumNamesMin()
{
    using Underlying = std::underlying_type_t<E>;

    if constexpr (requires { EnumTraits<E>::min; }) {
        static_assert(std::is_same_v<std::remove_cv_t<decltype(EnumTraits<E>::min)>, Underlying>,
            "EnumTraits<E>::min must have the same type as the underlying type of the enum.");
        return EnumTraits<E>::min;
    }

    // Default for both signed and unsigned enums.
    return 0;
}

template<typename E>
constexpr std::underlying_type_t<E> enumNamesMax()
{
    using Underlying = std::underlying_type_t<E>;

    if constexpr (requires { EnumTraits<E>::max; }) {
        static_assert(std::is_same_v<std::remove_cv_t<decltype(EnumTraits<E>::max)>, Underlying>,
            "EnumTraits<E>::max must have the same type as the underlying type of the enum.");
        return EnumTraits<E>::max;
    }

    constexpr Underlying defaultMax = std::is_signed_v<Underlying>
        ? static_cast<Underlying>(INT8_MAX) : static_cast<Underlying>(UINT8_MAX);
    constexpr Underlying computedMax = (sizeof(E) > 1) ? static_cast<Underlying>(defaultMax << 1) : defaultMax;
    return computedMax;
}

template<typename E>
constexpr size_t enumNamesSize()
{
    constexpr auto min = enumNamesMin<E>();
    constexpr auto max = enumNamesMax<E>();
    static_assert(min <= max, "Invalid enum range: min must be <= max.");
    return static_cast<size_t>(max - min) + 1;
}

template<typename E, size_t... Is>
constexpr auto makeEnumNames(std::index_sequence<Is...>)
{
    constexpr auto min = enumNamesMin<E>();
    return std::array<std::span<const char>, sizeof...(Is)> {
        enumName<E, static_cast<std::underlying_type_t<E>>(Is) + min>()...
    };
}

template<typename E>
constexpr auto enumNames()
{
    constexpr size_t size = enumNamesSize<E>();
    return makeEnumNames<E>(std::make_index_sequence<size> { });
}

template<typename E>
constexpr std::span<const char> enumName(E v)
{
    static_assert(std::is_enum_v<E>, "enumName can only be used with enum types.");

    using Underlying = std::underlying_type_t<E>;
    using Unsigned = std::make_unsigned_t<Underlying>;

    constexpr auto names = enumNames<E>();
    constexpr Underlying min = enumNamesMin<E>();
    constexpr Underlying max = enumNamesMax<E>();

    Underlying value = static_cast<Underlying>(v);
    if (value < min || value > max)
        return { "enum out of range" };

    // Compute index safely using unsigned extension.
    size_t index = static_cast<size_t>(static_cast<Unsigned>(value - min));
    return names[index];
}

#if COMPILER(CLANG)
#pragma clang diagnostic pop
#endif

} // namespace WTF

using WTF::enumToUnderlyingType;
using WTF::isValidEnum;
using WTF::isZeroBasedContiguousEnum;
using WTF::enumTypeName;
using WTF::enumNames;
using WTF::enumName;
