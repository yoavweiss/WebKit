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
#include "GridFormattingContext.h"

#include "GridLayout.h"
#include "LayoutChildIterator.h"
#include "PlacedGridItem.h"
#include "RenderStyleInlines.h"
#include "StylePrimitiveNumeric.h"
#include "UnplacedGridItem.h"

#include <wtf/Vector.h>

namespace WebCore {
namespace Layout {

GridFormattingContext::GridFormattingContext(const ElementBox& gridBox, LayoutState& layoutState)
    : m_gridBox(gridBox)
    , m_globalLayoutState(layoutState)
{
}

UnplacedGridItems GridFormattingContext::constructUnplacedGridItems() const
{
    struct GridItem {
        CheckedRef<const ElementBox> layoutBox;
        int order;
    };

    Vector<GridItem> gridItems;
    for (CheckedRef gridItem : childrenOfType<ElementBox>(m_gridBox)) {
        if (gridItem->isOutOfFlowPositioned())
            continue;

        gridItems.append({ gridItem, gridItem->style().order().value });
    }

    std::ranges::stable_sort(gridItems, { }, &GridItem::order);

    UnplacedGridItems unplacedGridItems;
    for (auto& gridItem : gridItems) {
        CheckedRef gridItemStyle = gridItem.layoutBox->style();

        auto gridItemColumnStart = gridItemStyle->gridItemColumnStart();
        auto gridItemColumnEnd = gridItemStyle->gridItemColumnEnd();
        auto gridItemRowStart = gridItemStyle->gridItemRowStart();
        auto gridItemRowEnd = gridItemStyle->gridItemRowEnd();

        // Check if this item is fully explicitly positioned
        bool fullyExplicitlyPositionedItem = gridItemColumnStart.isExplicit()
            && gridItemColumnEnd.isExplicit()
            && gridItemRowStart.isExplicit()
            && gridItemRowEnd.isExplicit();

        bool definiteRowPositioned = gridItemRowStart.isExplicit() || gridItemRowEnd.isExplicit();

        if (fullyExplicitlyPositionedItem) {
            unplacedGridItems.nonAutoPositionedItems.constructAndAppend(
                gridItem.layoutBox,
                gridItemColumnStart,
                gridItemColumnEnd,
                gridItemRowStart,
                gridItemRowEnd
            );
        } else if (definiteRowPositioned) {
            unplacedGridItems.definiteRowPositionedItems.constructAndAppend(
                gridItem.layoutBox,
                gridItemColumnStart,
                gridItemColumnEnd,
                gridItemRowStart,
                gridItemRowEnd
            );
        } else {
            unplacedGridItems.autoPositionedItems.constructAndAppend(
                gridItem.layoutBox,
                gridItemColumnStart,
                gridItemColumnEnd,
                gridItemRowStart,
                gridItemRowEnd
            );
        }
    }
    return unplacedGridItems;
}

void GridFormattingContext::layout(GridLayoutConstraints layoutConstraints)
{
    auto unplacedGridItems = constructUnplacedGridItems();
    GridLayout { *this }.layout(layoutConstraints, unplacedGridItems);
}

PlacedGridItems GridFormattingContext::constructPlacedGridItems(const GridAreas& gridAreas) const
{
    PlacedGridItems placedGridItems;
    placedGridItems.reserveInitialCapacity(gridAreas.size());
    for (auto [ unplacedGridItem, gridAreaLines ] : gridAreas)
        placedGridItems.constructAndAppend(unplacedGridItem, gridAreaLines);

    return placedGridItems;
}

} // namespace Layout
} // namespace WebCore
