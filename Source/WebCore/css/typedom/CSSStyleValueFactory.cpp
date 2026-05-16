/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER “AS IS” AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"
#include "CSSStyleValueFactory.h"

#include "CSSAppleColorFilterValue.h"
#include "CSSBoxShadowPropertyValue.h"
#include "CSSCalcValue.h"
#include "CSSCustomIdentValue.h"
#include "CSSCustomPropertyValue.h"
#include "CSSEasingFunctionValue.h"
#include "CSSFilterValue.h"
#include "CSSGridLineValue.h"
#include "CSSGridTemplateListValue.h"
#include "CSSGridTrackSizesValue.h"
#include "CSSNumericFactory.h"
#include "CSSOMKeywordValue.h"
#include "CSSParser.h"
#include "CSSPropertyParser.h"
#include "CSSSerializationContext.h"
#include "CSSShorthandSubstitutionValue.h"
#include "CSSStyleImageValue.h"
#include "CSSStyleValue.h"
#include "CSSSubstitutionValue.h"
#include "CSSTextShadowPropertyValue.h"
#include "CSSTokenizer.h"
#include "CSSTransformListValue.h"
#include "CSSTransformValue.h"
#include "CSSURLValue.h"
#include "CSSUnitValue.h"
#include "CSSUnparsedValue.h"
#include "CSSValueList.h"
#include "CSSValuePool.h"
#include "CSSVariableData.h"
#include "ExceptionOr.h"
#include "RenderStyle.h"
#include "ScriptWrappableInlines.h"
#include "StylePropertiesInlines.h"
#include "StylePropertyShorthand.h"
#include "StyleURL.h"
#include <wtf/FixedVector.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/StringView.h>

namespace WebCore {

RefPtr<CSSStyleValue> CSSStyleValueFactory::constructStyleValueForShorthandSerialization(Document& document, const String& serialization)
{
    if (serialization.isNull())
        return nullptr;

    CSSTokenizer tokenizer(serialization);
    if (serialization.contains("var("_s))
        return CSSUnparsedValue::create(tokenizer.tokenRange());
    return CSSStyleValue::create(CSSSubstitutionValue::create(tokenizer.tokenRange(), { }, { document }));
}

ExceptionOr<RefPtr<CSSValue>> CSSStyleValueFactory::extractCSSValue(Document& document, const CSSPropertyID& propertyID, const String& cssText)
{
    auto styleDeclaration = MutableStyleProperties::create();
    auto parseResult = CSSParser::parseValue(styleDeclaration, propertyID, cssText, IsImportant::Yes, { document });

    if (parseResult == CSSParser::ParseResult::Error)
        return Exception { ExceptionCode::TypeError, makeString(cssText, " cannot be parsed."_s) };

    return styleDeclaration->getPropertyCSSValue(propertyID);
}

ExceptionOr<RefPtr<CSSStyleValue>> CSSStyleValueFactory::extractShorthandCSSValues(Document& document, const CSSPropertyID& propertyID, const String& cssText)
{
    auto styleDeclaration = MutableStyleProperties::create();
    auto parseResult = CSSParser::parseValue(styleDeclaration, propertyID, cssText, IsImportant::Yes, { document });

    if (parseResult == CSSParser::ParseResult::Error)
        return Exception { ExceptionCode::TypeError, makeString(cssText, " cannot be parsed."_s) };

    return constructStyleValueForShorthandSerialization(document, styleDeclaration->getPropertyValue(propertyID));
}

ExceptionOr<Ref<CSSUnparsedValue>> CSSStyleValueFactory::extractCustomCSSValues(const String& cssText)
{
    CSSTokenizer tokenizer(cssText);
    return { CSSUnparsedValue::create(tokenizer.tokenRange()) };
}

// https://www.w3.org/TR/css-typed-om-1/#cssstylevalue
ExceptionOr<Vector<Ref<CSSStyleValue>>> CSSStyleValueFactory::parseStyleValue(Document& document, const AtomString& cssProperty, const String& cssText, bool parseMultiple)
{
    if (cssText.isEmpty())
        return Exception { ExceptionCode::TypeError, "Value cannot be parsed."_s };

    // Extract the CSSValue from cssText given cssProperty
    if (isCustomPropertyName(cssProperty)) {
        auto result = extractCustomCSSValues(cssText);
        if (result.hasException())
            return result.releaseException();
        return Vector { Ref<CSSStyleValue> { result.releaseReturnValue() } };
    }

    auto property = cssProperty.convertToASCIILowercase();
    auto propertyID = cssPropertyID(property);

    if (propertyID == CSSPropertyInvalid)
        return Exception { ExceptionCode::TypeError, "Property String is not a valid CSS property."_s };

    if (isShorthand(propertyID)) {
        auto result = extractShorthandCSSValues(document, propertyID, cssText);
        if (result.hasException())
            return result.releaseException();
        auto cssValue = result.releaseReturnValue();
        if (!cssValue)
            return Vector<Ref<CSSStyleValue>> { };
        return Vector { cssValue.releaseNonNull() };
    }

    auto result = extractCSSValue(document, propertyID, cssText);
    if (result.hasException())
        return result.releaseException();

    Vector<Ref<CSSValue>> cssValues;
    if (auto cssValue = result.releaseReturnValue()) {
        // https://drafts.css-houdini.org/css-typed-om/#subdivide-into-iterations
        if (CSSProperty::isListValuedProperty(propertyID)) {
            if (auto* values = dynamicDowncast<CSSValueContainingVector>(*cssValue)) {
                for (Ref value : *values)
                    cssValues.append(Ref { const_cast<CSSValue&>(value.get()) });
            }
        }
        if (cssValues.isEmpty())
            cssValues.append(cssValue.releaseNonNull());
    }

    Vector<Ref<CSSStyleValue>> results;

    for (auto& cssValue : cssValues) {
        auto reifiedValue = reifyValue(document, WTF::move(cssValue), propertyID);
        if (reifiedValue.hasException())
            return reifiedValue.releaseException();

        results.append(reifiedValue.releaseReturnValue());
        
        if (!parseMultiple)
            break;
    }
    
    return results;
}

static bool NODELETE mayConvertCSSValueListToSingleValue(std::optional<CSSPropertyID> propertyID)
{
    if (!propertyID)
        return true;

    // Even though the CSS Parser uses a CSSValueList to represent these, they are not
    // really lists and CSS-Typed-OM does not expect them to treat them as such.
    return *propertyID != CSSPropertyGridRowStart
        && *propertyID != CSSPropertyGridRowEnd
        && *propertyID != CSSPropertyGridColumnStart
        && *propertyID != CSSPropertyGridColumnEnd;
}

template<CSS::Numeric T>
static ExceptionOr<Ref<CSSStyleValue>> reifyValue(const T& numeric)
{
    return WTF::switchOn(numeric,
        [&](const typename T::Calc& calc) -> ExceptionOr<Ref<CSSStyleValue>> {
            auto result = CSSNumericValue::reifyMathExpression(calc.calcValue().tree());
            if (result.hasException())
                return result.releaseException();
            return upcast<CSSStyleValue>(result.releaseReturnValue());
        },
        [&](const typename T::Raw& raw) -> ExceptionOr<Ref<CSSStyleValue>> {
            if constexpr (T::category == CSS::Category::Integer) {
                // Integer is special cased to resolved the same as <number>.
                return upcast<CSSStyleValue>(CSSUnitValue::create(raw.value, CSSUnitType::CSS_NUMBER));
            } else {
                return upcast<CSSStyleValue>(CSSUnitValue::create(raw.value, toCSSUnitType(raw.unit)));
            }
        }
    );
}

static ExceptionOr<Ref<CSSStyleValue>> reifyValue(const CSSPrimitiveValue& primitiveValue)
{
    return WTF::switchOn(primitiveValue,
        [&](const CSSPrimitiveValue::Calc& calc) -> ExceptionOr<Ref<CSSStyleValue>> {
            auto result = CSSNumericValue::reifyMathExpression(calc.tree());
            if (result.hasException())
                return result.releaseException();
            return upcast<CSSStyleValue>(result.releaseReturnValue());
        },
        [&](const CSSPrimitiveValue::Raw& raw) -> ExceptionOr<Ref<CSSStyleValue>> {
            if (raw.unit == CSSUnitType::CSS_INTEGER) {
                // Integer is special cased to resolved the same as <number>.
                return upcast<CSSStyleValue>(CSSUnitValue::create(raw.value, CSSUnitType::CSS_NUMBER));
            } else {
                return upcast<CSSStyleValue>(CSSUnitValue::create(raw.value, raw.unit));
            }
        }
    );
}

static ExceptionOr<Ref<CSSStyleValue>> reifyValue(CSSValueID keyword)
{
    // Per the specification, the CSSKeywordValue's value slot should be set to the serialization
    // of the identifier. As a result, the identifier will be lowercase:
    // https://drafts.css-houdini.org/css-typed-om-1/#reify-ident
    return upcast<CSSStyleValue>(CSSOMKeywordValue::rectifyKeywordish(nameLiteralForSerialization(keyword)));
}

static ExceptionOr<Ref<CSSStyleValue>> reifyValue(CSS::Keyword keyword)
{
    return WebCore::reifyValue(keyword.value);
}

static ExceptionOr<Ref<CSSStyleValue>> reifyValue(CSS::SpecificKeyword auto const& keyword)
{
    return WebCore::reifyValue(keyword.value);
}

static ExceptionOr<Ref<CSSStyleValue>> reifyValue(const CSS::CustomIdent& customIdent)
{
    return upcast<CSSStyleValue>(CSSOMKeywordValue::rectifyKeywordish(CSS::serializationForCSS(CSS::defaultSerializationContext(), customIdent)));
}

ExceptionOr<Ref<CSSStyleValue>> CSSStyleValueFactory::reifyValue(Document& document, const CSSValue& cssValue, std::optional<CSSPropertyID> propertyID)
{
    if (auto* primitiveValue = dynamicDowncast<CSSPrimitiveValue>(cssValue))
        return WebCore::reifyValue(*primitiveValue);
    else if (auto* keywordValue = dynamicDowncast<CSSKeywordValue>(cssValue))
        return WebCore::reifyValue(keywordValue->keyword());
    else if (auto* customIdentValue = dynamicDowncast<CSSCustomIdentValue>(cssValue))
        return WebCore::reifyValue(customIdentValue->customIdent());
    else if (auto* imageValue = dynamicDowncast<CSSImageValue>(cssValue))
        return Ref<CSSStyleValue> { CSSStyleImageValue::create(const_cast<CSSImageValue&>(*imageValue), document) };
    else if (auto* referenceValue = dynamicDowncast<CSSSubstitutionValue>(cssValue))
        return Ref<CSSStyleValue> { CSSUnparsedValue::create(referenceValue->data().tokenRange()) };
    else if (auto* substitutionValue = dynamicDowncast<CSSShorthandSubstitutionValue>(cssValue))
        return Ref<CSSStyleValue> { CSSUnparsedValue::create(substitutionValue->shorthandValue().data().tokenRange()) };
    else if (auto* customPropertyValue = dynamicDowncast<CSSCustomPropertyValue>(cssValue)) {
        // FIXME: remove CSSStyleValue::create(WTF::move(cssValue)), add reification control flow
        return WTF::switchOn(customPropertyValue->value(),
            [&](const Ref<CSSSubstitutionValue>& value) {
                return reifyValue(document, value, propertyID);
            },
            [&](const Ref<CSSVariableData>& value) {
                return reifyValue(document, CSSSubstitutionValue::create(value.copyRef()), propertyID);
            },
            [&](const CSSWideKeyword&) {
                return ExceptionOr<Ref<CSSStyleValue>> { CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue))) };
            }
        );
    } else if (RefPtr transformList = dynamicDowncast<CSSTransformListValue>(cssValue)) {
        auto transformValue = CSSTransformValue::create(transformList.releaseNonNull(), document);
        if (transformValue.hasException())
            return transformValue.releaseException();
        return Ref<CSSStyleValue> { transformValue.releaseReturnValue() };
    } else if (RefPtr property = dynamicDowncast<CSSFilterValue>(cssValue)) {
        return WTF::switchOn(property->filter(),
            [&](CSS::Keyword::None keyword) -> ExceptionOr<Ref<CSSStyleValue>> {
                return WebCore::reifyValue(keyword);
            },
            [&](const auto&) -> ExceptionOr<Ref<CSSStyleValue>> {
                return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
            }
        );
    } else if (RefPtr property = dynamicDowncast<CSSAppleColorFilterValue>(cssValue)) {
        return WTF::switchOn(property->filter(),
            [&](CSS::Keyword::None keyword) -> ExceptionOr<Ref<CSSStyleValue>> {
                return WebCore::reifyValue(keyword);
            },
            [&](const auto&) -> ExceptionOr<Ref<CSSStyleValue>> {
                return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
            }
        );
    } else if (RefPtr property = dynamicDowncast<CSSBoxShadowPropertyValue>(cssValue)) {
        return WTF::switchOn(property->shadow(),
            [&](CSS::Keyword::None keyword) -> ExceptionOr<Ref<CSSStyleValue>> {
                return WebCore::reifyValue(keyword);
            },
            [&](const auto&) -> ExceptionOr<Ref<CSSStyleValue>> {
                return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
            }
        );
    } else if (RefPtr property = dynamicDowncast<CSSTextShadowPropertyValue>(cssValue)) {
        return WTF::switchOn(property->shadow(),
            [&](CSS::Keyword::None keyword) -> ExceptionOr<Ref<CSSStyleValue>> {
                return WebCore::reifyValue(keyword);
            },
            [&](const auto&) -> ExceptionOr<Ref<CSSStyleValue>> {
                return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
            }
        );
    } else if (RefPtr property = dynamicDowncast<CSSEasingFunctionValue>(cssValue)) {
        return WTF::switchOn(property->easingFunction(),
            [&](CSS::SpecificKeyword auto const& keyword) -> ExceptionOr<Ref<CSSStyleValue>> {
                return WebCore::reifyValue(keyword);
            },
            [&](const auto&) -> ExceptionOr<Ref<CSSStyleValue>> {
                return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
            }
        );
    } else if (RefPtr property = dynamicDowncast<CSSGridLineValue>(cssValue)) {
        return WTF::switchOn(property->line(),
            [&](CSS::Keyword::Auto keyword) -> ExceptionOr<Ref<CSSStyleValue>> {
                return WebCore::reifyValue(keyword);
            },
            [&](const CSS::CustomIdent& customIdent) -> ExceptionOr<Ref<CSSStyleValue>> {
                return WebCore::reifyValue(customIdent);
            },
            [&](const auto&) -> ExceptionOr<Ref<CSSStyleValue>> {
                return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
            }
        );
    } else if (RefPtr property = dynamicDowncast<CSSGridTemplateListValue>(cssValue)) {
        return WTF::switchOn(property->list(),
            [&](CSS::Keyword::None keyword) -> ExceptionOr<Ref<CSSStyleValue>> {
                return WebCore::reifyValue(keyword);
            },
            [&](const CSS::GridTrackList& trackList) -> ExceptionOr<Ref<CSSStyleValue>> {
                if (trackList.size() == 1) {
                    return WTF::switchOn(trackList[0],
                        [&](const CSS::GridLineNames&) -> ExceptionOr<Ref<CSSStyleValue>> {
                            return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
                        },
                        [&](const CSS::GridTrackSize& trackSize) -> ExceptionOr<Ref<CSSStyleValue>> {
                            return WTF::switchOn(trackSize,
                                [&](const CSS::GridTrackBreadth& breadth) -> ExceptionOr<Ref<CSSStyleValue>> {
                                    return WTF::switchOn(breadth,
                                        [&](const auto& value) -> ExceptionOr<Ref<CSSStyleValue>> {
                                            return WebCore::reifyValue(value);
                                        }
                                    );
                                },
                                [&](const CSS::GridMinMaxFunction&) -> ExceptionOr<Ref<CSSStyleValue>> {
                                    return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
                                },
                                [&](const CSS::GridFitContentFunction&) -> ExceptionOr<Ref<CSSStyleValue>> {
                                    return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
                                }
                            );
                        },
                        [&](const CSS::GridTrackRepeatFunction&) -> ExceptionOr<Ref<CSSStyleValue>> {
                            return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
                        }
                    );
                }
                return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
            },
            [&](const auto&) -> ExceptionOr<Ref<CSSStyleValue>> {
                return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
            }
        );
    } else if (RefPtr property = dynamicDowncast<CSSGridTrackSizesValue>(cssValue)) {
        if (property->list().size() == 1) {
            return WTF::switchOn(property->list()[0],
                [&](const CSS::GridTrackBreadth& breadth) -> ExceptionOr<Ref<CSSStyleValue>> {
                    return WTF::switchOn(breadth,
                        [&](const auto& value) -> ExceptionOr<Ref<CSSStyleValue>> {
                            return WebCore::reifyValue(value);
                        }
                    );
                },
                [&](const CSS::GridMinMaxFunction&) -> ExceptionOr<Ref<CSSStyleValue>> {
                    return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
                },
                [&](const CSS::GridFitContentFunction&) -> ExceptionOr<Ref<CSSStyleValue>> {
                    return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
                }
            );
        }
        return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
    } else if (auto* valueList = dynamicDowncast<CSSValueList>(cssValue)) {
        // Reifying the first value in value list.
        // FIXME: Verify this is the expected behavior.
        // Refer to LayoutTests/imported/w3c/web-platform-tests/css/css-typed-om/the-stylepropertymap/inline/get.html
        if (!valueList->length())
            return Exception { ExceptionCode::TypeError, "The CSSValueList should not be empty."_s };
        if ((valueList->length() == 1 && mayConvertCSSValueListToSingleValue(propertyID)) || (propertyID && CSSProperty::isListValuedProperty(*propertyID)))
            return reifyValue(document, protect((*valueList)[0]), propertyID);
    }

    return CSSStyleValue::create(Ref(const_cast<CSSValue&>(cssValue)));
}

ExceptionOr<Vector<Ref<CSSStyleValue>>> CSSStyleValueFactory::vectorFromStyleValuesOrStrings(Document& document, const AtomString& property, FixedVector<Variant<Ref<CSSStyleValue>, String>>&& values)
{
    Vector<Ref<CSSStyleValue>> styleValues;
    for (auto&& value : WTF::move(values)) {
        auto exception = switchOn(WTF::move(value),
            [&](Ref<CSSStyleValue>&& styleValue) -> std::optional<Exception> {
                styleValues.append(WTF::move(styleValue));
                return std::nullopt;
            },
            [&](String&& string) -> std::optional<Exception> {
                constexpr bool parseMultiple = true;
                auto result = CSSStyleValueFactory::parseStyleValue(document, property, string, parseMultiple);
                if (result.hasException())
                    return result.releaseException();
                styleValues.appendVector(result.releaseReturnValue());
                return std::nullopt;
            }
        );
        if (exception)
            return { WTF::move(*exception) };
    }
    return { WTF::move(styleValues) };
}

CSSStyleValueFactory::~CSSStyleValueFactory() = default;

} // namespace WebCore
