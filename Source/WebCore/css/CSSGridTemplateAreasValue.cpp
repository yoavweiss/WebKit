/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 * Copyright (C) 2013, 2014 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
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
#include "CSSGridTemplateAreasValue.h"

#include "GridArea.h"
#include <wtf/FixedVector.h>
#include <wtf/HashSet.h>
#include <wtf/text/StringBuilder.h>
#include <wtf/text/StringHash.h>

namespace WebCore {

CSSGridTemplateAreasValue::CSSGridTemplateAreasValue(NamedGridAreaMap map, size_t rowCount, size_t columnCount)
    : CSSValue(ClassType::GridTemplateAreas)
    , m_map(WTFMove(map))
    , m_rowCount(rowCount)
    , m_columnCount(columnCount)
{
    ASSERT(m_rowCount);
    ASSERT(m_columnCount);
}

Ref<CSSGridTemplateAreasValue> CSSGridTemplateAreasValue::create(NamedGridAreaMap map, size_t rowCount, size_t columnCount)
{
    return adoptRef(*new CSSGridTemplateAreasValue(WTFMove(map), rowCount, columnCount));
}

static String stringForPosition(const NamedGridAreaMap& gridAreaMap, size_t row, size_t column)
{
    HashSet<String> candidates;
    for (auto& it : gridAreaMap.map) {
        auto& area = it.value;
        if (row >= area.rows.startLine() && row < area.rows.endLine())
            candidates.add(it.key);
    }
    for (auto& it : gridAreaMap.map) {
        auto& area = it.value;
        if (column >= area.columns.startLine() && column < area.columns.endLine() && candidates.contains(it.key))
            return it.key;
    }
    return "."_s;
}

String CSSGridTemplateAreasValue::stringForRow(size_t row) const
{
    FixedVector<String> columns(m_columnCount);
    for (auto& it : m_map.map) {
        auto& area = it.value;
        if (row >= area.rows.startLine() && row < area.rows.endLine()) {
            for (unsigned i = area.columns.startLine(); i < area.columns.endLine(); i++)
                columns[i] = it.key;
        }
    }
    StringBuilder builder;
    bool first = true;
    for (auto& name : columns) {
        if (!first)
            builder.append(' ');
        first = false;
        if (name.isNull())
            builder.append('.');
        else
            builder.append(name);
    }
    return builder.toString();
}

String CSSGridTemplateAreasValue::customCSSText(const CSS::SerializationContext&) const
{
    StringBuilder builder;
    for (size_t row = 0; row < m_rowCount; ++row) {
        builder.append('"');
        for (size_t column = 0; column < m_columnCount; ++column) {
            builder.append(stringForPosition(m_map, row, column));
            if (column != m_columnCount - 1)
                builder.append(' ');
        }
        builder.append('"');
        if (row != m_rowCount - 1)
            builder.append(' ');
    }
    return builder.toString();
}

bool CSSGridTemplateAreasValue::equals(const CSSGridTemplateAreasValue& other) const
{
    return m_map.map == other.m_map.map && m_rowCount == other.m_rowCount && m_columnCount == other.m_columnCount;
}

} // namespace WebCore
