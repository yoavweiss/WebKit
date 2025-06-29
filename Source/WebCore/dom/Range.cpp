/*
 * (C) 1999 Lars Knoll (knoll@kde.org)
 * (C) 2000 Gunnstein Lye (gunnstein@netcom.no)
 * (C) 2000 Frederik Holljen (frederik.holljen@hig.no)
 * (C) 2001 Peter Kelly (pmk@post.com)
 * Copyright (C) 2004-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2011 Motorola Mobility. All rights reserved.
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
#include "Range.h"

#include "BoundaryPointInlines.h"
#include "Comment.h"
#include "ContainerNodeInlines.h"
#include "CustomElementReactionQueue.h"
#include "DOMRect.h"
#include "DOMRectList.h"
#include "DocumentFragment.h"
#include "DocumentType.h"
#include "ElementInlines.h"
#include "Event.h"
#include "FrameSelection.h"
#include "GeometryUtilities.h"
#include "HTMLBodyElement.h"
#include "HTMLHtmlElement.h"
#include "HTMLNames.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "NodeTraversal.h"
#include "NodeWithIndex.h"
#include "ProcessingInstruction.h"
#include "RangeBoundaryPointInlines.h"
#include "ScopedEventQueue.h"
#include "ShadowRoot.h"
#include "TextIterator.h"
#include "TrustedType.h"
#include "TypedElementDescendantIteratorInlines.h"
#include "VisibleUnits.h"
#include "WebCoreOpaqueRootInlines.h"
#include "markup.h"
#include <stdio.h>
#include <wtf/RefCountedLeakCounter.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>

namespace WebCore {

using namespace HTMLNames;

DEFINE_DEBUG_ONLY_GLOBAL(WTF::RefCountedLeakCounter, rangeCounter, ("Range"));

enum ContentsProcessDirection { ProcessContentsForward, ProcessContentsBackward };

static ExceptionOr<void> processNodes(Range::ActionType, Vector<Ref<Node>>&, Node* oldContainer, RefPtr<Node> newContainer);
static ExceptionOr<RefPtr<Node>> processContentsBetweenOffsets(Range::ActionType, RefPtr<DocumentFragment>, RefPtr<Node> container, unsigned startOffset, unsigned endOffset);
static ExceptionOr<RefPtr<Node>> processAncestorsAndTheirSiblings(Range::ActionType, Node* container, ContentsProcessDirection, ExceptionOr<RefPtr<Node>>&& passedClonedContainer, Node* commonRoot);

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(Range);

inline Range::Range(Document& ownerDocument)
    : m_ownerDocument(ownerDocument)
    , m_start(ownerDocument)
    , m_end(ownerDocument)
{
#ifndef NDEBUG
    rangeCounter.increment();
#endif

    protectedOwnerDocument()->attachRange(*this);
}

Ref<Range> Range::create(Document& ownerDocument)
{
    return adoptRef(*new Range(ownerDocument));
}

Range::~Range()
{
    ASSERT(!m_isAssociatedWithSelection);
    protectedOwnerDocument()->detachRange(*this);

#ifndef NDEBUG
    rangeCounter.decrement();
#endif
}

Ref<Document> Range::protectedOwnerDocument()
{
    return m_ownerDocument;
}

Node* Range::commonAncestorContainer() const
{
    return commonInclusiveAncestor(startContainer(), endContainer());
}

void Range::updateAssociatedSelection()
{
    if (m_isAssociatedWithSelection)
        protectedOwnerDocument()->selection().updateFromAssociatedLiveRange();
}

void Range::updateAssociatedHighlight()
{
    if (m_isAssociatedWithHighlight) {
        m_didChangeForHighlight = true;
        protectedOwnerDocument()->scheduleRenderingUpdate({ });
    }
}

void Range::updateDocument()
{
    Ref document = startContainer().document();
    if (m_ownerDocument.ptr() == document.ptr())
        return;
    ASSERT(!m_isAssociatedWithSelection);
    protectedOwnerDocument()->detachRange(*this);
    m_ownerDocument = WTFMove(document);
    protectedOwnerDocument()->attachRange(*this);
}

ExceptionOr<void> Range::setStart(Ref<Node>&& container, unsigned offset)
{
    auto childNode = checkNodeOffsetPair(container, offset);
    if (childNode.hasException())
        return childNode.releaseException();

    m_start.set(WTFMove(container), offset, childNode.releaseReturnValue());
    if (!is_lteq(treeOrder(makeBoundaryPoint(m_start), makeBoundaryPoint(m_end))))
        m_end = m_start;
    updateAssociatedSelection();
    updateDocument();
    updateAssociatedHighlight();
    return { };
}

ExceptionOr<void> Range::setEnd(Ref<Node>&& container, unsigned offset)
{
    auto childNode = checkNodeOffsetPair(container, offset);
    if (childNode.hasException())
        return childNode.releaseException();

    m_end.set(WTFMove(container), offset, childNode.releaseReturnValue());
    if (!is_lteq(treeOrder(makeBoundaryPoint(m_start), makeBoundaryPoint(m_end))))
        m_start = m_end;
    updateAssociatedSelection();
    updateDocument();
    updateAssociatedHighlight();
    return { };
}

void Range::collapse(bool toStart)
{
    if (toStart)
        m_end = m_start;
    else
        m_start = m_end;
    updateAssociatedSelection();
}

ExceptionOr<bool> Range::isPointInRange(Node& container, unsigned offset)
{
    if (auto checkResult = checkNodeOffsetPair(container, offset); checkResult.hasException()) {
        // DOM specification requires this check be done first but since there are no side effects,
        // we can do it in reverse order to avoid an extra root node check in the common case.
        if (&container.rootNode() != &startContainer().rootNode())
            return false;
        return checkResult.releaseException();
    }
    return contains(makeSimpleRange(*this), { container, offset });
}

ExceptionOr<short> Range::comparePoint(Node& container, unsigned offset) const
{
    if (auto checkResult = checkNodeOffsetPair(container, offset); checkResult.hasException()) {
        // DOM specification requires this check be done first but since there are no side effects,
        // we can do it in reverse order to avoid an extra root node check in the common case.
        if (&container.rootNode() != &startContainer().rootNode())
            return Exception { ExceptionCode::WrongDocumentError };
        return checkResult.releaseException();
    }
    auto ordering = treeOrder({ container, offset }, makeSimpleRange(*this));
    if (is_lt(ordering))
        return -1;
    if (is_eq(ordering))
        return 0;
    if (is_gt(ordering))
        return 1;
    return Exception { ExceptionCode::WrongDocumentError };
}

ExceptionOr<Range::CompareResults> Range::compareNode(Node& node) const
{
    // FIXME: This deprecated function should be removed.
    // We originally added it for interoperability with Firefox.
    // Recent versions of Firefox have removed it.

    // http://developer.mozilla.org/en/docs/DOM:range.compareNode
    // This method returns 0, 1, 2, or 3 based on if the node is before, after,
    // before and after(surrounds), or inside the range, respectively.

    if (!node.isConnected() || &node.document() != m_ownerDocument.ptr()) {
        // Match historical Firefox behavior.
        return NODE_BEFORE;
    }

    auto nodeRange = makeRangeSelectingNode(node);
    if (!nodeRange) {
        // Match historical Firefox behavior.
        return Exception { ExceptionCode::NotFoundError };
    }

    auto startOrdering = treeOrder(nodeRange->start, makeBoundaryPoint(m_start));
    auto endOrdering = treeOrder(nodeRange->end, makeBoundaryPoint(m_end));
    if (is_gteq(startOrdering) && is_lteq(endOrdering))
        return NODE_INSIDE;
    if (is_lteq(startOrdering) && is_gteq(endOrdering))
        return NODE_BEFORE_AND_AFTER;
    if (is_lteq(startOrdering))
        return NODE_BEFORE;
    if (is_gteq(endOrdering))
        return NODE_AFTER;
    return Exception { ExceptionCode::WrongDocumentError };
}

ExceptionOr<short> Range::compareBoundaryPoints(unsigned short how, const Range& sourceRange) const
{
    const RangeBoundaryPoint* thisPoint;
    const RangeBoundaryPoint* otherPoint;
    switch (how) {
    case START_TO_START:
        thisPoint = &m_start;
        otherPoint = &sourceRange.m_start;
        break;
    case START_TO_END:
        thisPoint = &m_end;
        otherPoint = &sourceRange.m_start;
        break;
    case END_TO_END:
        thisPoint = &m_end;
        otherPoint = &sourceRange.m_end;
        break;
    case END_TO_START:
        thisPoint = &m_start;
        otherPoint = &sourceRange.m_end;
        break;
    default:
        return Exception { ExceptionCode::NotSupportedError };
    }
    auto ordering = treeOrder(makeBoundaryPoint(*thisPoint), makeBoundaryPoint(*otherPoint));
    if (is_lt(ordering))
        return -1;
    if (is_eq(ordering))
        return 0;
    if (is_gt(ordering))
        return 1;
    return Exception { ExceptionCode::WrongDocumentError };
}

ExceptionOr<void> Range::deleteContents()
{
    auto result = processContents(Delete);
    if (result.hasException())
        return result.releaseException();
    return { };
}

bool Range::intersectsNode(Node& node) const
{
    return intersects(makeSimpleRange(*this), node);
}

static inline Node* highestAncestorUnderCommonRoot(Node* node, Node* commonRoot)
{
    if (node == commonRoot)
        return 0;

    ASSERT(commonRoot->contains(node));

    while (node->parentNode() != commonRoot)
        node = node->parentNode();

    return node;
}

static inline Node* childOfCommonRootBeforeOffset(Node* container, unsigned offset, Node* commonRoot)
{
    ASSERT(container);
    ASSERT(commonRoot);
    
    if (!commonRoot->contains(container))
        return nullptr;

    if (container == commonRoot) {
        container = container->firstChild();
        for (unsigned i = 0; container && i < offset; i++)
            container = container->nextSibling();
    } else {
        while (container->parentNode() != commonRoot)
            container = container->parentNode();
    }

    return container;
}

Ref<Node> Range::protectedStartContainer() const
{
    return startContainer();
}

Ref<Node> Range::protectedEndContainer() const
{
    return endContainer();
}

ExceptionOr<RefPtr<DocumentFragment>> Range::processContents(ActionType action)
{
    RefPtr<DocumentFragment> fragment;
    if (action == Extract || action == Clone)
        fragment = DocumentFragment::create(protectedOwnerDocument());

    if (collapsed())
        return fragment;

    RefPtr commonRoot = commonAncestorContainer();
    ASSERT(commonRoot);
    
    if (action == Extract) {
        Ref commonRootDocument = commonRoot->document();
        RefPtr doctype = commonRootDocument->doctype();
        if (doctype && contains(makeSimpleRange(*this), { *doctype, 0 }))
            return Exception { ExceptionCode::HierarchyRequestError };
    }

    if (&startContainer() == &endContainer()) {
        auto result = processContentsBetweenOffsets(action, fragment, protectedStartContainer().ptr(), m_start.offset(), m_end.offset());
        if (result.hasException())
            return result.releaseException();
        return fragment;
    }

    Vector<Ref<Element>> elementsToUpgrade;
    {
        CustomElementReactionStack customElementsReactionHoldingTank(commonRoot->document().globalObject());

        // Since mutation events can modify the range during the process, the boundary points need to be saved.
        RangeBoundaryPoint originalStart(m_start);
        RangeBoundaryPoint originalEnd(m_end);

        // what is the highest node that partially selects the start / end of the range?
        RefPtr partialStart = highestAncestorUnderCommonRoot(originalStart.protectedContainer().ptr(), commonRoot.get());
        RefPtr partialEnd = highestAncestorUnderCommonRoot(originalEnd.protectedContainer().ptr(), commonRoot.get());

        // Start and end containers are different.
        // There are three possibilities here:
        // 1. Start container == commonRoot (End container must be a descendant)
        // 2. End container == commonRoot (Start container must be a descendant)
        // 3. Neither is commonRoot, they are both descendants
        //
        // In case 3, we grab everything after the start (up until a direct child
        // of commonRoot) into leftContents, and everything before the end (up until
        // a direct child of commonRoot) into rightContents. Then we process all
        // commonRoot children between leftContents and rightContents
        //
        // In case 1 or 2, we skip either processing of leftContents or rightContents,
        // in which case the last lot of nodes either goes from the first or last
        // child of commonRoot.
        //
        // These are deleted, cloned, or extracted (i.e. both) depending on action.

        // Note that we are verifying that our common root hierarchy is still intact
        // after any DOM mutation event, at various stages below. See webkit bug 60350.

        RefPtr<Node> leftContents;
        if (&originalStart.container() != commonRoot && commonRoot->contains(originalStart.container())) {
            auto firstResult = processContentsBetweenOffsets(action, nullptr, originalStart.protectedContainer().ptr(), originalStart.offset(), originalStart.container().length());
            auto secondResult = processAncestorsAndTheirSiblings(action, originalStart.protectedContainer().ptr(), ProcessContentsForward, WTFMove(firstResult), commonRoot.get());
            // FIXME: A bit peculiar that we silently ignore the exception here, but we do have at least some regression tests that rely on this behavior.
            if (!secondResult.hasException())
                leftContents = secondResult.releaseReturnValue();
        }

        RefPtr<Node> rightContents;
        if (&endContainer() != commonRoot && commonRoot->contains(originalEnd.container())) {
            auto firstResult = processContentsBetweenOffsets(action, nullptr, originalEnd.protectedContainer().ptr(), 0, originalEnd.offset());
            auto secondResult = processAncestorsAndTheirSiblings(action, originalEnd.protectedContainer().ptr(), ProcessContentsBackward, WTFMove(firstResult), commonRoot.get());
            // FIXME: A bit peculiar that we silently ignore the exception here, but we do have at least some regression tests that rely on this behavior.
            if (!secondResult.hasException())
                rightContents = secondResult.releaseReturnValue();
        }

        // delete all children of commonRoot between the start and end container
        RefPtr processStart = childOfCommonRootBeforeOffset(originalStart.protectedContainer().ptr(), originalStart.offset(), commonRoot.get());
        if (processStart && &originalStart.container() != commonRoot) // processStart contains nodes before m_start.
            processStart = processStart->nextSibling();
        RefPtr processEnd = childOfCommonRootBeforeOffset(originalEnd.protectedContainer().ptr(), originalEnd.offset(), commonRoot.get());

        // Collapse the range, making sure that the result is not within a node that was partially selected.
        if (action == Extract || action == Delete) {
            if (partialStart && commonRoot->contains(*partialStart)) {
                auto result = setStart(partialStart->protectedParentNode().releaseNonNull(), partialStart->computeNodeIndex() + 1);
                if (result.hasException())
                    return result.releaseException();
            } else if (partialEnd && commonRoot->contains(*partialEnd)) {
                auto result = setStart(partialEnd->protectedParentNode().releaseNonNull(), partialEnd->computeNodeIndex());
                if (result.hasException())
                    return result.releaseException();
            }
            collapse(true);
        }

        // Now add leftContents, stuff in between, and rightContents to the fragment
        // (or just delete the stuff in between)

        if ((action == Extract || action == Clone) && leftContents) {
            auto result = fragment->appendChild(*leftContents);
            if (result.hasException())
                return result.releaseException();
        }

        if (processStart) {
            Vector<Ref<Node>> nodes;
            for (Node* node = processStart.get(); node && node != processEnd; node = node->nextSibling())
                nodes.append(*node);
            auto result = processNodes(action, nodes, commonRoot.get(), fragment.get());
            if (result.hasException())
                return result.releaseException();
        }

        if ((action == Extract || action == Clone) && rightContents) {
            auto result = fragment->appendChild(*rightContents);
            if (result.hasException())
                return result.releaseException();
        }

        UncheckedKeyHashSet<Ref<Element>> elementSet;
        for (Ref element : customElementsReactionHoldingTank.takeElements())
            elementSet.add(element.get());
        if (!elementSet.isEmpty()) {
            for (Ref element : descendantsOfType<Element>(*fragment)) {
                if (elementSet.contains(element.get()))
                    elementsToUpgrade.append(element.get());
            }
        }
    }
    CustomElementReactionQueue::enqueueElementsOnAppropriateElementQueue(elementsToUpgrade);

    return fragment;
}

static inline ExceptionOr<void> deleteCharacterData(CharacterData& data, unsigned startOffset, unsigned endOffset)
{
    if (data.length() - endOffset) {
        auto result = data.deleteData(endOffset, data.length() - endOffset);
        if (result.hasException())
            return result.releaseException();
    }
    if (startOffset) {
        auto result = data.deleteData(0, startOffset);
        if (result.hasException())
            return result.releaseException();
    }
    return { };
}

static ExceptionOr<RefPtr<Node>> processContentsBetweenOffsets(Range::ActionType action, RefPtr<DocumentFragment> fragment, RefPtr<Node> container, unsigned startOffset, unsigned endOffset)
{
    ASSERT(container);
    ASSERT(startOffset <= endOffset);

    RefPtr<Node> result;

    switch (container->nodeType()) {
    case Node::TEXT_NODE:
    case Node::CDATA_SECTION_NODE:
    case Node::COMMENT_NODE: {
        auto& dataNode = uncheckedDowncast<CharacterData>(*container);
        endOffset = std::min(endOffset, dataNode.length());
        startOffset = std::min(startOffset, endOffset);
        if (action == Range::Extract || action == Range::Clone) {
            Ref characters = uncheckedDowncast<CharacterData>(dataNode.cloneNode(true));
            auto deleteResult = deleteCharacterData(characters, startOffset, endOffset);
            if (deleteResult.hasException())
                return deleteResult.releaseException();
            if (fragment) {
                result = fragment;
                auto appendResult = result->appendChild(characters);
                if (appendResult.hasException())
                    return appendResult.releaseException();
            } else
                result = WTFMove(characters);
        }
        if (action == Range::Extract || action == Range::Delete) {
            auto deleteResult = dataNode.deleteData(startOffset, endOffset - startOffset);
            if (deleteResult.hasException())
                return deleteResult.releaseException();
        }
        break;
    }
    case Node::PROCESSING_INSTRUCTION_NODE: {
        auto& instruction = uncheckedDowncast<ProcessingInstruction>(*container);
        endOffset = std::min(endOffset, instruction.data().length());
        startOffset = std::min(startOffset, endOffset);
        if (action == Range::Extract || action == Range::Clone) {
            Ref processingInstruction = uncheckedDowncast<ProcessingInstruction>(instruction.cloneNode(true));
            processingInstruction->setData(processingInstruction->data().substring(startOffset, endOffset - startOffset));
            if (fragment) {
                result = fragment;
                auto appendResult = result->appendChild(processingInstruction);
                if (appendResult.hasException())
                    return appendResult.releaseException();
            } else
                result = WTFMove(processingInstruction);
        }
        if (action == Range::Extract || action == Range::Delete) {
            auto data = makeStringByRemoving(instruction.data(), startOffset, endOffset - startOffset);
            instruction.setData(WTFMove(data));
        }
        break;
    }
    case Node::ELEMENT_NODE:
    case Node::ATTRIBUTE_NODE:
    case Node::DOCUMENT_NODE:
    case Node::DOCUMENT_TYPE_NODE:
    case Node::DOCUMENT_FRAGMENT_NODE:
        // FIXME: Should we assert that some nodes never appear here?
        if (action == Range::Extract || action == Range::Clone) {
            if (fragment)
                result = fragment;
            else
                result = container->cloneNode(false);
        }
        Vector<Ref<Node>> nodes;
        Node* n = container->firstChild();
        for (unsigned i = startOffset; n && i; i--)
            n = n->nextSibling();
        for (unsigned i = startOffset; n && i < endOffset; i++, n = n->nextSibling()) {
            if (action != Range::Delete && n->isDocumentTypeNode()) {
                return Exception { ExceptionCode::HierarchyRequestError };
            }
            nodes.append(*n);
        }
        auto processResult = processNodes(action, nodes, container.get(), result);
        if (processResult.hasException())
            return processResult.releaseException();
        break;
    }

    return result;
}

static ExceptionOr<void> processNodes(Range::ActionType action, Vector<Ref<Node>>& nodes, Node* oldContainer, RefPtr<Node> newContainer)
{
    for (auto& node : nodes) {
        switch (action) {
        case Range::Delete: {
            auto result = oldContainer->removeChild(node);
            if (result.hasException())
                return result.releaseException();
            break;
        }
        case Range::Extract: {
            auto result = newContainer->appendChild(node); // will remove node from its parent
            if (result.hasException())
                return result.releaseException();
            break;
        }
        case Range::Clone: {
            auto result = newContainer->appendChild(node->cloneNode(true));
            if (result.hasException())
                return result.releaseException();
            break;
        }
        }
    }
    return { };
}

ExceptionOr<RefPtr<Node>> processAncestorsAndTheirSiblings(Range::ActionType action, Node* container, ContentsProcessDirection direction, ExceptionOr<RefPtr<Node>>&& passedClonedContainer, Node* commonRoot)
{
    if (passedClonedContainer.hasException())
        return WTFMove(passedClonedContainer);

    RefPtr clonedContainer = passedClonedContainer.releaseReturnValue();

    Vector<Ref<ContainerNode>> ancestors;
    for (ContainerNode* ancestor = container->parentNode(); ancestor && ancestor != commonRoot; ancestor = ancestor->parentNode())
        ancestors.append(*ancestor);

    RefPtr firstChildInAncestorToProcess = direction == ProcessContentsForward ? container->nextSibling() : container->previousSibling();
    for (Ref ancestor : ancestors) {
        if (action == Range::Extract || action == Range::Clone) {
            if (auto shadowRoot = dynamicDowncast<ShadowRoot>(ancestor.get())) {
                if (!shadowRoot->isClonable())
                    continue;
            }
            Ref clonedAncestor = ancestor->cloneNode(false); // Might have been removed already during mutation event.
            if (clonedContainer) {
                auto result = clonedAncestor->appendChild(*clonedContainer);
                if (result.hasException())
                    return result.releaseException();
            }
            clonedContainer = WTFMove(clonedAncestor);
        }

        // Copy siblings of an ancestor of start/end containers
        // FIXME: This assertion may fail if DOM is modified during mutation event
        // FIXME: Share code with Range::processNodes
        ASSERT(!firstChildInAncestorToProcess || firstChildInAncestorToProcess->parentNode() == ancestor.ptr());
        
        Vector<Ref<Node>> nodes;
        for (Node* child = firstChildInAncestorToProcess.get(); child;
            child = (direction == ProcessContentsForward) ? child->nextSibling() : child->previousSibling())
            nodes.append(*child);

        for (Ref child : nodes) {
            switch (action) {
            case Range::Delete: {
                auto result = ancestor->removeChild(child.get());
                if (result.hasException())
                    return result.releaseException();
                break;
            }
            case Range::Extract: // will remove child from ancestor
                if (direction == ProcessContentsForward) {
                    auto result = clonedContainer->appendChild(child.get());
                    if (result.hasException())
                        return result.releaseException();
                } else {
                    auto result = clonedContainer->insertBefore(child.get(), clonedContainer->protectedFirstChild());
                    if (result.hasException())
                        return result.releaseException();
                }
                break;
            case Range::Clone:
                if (direction == ProcessContentsForward) {
                    auto result = clonedContainer->appendChild(child->cloneNode(true));
                    if (result.hasException())
                        return result.releaseException();
                } else {
                    auto result = clonedContainer->insertBefore(child->cloneNode(true), clonedContainer->protectedFirstChild());
                    if (result.hasException())
                        return result.releaseException();
                }
                break;
            }
        }
        firstChildInAncestorToProcess = direction == ProcessContentsForward ? ancestor->nextSibling() : ancestor->previousSibling();
    }

    return clonedContainer;
}

ExceptionOr<Ref<DocumentFragment>> Range::extractContents()
{
    auto result = processContents(Extract);
    if (result.hasException())
        return result.releaseException();
    return result.releaseReturnValue().releaseNonNull();
}

ExceptionOr<Ref<DocumentFragment>> Range::cloneContents()
{
    auto result = processContents(Clone);
    if (result.hasException())
        return result.releaseException();
    return result.releaseReturnValue().releaseNonNull();
}

ExceptionOr<void> Range::insertNode(Ref<Node>&& node)
{
    auto startContainerNodeType = startContainer().nodeType();

    if (startContainerNodeType == Node::COMMENT_NODE || startContainerNodeType == Node::PROCESSING_INSTRUCTION_NODE)
        return Exception { ExceptionCode::HierarchyRequestError };
    RefPtr startContainerText = dynamicDowncast<Text>(startContainer());
    if (startContainerText && !startContainer().parentNode())
        return Exception { ExceptionCode::HierarchyRequestError };
    if (node.ptr() == &startContainer())
        return Exception { ExceptionCode::HierarchyRequestError };

    RefPtr referenceNode = startContainerText ? &startContainer() : startContainer().traverseToChildAt(startOffset());
    RefPtr parent = dynamicDowncast<ContainerNode>(referenceNode ? referenceNode->parentNode() : &startContainer());
    if (!parent)
        return Exception { ExceptionCode::HierarchyRequestError };

    auto result = parent->ensurePreInsertionValidity(node, referenceNode.get());
    if (result.hasException())
        return result.releaseException();

    EventQueueScope scope;
    if (startContainerText) {
        auto result = startContainerText->splitText(startOffset());
        if (result.hasException())
            return result.releaseException();
        referenceNode = result.releaseReturnValue();
    }

    if (referenceNode == node.ptr())
        referenceNode = referenceNode->nextSibling();

    auto removeResult = node->remove();
    if (removeResult.hasException())
        return removeResult.releaseException();

    unsigned newOffset = referenceNode ? referenceNode->computeNodeIndex() : parent->countChildNodes();
    if (RefPtr fragment = dynamicDowncast<DocumentFragment>(node.get()))
        newOffset += fragment->countChildNodes();
    else
        ++newOffset;

    auto insertResult = parent->insertBefore(node, WTFMove(referenceNode));
    if (insertResult.hasException())
        return insertResult.releaseException();

    if (collapsed())
        return setEnd(parent.releaseNonNull(), newOffset);

    return { };
}

String Range::toString() const
{
    auto range = makeSimpleRange(*this);
    StringBuilder builder;
    for (Ref node : intersectingNodes(range)) {
        if (RefPtr text = dynamicDowncast<Text>(node.get())) {
            auto offsetRange = characterDataOffsetRange(range, node);
            builder.appendSubstring(text->data(), offsetRange.start, offsetRange.end - offsetRange.start);
        }
    }
    return builder.toString();
}

// https://w3c.github.io/DOM-Parsing/#widl-Range-createContextualFragment-DocumentFragment-DOMString-fragment
ExceptionOr<Ref<DocumentFragment>> Range::createContextualFragment(Variant<RefPtr<TrustedHTML>, String>&& markup)
{
    Node& node = startContainer();
    auto stringValueHolder = trustedTypeCompliantString(*node.document().scriptExecutionContext(), WTFMove(markup), "Range createContextualFragment"_s);

    if (stringValueHolder.hasException())
        return stringValueHolder.releaseException();

    RefPtr<Element> element;
    if (is<Document>(node) || is<DocumentFragment>(node))
        element = nullptr;
    else if (auto* maybeElement = dynamicDowncast<Element>(node))
        element = maybeElement;
    else
        element = node.parentElement();
    if (!element || (element->document().isHTMLDocument() && is<HTMLHtmlElement>(*element)))
        element = HTMLBodyElement::create(node.protectedDocument());
    return WebCore::createContextualFragment(*element, stringValueHolder.releaseReturnValue(), { ParserContentPolicy::AllowScriptingContent, ParserContentPolicy::DoNotMarkAlreadyStarted });
}

ExceptionOr<RefPtr<Node>> Range::checkNodeOffsetPair(Node& node, unsigned offset)
{
    switch (node.nodeType()) {
    case Node::DOCUMENT_TYPE_NODE:
        return Exception { ExceptionCode::InvalidNodeTypeError };
    case Node::CDATA_SECTION_NODE:
    case Node::COMMENT_NODE:
    case Node::TEXT_NODE:
    case Node::PROCESSING_INSTRUCTION_NODE:
        if (offset > uncheckedDowncast<CharacterData>(node).length())
            return Exception { ExceptionCode::IndexSizeError };
        return nullptr;
    case Node::ATTRIBUTE_NODE:
    case Node::DOCUMENT_FRAGMENT_NODE:
    case Node::DOCUMENT_NODE:
    case Node::ELEMENT_NODE:
        if (!offset)
            return nullptr;
        RefPtr childBefore = node.traverseToChildAt(offset - 1);
        if (!childBefore)
            return Exception { ExceptionCode::IndexSizeError };
        return childBefore;
    }
    ASSERT_NOT_REACHED();
    return Exception { ExceptionCode::InvalidNodeTypeError };
}

Ref<Range> Range::cloneRange() const
{
    auto result = create(m_ownerDocument);
    result->setStart(protectedStartContainer(), m_start.offset());
    result->setEnd(protectedEndContainer(), m_end.offset());
    return result;
}

ExceptionOr<void> Range::setStartAfter(Node& node)
{
    RefPtr parent = node.parentNode();
    if (!parent)
        return Exception { ExceptionCode::InvalidNodeTypeError };
    return setStart(parent.releaseNonNull(), node.computeNodeIndex() + 1);
}

ExceptionOr<void> Range::setEndBefore(Node& node)
{
    RefPtr parent = node.parentNode();
    if (!parent)
        return Exception { ExceptionCode::InvalidNodeTypeError };
    return setEnd(parent.releaseNonNull(), node.computeNodeIndex());
}

ExceptionOr<void> Range::setEndAfter(Node& node)
{
    RefPtr parent = node.parentNode();
    if (!parent)
        return Exception { ExceptionCode::InvalidNodeTypeError };
    return setEnd(parent.releaseNonNull(), node.computeNodeIndex() + 1);
}

ExceptionOr<void> Range::selectNode(Node& node)
{
    RefPtr parent = node.parentNode();
    if (!parent)
        return Exception { ExceptionCode::InvalidNodeTypeError };
    unsigned index = node.computeNodeIndex();
    auto result = setStart(*parent, index);
    if (result.hasException())
        return result.releaseException();
    return setEnd(parent.releaseNonNull(), index + 1);
}

ExceptionOr<void> Range::selectNodeContents(Node& node)
{
    if (node.isDocumentTypeNode())
        return Exception { ExceptionCode::InvalidNodeTypeError };
    m_start.setToBeforeContents(node);
    m_end.setToAfterContents(node);
    updateAssociatedSelection();
    updateDocument();
    return { };
}

// https://dom.spec.whatwg.org/#dom-range-surroundcontents
ExceptionOr<void> Range::surroundContents(Node& newParent)
{
    Ref protectedNewParent = newParent;

    // Step 1: If a non-Text node is partially contained in the context object, then throw an InvalidStateError.
    RefPtr startNonTextContainer = startContainer();
    if (is<Text>(startNonTextContainer))
        startNonTextContainer = startNonTextContainer->parentNode();
    RefPtr endNonTextContainer = endContainer();
    if (is<Text>(endNonTextContainer))
        endNonTextContainer = endNonTextContainer->parentNode();
    if (startNonTextContainer != endNonTextContainer)
        return Exception { ExceptionCode::InvalidStateError };

    // Step 2: If newParent is a Document, DocumentType, or DocumentFragment node, then throw an InvalidNodeTypeError.
    switch (newParent.nodeType()) {
        case Node::ATTRIBUTE_NODE:
        case Node::DOCUMENT_FRAGMENT_NODE:
        case Node::DOCUMENT_NODE:
        case Node::DOCUMENT_TYPE_NODE:
            return Exception { ExceptionCode::InvalidNodeTypeError };
        case Node::CDATA_SECTION_NODE:
        case Node::COMMENT_NODE:
        case Node::ELEMENT_NODE:
        case Node::PROCESSING_INSTRUCTION_NODE:
        case Node::TEXT_NODE:
            break;
    }

    // Step 3: Let fragment be the result of extracting context object.
    auto fragment = extractContents();
    if (fragment.hasException())
        return fragment.releaseException();

    // Step 4: If newParent has children, replace all with null within newParent.
    if (RefPtr containerNode = dynamicDowncast<ContainerNode>(newParent); containerNode && containerNode->hasChildNodes())
        containerNode->replaceAll(nullptr);

    // Step 5: Insert newParent into context object.
    auto insertResult = insertNode(newParent);
    if (insertResult.hasException())
        return insertResult.releaseException();

    // Step 6: Append fragment to newParent.
    auto appendResult = newParent.appendChild(fragment.releaseReturnValue());
    if (appendResult.hasException())
        return appendResult.releaseException();

    // Step 7: Select newParent within context object.
    return selectNode(newParent);
}

ExceptionOr<void> Range::setStartBefore(Node& node)
{
    RefPtr parent = node.parentNode();
    if (!parent)
        return Exception { ExceptionCode::InvalidNodeTypeError };
    return setStart(parent.releaseNonNull(), node.computeNodeIndex());
}

#if ENABLE(TREE_DEBUGGING)
String Range::debugDescription() const
{
    return makeString("from offset "_s, m_start.offset(), " of "_s, startContainer().debugDescription(), " to offset "_s, m_end.offset(), " of "_s, endContainer().debugDescription());
}
#endif

static inline void boundaryNodeChildrenChanged(RangeBoundaryPoint& boundary, ContainerNode& container)
{
    if (boundary.childBefore() && &boundary.container() == &container)
        boundary.invalidateOffset();
}

void Range::nodeChildrenChanged(ContainerNode& container)
{
    ASSERT(&container.document() == m_ownerDocument.ptr());
    boundaryNodeChildrenChanged(m_start, container);
    boundaryNodeChildrenChanged(m_end, container);
    m_didChangeForHighlight = true;
}

static inline void boundaryNodeChildrenWillBeRemoved(RangeBoundaryPoint& boundary, ContainerNode& containerOfNodesToBeRemoved)
{
    if (containerOfNodesToBeRemoved.contains(&boundary.container()))
        boundary.setToBeforeContents(containerOfNodesToBeRemoved);
}

void Range::nodeChildrenWillBeRemoved(ContainerNode& container)
{
    ASSERT(&container.document() == m_ownerDocument.ptr());
    boundaryNodeChildrenWillBeRemoved(m_start, container);
    boundaryNodeChildrenWillBeRemoved(m_end, container);
    m_didChangeForHighlight = true;
}

static inline void boundaryNodeWillBeRemoved(RangeBoundaryPoint& boundary, Node& nodeToBeRemoved)
{
    if (boundary.childBefore() == &nodeToBeRemoved)
        boundary.childBeforeWillBeRemoved();
    else if (nodeToBeRemoved.contains(boundary.protectedContainer().ptr()))
        boundary.setToBeforeNode(nodeToBeRemoved);
}

void Range::nodeWillBeRemoved(Node& node)
{
    ASSERT(&node.document() == m_ownerDocument.ptr());
    ASSERT(&node != m_ownerDocument.ptr());
    ASSERT(node.parentNode());
    boundaryNodeWillBeRemoved(m_start, node);
    boundaryNodeWillBeRemoved(m_end, node);
    m_didChangeForHighlight = true;
}

bool Range::parentlessNodeMovedToNewDocumentAffectsRange(Node& node)
{
    return node.isShadowIncludingInclusiveAncestorOf(&m_start.container());
}

void Range::updateRangeForParentlessNodeMovedToNewDocument(Node& node)
{
    protectedOwnerDocument()->detachRange(*this);
    m_ownerDocument = node.document();
    protectedOwnerDocument()->attachRange(*this);
}

static inline void boundaryTextInserted(RangeBoundaryPoint& boundary, Node& text, unsigned offset, unsigned length)
{
    if (&boundary.container() != &text)
        return;
    unsigned boundaryOffset = boundary.offset();
    if (offset >= boundaryOffset)
        return;
    boundary.setOffset(boundaryOffset + length);
}

void Range::textInserted(Node& text, unsigned offset, unsigned length)
{
    ASSERT(&text.document() == m_ownerDocument.ptr());
    boundaryTextInserted(m_start, text, offset, length);
    boundaryTextInserted(m_end, text, offset, length);
    m_didChangeForHighlight = true;
}

static inline void boundaryTextRemoved(RangeBoundaryPoint& boundary, Node& text, unsigned offset, unsigned length)
{
    if (&boundary.container() != &text)
        return;
    unsigned boundaryOffset = boundary.offset();
    if (offset >= boundaryOffset)
        return;
    if (offset + length >= boundaryOffset)
        boundary.setOffset(offset);
    else
        boundary.setOffset(boundaryOffset - length);
}

void Range::textRemoved(Node& text, unsigned offset, unsigned length)
{
    ASSERT(&text.document() == m_ownerDocument.ptr());
    boundaryTextRemoved(m_start, text, offset, length);
    boundaryTextRemoved(m_end, text, offset, length);
    m_didChangeForHighlight = true;
}

static inline void boundaryTextNodesMerged(RangeBoundaryPoint& boundary, NodeWithIndex& oldNode, unsigned offset)
{
    if (&boundary.container() == oldNode.node())
        boundary.set(oldNode.node()->protectedPreviousSibling().releaseNonNull(), boundary.offset() + offset, nullptr);
    else if (&boundary.container() == oldNode.node()->parentNode() && boundary.offset() == static_cast<unsigned>(oldNode.index()))
        boundary.set(oldNode.node()->protectedPreviousSibling().releaseNonNull(), offset, nullptr);
}

void Range::textNodesMerged(NodeWithIndex& oldNode, unsigned offset)
{
    ASSERT(oldNode.node());
    ASSERT(&oldNode.node()->document() == m_ownerDocument.ptr());
    ASSERT(oldNode.node()->parentNode());
    ASSERT(oldNode.node()->isTextNode());
    ASSERT(oldNode.node()->previousSibling());
    ASSERT(oldNode.node()->previousSibling()->isTextNode());
    boundaryTextNodesMerged(m_start, oldNode, offset);
    boundaryTextNodesMerged(m_end, oldNode, offset);
    m_didChangeForHighlight = true;
}

static inline void boundaryTextNodesSplit(RangeBoundaryPoint& boundary, Text& oldNode)
{
    RefPtr parent = oldNode.parentNode();
    if (&boundary.container() == &oldNode) {
        unsigned splitOffset = oldNode.length();
        unsigned boundaryOffset = boundary.offset();
        if (boundaryOffset > splitOffset) {
            if (parent)
                boundary.set(oldNode.protectedNextSibling().releaseNonNull(), boundaryOffset - splitOffset, nullptr);
            else
                boundary.setOffset(splitOffset);
        }
        return;
    }
    if (!parent)
        return;
    if (&boundary.container() == parent && boundary.childBefore() == &oldNode) {
        RefPtr newChild = oldNode.nextSibling();
        ASSERT(newChild);
        boundary.setToAfterNode(newChild.releaseNonNull());
    }
}

void Range::textNodeSplit(Text& oldNode)
{
    ASSERT(&oldNode.document() == m_ownerDocument.ptr());
    ASSERT(!oldNode.parentNode() || oldNode.nextSibling());
    ASSERT(!oldNode.parentNode() || oldNode.nextSibling()->isTextNode());
    boundaryTextNodesSplit(m_start, oldNode);
    boundaryTextNodesSplit(m_end, oldNode);
    m_didChangeForHighlight = true;
}

ExceptionOr<void> Range::expand(const String& unit)
{
    auto start = VisiblePosition { makeContainerOffsetPosition(protectedStartContainer(), startOffset()) };
    auto end = VisiblePosition { makeContainerOffsetPosition(protectedEndContainer(), endOffset()) };
    if (unit == "word"_s) {
        start = startOfWord(start);
        end = endOfWord(end);
    } else if (unit == "sentence"_s) {
        start = startOfSentence(start);
        end = endOfSentence(end);
    } else if (unit == "block"_s) {
        start = startOfParagraph(start);
        end = endOfParagraph(end);
    } else if (unit == "document"_s) {
        start = startOfDocument(start);
        end = endOfDocument(end);
    } else
        return { };

    RefPtr startContainer = start.deepEquivalent().containerNode();
    if (!startContainer)
        return Exception { ExceptionCode::TypeError };
    auto result = setStart(startContainer.releaseNonNull(), start.deepEquivalent().computeOffsetInContainerNode());
    if (result.hasException())
        return result.releaseException();
    RefPtr endContainer = end.deepEquivalent().containerNode();
    if (!endContainer)
        return Exception { ExceptionCode::TypeError };
    return setEnd(endContainer.releaseNonNull(), end.deepEquivalent().computeOffsetInContainerNode());
}

Ref<DOMRectList> Range::getClientRects() const
{
    startContainer().protectedDocument()->updateLayout();
    return DOMRectList::create(RenderObject::clientBorderAndTextRects(makeSimpleRange(*this)));
}

Ref<DOMRect> Range::getBoundingClientRect() const
{
    return boundingClientRect(makeSimpleRange(*this));
}

Ref<DOMRect> Range::boundingClientRect(const SimpleRange& simpleRange)
{
    simpleRange.startContainer().protectedDocument()->updateLayout();
    return DOMRect::create(unionRectIgnoringZeroRects(RenderObject::clientBorderAndTextRects(simpleRange)));
}

static void setBothEndpoints(Range& range, const SimpleRange& value)
{
    Ref startContainer = value.start.container;
    range.setStart(WTFMove(startContainer), value.start.offset);
    Ref endContainer = value.end.container;
    range.setEnd(WTFMove(endContainer), value.end.offset);
}

void Range::updateFromSelection(const SimpleRange& value)
{
    ASSERT(m_isAssociatedWithSelection);
    m_isAssociatedWithSelection = false;
    setBothEndpoints(*this, value);
    m_isAssociatedWithSelection = true;
}

LocalDOMWindow* Range::window() const
{
    return m_isAssociatedWithSelection ? m_ownerDocument->window() : nullptr;
}

SimpleRange makeSimpleRange(const Range& range)
{
    return { { range.protectedStartContainer(), range.startOffset() }, { range.protectedEndContainer(), range.endOffset() } };
}

SimpleRange makeSimpleRange(const Ref<Range>& range)
{
    return makeSimpleRange(range.get());
}

std::optional<SimpleRange> makeSimpleRange(const Range* range)
{
    if (!range)
        return std::nullopt;
    return makeSimpleRange(*range);
}

std::optional<SimpleRange> makeSimpleRange(const RefPtr<Range>& range)
{
    return makeSimpleRange(range.get());
}

Ref<Range> createLiveRange(const SimpleRange& range)
{
    Ref result = Range::create(range.start.document());
    setBothEndpoints(result, range);
    return result;
}

RefPtr<Range> createLiveRange(const std::optional<SimpleRange>& range)
{
    if (!range)
        return nullptr;
    return createLiveRange(*range);
}

void Range::visitNodesConcurrently(JSC::AbstractSlotVisitor& visitor) const
{
    addWebCoreOpaqueRoot(visitor, m_start.container());
    addWebCoreOpaqueRoot(visitor, m_end.container());
}

} // namespace WebCore

#if ENABLE(TREE_DEBUGGING)

void showTree(const WebCore::Range* range)
{
    if (range) {
        range->startContainer().showTreeAndMark(&range->startContainer(), "S"_s, &range->endContainer(), "E"_s);
        fprintf(stderr, "start offset: %d, end offset: %d\n", range->startOffset(), range->endOffset());
    }
}

#endif
