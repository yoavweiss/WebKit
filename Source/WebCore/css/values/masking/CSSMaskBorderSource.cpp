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
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CSSMaskBorderSource.h"

#include "CSSMaskBorderSourceValue.h"

namespace WebCore {
namespace CSS {

// MARK: - CSSValue Visitation

auto CSSValueChildrenVisitor<MaskBorderSource>::operator()(NOESCAPE const Function<IterationStatus(CSSValue&)>& function, const MaskBorderSource& value) -> IterationStatus
{
    return WTF::switchOn(value,
        [&](CSS::Keyword::None) {
            return IterationStatus::Continue;
        },
        [&](Ref<CSSValue> cssValue) {
            return function(cssValue);
        }
    );
}

// MARK: - Conversion

auto CSSValueCreation<MaskBorderSource>::operator()(CSSValuePool&, const MaskBorderSource& value) -> Ref<CSSValue>
{
    return CSSMaskBorderSourceValue::create(MaskBorderSource { value });
}

// MARK: - Serialization

void Serialize<MaskBorderSource>::operator()(StringBuilder& builder, const SerializationContext& context, const MaskBorderSource& value)
{
    WTF::switchOn(value,
        [&](CSS::Keyword::None keyword) {
            CSS::serializationForCSS(builder, context, keyword);
        },
        [&](Ref<CSSValue> cssValue) {
            builder.append(cssValue->cssText(context));
        }
    );
}

} // namespace CSS
} // namespace WebCore
