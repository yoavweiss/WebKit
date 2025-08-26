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

#include "config.h"
#include "StyleTextDecorationLine.h"

#include "AnimationUtilities.h"
#include "CSSPrimitiveValue.h"
#include "CSSPrimitiveValueMappings.h"
#include "CSSValueList.h"
#include "StyleBuilderChecking.h"
#include "StyleValueTypes.h"
#include <wtf/Assertions.h>

namespace WebCore {
namespace Style {

void TextDecorationLine::addOrReplaceIfNotNone(const TextDecorationLine& value)
{
    value.switchOn(
        [&](CSS::Keyword::None) {
        },
        [&](CSS::Keyword::SpellingError keyword) {
            m_value = keyword;
        },
        [&](CSS::Keyword::GrammarError) {
            ASSERT_NOT_IMPLEMENTED_YET();
        },
        [&](const OptionSet<TextDecorationLineFlags>& newValue) {
            if (auto* currentValue = std::get_if<OptionSet<TextDecorationLineFlags>>(&m_value)) {
                m_value = *currentValue | newValue;
                return;
            }
            m_value = newValue;
        }
    );
}

// MARK: - Conversion

auto CSSValueConversion<TextDecorationLine>::operator()(BuilderState& state, const CSSValue& value) -> TextDecorationLine
{
    auto invalidValue = [&state]() -> TextDecorationLine {
        state.setCurrentPropertyInvalidAtComputedValueTime();
        return CSS::Keyword::None { };
    };

    if (RefPtr primitiveValue = dynamicDowncast<CSSPrimitiveValue>(value)) {
        if (primitiveValue->isValueID()) {
            switch (primitiveValue->valueID()) {
            case CSSValueNone:
                return CSS::Keyword::None { };
            case CSSValueSpellingError:
                return CSS::Keyword::SpellingError { };
            case CSSValueGrammarError:
                return CSS::Keyword::GrammarError { };
            default:
                break;
            }
        }

        return invalidValue();
    }

    if (RefPtr valueList = dynamicDowncast<CSSValueList>(value)) {
        OptionSet<TextDecorationLineFlags> flags;

        for (Ref item : *valueList) {
            RefPtr primitiveValue = requiredDowncast<CSSPrimitiveValue>(state, item);
            if (!primitiveValue)
                return invalidValue();

            switch (primitiveValue->valueID()) {
            case CSSValueUnderline:
                flags.add(TextDecorationLineFlags::Underline);
                break;
            case CSSValueOverline:
                flags.add(TextDecorationLineFlags::Overline);
                break;
            case CSSValueLineThrough:
                flags.add(TextDecorationLineFlags::LineThrough);
                break;
            case CSSValueBlink:
                flags.add(TextDecorationLineFlags::Blink);
                break;
            default:
                return invalidValue();
            }
        }

        if (flags.isEmpty())
            return invalidValue();

        return flags;
    }

    return invalidValue();
}

Ref<CSSValue> CSSValueCreation<OptionSet<TextDecorationLineFlags>>::operator()(CSSValuePool&, const RenderStyle&, const OptionSet<TextDecorationLineFlags>& value)
{
    ASSERT(!value.isEmpty());

    CSSValueListBuilder list;
    if (value.contains(TextDecorationLineFlags::Underline))
        list.append(CSSPrimitiveValue::create(CSSValueUnderline));
    if (value.contains(TextDecorationLineFlags::Overline))
        list.append(CSSPrimitiveValue::create(CSSValueOverline));
    if (value.contains(TextDecorationLineFlags::LineThrough))
        list.append(CSSPrimitiveValue::create(CSSValueLineThrough));
    if (value.contains(TextDecorationLineFlags::Blink))
        list.append(CSSPrimitiveValue::create(CSSValueBlink));
    return CSSValueList::createSpaceSeparated(WTFMove(list));
}

// MARK: - Serialization

void Serialize<OptionSet<TextDecorationLineFlags>>::operator()(StringBuilder& builder, const CSS::SerializationContext&, const RenderStyle&, const OptionSet<TextDecorationLineFlags>& value)
{
    ASSERT(!value.isEmpty());

    bool listEmpty = true;
    auto appendOption = [&](TextDecorationLineFlags option, CSSValueID valueID) {
        if (value.contains(option)) {
            if (!listEmpty)
                builder.append(' ');
            builder.append(nameLiteralForSerialization(valueID));
            listEmpty = false;
        }
    };
    appendOption(TextDecorationLineFlags::Underline, CSSValueUnderline);
    appendOption(TextDecorationLineFlags::Overline, CSSValueOverline);
    appendOption(TextDecorationLineFlags::LineThrough, CSSValueLineThrough);
    // Blink value is ignored for rendering but not for the computed value.
    appendOption(TextDecorationLineFlags::Blink, CSSValueBlink);
}

// MARK: - Logging

WTF::TextStream& operator<<(WTF::TextStream& ts, const TextDecorationLine& decoration)
{
    decoration.switchOn(
        [&](CSS::Keyword::None) {
            ts << "none";
        },
        [&](CSS::Keyword::SpellingError) {
            ts << "spelling-error";
        },
        [&](CSS::Keyword::GrammarError) {
            ts << "grammar-error";
        },
        [&](const OptionSet<TextDecorationLineFlags>& flags) {
            bool needsSpace = false;
            auto streamFlag = [&](TextDecorationLineFlags flag, CSSValueID valueID) {
                if (flags.contains(flag)) {
                    if (needsSpace)
                        ts << " ";
                    ts << nameLiteralForSerialization(valueID);
                    needsSpace = true;
                }
            };
            streamFlag(TextDecorationLineFlags::Underline, CSSValueUnderline);
            streamFlag(TextDecorationLineFlags::Overline, CSSValueOverline);
            streamFlag(TextDecorationLineFlags::LineThrough, CSSValueLineThrough);
            streamFlag(TextDecorationLineFlags::Blink, CSSValueBlink);
        }
    );
    return ts;
}

} // namespace Style

} // namespace WebCore
