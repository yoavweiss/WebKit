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

#include "config.h"
#include "RenderProgress.h"

#include "HTMLProgressElement.h"
#include "RenderBoxModelObjectInlines.h"
#include "RenderElementInlines.h"
#include "RenderStyleInlines.h"
#include "RenderTheme.h"
#include <wtf/RefPtr.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RenderProgress);

RenderProgress::RenderProgress(HTMLElement& element, RenderStyle&& style)
    : RenderBlockFlow(Type::Progress, element, WTFMove(style))
    , m_position(HTMLProgressElement::InvalidPosition)
    , m_animationTimer(*this, &RenderProgress::animationTimerFired)
{
    ASSERT(isRenderProgress());
}

RenderProgress::~RenderProgress() = default;

void RenderProgress::willBeDestroyed()
{
    m_animationTimer.stop();
    RenderBlockFlow::willBeDestroyed();
}

void RenderProgress::updateFromElement()
{
    HTMLProgressElement* element = progressElement();
    if (m_position == element->position())
        return;
    m_position = element->position();

    updateAnimationState();
    repaint();
    RenderBlockFlow::updateFromElement();
}

RenderBox::LogicalExtentComputedValues RenderProgress::computeLogicalHeight(LayoutUnit logicalHeight, LayoutUnit logicalTop) const
{
    auto computedValues = RenderBox::computeLogicalHeight(logicalHeight, logicalTop);
    LayoutRect frame = frameRect();
    if (isHorizontalWritingMode())
        frame.setHeight(computedValues.m_extent);
    else
        frame.setWidth(computedValues.m_extent);
    IntSize frameSize = theme().progressBarRectForBounds(*this, snappedIntRect(frame)).size();
    computedValues.m_extent = isHorizontalWritingMode() ? frameSize.height() : frameSize.width();
    return computedValues;
}

double RenderProgress::animationProgress() const
{
    auto duration = theme().animationDurationForProgressBar();
    ASSERT(duration > 0_s);
    return m_animating ? (fmod((MonotonicTime::now() - m_animationStartTime).seconds(), duration.seconds()) / duration.seconds()) : 0;
}

bool RenderProgress::isDeterminate() const
{
    return (HTMLProgressElement::IndeterminatePosition != position()
            && HTMLProgressElement::InvalidPosition != position());
}

void RenderProgress::animationTimerFired()
{
    // FIXME: Ideally obtaining the repeat interval from Page is not RenderTheme-specific, but it
    // currently is as it also determines whether we animate at all.
    auto repeatInterval = theme().animationRepeatIntervalForProgressBar(*this);

    repaint();
    if (!m_animationTimer.isActive() && m_animating)
        m_animationTimer.startOneShot(repeatInterval);
}

void RenderProgress::updateAnimationState()
{
    auto repeatInterval = theme().animationRepeatIntervalForProgressBar(*this);

    bool animating = style().hasUsedAppearance() && repeatInterval > 0_s && !isDeterminate();
    if (animating == m_animating)
        return;

    m_animating = animating;
    if (m_animating) {
        m_animationStartTime = MonotonicTime::now();
        m_animationTimer.startOneShot(repeatInterval);
    } else
        m_animationTimer.stop();
}

HTMLProgressElement* RenderProgress::progressElement() const
{
    if (!element())
        return nullptr;

    if (auto* progressElement = dynamicDowncast<HTMLProgressElement>(*element()))
        return progressElement;

    ASSERT(element()->shadowHost());
    return downcast<HTMLProgressElement>(element()->shadowHost());
}    

} // namespace WebCore

