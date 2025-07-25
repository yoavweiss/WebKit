/*
 * Copyright (C) 2004-2022 Apple Inc. All rights reserved.
 * Copyright (C) 2014 Google Inc. All rights reserved.
 * Portions Copyright (c) 2011 Motorola Mobility, Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "VisiblePosition.h"

#include "BoundaryPoint.h"
#include "CaretRectComputation.h"
#include "Document.h"
#include "Editing.h"
#include "FloatQuad.h"
#include "HTMLElement.h"
#include "HTMLHtmlElement.h"
#include "HTMLNames.h"
#include "InlineIteratorBox.h"
#include "InlineIteratorLineBox.h"
#include "InlineRunAndOffset.h"
#include "LineSelection.h"
#include "Logging.h"
#include "PositionInlines.h"
#include "Range.h"
#include "RenderBlockFlow.h"
#include "RenderObjectInlines.h"
#include "SimpleRange.h"
#include "Text.h"
#include "TextIterator.h"
#include "VisibleUnits.h"
#include <stdio.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/TextStream.h>

namespace WebCore {

using namespace HTMLNames;

VisiblePosition::VisiblePosition(const Position& position, Affinity affinity)
    : m_deepPosition { canonicalPosition(position) }
{
    if (affinity == Affinity::Upstream && !isNull()) {
        auto upstreamCopy = *this;
        upstreamCopy.m_affinity = Affinity::Upstream;
        if (!inSameLine(*this, upstreamCopy))
            m_affinity = Affinity::Upstream;
    }
}

VisiblePosition VisiblePosition::next(EditingBoundaryCrossingRule rule, bool* reachedBoundary) const
{
    if (reachedBoundary)
        *reachedBoundary = false;
    // FIXME: Support CanSkipEditingBoundary
    ASSERT(rule == CanCrossEditingBoundary || rule == CannotCrossEditingBoundary);
    VisiblePosition next(nextVisuallyDistinctCandidate(m_deepPosition), m_affinity);

    if (rule == CanCrossEditingBoundary)
        return next;

    return honorEditingBoundaryAtOrAfter(next, reachedBoundary);
}

VisiblePosition VisiblePosition::previous(EditingBoundaryCrossingRule rule, bool* reachedBoundary) const
{
    if (reachedBoundary)
        *reachedBoundary = false;
    // FIXME: Support CanSkipEditingBoundary
    ASSERT(rule == CanCrossEditingBoundary || rule == CannotCrossEditingBoundary);
    // find first previous DOM position that is visible
    Position pos = previousVisuallyDistinctCandidate(m_deepPosition);

    // return null visible position if there is no previous visible position
    if (pos.atStartOfTree()) {
        if (reachedBoundary)
            *reachedBoundary = true;
        return VisiblePosition();
    }

    VisiblePosition prev = pos;
    ASSERT(prev != *this);

#if ASSERT_ENABLED
    // We should always be able to make the affinity downstream, because going previous from an
    // upstream position can never yield another upstream position unless line wrap length is 0.
    if (prev.isNotNull() && m_affinity == Affinity::Upstream) {
        auto upstreamCopy = prev;
        upstreamCopy.setAffinity(Affinity::Upstream);
        ASSERT(inSameLine(upstreamCopy, prev));
    }
#endif

    if (rule == CanCrossEditingBoundary)
        return prev;
    
    return honorEditingBoundaryAtOrBefore(prev, reachedBoundary);
}

Position VisiblePosition::leftVisuallyDistinctCandidate() const
{
    Position p = m_deepPosition;
    if (p.isNull())
        return Position();

    Position downstreamStart = p.downstream();
    TextDirection primaryDirection = p.primaryDirection();

    InlineIterator::LineLogicalOrderCache orderCache;

    while (true) {
        auto [box, offset] = p.inlineBoxAndOffset(m_affinity, primaryDirection);
        if (!box)
            return primaryDirection == TextDirection::LTR ? previousVisuallyDistinctCandidate(m_deepPosition) : nextVisuallyDistinctCandidate(m_deepPosition);

        CheckedPtr renderer = &box->renderer();

        while (true) {
            if ((renderer->isBlockLevelReplacedOrAtomicInline() || renderer->isBR()) && offset == box->rightmostCaretOffset())
                return box->isLeftToRightDirection() ? previousVisuallyDistinctCandidate(m_deepPosition) : nextVisuallyDistinctCandidate(m_deepPosition);

            if (!renderer->node()) {
                box.traverseLineLeftwardOnLine();
                if (!box)
                    return primaryDirection == TextDirection::LTR ? previousVisuallyDistinctCandidate(m_deepPosition) : nextVisuallyDistinctCandidate(m_deepPosition);
                renderer = &box->renderer();
                offset = box->rightmostCaretOffset();
                continue;
            }

            // Note that this may underflow the (unsigned) offset. This is fine and handled below.
            offset = box->isLeftToRightDirection() ? renderer->previousOffset(offset) : renderer->nextOffset(offset);

            auto caretMinOffset = box->minimumCaretOffset();
            auto caretMaxOffset = box->maximumCaretOffset();

            if (offset > caretMinOffset && offset < caretMaxOffset)
                break;

            if (offset != box->leftmostCaretOffset()) {
                // Overshot to the left.
                auto previousBox = box->nextLineLeftwardOnLineIgnoringLineBreak();
                if (!previousBox) {
                    Position positionOnLeft = primaryDirection == TextDirection::LTR ? previousVisuallyDistinctCandidate(m_deepPosition) : nextVisuallyDistinctCandidate(m_deepPosition);
                    auto boxOnLeft = positionOnLeft.inlineBoxAndOffset(m_affinity, primaryDirection).box;
                    if (boxOnLeft && boxOnLeft->lineBox() == box->lineBox())
                        return Position();
                    return positionOnLeft;
                }

                // Reposition at the other logical position corresponding to our edge's visual position and go for another round.
                box = previousBox;
                renderer = &box->renderer();
                offset = previousBox->rightmostCaretOffset();
                continue;
            }

            unsigned char level = box->bidiLevel();
            auto previousBox = box->nextLineLeftwardOnLine();

            if (box->direction() == primaryDirection) {
                if (!previousBox) {
                    auto logicalStart = primaryDirection == TextDirection::LTR
                        ? InlineIterator::firstLeafOnLineInLogicalOrderWithNode(box->lineBox(), orderCache)
                        : InlineIterator::lastLeafOnLineInLogicalOrderWithNode(box->lineBox(), orderCache);
                    if (logicalStart) {
                        box = logicalStart;
                        renderer = &box->renderer();
                        offset = primaryDirection == TextDirection::LTR ? box->minimumCaretOffset() : box->maximumCaretOffset();
                    }
                    break;
                }
                if (previousBox->bidiLevel() >= level)
                    break;

                level = previousBox->bidiLevel();

                auto nextBox = box;
                do {
                    nextBox.traverseLineRightwardOnLine();
                } while (nextBox && nextBox->bidiLevel() > level);

                if (nextBox && nextBox->bidiLevel() == level)
                    break;

                box = previousBox;
                renderer = &box->renderer();
                offset = box->rightmostCaretOffset();
                if (box->direction() == primaryDirection)
                    break;
                continue;
            }

            while (previousBox && !previousBox->renderer().node())
                previousBox.traverseLineLeftwardOnLine();

            if (previousBox) {
                box = previousBox;
                renderer = &box->renderer();
                offset = box->rightmostCaretOffset();
                if (box->bidiLevel() > level) {
                    do {
                        previousBox = previousBox.traverseLineLeftwardOnLine();
                    } while (previousBox && previousBox->bidiLevel() > level);

                    if (!previousBox || previousBox->bidiLevel() < level)
                        continue;
                }
            } else {
                // Trailing edge of a secondary box. Set to the leading edge of the entire box.
                while (true) {
                    while (auto nextBox = box->nextLineRightwardOnLine()) {
                        if (nextBox->bidiLevel() < level)
                            break;
                        box = nextBox;
                    }
                    if (box->bidiLevel() == level)
                        break;
                    level = box->bidiLevel();
                    while (auto previousBox = box->nextLineLeftwardOnLine()) {
                        if (previousBox->bidiLevel() < level)
                            break;
                        box = previousBox;
                    }
                    if (box->bidiLevel() == level)
                        break;
                    level = box->bidiLevel();
                }
                renderer = &box->renderer();
                offset = primaryDirection == TextDirection::LTR ? box->minimumCaretOffset() : box->maximumCaretOffset();
            }
            break;
        }

        p = makeDeprecatedLegacyPosition(renderer->protectedNode().get(), offset);

        if ((p.isCandidate() && p.downstream() != downstreamStart) || p.atStartOfTree() || p.atEndOfTree())
            return p;

        ASSERT(p != m_deepPosition);
    }
}

VisiblePosition VisiblePosition::left(bool stayInEditableContent, bool* reachedBoundary) const
{
    if (reachedBoundary)
        *reachedBoundary = false;
    Position pos = leftVisuallyDistinctCandidate();
    // FIXME: Why can't we move left from the last position in a tree?
    if (pos.atStartOfTree() || pos.atEndOfTree()) {
        if (reachedBoundary)
            *reachedBoundary = true;
        return VisiblePosition();
    }

    VisiblePosition left = pos;
    ASSERT(left != *this);

    if (!stayInEditableContent)
        return left;

    return directionOfEnclosingBlock(left.deepEquivalent()) == TextDirection::LTR ? honorEditingBoundaryAtOrBefore(left, reachedBoundary) : honorEditingBoundaryAtOrAfter(left, reachedBoundary);
}

Position VisiblePosition::rightVisuallyDistinctCandidate() const
{
    Position p = m_deepPosition;
    if (p.isNull())
        return Position();

    Position downstreamStart = p.downstream();
    TextDirection primaryDirection = p.primaryDirection();

    InlineIterator::LineLogicalOrderCache orderCache;

    while (true) {
        auto [box, offset] = p.inlineBoxAndOffset(m_affinity, primaryDirection);
        if (!box)
            return primaryDirection == TextDirection::LTR ? nextVisuallyDistinctCandidate(m_deepPosition) : previousVisuallyDistinctCandidate(m_deepPosition);

        CheckedPtr renderer = &box->renderer();

        while (true) {
            if ((renderer->isBlockLevelReplacedOrAtomicInline() || renderer->isBR()) && offset == box->leftmostCaretOffset())
                return box->isLeftToRightDirection() ? nextVisuallyDistinctCandidate(m_deepPosition) : previousVisuallyDistinctCandidate(m_deepPosition);

            if (!renderer->node()) {
                box.traverseLineRightwardOnLine();
                if (!box)
                    return primaryDirection == TextDirection::LTR ? nextVisuallyDistinctCandidate(m_deepPosition) : previousVisuallyDistinctCandidate(m_deepPosition);
                renderer = &box->renderer();
                offset = box->leftmostCaretOffset();
                continue;
            }

            // Note that this may underflow the (unsigned) offset. This is fine and handled below.
            offset = box->isLeftToRightDirection() ? renderer->nextOffset(offset) : renderer->previousOffset(offset);

            auto caretMinOffset = box->minimumCaretOffset();
            auto caretMaxOffset = box->maximumCaretOffset();

            if (offset > caretMinOffset && offset < caretMaxOffset)
                break;

            if (offset != box->rightmostCaretOffset()) {
                // Overshot to the right.
                auto nextBox = box->nextLineRightwardOnLineIgnoringLineBreak();
                if (!nextBox) {
                    Position positionOnRight = primaryDirection == TextDirection::LTR ? nextVisuallyDistinctCandidate(m_deepPosition) : previousVisuallyDistinctCandidate(m_deepPosition);
                    auto boxOnRight = positionOnRight.inlineBoxAndOffset(m_affinity, primaryDirection).box;
                    if (boxOnRight && boxOnRight->lineBox() == box->lineBox())
                        return Position();
                    return positionOnRight;
                }

                // Reposition at the other logical position corresponding to our edge's visual position and go for another round.
                box = nextBox;
                renderer = &box->renderer();
                offset = nextBox->leftmostCaretOffset();
                continue;
            }

            unsigned char level = box->bidiLevel();
            auto nextBox = box->nextLineRightwardOnLine();

            if (box->direction() == primaryDirection) {
                if (!nextBox) {
                    auto logicalEnd = primaryDirection == TextDirection::LTR
                        ? InlineIterator::lastLeafOnLineInLogicalOrderWithNode(box->lineBox(), orderCache)
                        : InlineIterator::firstLeafOnLineInLogicalOrderWithNode(box->lineBox(), orderCache);

                    if (logicalEnd) {
                        box = logicalEnd;
                        renderer = &box->renderer();
                        offset = primaryDirection == TextDirection::LTR ? box->maximumCaretOffset() : box->minimumCaretOffset();
                    }
                    break;
                }

                if (nextBox->bidiLevel() >= level)
                    break;

                level = nextBox->bidiLevel();

                auto previousBox = box;
                do {
                    previousBox.traverseLineLeftwardOnLine();
                } while (previousBox && previousBox->bidiLevel() > level);

                if (previousBox && previousBox->bidiLevel() == level) // For example, abc FED 123 ^ CBA
                    break;

                // For example, abc 123 ^ CBA or 123 ^ CBA abc
                box = nextBox;
                renderer = &box->renderer();
                offset = box->leftmostCaretOffset();
                if (box->direction() == primaryDirection)
                    break;
                continue;
            }

            while (nextBox && !nextBox->renderer().node())
                nextBox.traverseLineRightwardOnLine();

            if (nextBox) {
                box = nextBox;
                renderer = &box->renderer();
                offset = box->leftmostCaretOffset();

                if (box->bidiLevel() > level) {
                    do {
                        nextBox.traverseLineRightwardOnLine();
                    } while (nextBox && nextBox->bidiLevel() > level);

                    if (!nextBox || nextBox->bidiLevel() < level)
                        continue;
                }
            } else {
                // Trailing edge of a secondary box. Set to the leading edge of the entire box.
                while (true) {
                    while (auto previousBox = box->nextLineLeftwardOnLine()) {
                        if (previousBox->bidiLevel() < level)
                            break;
                        box = previousBox;
                    }
                    if (box->bidiLevel() == level)
                        break;
                    level = box->bidiLevel();
                    while (auto nextBox = box->nextLineRightwardOnLine()) {
                        if (nextBox->bidiLevel() < level)
                            break;
                        box = nextBox;
                    }
                    if (box->bidiLevel() == level)
                        break;
                    level = box->bidiLevel();
                }
                renderer = &box->renderer();
                offset = primaryDirection == TextDirection::LTR ? box->maximumCaretOffset() : box->minimumCaretOffset();
            }
            break;
        }

        p = makeDeprecatedLegacyPosition(renderer->protectedNode().get(), offset);

        if ((p.isCandidate() && p.downstream() != downstreamStart) || p.atStartOfTree() || p.atEndOfTree())
            return p;

        ASSERT(p != m_deepPosition);
    }
}

VisiblePosition VisiblePosition::right(bool stayInEditableContent, bool* reachedBoundary) const
{
    if (reachedBoundary)
        *reachedBoundary = false;
    Position pos = rightVisuallyDistinctCandidate();
    // FIXME: Why can't we move left from the last position in a tree?
    if (pos.atStartOfTree() || pos.atEndOfTree()) {
        if (reachedBoundary)
            *reachedBoundary = true;
        return VisiblePosition();
    }

    VisiblePosition right = pos;
    ASSERT(right != *this);

    if (!stayInEditableContent)
        return right;

    return directionOfEnclosingBlock(right.deepEquivalent()) == TextDirection::LTR ? honorEditingBoundaryAtOrAfter(right, reachedBoundary) : honorEditingBoundaryAtOrBefore(right, reachedBoundary);
}

VisiblePosition VisiblePosition::honorEditingBoundaryAtOrBefore(const VisiblePosition& position, bool* reachedBoundary) const
{
    if (reachedBoundary)
        *reachedBoundary = false;
    if (position.isNull())
        return position;
    
    RefPtr highestRoot = highestEditableRoot(deepEquivalent());
    
    // Return empty position if pos is not somewhere inside the editable region containing this position
    if (highestRoot && !position.deepEquivalent().protectedDeprecatedNode()->isDescendantOf(*highestRoot)) {
        if (reachedBoundary)
            *reachedBoundary = true;
        return VisiblePosition();
    }
    
    // Return position itself if the two are from the very same editable region, or both are non-editable
    // FIXME: In the non-editable case, just because the new position is non-editable doesn't mean movement
    // to it is allowed.  VisibleSelection::adjustForEditableContent has this problem too.
    if (highestEditableRoot(position.deepEquivalent()) == highestRoot) {
        if (reachedBoundary)
            *reachedBoundary = *this == position;
        return position;
    }
  
    // Return empty position if this position is non-editable, but pos is editable
    // FIXME: Move to the previous non-editable region.
    if (!highestRoot) {
        if (reachedBoundary)
            *reachedBoundary = true;
        return VisiblePosition();
    }

    // Return the last position before pos that is in the same editable region as this position
    return lastEditablePositionBeforePositionInRoot(position.deepEquivalent(), highestRoot.get());
}

VisiblePosition VisiblePosition::honorEditingBoundaryAtOrAfter(const VisiblePosition& otherPosition, bool* reachedBoundary) const
{
    if (reachedBoundary)
        *reachedBoundary = false;
    if (otherPosition.isNull())
        return otherPosition;
    
    RefPtr highestRoot = highestEditableRoot(deepEquivalent());
    
    // Return empty position if otherPosition is not somewhere inside the editable region containing this position
    if (highestRoot && !otherPosition.deepEquivalent().protectedDeprecatedNode()->isDescendantOf(*highestRoot)) {
        if (reachedBoundary)
            *reachedBoundary = true;
        return VisiblePosition();
    }
    
    // Return otherPosition itself if the two are from the very same editable region, or both are non-editable
    // FIXME: In the non-editable case, just because the new position is non-editable doesn't mean movement
    // to it is allowed.  VisibleSelection::adjustForEditableContent has this problem too.
    if (highestEditableRoot(otherPosition.deepEquivalent()) == highestRoot) {
        if (reachedBoundary)
            *reachedBoundary = *this == otherPosition;
        return otherPosition;
    }

    // Return empty position if this position is non-editable, but otherPosition is editable
    // FIXME: Move to the next non-editable region.
    if (!highestRoot) {
        if (reachedBoundary)
            *reachedBoundary = true;
        return VisiblePosition();
    }

    // Return the next position after pos that is in the same editable region as this position
    return firstEditablePositionAfterPositionInRoot(otherPosition.deepEquivalent(), highestRoot.get());
}

static Position canonicalizeCandidate(const Position& candidate)
{
    if (candidate.isNull())
        return Position();
    ASSERT(candidate.isCandidate());
    Position upstream = candidate.upstream();
    if (upstream.isCandidate())
        return upstream;
    return candidate;
}

Position VisiblePosition::canonicalPosition(const Position& passedPosition)
{
    // The updateLayout call below can do so much that even the position passed
    // in to us might get changed as a side effect. Specifically, there are code
    // paths that pass selection endpoints, and updateLayout can change the selection.
    Position position = passedPosition;

    // FIXME (9535):  Canonicalizing to the leftmost candidate means that if we're at a line wrap, we will 
    // ask renderers to paint downstream carets for other renderers.
    // To fix this, we need to either a) add code to all paintCarets to pass the responsibility off to
    // the appropriate renderer for VisiblePosition's like these, or b) canonicalize to the rightmost candidate
    // unless the affinity is upstream.
    if (position.isNull())
        return Position();

    ASSERT(position.document());
    position.document()->updateLayoutIgnorePendingStylesheets();

    RefPtr node = position.containerNode();

    Position candidate = position.upstream();
    if (candidate.isCandidate())
        return candidate;
    candidate = position.downstream();
    if (candidate.isCandidate())
        return candidate;

    // When neither upstream or downstream gets us to a candidate (upstream/downstream won't leave 
    // blocks or enter new ones), we search forward and backward until we find one.
    Position next = canonicalizeCandidate(nextCandidate(position));
    Position prev = canonicalizeCandidate(previousCandidate(position));
    RefPtr nextNode = next.deprecatedNode();
    RefPtr prevNode = prev.deprecatedNode();

    // The new position must be in the same editable element. Enforce that first.
    // Unless the descent is from a non-editable html element to an editable body.
    if (is<HTMLHtmlElement>(node) && !node->hasEditableStyle()) {
        RefPtr body = node->document().bodyOrFrameset();
        if (body && body->hasEditableStyle())
            return next.isNotNull() ? next : prev;
    }

    RefPtr editingRoot = editableRootForPosition(position);
        
    // If the html element is editable, descending into its body will look like a descent 
    // from non-editable to editable content since rootEditableElement() always stops at the body.
    if ((editingRoot && editingRoot->hasTagName(htmlTag)) || (node && (node->isDocumentNode() || node->isShadowRoot())))
        return next.isNotNull() ? next : prev;
        
    bool prevIsInSameEditableElement = prevNode && editableRootForPosition(prev) == editingRoot;
    bool nextIsInSameEditableElement = nextNode && editableRootForPosition(next) == editingRoot;
    if (prevIsInSameEditableElement && !nextIsInSameEditableElement)
        return prev;

    if (nextIsInSameEditableElement && !prevIsInSameEditableElement)
        return next;

    if (!nextIsInSameEditableElement && !prevIsInSameEditableElement)
        return Position();

    // The new position should be in the same block flow element. Favor that.
    RefPtr originalBlock = deprecatedEnclosingBlockFlowElement(node.get());
    bool nextIsOutsideOriginalBlock = !nextNode->isDescendantOf(originalBlock.get()) && nextNode != originalBlock;
    bool prevIsOutsideOriginalBlock = !prevNode->isDescendantOf(originalBlock.get()) && prevNode != originalBlock;
    if (nextIsOutsideOriginalBlock && !prevIsOutsideOriginalBlock)
        return prev;
        
    return next;
}

char32_t VisiblePosition::characterAfter() const
{
    // We canonicalize to the first of two equivalent candidates, but the second of the two candidates
    // is the one that will be inside the text node containing the character after this visible position.
    Position pos = m_deepPosition.downstream();
    if (!is<Text>(pos.containerNode()))
        return 0;
    switch (pos.anchorType()) {
    case Position::PositionIsAfterChildren:
    case Position::PositionIsAfterAnchor:
    case Position::PositionIsBeforeAnchor:
    case Position::PositionIsBeforeChildren:
        return 0;
    case Position::PositionIsOffsetInAnchor:
        break;
    }
    unsigned offset = static_cast<unsigned>(pos.offsetInContainerNode());
    RefPtr textNode = pos.containerText();
    unsigned length = textNode->length();
    if (offset >= length)
        return 0;

    char32_t ch;
    U16_NEXT(textNode->data(), offset, length, ch);
    return ch;
}

InlineBoxAndOffset VisiblePosition::inlineBoxAndOffset() const
{
    return m_deepPosition.inlineBoxAndOffset(m_affinity);
}

InlineBoxAndOffset VisiblePosition::inlineBoxAndOffset(TextDirection primaryDirection) const
{
    return m_deepPosition.inlineBoxAndOffset(m_affinity, primaryDirection);
}

auto VisiblePosition::localCaretRect() const -> LocalCaretRect
{
    RefPtr node = m_deepPosition.anchorNode();
    if (!node)
        return { };

    auto boxAndOffset = inlineBoxAndOffset();
    CheckedPtr renderer = boxAndOffset.box ? &boxAndOffset.box->renderer() : node->renderer();
    if (!renderer)
        return { };

    return { computeLocalCaretRect(*renderer, boxAndOffset), const_cast<RenderObject*>(renderer.get()) };
}

IntRect VisiblePosition::absoluteCaretBounds(bool* insideFixed) const
{
    RenderBlock* renderer = nullptr;
    LayoutRect localRect = localCaretRectInRendererForCaretPainting(*this, renderer);
    return absoluteBoundsForLocalCaretRect(renderer, localRect, insideFixed);
}

FloatRect VisiblePosition::absoluteSelectionBoundsForLine() const
{
    auto box = inlineBoxAndOffset().box;
    if (!box)
        return { };

    auto line = box->lineBox();
    return line->formattingContextRoot().localToAbsoluteQuad(FloatRect { LineSelection::physicalRect(*line) }).boundingBox();
}

int VisiblePosition::lineDirectionPointForBlockDirectionNavigation() const
{
    auto localRect = localCaretRect();
    if (localRect.rect.isEmpty() || !localRect.renderer)
        return 0;

    // This ignores transforms on purpose, for now. Vertical navigation is done
    // without consulting transforms, so that 'up' in transformed text is 'up'
    // relative to the text, not absolute 'up'.
    CheckedPtr renderer = localRect.renderer;
    auto caretPoint = renderer->localToAbsolute(localRect.rect.location());
    CheckedPtr<RenderObject> containingBlock = renderer->containingBlock();
    if (!containingBlock)
        containingBlock = WTFMove(renderer); // Just use ourselves to determine the writing mode if we have no containing block.
    return containingBlock->isHorizontalWritingMode() ? caretPoint.x() : caretPoint.y();
}

#if ENABLE(TREE_DEBUGGING)

void VisiblePosition::debugPosition(ASCIILiteral msg) const
{
    if (isNull())
        SAFE_FPRINTF(stderr, "Position [%s]: null\n", msg);
    else {
        SAFE_FPRINTF(stderr, "Position [%s]: %s, ", msg, m_deepPosition.deprecatedNode()->nodeName().utf8());
        m_deepPosition.showAnchorTypeAndOffset();
    }
}

String VisiblePosition::debugDescription() const
{
    // Only log affinity when it's the non-default value of upstream.
    return makeString(m_deepPosition.debugDescription(), ", affinity: "_s, m_affinity == Affinity::Upstream ? "upstream"_s : ""_s);
}

void VisiblePosition::showTreeForThis() const
{
    m_deepPosition.showTreeForThis();
}

String VisiblePositionRange::debugDescription() const
{
    return makeString("start: "_s, start.debugDescription(), ", end: "_s, end.debugDescription());
}
#endif // ENABLE(TREE_DEBUGGING)

// FIXME: Maybe this should be deprecated too, like the underlying function?
Element* enclosingBlockFlowElement(const VisiblePosition& visiblePosition)
{
    if (visiblePosition.isNull())
        return nullptr;

    return deprecatedEnclosingBlockFlowElement(visiblePosition.deepEquivalent().protectedDeprecatedNode().get());
}

bool isFirstVisiblePositionInNode(const VisiblePosition& visiblePosition, const Node* node)
{
    if (visiblePosition.isNull())
        return false;

    if (!visiblePosition.deepEquivalent().protectedContainerNode()->isDescendantOf(node))
        return false;

    VisiblePosition previous = visiblePosition.previous();
    return previous.isNull() || !previous.deepEquivalent().protectedDeprecatedNode()->isDescendantOf(node);
}

bool isLastVisiblePositionInNode(const VisiblePosition& visiblePosition, const Node* node)
{
    if (visiblePosition.isNull())
        return false;

    if (!visiblePosition.deepEquivalent().protectedContainerNode()->isDescendantOf(node))
        return false;

    VisiblePosition next = visiblePosition.next();
    return next.isNull() || !next.deepEquivalent().protectedDeprecatedNode()->isDescendantOf(node);
}

bool areVisiblePositionsInSameTreeScope(const VisiblePosition& a, const VisiblePosition& b)
{
    return connectedInSameTreeScope(a.deepEquivalent().protectedAnchorNode().get(), b.deepEquivalent().protectedAnchorNode().get());
}

bool VisiblePosition::equals(const VisiblePosition& other) const
{
    return m_affinity == other.m_affinity && m_deepPosition.equals(other.m_deepPosition);
}

std::optional<BoundaryPoint> makeBoundaryPoint(const VisiblePosition& position)
{
    return makeBoundaryPoint(position.deepEquivalent());
}

Node* commonInclusiveAncestor(const VisiblePosition& a, const VisiblePosition& b)
{
    return commonInclusiveAncestor(a.deepEquivalent(), b.deepEquivalent());
}

TextStream& operator<<(TextStream& stream, Affinity affinity)
{
    switch (affinity) {
    case Affinity::Upstream:
        stream << "upstream";
        break;
    case Affinity::Downstream:
        stream << "downstream";
        break;
    }
    return stream;
}

TextStream& operator<<(TextStream& ts, const VisiblePosition& visiblePosition)
{
    TextStream::GroupScope scope(ts);
    ts << "VisiblePosition "_s << &visiblePosition;

    ts.dumpProperty("position"_s, visiblePosition.deepEquivalent());
    ts.dumpProperty("affinity"_s, visiblePosition.affinity());

    return ts;
}

std::optional<SimpleRange> makeSimpleRange(const VisiblePositionRange& range)
{
    return makeSimpleRange(range.start, range.end);
}

VisiblePositionRange makeVisiblePositionRange(const std::optional<SimpleRange>& range)
{
    if (!range)
        return { };
    return { makeContainerOffsetPosition(range->start), makeContainerOffsetPosition(range->end) };
}

std::partial_ordering operator<=>(const VisiblePosition& a, const VisiblePosition& b)
{
    // FIXME: Should two positions with different affinity be considered equivalent or not?
    return treeOrder<ComposedTree>(a.deepEquivalent(), b.deepEquivalent());
}

bool intersects(const VisiblePositionRange& a, const VisiblePositionRange& b)
{
    return a.start <= b.end && b.start <= a.end;
}

bool contains(const VisiblePositionRange& range, const VisiblePosition& point)
{
    return point >= range.start && point <= range.end;
}

VisiblePositionRange intersection(const VisiblePositionRange& a, const VisiblePositionRange& b)
{
    return { std::max(a.start, b.start), std::min(a.end, b.end) };
}

Node* commonInclusiveAncestor(const VisiblePositionRange& range)
{
    return commonInclusiveAncestor(range.start, range.end);
}

VisiblePosition midpoint(const VisiblePositionRange& range)
{
    RefPtr rootNode = commonInclusiveAncestor(range);
    if (!rootNode)
        return { };
    RefPtr rootContainerNode = dynamicDowncast<ContainerNode>(rootNode);
    if (!rootContainerNode)
        rootContainerNode = rootNode->parentNode();
    if (!rootContainerNode)
        return { };
    auto scope = makeRangeSelectingNodeContents(*rootContainerNode);
    auto characterRange = WebCore::characterRange(scope, *makeSimpleRange(range.start, range.end));
    return makeContainerOffsetPosition(resolveCharacterLocation(scope, characterRange.location + characterRange.length / 2));
}

}  // namespace WebCore

#if ENABLE(TREE_DEBUGGING)

void showTree(const WebCore::VisiblePosition* vpos)
{
    if (vpos)
        vpos->showTreeForThis();
}

void showTree(const WebCore::VisiblePosition& vpos)
{
    vpos.showTreeForThis();
}

#endif
