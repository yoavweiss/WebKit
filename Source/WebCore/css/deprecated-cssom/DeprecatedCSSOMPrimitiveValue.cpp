/*
 * Copyright (C) 2016-2021 Apple Inc. All rights reserved.
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
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "DeprecatedCSSOMPrimitiveValue.h"

#include "CSSAttrValue.h"
#include "CSSColorValue.h"
#include "CSSCounterValue.h"
#include "CSSCustomIdentValue.h"
#include "CSSFontFamilyNameValue.h"
#include "CSSKeywordValue.h"
#include "CSSParserIdioms.h"
#include "CSSPrimitiveValue.h"
#include "CSSRectValue.h"
#include "CSSSerializationContext.h"
#include "CSSStringValue.h"
#include "CSSURLValue.h"
#include "CSSUnevaluatedCalc.h"
#include "DeprecatedCSSOMCounter.h"
#include "DeprecatedCSSOMRGBColor.h"
#include "DeprecatedCSSOMRect.h"

namespace WebCore {

Ref<DeprecatedCSSOMPrimitiveValue> DeprecatedCSSOMPrimitiveValue::create(Ref<const CSSValue> value, CSSStyleDeclaration& owner)
{
    return adoptRef(*new DeprecatedCSSOMPrimitiveValue(WTF::move(value), owner));
}

DeprecatedCSSOMPrimitiveValue::DeprecatedCSSOMPrimitiveValue(Ref<const CSSValue> value, CSSStyleDeclaration& owner)
    : DeprecatedCSSOMValue(owner)
    , m_value(value)
{
}

String DeprecatedCSSOMPrimitiveValue::cssText() const
{
    return protect(m_value)->cssText(CSS::defaultSerializationContext());
}

unsigned short DeprecatedCSSOMPrimitiveValue::cssValueType() const
{
    // These values are exposed in the DOM, but constants for them are not.
    constexpr unsigned short CSS_INITIAL = 4;
    constexpr unsigned short CSS_UNSET = 5;
    constexpr unsigned short CSS_REVERT = 6;

    RefPtr keywordValue = dynamicDowncast<CSSKeywordValue>(m_value);
    if (!keywordValue)
        return CSS_PRIMITIVE_VALUE;

    switch (keywordValue->valueID()) {
    case CSSValueInherit:
        return CSS_INHERIT;
    case CSSValueInitial:
        return CSS_INITIAL;
    case CSSValueUnset:
        return CSS_UNSET;
    case CSSValueRevert:
        return CSS_REVERT;
    default:
        return CSS_PRIMITIVE_VALUE;
    }
}

unsigned short DeprecatedCSSOMPrimitiveValue::primitiveType() const
{
    if (m_value->isCounter())
        return CSS_COUNTER;
    if (m_value->isRect())
        return CSS_RECT;
    if (m_value->isColor())
        return CSS_RGBCOLOR;
    if (m_value->isURL())
        return CSS_URI;
    if (m_value->isKeywordValue() || m_value->isCustomIdentValue())
        return CSS_IDENT;
    if (m_value->isStringValue() || m_value->isFontFamilyNameValue())
        return CSS_STRING;
    if (m_value->isAttrValue())
        return CSS_ATTR;

    RefPtr primitiveValue = dynamicDowncast<CSSPrimitiveValue>(m_value.get());
    if (!primitiveValue)
        return CSS_UNKNOWN;

    switch (primitiveValue->primitiveType()) {
    case CSSUnitType::CSS_CM:                           return CSS_CM;
    case CSSUnitType::CSS_DEG:                          return CSS_DEG;
    case CSSUnitType::CSS_EM:                           return CSS_EMS;
    case CSSUnitType::CSS_EX:                           return CSS_EXS;
    case CSSUnitType::CSS_GRAD:                         return CSS_GRAD;
    case CSSUnitType::CSS_HZ:                           return CSS_HZ;
    case CSSUnitType::CSS_INTEGER:                      return CSS_NUMBER;
    case CSSUnitType::CSS_IN:                           return CSS_IN;
    case CSSUnitType::CSS_KHZ:                          return CSS_KHZ;
    case CSSUnitType::CSS_MM:                           return CSS_MM;
    case CSSUnitType::CSS_MS:                           return CSS_MS;
    case CSSUnitType::CSS_NUMBER:                       return CSS_NUMBER;
    case CSSUnitType::CSS_PC:                           return CSS_PC;
    case CSSUnitType::CSS_PERCENTAGE:                   return CSS_PERCENTAGE;
    case CSSUnitType::CSS_PT:                           return CSS_PT;
    case CSSUnitType::CSS_PX:                           return CSS_PX;
    case CSSUnitType::CSS_RAD:                          return CSS_RAD;
    case CSSUnitType::CSS_S:                            return CSS_S;

    // All other, including newer types, should return UNKNOWN.
    default:                                            return CSS_UNKNOWN;
    }
}

ExceptionOr<float> DeprecatedCSSOMPrimitiveValue::getFloatValue(unsigned short unitType) const
{
    RefPtr primitiveValue = dynamicDowncast<CSSPrimitiveValue>(m_value.get());
    if (!primitiveValue)
        return Exception { ExceptionCode::InvalidAccessError };

    auto doubleValueDeprecated = [&] {
        return WTF::switchOn(*primitiveValue,
            [](const CSSPrimitiveValue::Calc& calc) {
                return calc.evaluateDeprecated();
            },
            [](const CSSPrimitiveValue::Raw& raw) {
                return raw.value;
            }
        );
    };

    if (unitType == CSS_DIMENSION)
        return clampTo<float>(doubleValueDeprecated());

    auto requestedUnitType = [&] -> std::optional<CSSUnitType> {
        switch (unitType) {
        case CSS_CM:            return CSSUnitType::CSS_CM;
        case CSS_DEG:           return CSSUnitType::CSS_DEG;
        case CSS_EMS:           return CSSUnitType::CSS_EM;
        case CSS_EXS:           return CSSUnitType::CSS_EX;
        case CSS_GRAD:          return CSSUnitType::CSS_GRAD;
        case CSS_HZ:            return CSSUnitType::CSS_HZ;
        case CSS_IN:            return CSSUnitType::CSS_IN;
        case CSS_KHZ:           return CSSUnitType::CSS_KHZ;
        case CSS_MM:            return CSSUnitType::CSS_MM;
        case CSS_MS:            return CSSUnitType::CSS_MS;
        case CSS_NUMBER:        return CSSUnitType::CSS_NUMBER;
        case CSS_PC:            return CSSUnitType::CSS_PC;
        case CSS_PERCENTAGE:    return CSSUnitType::CSS_PERCENTAGE;
        case CSS_PT:            return CSSUnitType::CSS_PT;
        case CSS_PX:            return CSSUnitType::CSS_PX;
        case CSS_RAD:           return CSSUnitType::CSS_RAD;
        case CSS_S:             return CSSUnitType::CSS_S;
        default:                return std::nullopt;
        }
    }();
    if (!requestedUnitType)
        return Exception { ExceptionCode::InvalidAccessError };
    auto targetUnitType = *requestedUnitType;

    auto selfUnitType = WTF::switchOn(*primitiveValue,
        [](const CSSPrimitiveValue::Calc&) -> std::optional<CSSUnitType> {
            return std::nullopt;
        },
        [](const CSSPrimitiveValue::Raw& raw) -> std::optional<CSSUnitType> {
            return raw.unit;
        }
    );
    if (!selfUnitType)
        return Exception { ExceptionCode::InvalidAccessError };
    auto sourceUnitType = *selfUnitType;

    if (targetUnitType == sourceUnitType)
        return clampTo<float>(doubleValueDeprecated());

    auto sourceCategory = unitCategory(sourceUnitType);
    ASSERT(sourceCategory != CSSUnitCategory::Other);
    auto targetCategory = unitCategory(targetUnitType);
    ASSERT(targetCategory != CSSUnitCategory::Other);

    // Cannot convert between unrelated unit categories if one of them is not CSSUnitCategory::Number.
    if (sourceCategory != targetCategory && sourceCategory != CSSUnitCategory::Number && targetCategory != CSSUnitCategory::Number)
        return Exception { ExceptionCode::InvalidAccessError };

    if (targetCategory == CSSUnitCategory::Number) {
        // Cannot convert between numbers and percent.
        if (sourceCategory == CSSUnitCategory::Percent)
            return Exception { ExceptionCode::InvalidAccessError };
        // We interpret conversion to CSSUnitType::CSS_NUMBER as conversion to a canonical unit in this value's category.
        targetUnitType = canonicalUnitTypeForCategory(sourceCategory);
        if (targetUnitType == CSSUnitType::CSS_UNKNOWN)
            return Exception { ExceptionCode::InvalidAccessError };
    }

    if (sourceUnitType == CSSUnitType::CSS_NUMBER || sourceUnitType == CSSUnitType::CSS_INTEGER) {
        // Cannot convert between numbers and percent.
        if (targetCategory == CSSUnitCategory::Percent)
            return Exception { ExceptionCode::InvalidAccessError };
        // We interpret conversion from CSSUnitType::CSS_NUMBER in the same way as CSSParser::validUnit() while using non-strict mode.
        sourceUnitType = canonicalUnitTypeForCategory(targetCategory);
        if (sourceUnitType == CSSUnitType::CSS_UNKNOWN)
            return Exception { ExceptionCode::InvalidAccessError };
    }

    double convertedValue = doubleValueDeprecated();

    // If we don't need to scale it, don't worry about if we can scale it.
    if (sourceUnitType == targetUnitType)
        return clampTo<float>(convertedValue);

    // First convert the value from the source unit type the to the canonical type.
    auto sourceFactor = conversionToCanonicalUnitsScaleFactor(sourceUnitType);
    if (!sourceFactor.has_value())
        return Exception { ExceptionCode::InvalidAccessError };
    convertedValue *= sourceFactor.value();

    // Now convert from canonical type to the target unitType.
    auto targetFactor = conversionToCanonicalUnitsScaleFactor(targetUnitType);
    if (!targetFactor.has_value())
        return Exception { ExceptionCode::InvalidAccessError };
    convertedValue /= targetFactor.value();

    return clampTo<float>(convertedValue);
}

ExceptionOr<String> DeprecatedCSSOMPrimitiveValue::getStringValue() const
{
    switch (primitiveType()) {
    case CSS_ATTR:
        return downcast<CSSAttrValue>(m_value.get()).cssText(CSS::defaultSerializationContext());
    case CSS_IDENT:
        if (RefPtr customIdentValue = dynamicDowncast<CSSCustomIdentValue>(m_value))
            return customIdentValue->stringValue();
        return downcast<CSSKeywordValue>(m_value.get()).stringValue();
    case CSS_STRING:
        if (RefPtr fontFamilyNameValue = dynamicDowncast<CSSFontFamilyNameValue>(m_value))
            return fontFamilyNameValue->stringValue();
        return downcast<CSSStringValue>(m_value.get()).stringValue();
    case CSS_URI:
        return downcast<CSSURLValue>(m_value.get()).stringValue();

    // All other, including newer types, should raise an exception.
    default:
        return Exception { ExceptionCode::InvalidAccessError };
    }
}

ExceptionOr<Ref<DeprecatedCSSOMCounter>> DeprecatedCSSOMPrimitiveValue::getCounterValue() const
{
    if (RefPtr value = dynamicDowncast<CSSCounterValue>(m_value.get())) {
        auto counterStyle = WTF::switchOn(value->counterStyle().identifier,
            [](const CSS::Keyword& predefinedKeyword) -> String {
                return nameLiteralForSerialization(predefinedKeyword.value);
            },
            [](const CSS::CustomIdent& customIdent) -> String {
                return customIdent.value.string();
            }
        );
        return DeprecatedCSSOMCounter::create(value->identifier().value, value->separator().value, WTF::move(counterStyle));
    }
    return Exception { ExceptionCode::InvalidAccessError };
}

ExceptionOr<Ref<DeprecatedCSSOMRect>> DeprecatedCSSOMPrimitiveValue::getRectValue() const
{
    if (RefPtr rectValue = dynamicDowncast<CSSRectValue>(m_value.get()))
        return DeprecatedCSSOMRect::create(rectValue->rect(), m_owner);
    return Exception { ExceptionCode::InvalidAccessError };
}

ExceptionOr<Ref<DeprecatedCSSOMRGBColor>> DeprecatedCSSOMPrimitiveValue::getRGBColorValue() const
{
    if (primitiveType() != CSS_RGBCOLOR)
        return Exception { ExceptionCode::InvalidAccessError };
    return DeprecatedCSSOMRGBColor::create(downcast<CSSColorValue>(m_value.get()).color().absoluteColor(), m_owner);
}

bool DeprecatedCSSOMPrimitiveValue::isCSSWideKeyword() const
{
    if (RefPtr keywordValue = dynamicDowncast<CSSKeywordValue>(m_value))
        return WebCore::isCSSWideKeyword(keywordValue->valueID());
    return false;
}

} // namespace WebCore
