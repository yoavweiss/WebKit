/*
 * Copyright (C) 2004, 2005, 2007, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2007, 2008 Rob Buis <buis@kde.org>
 * Copyright (C) 2018-2019 Apple Inc. All rights reserved.
 * Copyright (C) 2015 Google Inc. All rights reserved.
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

#include "config.h"
#include "SVGTextContentElement.h"

#include "CSSPropertyNames.h"
#include "CSSValueKeywords.h"
#include "ContainerNodeInlines.h"
#include "DOMPoint.h"
#include "FrameSelection.h"
#include "LegacyRenderSVGResource.h"
#include "LocalFrame.h"
#include "NodeInlines.h"
#include "RenderObjectInlines.h"
#include "RenderSVGText.h"
#include "SVGElementTypeHelpers.h"
#include "SVGNames.h"
#include "SVGParsingError.h"
#include "SVGPoint.h"
#include "SVGPropertyOwnerRegistry.h"
#include "SVGRect.h"
#include "SVGTextQuery.h"
#include "XMLNames.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(SVGTextContentElement);
 
SVGTextContentElement::SVGTextContentElement(const QualifiedName& tagName, Document& document, UniqueRef<SVGPropertyRegistry>&& propertyRegistry)
    : SVGGraphicsElement(tagName, document, WTFMove(propertyRegistry))
{
    static std::once_flag onceFlag;
    std::call_once(onceFlag, [] {
        PropertyRegistry::registerProperty<SVGNames::textLengthAttr, &SVGTextContentElement::m_textLength>();
        PropertyRegistry::registerProperty<SVGNames::lengthAdjustAttr, SVGLengthAdjustType, &SVGTextContentElement::m_lengthAdjust>();
    });
}

unsigned SVGTextContentElement::getNumberOfChars()
{
    protectedDocument()->updateLayoutIgnorePendingStylesheets({ LayoutOptions::TreatContentVisibilityHiddenAsVisible, LayoutOptions::TreatContentVisibilityAutoAsVisible }, this);
    return SVGTextQuery(checkedRenderer().get()).numberOfCharacters();
}

float SVGTextContentElement::getComputedTextLength()
{
    protectedDocument()->updateLayoutIgnorePendingStylesheets({ LayoutOptions::TreatContentVisibilityHiddenAsVisible, LayoutOptions::TreatContentVisibilityAutoAsVisible }, this);
    return SVGTextQuery(checkedRenderer().get()).textLength();
}

ExceptionOr<float> SVGTextContentElement::getSubStringLength(unsigned charnum, unsigned nchars)
{
    unsigned numberOfChars = getNumberOfChars();
    if (charnum >= numberOfChars)
        return Exception { ExceptionCode::IndexSizeError };

    nchars = std::min(nchars, numberOfChars - charnum);
    return SVGTextQuery(checkedRenderer().get()).subStringLength(charnum, nchars);
}

ExceptionOr<Ref<SVGPoint>> SVGTextContentElement::getStartPositionOfChar(unsigned charnum)
{
    if (charnum >= getNumberOfChars())
        return Exception { ExceptionCode::IndexSizeError };

    return SVGPoint::create(SVGTextQuery(checkedRenderer().get()).startPositionOfCharacter(charnum));
}

ExceptionOr<Ref<SVGPoint>> SVGTextContentElement::getEndPositionOfChar(unsigned charnum)
{
    if (charnum >= getNumberOfChars())
        return Exception { ExceptionCode::IndexSizeError };

    return SVGPoint::create(SVGTextQuery(checkedRenderer().get()).endPositionOfCharacter(charnum));
}

ExceptionOr<Ref<SVGRect>> SVGTextContentElement::getExtentOfChar(unsigned charnum)
{
    if (charnum >= getNumberOfChars())
        return Exception { ExceptionCode::IndexSizeError };

    return SVGRect::create(SVGTextQuery(checkedRenderer().get()).extentOfCharacter(charnum));
}

ExceptionOr<float> SVGTextContentElement::getRotationOfChar(unsigned charnum)
{
    if (charnum >= getNumberOfChars())
        return Exception { ExceptionCode::IndexSizeError };

    return SVGTextQuery(checkedRenderer().get()).rotationOfCharacter(charnum);
}

int SVGTextContentElement::getCharNumAtPosition(DOMPointInit&& pointInit)
{
    protectedDocument()->updateLayoutIgnorePendingStylesheets({ LayoutOptions::TreatContentVisibilityHiddenAsVisible, LayoutOptions::TreatContentVisibilityAutoAsVisible }, this);
    FloatPoint transformPoint {static_cast<float>(pointInit.x), static_cast<float>(pointInit.y)};
    return SVGTextQuery(checkedRenderer().get()).characterNumberAtPosition(transformPoint);
}

ExceptionOr<void> SVGTextContentElement::selectSubString(unsigned charnum, unsigned nchars)
{
    unsigned numberOfChars = getNumberOfChars();
    if (charnum >= numberOfChars)
        return Exception { ExceptionCode::IndexSizeError };

    nchars = std::min(nchars, numberOfChars - charnum);

    RefPtr frame = document().frame();
    ASSERT(frame);
    CheckedRef selection = frame->selection();

    // Find selection start
    VisiblePosition start(firstPositionInNode(const_cast<SVGTextContentElement*>(this)));
    for (unsigned i = 0; i < charnum; ++i)
        start = start.next();

    // Find selection end
    VisiblePosition end(start);
    for (unsigned i = 0; i < nchars; ++i)
        end = end.next();

    selection->setSelection(VisibleSelection(start, end));

    return { };
}

bool SVGTextContentElement::hasPresentationalHintsForAttribute(const QualifiedName& name) const
{
    if (name.matches(XMLNames::spaceAttr))
        return true;
    return SVGGraphicsElement::hasPresentationalHintsForAttribute(name);
}

void SVGTextContentElement::collectPresentationalHintsForAttribute(const QualifiedName& name, const AtomString& value, MutableStyleProperties& style)
{
    if (name.matches(XMLNames::spaceAttr)) {
        if (value == "preserve"_s)
            addPropertyToPresentationalHintStyle(style, CSSPropertyWhiteSpaceCollapse, CSSValuePreserve);
        else
            addPropertyToPresentationalHintStyle(style, CSSPropertyWhiteSpaceCollapse, CSSValueCollapse);
        addPropertyToPresentationalHintStyle(style, CSSPropertyTextWrapMode, CSSValueNowrap);
        return;
    }

    SVGGraphicsElement::collectPresentationalHintsForAttribute(name, value, style);
}

void SVGTextContentElement::attributeChanged(const QualifiedName& name, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason attributeModificationReason)
{
    auto parseError = SVGParsingError::None;

    if (name == SVGNames::lengthAdjustAttr) {
        auto propertyValue = SVGPropertyTraits<SVGLengthAdjustType>::fromString(newValue);
        if (propertyValue > 0)
            m_lengthAdjust->setBaseValInternal<SVGLengthAdjustType>(propertyValue);
    } else if (name == SVGNames::textLengthAttr)
        m_textLength->setBaseValInternal(SVGLengthValue::construct(SVGLengthMode::Other, newValue, parseError, SVGLengthNegativeValuesMode::Forbid));

    reportAttributeParsingError(parseError, name, newValue);

    SVGGraphicsElement::attributeChanged(name, oldValue, newValue, attributeModificationReason);
}

void SVGTextContentElement::svgAttributeChanged(const QualifiedName& attrName)
{
    if (PropertyRegistry::isKnownAttribute(attrName)) {
        if (attrName == SVGNames::textLengthAttr)
            m_specifiedTextLength = m_textLength->baseVal()->value();

        InstanceInvalidationGuard guard(*this);
        updateSVGRendererForElementChange();
        invalidateResourceImageBuffersIfNeeded();
        return;
    }

    SVGGraphicsElement::svgAttributeChanged(attrName);
}

SVGAnimatedLength& SVGTextContentElement::textLengthAnimated()
{
    static NeverDestroyed<SVGLengthValue> defaultTextLength(SVGLengthMode::Other);
    if (m_textLength->baseVal()->value() == defaultTextLength)
        m_textLength->baseVal()->value() = { getComputedTextLength(), SVGLengthType::Number };
    return m_textLength;
}

bool SVGTextContentElement::selfHasRelativeLengths() const
{
    // Any element of the <text> subtree is advertized as using relative lengths.
    // On any window size change, we have to relayout the text subtree, as the
    // effective 'on-screen' font size may change.
    return true;
}

const SVGTextContentElement* SVGTextContentElement::elementFromRenderer(const RenderObject* renderer)
{
    if (!renderer)
        return nullptr;

    if (!renderer->isRenderSVGText() && !renderer->isRenderSVGInline())
        return nullptr;

    auto* element = downcast<SVGElement>(renderer->node());
    ASSERT(element);
    return dynamicDowncast<SVGTextContentElement>(element);
}

}
