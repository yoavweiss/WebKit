/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
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

#include "Document.h"
#include "FloatRect.h"
#include "GCReachableRef.h"
#include "IntersectionObserverCallback.h"
#include "LengthBox.h"
#include "ReducedResolutionSeconds.h"
#include <wtf/RefCountedAndCanMakeWeakPtr.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/WTFString.h>

namespace JSC {

class AbstractSlotVisitor;

}

namespace WebCore {

class ContainerNode;
class Document;
class Element;
class IntersectionObserverEntry;

struct IntersectionObserverRegistration {
    WeakPtr<IntersectionObserver> observer;
    std::optional<size_t> previousThresholdIndex;
};

struct IntersectionObserverData {
    WTF_DEPRECATED_MAKE_STRUCT_FAST_ALLOCATED(IntersectionObserverData);

    // IntersectionObservers for which the node that owns this IntersectionObserverData is the root.
    // An IntersectionObserver is only owned by a JavaScript wrapper. ActiveDOMObject::virtualHasPendingActivity
    // is overridden to keep this wrapper alive while the observer has ongoing observations.
    Vector<WeakPtr<IntersectionObserver>> observers;

    // IntersectionObserverRegistrations for which the node that owns this IntersectionObserverData is the target.
    Vector<IntersectionObserverRegistration> registrations;
};

enum class IncludeObscuredInsets : bool { No, Yes };

class IntersectionObserver : public RefCountedAndCanMakeWeakPtr<IntersectionObserver> {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(IntersectionObserver);
public:
    struct Init {
        std::optional<Variant<RefPtr<Element>, RefPtr<Document>>> root;
        String rootMargin;
        String scrollMargin;
        Variant<double, Vector<double>> threshold;
    };

    static ExceptionOr<Ref<IntersectionObserver>> create(Document&, Ref<IntersectionObserverCallback>&&, Init&&, IncludeObscuredInsets = IncludeObscuredInsets::No);

    ~IntersectionObserver();

    Document* trackingDocument() const { return m_root ? &m_root->document() : m_implicitRootDocument.get(); }

    ContainerNode* root() const { return m_root.get(); }
    String rootMargin() const;
    String scrollMargin() const;
    const LengthBox& rootMarginBox() const { return m_rootMargin; }
    const LengthBox& scrollMarginBox() const { return m_scrollMargin; }
    const Vector<double>& thresholds() const { return m_thresholds; }
    const Vector<WeakPtr<Element, WeakPtrImplWithEventTargetData>>& observationTargets() const { return m_observationTargets; }
    bool hasObservationTargets() const { return m_observationTargets.size(); }
    bool isObserving(const Element&) const;

    void observe(Element&);
    void unobserve(Element&);
    void disconnect();

    struct TakenRecords {
        Vector<Ref<IntersectionObserverEntry>> records;
        Vector<GCReachableRef<Element>> pendingTargets;
    };
    TakenRecords takeRecords();

    void targetDestroyed(Element&);
    void rootDestroyed();

    enum class NeedNotify : bool { No, Yes };
    NeedNotify updateObservations(Document&);

    std::optional<ReducedResolutionSeconds> nowTimestamp() const;

    void appendQueuedEntry(Ref<IntersectionObserverEntry>&&);
    void notify();

    IntersectionObserverCallback* callbackConcurrently() { return m_callback.get(); }
    bool isReachableFromOpaqueRoots(JSC::AbstractSlotVisitor&) const;

private:
    IntersectionObserver(Document&, Ref<IntersectionObserverCallback>&&, ContainerNode* root, LengthBox&& parsedRootMargin, LengthBox&& parsedScrollMargin, Vector<double>&& thresholds, IncludeObscuredInsets);

    bool removeTargetRegistration(Element&);
    void removeAllTargets();

    struct IntersectionObservationState {
        FloatRect rootBounds;
        std::optional<FloatRect> absoluteIntersectionRect; // Only computed if intersecting.
        std::optional<FloatRect> absoluteTargetRect; // Only computed if first observation, or intersecting.
        std::optional<FloatRect> absoluteRootBounds; // Only computed if observationChanged.
        float intersectionRatio { 0 };
        size_t thresholdIndex { 0 };
        bool canComputeIntersection { false };
        bool isIntersecting { false };
        bool observationChanged { false };
    };

    enum class ApplyRootMargin : bool { No, Yes };
    IntersectionObservationState computeIntersectionState(const IntersectionObserverRegistration&, LocalFrameView&, Element& target, ApplyRootMargin) const;

    WeakPtr<Document, WeakPtrImplWithEventTargetData> m_implicitRootDocument;
    WeakPtr<ContainerNode, WeakPtrImplWithEventTargetData> m_root;
    LengthBox m_rootMargin;
    LengthBox m_scrollMargin;
    Vector<double> m_thresholds;
    RefPtr<IntersectionObserverCallback> m_callback;
    Vector<WeakPtr<Element, WeakPtrImplWithEventTargetData>> m_observationTargets;
    Vector<GCReachableRef<Element>> m_pendingTargets;
    Vector<Ref<IntersectionObserverEntry>> m_queuedEntries;
    Vector<GCReachableRef<Element>> m_targetsWaitingForFirstObservation;
    IncludeObscuredInsets m_includeObscuredInsets { IncludeObscuredInsets::No };
};


} // namespace WebCore
