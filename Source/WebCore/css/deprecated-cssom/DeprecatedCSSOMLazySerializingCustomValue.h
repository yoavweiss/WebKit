/*
 * Copyright (C) 2026 Samuel Weinig <sam@webkit.org>
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "CSSValueTypes.h"
#include "DeprecatedCSSOMValue.h"
#include <wtf/Function.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

// This type is equivalent to `DeprecatedCSSOMComplexValue`, but is used for values that have no `CSSValue` wrapper.
// Lazy serialization is used to guard against the case of a user accessing a `DeprecatedCSSOMValue` just to check
// what its type is. If that is deemed to niche, we can simplify this type by eagerly serializing, avoiding the need
// to hold onto the serialization functor.
class DeprecatedCSSOMLazySerializingCustomValue final : public DeprecatedCSSOMValue {
public:
    using SerializationFunctor = Function<String(const CSS::SerializationContext&)>;

    static Ref<DeprecatedCSSOMLazySerializingCustomValue> create(SerializationFunctor&&, CSSStyleDeclaration&);

    String cssText() const;
    unsigned short cssValueType() const { return CSS_CUSTOM; }

private:
    DeprecatedCSSOMLazySerializingCustomValue(SerializationFunctor&&, CSSStyleDeclaration&);

    SerializationFunctor m_serializationFunctor;
    mutable String m_cachedSerialization;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_CSSOM_VALUE(DeprecatedCSSOMLazySerializingCustomValue, isLazySerializingCustomValue())
