/*
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

#pragma once

#include <WebCore/FloatConversion.h>
#include <WebCore/FloatPoint.h>
#include <WebCore/FloatSize.h>
#include <WebCore/LayoutPoint.h>
#include <WebCore/LayoutSize.h>
#include <WebCore/LayoutUnit.h>
#include <WebCore/StylePrimitiveNumericTypes+Calculation.h>
#include <WebCore/StylePrimitiveNumericTypes.h>
#include <WebCore/StyleValueTypes.h>

namespace WebCore {
namespace Style {

using namespace CSS::Literals;

// MARK: - Length

template<auto R, typename V> struct Evaluation<Length<R, V>> {
    constexpr typename Length<R, V>::ResolvedValueType operator()(const Length<R, V>& value, ZoomNeeded token)
        requires (R.zoomOptions == CSS::RangeZoomOptions::Default)
    {
        return static_cast<typename Length<R, V>::ResolvedValueType>(value.resolveZoom(token));
    }
    template<typename Reference> constexpr auto operator()(const Length<R, V>& value, Reference, ZoomNeeded token) -> Reference
        requires (R.zoomOptions == CSS::RangeZoomOptions::Default)
    {
        return Reference(value.resolveZoom(token));
    }

    constexpr typename Length<R, V>::ResolvedValueType operator()(const Length<R, V>& value, float zoom)
        requires (R.zoomOptions == CSS::RangeZoomOptions::Unzoomed)
    {
        return static_cast<typename Length<R, V>::ResolvedValueType>(value.resolveZoom(zoom));
    }
    template<typename Reference> constexpr auto operator()(const Length<R, V>& value, Reference, float zoom) -> Reference
        requires (R.zoomOptions == CSS::RangeZoomOptions::Unzoomed)
    {
        return Reference(value.resolveZoom(zoom));
    }
};

// MARK: - Percentage

template<auto R, typename V> struct Evaluation<Percentage<R, V>> {
    constexpr typename Percentage<R, V>::ResolvedValueType operator()(const Percentage<R, V>& percentage)
    {
        return percentage.value / static_cast<typename Percentage<R, V>::ResolvedValueType>(100.0);
    }
    constexpr auto operator()(const Percentage<R, V>& percentage, float referenceLength) -> float
    {
        return static_cast<float>(percentage.value) / 100.0 * referenceLength;
    }
    constexpr auto operator()(const Percentage<R, V>& percentage, double referenceLength) -> double
    {
        return static_cast<double>(percentage.value) / 100.0 * referenceLength;
    }
    constexpr auto operator()(const Percentage<R, V>& percentage, LayoutUnit referenceLength) -> LayoutUnit
    {
        return LayoutUnit(percentage.value / 100.0 * static_cast<double>(referenceLength));
    }
};

// MARK: - Numeric

template<NonCompositeNumeric StyleType> struct Evaluation<StyleType> {
    constexpr typename StyleType::ResolvedValueType operator()(const StyleType& value)
    {
        return value.value;
    }
    template<typename Reference> constexpr auto operator()(const StyleType& value, Reference) -> Reference
    {
        return Reference(value.value);
    }
};

// MARK: - Calculation

template<> struct Evaluation<Ref<CalculationValue>> {
    template<typename Reference> auto operator()(Ref<CalculationValue> calculation, Reference referenceLength)
    {
        return Reference(calculation->evaluate(referenceLength));
    }
};

template<Calc Calculation> struct Evaluation<Calculation> {
    template<typename... Rest> decltype(auto) operator()(const Calculation& calculation, Rest&&... rest)
    {
        return evaluate(calculation.protectedCalculation(), std::forward<Rest>(rest)...);
    }
};

// MARK: - LengthPercentage

template<auto R, typename V> struct Evaluation<LengthPercentage<R, V>> {
    template<typename Reference> constexpr auto operator()(const LengthPercentage<R, V>& lengthPercentage, Reference referenceLength, ZoomNeeded token) -> Reference
        requires (R.zoomOptions == CSS::RangeZoomOptions::Default)
    {
        return WTF::switchOn(lengthPercentage,
            [&](const typename LengthPercentage<R, V>::Dimension& length) -> Reference {
                return Reference(evaluate(length, token));
            },
            [&](const typename LengthPercentage<R, V>::Percentage& percentage) -> Reference {
                return Reference(evaluate(percentage, referenceLength));
            },
            [&](const typename LengthPercentage<R, V>::Calc& calculation) -> Reference {
                return Reference(evaluate(calculation, referenceLength));
            }
        );
    }

    template<typename Reference> constexpr auto operator()(const LengthPercentage<R, V>& lengthPercentage, Reference referenceLength, float zoom) -> Reference
        requires (R.zoomOptions == CSS::RangeZoomOptions::Unzoomed)
    {
        return WTF::switchOn(lengthPercentage,
            [&](const typename LengthPercentage<R, V>::Dimension& length) -> Reference {
                return Reference(evaluate(length, zoom));
            },
            [&](const typename LengthPercentage<R, V>::Percentage& percentage) -> Reference {
                return Reference(evaluate(percentage, referenceLength));
            },
            [&](const typename LengthPercentage<R, V>::Calc& calculation) -> Reference {
                return Reference(evaluate(calculation, referenceLength));
            }
        );
    }
};

// MARK: - SpaceSeparatedPoint

template<typename T> struct Evaluation<SpaceSeparatedPoint<T>> {
    FloatPoint operator()(const SpaceSeparatedPoint<T>& value, FloatSize referenceBox)
        requires HasTwoParameterEvaluate<T, float>
    {
        return {
            evaluate(value.x(), referenceBox.width()),
            evaluate(value.y(), referenceBox.height())
        };
    }
    LayoutPoint operator()(const SpaceSeparatedPoint<T>& value, LayoutSize referenceBox)
        requires HasTwoParameterEvaluate<T, LayoutUnit>
    {
        return {
            evaluate(value.x(), referenceBox.width()),
            evaluate(value.y(), referenceBox.height())
        };
    }
    FloatPoint operator()(const SpaceSeparatedPoint<T>& value, FloatSize referenceBox, ZoomNeeded token)
        requires HasThreeParameterEvaluate<T, float, ZoomNeeded>
    {
        return {
            evaluate(value.x(), referenceBox.width(), token),
            evaluate(value.y(), referenceBox.height(), token)
        };
    }
    LayoutPoint operator()(const SpaceSeparatedPoint<T>& value, LayoutSize referenceBox, ZoomNeeded token)
        requires HasThreeParameterEvaluate<T, LayoutUnit, ZoomNeeded>
    {
        return {
            evaluate(value.x(), referenceBox.width(), token),
            evaluate(value.y(), referenceBox.height(), token)
        };
    }
    FloatPoint operator()(const SpaceSeparatedPoint<T>& value, FloatSize referenceBox, float zoom)
        requires HasThreeParameterEvaluate<T, float, float>
    {
        return {
            evaluate(value.x(), referenceBox.width(), zoom),
            evaluate(value.y(), referenceBox.height(), zoom)
        };
    }
    LayoutPoint operator()(const SpaceSeparatedPoint<T>& value, LayoutSize referenceBox, float zoom)
        requires HasThreeParameterEvaluate<T, LayoutUnit, float>
    {
        return {
            evaluate(value.x(), referenceBox.width(), zoom),
            evaluate(value.y(), referenceBox.height(), zoom)
        };
    }
};

// MARK: - SpaceSeparatedSize

template<typename T> struct Evaluation<SpaceSeparatedSize<T>> {
    FloatSize operator()(const SpaceSeparatedSize<T>& value, FloatSize referenceBox)
        requires HasTwoParameterEvaluate<T, float>
    {
        return {
            evaluate(value.width(), referenceBox.width()),
            evaluate(value.height(), referenceBox.height())
        };
    }
    LayoutSize operator()(const SpaceSeparatedSize<T>& value, LayoutSize referenceBox)
        requires HasTwoParameterEvaluate<T, LayoutUnit>
    {
        return {
            evaluate(value.width(), referenceBox.width()),
            evaluate(value.height(), referenceBox.height())
        };
    }
    FloatSize operator()(const SpaceSeparatedSize<T>& value, FloatSize referenceBox, ZoomNeeded token)
        requires HasThreeParameterEvaluate<T, float, ZoomNeeded>
    {
        return {
            evaluate(value.width(), referenceBox.width(), token),
            evaluate(value.height(), referenceBox.height(), token)
        };
    }
    LayoutSize operator()(const SpaceSeparatedSize<T>& value, LayoutSize referenceBox, ZoomNeeded token)
        requires HasThreeParameterEvaluate<T, LayoutUnit, ZoomNeeded>
    {
        return {
            evaluate(value.width(), referenceBox.width(), token),
            evaluate(value.height(), referenceBox.height(), token)
        };
    }
    FloatSize operator()(const SpaceSeparatedSize<T>& value, FloatSize referenceBox, float zoom)
        requires HasThreeParameterEvaluate<T, float, float>
    {
        return {
            evaluate(value.width(), referenceBox.width(), zoom),
            evaluate(value.height(), referenceBox.height(), zoom)
        };
    }
    LayoutSize operator()(const SpaceSeparatedSize<T>& value, LayoutSize referenceBox, float zoom)
        requires HasThreeParameterEvaluate<T, LayoutUnit, float>
    {
        return {
            evaluate(value.width(), referenceBox.width(), zoom),
            evaluate(value.height(), referenceBox.height(), zoom)
        };
    }
};

// MARK: - MinimallySerializingSpaceSeparatedSize

template<typename T> struct Evaluation<MinimallySerializingSpaceSeparatedSize<T>> {
    FloatSize operator()(const MinimallySerializingSpaceSeparatedSize<T>& value, FloatSize referenceBox)
        requires HasTwoParameterEvaluate<T, float>
    {
        return {
            evaluate(value.width(), referenceBox.width()),
            evaluate(value.height(), referenceBox.height())
        };
    }
    LayoutSize operator()(const MinimallySerializingSpaceSeparatedSize<T>& value, LayoutSize referenceBox)
        requires HasTwoParameterEvaluate<T, LayoutUnit>
    {
        return {
            evaluate(value.width(), referenceBox.width()),
            evaluate(value.height(), referenceBox.height())
        };
    }
    FloatSize operator()(const MinimallySerializingSpaceSeparatedSize<T>& value, FloatSize referenceBox, ZoomNeeded token)
        requires HasThreeParameterEvaluate<T, float, ZoomNeeded>
    {
        return {
            evaluate(value.width(), referenceBox.width(), token),
            evaluate(value.height(), referenceBox.height(), token)
        };
    }
    LayoutSize operator()(const MinimallySerializingSpaceSeparatedSize<T>& value, LayoutSize referenceBox, ZoomNeeded token)
        requires HasThreeParameterEvaluate<T, LayoutUnit, ZoomNeeded>
    {
        return {
            evaluate(value.width(), referenceBox.width(), token),
            evaluate(value.height(), referenceBox.height(), token)
        };
    }
    FloatSize operator()(const MinimallySerializingSpaceSeparatedSize<T>& value, FloatSize referenceBox, float zoom)
        requires HasThreeParameterEvaluate<T, float, float>
    {
        return {
            evaluate(value.width(), referenceBox.width(), zoom),
            evaluate(value.height(), referenceBox.height(), zoom)
        };
    }
    LayoutSize operator()(const MinimallySerializingSpaceSeparatedSize<T>& value, LayoutSize referenceBox, float zoom)
        requires HasThreeParameterEvaluate<T, LayoutUnit, float>
    {
        return {
            evaluate(value.width(), referenceBox.width(), zoom),
            evaluate(value.height(), referenceBox.height(), zoom)
        };
    }
};

// MARK: - Calculated Evaluations

// Convert to `calc(100% - value)`.
template<auto R, typename V> auto reflect(const LengthPercentage<R, V>& value) -> LengthPercentage<R, V>
{
    using Result = LengthPercentage<R, V>;
    using Dimension = typename Result::Dimension;
    using Percentage = typename Result::Percentage;
    using Calc = typename Result::Calc;

    return WTF::switchOn(value,
        [&](const Dimension& value) -> Result {
            // If `value` is 0, we can avoid the `calc` altogether.
            if (value == 0_css_px)
                return 100_css_percentage;

            // Turn this into a calc expression: `calc(100% - value)`.
            return Calc { Calculation::subtract(Calculation::percentage(100), copyCalculation(value)) };
        },
        [&](const Percentage& value) -> Result {
            // If `value` is a percentage, we can avoid the `calc` altogether.
            return 100_css_percentage - value.value;
        },
        [&](const Calc& value) -> Result {
            // Turn this into a calc expression: `calc(100% - value)`.
            return Calc { Calculation::subtract(Calculation::percentage(100), copyCalculation(value)) };
        }
    );
}

// Merges the two ranges, `aR` and `bR`, creating a union of their ranges.
consteval CSS::Range mergeRanges(CSS::Range aR, CSS::Range bR)
{
    return CSS::Range { std::min(aR.min, bR.min), std::max(aR.max, bR.max) };
}

// Convert to `calc(100% - (a + b))`.
//
// Returns a LengthPercentage with range, `resultR`, equal to union of the two input ranges `aR` and `bR`.
template<auto aR, auto bR, typename V> auto reflectSum(const LengthPercentage<aR, V>& a, const LengthPercentage<bR, V>& b) -> LengthPercentage<mergeRanges(aR, bR), V>
{
    constexpr auto resultR = mergeRanges(aR, bR);

    using Result = LengthPercentage<resultR, V>;
    using CalcResult = typename Result::Calc;
    using PercentageA = typename LengthPercentage<aR, V>::Percentage;
    using PercentageB = typename LengthPercentage<bR, V>::Percentage;

    bool aIsZero = a.isZero();
    bool bIsZero = b.isZero();

    // If both `a` and `b` are 0, turn this into a calc expression: `calc(100% - (0 + 0))` aka `100%`.
    if (aIsZero && bIsZero)
        return 100_css_percentage;

    // If just `a` is 0, we can just consider the case of `calc(100% - b)`.
    if (aIsZero) {
        return WTF::switchOn(b,
            [&](const PercentageB& b) -> Result {
                // And if `b` is a percent, we can avoid the `calc` altogether.
                return 100_css_percentage - b.value;
            },
            [&](const auto& b) -> Result {
                // Otherwise, turn this into a calc expression: `calc(100% - b)`.
                return CalcResult { Calculation::subtract(Calculation::percentage(100), copyCalculation(b)) };
            }
        );
    }

    // If just `b` is 0, we can just consider the case of `calc(100% - a)`.
    if (bIsZero) {
        return WTF::switchOn(a,
            [&](const PercentageA& a) -> Result {
                // And if `a` is a percent, we can avoid the `calc` altogether.
                return 100_css_percentage - a.value;
            },
            [&](const auto& a) -> Result {
                // Otherwise, turn this into a calc expression: `calc(100% - a)`.
                return CalcResult { Calculation::subtract(Calculation::percentage(100), copyCalculation(a)) };
            }
        );
    }

    // If both and `a` and `b` are percentages, we can avoid the `calc` altogether.
    if (WTF::holdsAlternative<PercentageA>(a) && WTF::holdsAlternative<PercentageB>(b))
        return 100_css_percentage - (get<PercentageA>(a).value + get<PercentageB>(b).value);

    // Otherwise, turn this into a calc expression: `calc(100% - (a + b))`.
    return CalcResult { Calculation::subtract(Calculation::percentage(100), Calculation::add(copyCalculation(a), copyCalculation(b))) };
}

} // namespace Style
} // namespace WebCore
