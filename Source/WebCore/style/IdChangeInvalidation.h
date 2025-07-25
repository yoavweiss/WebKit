/*
 * Copyright (C) 2016-2025 Apple Inc. All rights reserved.
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

#pragma once

#include "Element.h"
#include "StyleInvalidator.h"

namespace WebCore {

namespace Style {

class IdChangeInvalidation {
public:
    IdChangeInvalidation(Ref<Element>&&, const AtomString& oldId, const AtomString& newId);
    ~IdChangeInvalidation();

private:
    void invalidateStyle(const AtomString&);
    void invalidateStyleWithRuleSets();

    const bool m_isEnabled;
    const Ref<Element> m_element;

    AtomString m_newId;

    Invalidator::MatchElementRuleSets m_matchElementRuleSets;
};

inline IdChangeInvalidation::IdChangeInvalidation(Ref<Element>&& element, const AtomString& oldId, const AtomString& newId)
    : m_isEnabled(element->needsStyleInvalidation())
    , m_element(WTFMove(element))
{
    if (!m_isEnabled)
        return;
    if (oldId == newId)
        return;
    m_newId = newId;

    invalidateStyle(oldId);
    invalidateStyleWithRuleSets();
}

inline IdChangeInvalidation::~IdChangeInvalidation()
{
    if (!m_isEnabled)
        return;
    invalidateStyle(m_newId);
    invalidateStyleWithRuleSets();
}

}
}
