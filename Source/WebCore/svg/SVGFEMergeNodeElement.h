/*
 * Copyright (C) 2004, 2005 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006 Rob Buis <buis@kde.org>
 * Copyright (C) 2018-2019 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include "SVGAnimatedString.h"
#include "SVGElement.h"
#include <wtf/TZoneMalloc.h>

namespace WebCore {

class SVGFEMergeNodeElement final : public SVGElement {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(SVGFEMergeNodeElement);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(SVGFEMergeNodeElement);
public:
    static Ref<SVGFEMergeNodeElement> create(const QualifiedName&, Document&);

    String in1() const { return m_in1->currentValue(); }
    SVGAnimatedString& in1Animated() { return m_in1; }

    using PropertyRegistry = SVGPropertyOwnerRegistry<SVGFEMergeNodeElement, SVGElement>;

private:
    SVGFEMergeNodeElement(const QualifiedName&, Document&);

    void attributeChanged(const QualifiedName&, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason) final;
    void svgAttributeChanged(const QualifiedName&) final;

    bool rendererIsNeeded(const RenderStyle&) final { return false; }

    Ref<SVGAnimatedString> m_in1 { SVGAnimatedString::create(this) };
};

} // namespace WebCore
