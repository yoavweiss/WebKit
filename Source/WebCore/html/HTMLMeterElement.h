/*
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
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
 *
 */

#pragma once

#include "HTMLElement.h"

namespace WebCore {

class MeterValueElement;
class RenderMeter;

class HTMLMeterElement final : public HTMLElement {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(HTMLMeterElement);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(HTMLMeterElement);
public:
    static Ref<HTMLMeterElement> create(const QualifiedName&, Document&);

    enum GaugeRegion {
        GaugeRegionOptimum,
        GaugeRegionSuboptimal,
        GaugeRegionEvenLessGood
    };

    double min() const;
    double max() const;
    double value() const;
    double low() const;
    double high() const;
    double optimum() const;

    double valueRatio() const;
    GaugeRegion gaugeRegion() const;

    bool canContainRangeEndPoint() const final { return false; }

    bool isDevolvableWidget() const override { return true; }

private:
    HTMLMeterElement(const QualifiedName&, Document&);
    virtual ~HTMLMeterElement();

    RenderMeter* renderMeter() const;

    bool isLabelable() const final { return true; }

    RenderPtr<RenderElement> createElementRenderer(RenderStyle&&, const RenderTreePosition&) final;
    bool childShouldCreateRenderer(const Node&) const final;
    void attributeChanged(const QualifiedName&, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason) final;

    void didElementStateChange();
    void didAddUserAgentShadowRoot(ShadowRoot&) final;

    RefPtr<HTMLElement> m_valueElement;
};

} // namespace WebCore
