/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
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

#include "RenderGrid.h"

namespace WebCore {

namespace Style {
enum class GridTrackSizingDirection : bool;
}

class AncestorSubgridIterator {
public:
    AncestorSubgridIterator();
    AncestorSubgridIterator(SingleThreadWeakPtr<RenderGrid> firstAncestorSubgrid, Style::GridTrackSizingDirection);

    RenderGrid& operator*();

    bool operator==(const AncestorSubgridIterator&) const;

    AncestorSubgridIterator& operator++();
    AncestorSubgridIterator begin();
    AncestorSubgridIterator end();
private:
    AncestorSubgridIterator(SingleThreadWeakPtr<RenderGrid> firstAncestorSubgrid, SingleThreadWeakPtr<RenderGrid> currentAncestor, Style::GridTrackSizingDirection);
    AncestorSubgridIterator(SingleThreadWeakPtr<RenderGrid> firstAncestorSubgrid, SingleThreadWeakPtr<RenderGrid> currentAncestor, std::optional<Style::GridTrackSizingDirection>);

    const SingleThreadWeakPtr<const RenderGrid> m_firstAncestorSubgrid;
    SingleThreadWeakPtr<RenderGrid> m_currentAncestorSubgrid;
    const std::optional<Style::GridTrackSizingDirection> m_direction;

};

AncestorSubgridIterator ancestorSubgridsOfGridItem(const RenderBox& gridItem, const Style::GridTrackSizingDirection);

} // namespace WebCore
