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

#include "config.h"
#include "DeprecatedCSSOMRect.h"

#include "Rect.h"

namespace WebCore {

Ref<DeprecatedCSSOMRect> DeprecatedCSSOMRect::create(const Rect& rect, CSSStyleDeclaration& owner)
{
    return adoptRef(*new DeprecatedCSSOMRect(rect, owner));
}

DeprecatedCSSOMRect::DeprecatedCSSOMRect(const Rect& rect, CSSStyleDeclaration& owner)
    : m_top(DeprecatedCSSOMPrimitiveValue::create(rect.top(), owner))
    , m_right(DeprecatedCSSOMPrimitiveValue::create(rect.right(), owner))
    , m_bottom(DeprecatedCSSOMPrimitiveValue::create(rect.bottom(), owner))
    , m_left(DeprecatedCSSOMPrimitiveValue::create(rect.left(), owner))
{
}

} // namespace WebCore
