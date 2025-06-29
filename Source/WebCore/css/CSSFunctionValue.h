/*
 * Copyright (C) 2008 Apple Inc. All rights reserved.
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

#include "CSSValueList.h"

namespace WebCore {

enum CSSValueID : uint16_t;

class CSSFunctionValue final : public CSSValueContainingVector {
public:
    static Ref<CSSFunctionValue> create(CSSValueID name, CSSValueListBuilder arguments);
    static Ref<CSSFunctionValue> create(CSSValueID name);
    static Ref<CSSFunctionValue> create(CSSValueID name, Ref<CSSValue> argument);
    static Ref<CSSFunctionValue> create(CSSValueID name, Ref<CSSValue> firstArgument, Ref<CSSValue> secondArgument);
    static Ref<CSSFunctionValue> create(CSSValueID name, Ref<CSSValue> firstArgument, Ref<CSSValue> secondArgument, Ref<CSSValue> thirdArgument);
    static Ref<CSSFunctionValue> create(CSSValueID name, Ref<CSSValue> firstArgument, Ref<CSSValue> secondArgument, Ref<CSSValue> thirdArgument, Ref<CSSValue> fourthArgument);

    CSSValueID name() const { return m_name; }

    String customCSSText(const CSS::SerializationContext&) const;
    bool equals(const CSSFunctionValue& other) const { return m_name == other.m_name && itemsEqual(other); }

private:
    friend bool CSSValue::addHash(Hasher&) const;

    CSSFunctionValue(CSSValueID name, CSSValueListBuilder);
    explicit CSSFunctionValue(CSSValueID name);
    CSSFunctionValue(CSSValueID name, Ref<CSSValue>);
    CSSFunctionValue(CSSValueID name, Ref<CSSValue>, Ref<CSSValue>);
    CSSFunctionValue(CSSValueID name, Ref<CSSValue>, Ref<CSSValue>, Ref<CSSValue>);
    CSSFunctionValue(CSSValueID name, Ref<CSSValue>, Ref<CSSValue>, Ref<CSSValue>, Ref<CSSValue>);

    bool addDerivedHash(Hasher&) const;

    CSSValueID m_name { };
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_CSS_VALUE(CSSFunctionValue, isFunctionValue())
