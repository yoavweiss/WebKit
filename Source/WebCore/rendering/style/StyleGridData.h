/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
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
 *  THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 *  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#pragma once

#include "RenderStyleConstants.h"
#include "StyleGridTemplateAreas.h"
#include "StyleGridTemplateList.h"
#include <wtf/Ref.h>
#include <wtf/RefCounted.h>
#include <wtf/Vector.h>

namespace WebCore {

class RenderStyle;

DECLARE_ALLOCATOR_WITH_HEAP_IDENTIFIER(StyleGridData);
class StyleGridData : public RefCounted<StyleGridData> {
    WTF_MAKE_FAST_ALLOCATED_WITH_HEAP_IDENTIFIER(StyleGridData);
public:
    static Ref<StyleGridData> create() { return adoptRef(*new StyleGridData); }
    Ref<StyleGridData> copy() const;

    bool operator==(const StyleGridData&) const;

#if !LOG_DISABLED
    void dumpDifferences(TextStream&, const StyleGridData&) const;
#endif

    unsigned gridAutoFlow() const { return m_gridAutoFlow; }
    const Vector<Style::GridTrackSize>& gridAutoColumns() const { return m_gridAutoColumns; }
    const Vector<Style::GridTrackSize>& gridAutoRows() const { return m_gridAutoRows; }
    const Style::GridTemplateAreas& gridTemplateAreas() { return m_gridTemplateAreas; }
    const Style::GridTemplateList& gridTemplateColumns() const { return m_gridTemplateColumns; }
    const Style::GridTemplateList& gridTemplateRows() const { return m_gridTemplateRows; }

    const Vector<Style::GridTrackSize>& gridColumnTrackSizes() const { return m_gridTemplateColumns.sizes; }
    const Vector<Style::GridTrackSize>& gridRowTrackSizes() const { return m_gridTemplateRows.sizes; }
    const Style::GridNamedLinesMap& namedGridColumnLines() const { return m_gridTemplateColumns.namedLines; };
    const Style::GridNamedLinesMap& namedGridRowLines() const { return m_gridTemplateRows.namedLines; };
    const Style::GridOrderedNamedLinesMap& orderedNamedGridColumnLines() const { return m_gridTemplateColumns.orderedNamedLines; }
    const Style::GridOrderedNamedLinesMap& orderedNamedGridRowLines() const { return m_gridTemplateRows.orderedNamedLines; }

    const Vector<Style::GridTrackSize>& gridAutoRepeatColumns() const { return m_gridTemplateColumns.autoRepeatSizes; }
    const Vector<Style::GridTrackSize>& gridAutoRepeatRows() const { return m_gridTemplateRows.autoRepeatSizes; }
    const Style::GridNamedLinesMap& autoRepeatNamedGridColumnLines() const { return m_gridTemplateColumns.autoRepeatNamedLines; }
    const Style::GridNamedLinesMap& autoRepeatNamedGridRowLines() const { return m_gridTemplateRows.autoRepeatNamedLines; }
    const Style::GridOrderedNamedLinesMap& autoRepeatOrderedNamedGridColumnLines() const { return m_gridTemplateColumns.autoRepeatOrderedNamedLines; }
    const Style::GridOrderedNamedLinesMap& autoRepeatOrderedNamedGridRowLines() const { return m_gridTemplateRows.autoRepeatOrderedNamedLines; }

    unsigned autoRepeatColumnsInsertionPoint() const { return m_gridTemplateColumns.autoRepeatInsertionPoint; }
    unsigned autoRepeatRowsInsertionPoint() const { return m_gridTemplateRows.autoRepeatInsertionPoint; }
    AutoRepeatType autoRepeatColumnsType() const { return m_gridTemplateColumns.autoRepeatType; }
    AutoRepeatType autoRepeatRowsType() const { return m_gridTemplateRows.autoRepeatType; }

    bool subgridColumns() const { return m_gridTemplateColumns.subgrid; }
    bool subgridRows() const { return m_gridTemplateRows.subgrid; };

    bool masonryColumns() const { return m_gridTemplateColumns.masonry; }
    bool masonryRows() const { return m_gridTemplateRows.masonry; }

private:
    friend class RenderStyle;

    unsigned m_gridAutoFlow : GridAutoFlowBits;
    Vector<Style::GridTrackSize> m_gridAutoColumns;
    Vector<Style::GridTrackSize> m_gridAutoRows;
    Style::GridTemplateAreas m_gridTemplateAreas;
    Style::GridTemplateList m_gridTemplateColumns;
    Style::GridTemplateList m_gridTemplateRows;

    StyleGridData();
    StyleGridData(const StyleGridData&);
};

} // namespace WebCore

