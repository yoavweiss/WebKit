/*
 * Copyright (C) 2026 Apple Inc. All rights reserved.
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
#include "TextExtractionCache.h"

#include <optional>
#include <wtf/StdLibExtras.h>
#include <wtf/text/WTFString.h>

namespace WebKit {

void TextExtractionCache::add(const String& url, Vector<TextExtractionLineContent>&& lineContents)
{
    if (lineContents.isEmpty())
        return;

    Entry entry;
    entry.url = url;
    entry.lines = WTF::move(lineContents);
    for (unsigned index = 0; index < entry.lines.size(); ++index) {
        if (auto nodeIdentifier = entry.lines[index].nodeIdentifier)
            entry.lineIndexForUID.add(WTF::move(*nodeIdentifier), index);
    }

    m_entries.append(WTF::move(entry));
    if (m_entries.size() > maxEntries)
        m_entries.removeAt(0, m_entries.size() - maxEntries);
}

Vector<String> TextExtractionCache::contextWindowBefore(const Vector<TextExtractionLineContent>& lines, unsigned targetIndex)
{
    Vector<String> window;
    window.reserveInitialCapacity(contextLineCount);
    for (unsigned offset = contextLineCount; offset >= 1; --offset) {
        if (offset > targetIndex)
            window.append(emptyString());
        else
            window.append(lines[targetIndex - offset].contentWithoutIdentifier);
    }
    return window;
}

Vector<String> TextExtractionCache::contextWindowAfter(const Vector<TextExtractionLineContent>& lines, unsigned targetIndex)
{
    unsigned lineCount = lines.size();
    Vector<String> window;
    window.reserveInitialCapacity(contextLineCount);
    for (unsigned offset = 1; offset <= contextLineCount; ++offset) {
        unsigned index = targetIndex + offset;
        if (index >= lineCount)
            window.append(emptyString());
        else
            window.append(lines[index].contentWithoutIdentifier);
    }
    return window;
}

auto TextExtractionCache::resolve(const String& identifier) const -> ResolvedNode
{
    if (m_entries.isEmpty())
        return { identifier, NodeResolution::Unknown };

    size_t newestIndex = m_entries.size() - 1;
    if (m_entries[newestIndex].lineIndexForUID.contains(identifier))
        return { identifier, NodeResolution::Current };

    std::optional<size_t> sourceEntryIndex;
    unsigned sourceLineIndex = 0;
    for (size_t index = 0; index < newestIndex; ++index) {
        if (auto it = m_entries[index].lineIndexForUID.find(identifier); it != m_entries[index].lineIndexForUID.end()) {
            sourceEntryIndex = index;
            sourceLineIndex = it->value;
        }
    }

    if (!sourceEntryIndex)
        return { identifier, NodeResolution::Unknown };

    auto& newestURL = m_entries[newestIndex].url;
    auto& source = m_entries[*sourceEntryIndex];
    auto& sourceLineContent = source.lines[sourceLineIndex].contentWithoutIdentifier;
    auto sourceContextBefore = contextWindowBefore(source.lines, sourceLineIndex);
    auto sourceContextAfter = contextWindowAfter(source.lines, sourceLineIndex);

    for (size_t entryIndex = newestIndex; entryIndex > *sourceEntryIndex; --entryIndex) {
        auto& entry = m_entries[entryIndex];
        if (entry.url != newestURL)
            continue;

        std::optional<unsigned> uniqueTargetIndex;
        unsigned matchCount = 0;
        for (unsigned candidate = 0; candidate < entry.lines.size(); ++candidate) {
            if (entry.lines[candidate].contentWithoutIdentifier != sourceLineContent)
                continue;

            bool contextBeforeMatches = contextWindowBefore(entry.lines, candidate) == sourceContextBefore;
            bool contextAfterMatches = contextWindowAfter(entry.lines, candidate) == sourceContextAfter;
            if (!contextBeforeMatches && !contextAfterMatches)
                continue;

            if (++matchCount > 1) {
                uniqueTargetIndex = { };
                break;
            }

            uniqueTargetIndex = candidate;
        }

        if (!matchCount)
            continue;

        if (matchCount > 1)
            return { { }, NodeResolution::Ambiguous };

        if (auto remapped = entry.lines[*uniqueTargetIndex].nodeIdentifier)
            return { WTF::move(*remapped), NodeResolution::Remapped };

        return { { }, NodeResolution::Stale };
    }

    return { { }, NodeResolution::Stale };
}

} // namespace WebKit
