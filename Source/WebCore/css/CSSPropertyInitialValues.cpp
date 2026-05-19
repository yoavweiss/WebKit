/*
 * Copyright (C) 2023-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2024-2025 Samuel Weinig <sam@webkit.org>
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

#include "config.h"
#include "CSSPropertyInitialValues.h"
#include "CSSPropertyInitialValuesGeneratedInlines.h"

#include "CSSBorderImageSliceValue.h"
#include "CSSBorderImageWidthValue.h"
#include "CSSCalcValue.h"
#include "CSSKeywordValueInlines.h"
#include "CSSOffsetRotateValue.h"
#include "CSSPrimitiveValue.h"
#include "CSSPropertyNames.h"
#include "CSSQuadValue.h"
#include "CSSUnits.h"
#include "CSSValueKeywords.h"
#include "CSSValuePair.h"
#include "RectBase.h"
#include <wtf/Variant.h>
#include <wtf/text/ASCIILiteral.h>

namespace WebCore {

static bool NODELETE isValueIDPair(const CSSValue& value, CSSValueID valueID)
{
    return value.isPair() && isValueID(value.first(), valueID) && isValueID(value.second(), valueID);
}

static bool NODELETE isNumber(const CSSPrimitiveValue& value, CSSPrimitiveValue::Raw number)
{
    return WTF::switchOn(value,
        [&](const CSSPrimitiveValue::Calc&) {
            return false;
        },
        [&](const CSSPrimitiveValue::Raw& raw) {
            return raw == number;
        }
    );
}

template<auto unit>
static bool NODELETE isNumber(const CSSPrimitiveValue& value, CSS::ValueLiteral<unit> literal)
{
    return WTF::switchOn(value,
        [&](const CSSPrimitiveValue::Calc&) {
            return false;
        },
        [&](const CSSPrimitiveValue::Raw& raw) {
            return raw.unit == toCSSUnitType(literal.unit)
                && raw.value == literal.value;
        }
    );
}

static bool NODELETE isNumber(const CSSPrimitiveValue* value, auto number)
{
    return value && isNumber(*value, number);
}

static bool NODELETE isNumber(const CSSValue& value, auto number)
{
    return isNumber(dynamicDowncast<CSSPrimitiveValue>(value), number);
}

static bool NODELETE isNumber(const RectBase& quad, auto number)
{
    return isNumber(quad.top(), number)
        && isNumber(quad.right(), number)
        && isNumber(quad.bottom(), number)
        && isNumber(quad.left(), number);
}

static bool NODELETE isValueID(const RectBase& quad, CSSValueID valueID)
{
    return isValueID(quad.top(), valueID)
        && isValueID(quad.right(), valueID)
        && isValueID(quad.bottom(), valueID)
        && isValueID(quad.left(), valueID);
}

static bool NODELETE isNumericQuad(const CSSValue& value, auto number)
{
    return value.isQuad() && isNumber(value.quad(), number);
}

bool isInitialValueForLonghand(CSSPropertyID longhand, const CSSValue& value)
{
    using namespace CSS::Literals;

    if (value.isImplicitInitialValue())
        return true;
    switch (longhand) {
    case CSSPropertyBackgroundSize:
    case CSSPropertyMaskSize:
        if (isValueIDPair(value, CSSValueAuto))
            return true;
        break;
    case CSSPropertyBorderImageOutset:
    case CSSPropertyMaskBorderOutset:
        if (isNumericQuad(value, 0_css_number))
            return true;
        break;
    case CSSPropertyBorderImageRepeat:
    case CSSPropertyMaskBorderRepeat:
        if (isValueIDPair(value, CSSValueStretch))
            return true;
        break;
    case CSSPropertyBorderImageSlice:
        if (auto sliceValue = dynamicDowncast<CSSBorderImageSliceValue>(value)) {
            if (!sliceValue->fill() && isNumber(sliceValue->slices(), 100_css_percentage))
                return true;
        }
        break;
    case CSSPropertyBorderImageWidth:
        if (auto widthValue = dynamicDowncast<CSSBorderImageWidthValue>(value)) {
            if (!widthValue->overridesBorderWidths() && isNumber(widthValue->widths(), 1_css_number))
                return true;
        }
        break;
    case CSSPropertyOffsetRotate:
        if (auto rotateValue = dynamicDowncast<CSSOffsetRotateValue>(value)) {
            if (rotateValue->isInitialValue())
                return true;
        }
        break;
    case CSSPropertyMaskBorderSlice:
        if (auto sliceValue = dynamicDowncast<CSSBorderImageSliceValue>(value)) {
            if (!sliceValue->fill() && isNumber(sliceValue->slices(), 0_css_number))
                return true;
        }
        return false;
    case CSSPropertyMaskBorderWidth:
        if (auto widthValue = dynamicDowncast<CSSBorderImageWidthValue>(value)) {
            if (!widthValue->overridesBorderWidths() && isValueID(widthValue->widths(), CSSValueAuto))
                return true;
        }
        break;
    default:
        break;
    }
    return WTF::switchOn(initialValueForLonghand(longhand),
        [&](CSSValueID initialValue) {
            return isValueID(value, initialValue);
        },
        [&](CSSPrimitiveValue::Raw initialValue) {
            return isNumber(value, initialValue);
        }
    );
}

ASCIILiteral initialValueTextForLonghand(CSSPropertyID longhand)
{
    return WTF::switchOn(initialValueForLonghand(longhand),
        [](CSSValueID value) {
            return nameLiteral(value);
        },
        [](CSSPrimitiveValue::Raw initialValue) {
            switch (initialValue.unit) {
            case CSSUnitType::CSS_NUMBER:
                if (initialValue.value == 0.0)
                    return "0"_s;
                if (initialValue.value == 1.0)
                    return "1"_s;
                if (initialValue.value == 2.0)
                    return "2"_s;
                if (initialValue.value == 4.0)
                    return "4"_s;
                if (initialValue.value == 8.0)
                    return "8"_s;
                break;
            case CSSUnitType::CSS_PERCENTAGE:
                if (initialValue.value == 0.0)
                    return "0%"_s;
                if (initialValue.value == 50.0)
                    return "50%"_s;
                if (initialValue.value == 100.0)
                    return "100%"_s;
                break;
            case CSSUnitType::CSS_PX:
                if (initialValue.value == 0.0)
                    return "0px"_s;
                if (initialValue.value == 1.0)
                    return "1px"_s;
                break;
            case CSSUnitType::CSS_S:
                if (initialValue.value == 0.0)
                    return "0s"_s;
                break;
            default:
                break;
            }
            ASSERT_NOT_REACHED();
            return ""_s;
        }
    );
}

CSSValueID initialValueIDForLonghand(CSSPropertyID longhand)
{
    return WTF::switchOn(initialValueForLonghand(longhand),
        [](CSSValueID value) {
            return value;
        },
        [](CSSPrimitiveValue::Raw) {
            return CSSValueInvalid;
        }
    );
}

} // namespace WebCore
