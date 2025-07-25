/*
 * Copyright (C) 2013 The MathJax Consortium. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "RenderMathMLSpace.h"

#if ENABLE(MATHML)

#include "GraphicsContext.h"
#include "RenderBoxInlines.h"
#include "RenderBoxModelObjectInlines.h"
#include "RenderObjectInlines.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RenderMathMLSpace);

RenderMathMLSpace::RenderMathMLSpace(MathMLSpaceElement& element, RenderStyle&& style)
    : RenderMathMLBlock(Type::MathMLSpace, element, WTFMove(style))
{
    ASSERT(isRenderMathMLSpace());
}

RenderMathMLSpace::~RenderMathMLSpace() = default;

void RenderMathMLSpace::computePreferredLogicalWidths()
{
    ASSERT(needsPreferredLogicalWidthsUpdate());

    m_minPreferredLogicalWidth = m_maxPreferredLogicalWidth = spaceWidth();

    auto sizes = sizeAppliedToMathContent(LayoutPhase::CalculatePreferredLogicalWidth);
    applySizeToMathContent(LayoutPhase::CalculatePreferredLogicalWidth, sizes);

    adjustPreferredLogicalWidthsForBorderAndPadding();

    clearNeedsPreferredWidthsUpdate();
}

LayoutUnit RenderMathMLSpace::spaceWidth() const
{
    Ref spaceElement = element();
    // FIXME: Negative width values are not supported yet.
    return std::max<LayoutUnit>(0, toUserUnits(spaceElement->width(), style(), 0));
}

void RenderMathMLSpace::getSpaceHeightAndDepth(LayoutUnit& height, LayoutUnit& depth) const
{
    Ref spaceElement = element();
    height = toUserUnits(spaceElement->height(), style(), 0);
    depth = toUserUnits(spaceElement->depth(), style(), 0);

    // If the total height is negative, set vertical dimensions to 0.
    if (height + depth < 0) {
        height = 0;
        depth = 0;
    }
}

void RenderMathMLSpace::layoutBlock(RelayoutChildren relayoutChildren, LayoutUnit)
{
    ASSERT(needsLayout());

    insertPositionedChildrenIntoContainingBlock();

    if (relayoutChildren == RelayoutChildren::No && simplifiedLayout())
        return;

    layoutFloatingChildren();

    recomputeLogicalWidth();

    setLogicalWidth(spaceWidth());
    LayoutUnit height, depth;
    getSpaceHeightAndDepth(height, depth);
    setLogicalHeight(height + depth);

    auto sizes = sizeAppliedToMathContent(LayoutPhase::Layout);
    applySizeToMathContent(LayoutPhase::Layout, sizes);

    adjustLayoutForBorderAndPadding();

    updateScrollInfoAfterLayout();

    clearNeedsLayout();
}

std::optional<LayoutUnit> RenderMathMLSpace::firstLineBaseline() const
{
    LayoutUnit height, depth;
    getSpaceHeightAndDepth(height, depth);
    return height + borderAndPaddingBefore();
}

}

#endif
