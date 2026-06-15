/*
 * Copyright (C) 2015-2017 Apple Inc. All rights reserved.
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

#pragma once

#include <algorithm>
#include <ranges>
#include <wtf/BitVector.h>
#include <wtf/IndexSparseSet.h>
#include <wtf/SparseBitVector.h>
#include <wtf/StdLibExtras.h>

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

namespace WTF {

// HEADS UP: The algorithm here is duplicated in AirRegLiveness.h. That one uses sets rather
// than fancy vectors, because that's better for register liveness analysis.
template<typename Adapter>
class Liveness : public Adapter {
public:
    typedef typename Adapter::CFG CFG;
    typedef typename Adapter::Thing Thing;
    typedef Vector<unsigned, 4, UnsafeVectorOverflow> IndexVector;
    typedef IndexSparseSet<unsigned, DefaultIndexSparseSetTraits<unsigned>, UnsafeVectorOverflow> Workset;
    
    template<typename... Arguments>
    Liveness(CFG& cfg, Arguments&&... arguments)
        : Adapter(std::forward<Arguments>(arguments)...)
        , m_cfg(cfg)
        , m_workset(Adapter::numIndices())
        , m_liveAtHead(cfg.template newMap<IndexVector>())
        , m_liveAtTail(cfg.template newMap<IndexVector>())
    {
    }
    
    // This calculator has to be run in reverse.
    class LocalCalc {
        WTF_DEPRECATED_MAKE_FAST_ALLOCATED(LocalCalc);
    public:
        LocalCalc(Liveness& liveness, typename CFG::Node block)
            : m_liveness(liveness)
            , m_block(block)
        {
            auto& workset = liveness.m_workset;
            workset.clear();
            IndexVector& liveAtTail = liveness.m_liveAtTail[block];
            for (unsigned index : liveAtTail)
                workset.add(index);
        }

        class Iterable {
            WTF_DEPRECATED_MAKE_FAST_ALLOCATED(Iterable);
        public:
            Iterable(Liveness& liveness)
                : m_liveness(liveness)
            {
            }

            class iterator {
                WTF_DEPRECATED_MAKE_FAST_ALLOCATED(iterator);
            public:
                iterator(Adapter& adapter, Workset::const_iterator sparceSetIterator)
                    : m_adapter(adapter)
                    , m_sparceSetIterator(sparceSetIterator)
                {
                }

                iterator& operator++()
                {
                    ++m_sparceSetIterator;
                    return *this;
                }

                typename Adapter::Thing operator*() const
                {
                    return m_adapter.indexToValue(*m_sparceSetIterator);
                }

                bool operator==(const iterator& other) const { return m_sparceSetIterator == other.m_sparceSetIterator; }

            private:
                Adapter& m_adapter;
                Workset::const_iterator m_sparceSetIterator;
            };

            iterator begin() const LIFETIME_BOUND { return iterator(m_liveness, m_liveness.m_workset.begin()); }
            iterator end() const LIFETIME_BOUND { return iterator(m_liveness, m_liveness.m_workset.end()); }
            
            bool contains(const typename Adapter::Thing& thing) const
            {
                return m_liveness.m_workset.contains(m_liveness.valueToIndex(thing));
            }

        private:
            Liveness& m_liveness;
        };

        Iterable live() const
        {
            return Iterable(m_liveness);
        }

        bool isLive(const typename Adapter::Thing& thing) const
        {
            return live().contains(thing);
        }

        void execute(unsigned instIndex)
        {
            auto& workset = m_liveness.m_workset;

            // Want an easy example to help you visualize how this works?
            // Check out B3VariableLiveness.h.
            //
            // Want a hard example to help you understand the hard cases?
            // Check out AirLiveness.h.
            
            m_liveness.forEachDef(
                m_block, instIndex + 1,
                [&] (unsigned index) {
                    workset.remove(index);
                });
            
            m_liveness.forEachUse(
                m_block, instIndex,
                [&] (unsigned index) {
                    workset.add(index);
                });
        }
        
    private:
        Liveness& m_liveness;
        typename CFG::Node m_block;
    };

    const IndexVector& rawLiveAtHead(typename CFG::Node block)
    {
        return m_liveAtHead[block];
    }

    template<typename UnderlyingIterable>
    class Iterable {
        WTF_DEPRECATED_MAKE_FAST_ALLOCATED(Iterable);
    public:
        Iterable(Liveness& liveness, const UnderlyingIterable& iterable)
            : m_liveness(liveness)
            , m_iterable(iterable)
        {
        }

        class iterator {
            WTF_DEPRECATED_MAKE_FAST_ALLOCATED(iterator);
        public:
            iterator()
                : m_liveness(nullptr)
                , m_iter()
            {
            }
            
            iterator(Liveness& liveness, typename UnderlyingIterable::const_iterator iter)
                : m_liveness(&liveness)
                , m_iter(iter)
            {
            }

            typename Adapter::Thing operator*()
            {
                return m_liveness->indexToValue(*m_iter);
            }

            iterator& operator++()
            {
                ++m_iter;
                return *this;
            }

            bool operator==(const iterator& other) const
            {
                ASSERT(m_liveness == other.m_liveness);
                return m_iter == other.m_iter;
            }

        private:
            Liveness* m_liveness;
            typename UnderlyingIterable::const_iterator m_iter;
        };

        iterator begin() const LIFETIME_BOUND { return iterator(m_liveness, m_iterable.begin()); }
        iterator end() const LIFETIME_BOUND { return iterator(m_liveness, m_iterable.end()); }

        bool contains(const typename Adapter::Thing& thing) const
        {
            return m_liveness.m_workset.contains(m_liveness.valueToIndex(thing));
        }

    private:
        Liveness& m_liveness;
        const UnderlyingIterable& m_iterable;
    };

    Iterable<IndexVector> liveAtHead(typename CFG::Node block)
    {
        return Iterable<IndexVector>(*this, m_liveAtHead[block]);
    }

    Iterable<IndexVector> liveAtTail(typename CFG::Node block)
    {
        return Iterable<IndexVector>(*this, m_liveAtTail[block]);
    }

    Workset& workset() LIFETIME_BOUND { return m_workset; }
    
    class LiveAtHead {
        WTF_DEPRECATED_MAKE_FAST_ALLOCATED(LiveAtHead);
    public:
        LiveAtHead(Liveness& liveness)
            : m_liveness(liveness)
        {
            for (unsigned blockIndex = m_liveness.m_cfg.numNodes(); blockIndex--;) {
                typename CFG::Node block = m_liveness.m_cfg.node(blockIndex);
                if (!block)
                    continue;
                
                std::ranges::sort(m_liveness.m_liveAtHead[block]);
            }
        }
        
        bool isLiveAtHead(typename CFG::Node block, const typename Adapter::Thing& thing)
        {
            return !!tryBinarySearch<unsigned>(m_liveness.m_liveAtHead[block], m_liveness.m_liveAtHead[block].size(), m_liveness.valueToIndex(thing), [] (unsigned* value) { return *value; });
        }
        
    private:
        Liveness& m_liveness;
    };
    
    LiveAtHead liveAtHead() { return LiveAtHead(*this); }

protected:
    void compute()
    {
        uint64_t denseMatrixBits = static_cast<uint64_t>(m_cfg.numNodes()) * Adapter::numIndices();
        constexpr uint64_t denseMatrixBitBudget = 32 * 1024 * 1024; // 4 MB per live-set matrix.
        bool useSparse = denseMatrixBits > denseMatrixBitBudget;
#if ASSERT_ENABLED
        // Most JSC stress test functions are small enough that the dense path would always win.
        // Force the sparse path on roughly half of them in debug builds so the sparse implementation
        // gets meaningful coverage from the regular test suite.
        if (!useSparse && (m_cfg.numNodes() & 1))
            useSparse = true;
#endif
        if (useSparse)
            computeSparse();
        else
            computeDense();
    }

    void computeDense()
    {
        Adapter::prepareToCompute();

        unsigned numNodes = m_cfg.numNodes();
        unsigned numIndices = Adapter::numIndices();

        // Per-block live sets are stored as a flat bit-matrix: a single Vector<uint64_t> of
        // numNodes * wordsPerSet words, where block b's set occupies the contiguous slice
        // [b * wordsPerSet, (b + 1) * wordsPerSet). Keeping each set as a span of machine words lets
        // the whole dataflow run as plain word loops, with one allocation per matrix (rather than
        // one per block) and no inline/out-of-line special-casing. This is what makes liveness fast
        // on the very large functions (e.g. SQLite's VDBE) that dominate its cost.
        constexpr unsigned wordBits = 64;
        size_t wordsPerSet = (numIndices + wordBits - 1) / wordBits;
        size_t matrixWords = static_cast<size_t>(numNodes) * wordsPerSet;

        Vector<uint64_t> liveInStore(matrixWords);
        Vector<uint64_t> liveOutStore(matrixWords);
        Vector<uint64_t> genStore(matrixWords);
        Vector<uint64_t> killStore(matrixWords);
        // Vector's sized constructor does not zero POD storage, and the dataflow ORs into these
        // sets, so they must start empty.
        std::ranges::fill(liveInStore, 0);
        std::ranges::fill(liveOutStore, 0);
        std::ranges::fill(genStore, 0);
        std::ranges::fill(killStore, 0);
        std::span<uint64_t> liveInMatrix = liveInStore.mutableSpan();
        std::span<uint64_t> liveOutMatrix = liveOutStore.mutableSpan();
        std::span<uint64_t> genMatrix = genStore.mutableSpan();
        std::span<uint64_t> killMatrix = killStore.mutableSpan();

        auto setFor = [&] (std::span<uint64_t> matrix, unsigned blockIndex) -> std::span<uint64_t> {
            return matrix.subspan(static_cast<size_t>(blockIndex) * wordsPerSet, wordsPerSet);
        };
        auto setBit = [] (std::span<uint64_t> set, unsigned index) {
            set[index / wordBits] |= static_cast<uint64_t>(1) << (index % wordBits);
        };

        BitVector dirtyBlocks(numNodes);
        Vector<unsigned, 64> worklist;
        for (unsigned blockIndex = 0; blockIndex < numNodes; ++blockIndex) {
            auto block = m_cfg.node(blockIndex);
            if (!block)
                continue;

            auto killSet = setFor(killMatrix, blockIndex);
            auto genSet = setFor(genMatrix, blockIndex);
            auto liveOutSet = setFor(liveOutMatrix, blockIndex);

            // kill: every value defined anywhere in the block.
            for (size_t boundary = 0; boundary <= Adapter::blockSize(block); ++boundary) {
                Adapter::forEachDef(block, boundary, [&] (unsigned index) {
                    setBit(killSet, index);
                });
            }

            // gen: the block's transfer function applied to an empty live-out, i.e. the uses that
            // are exposed at the head. This is the only place we walk instructions; the fixpoint
            // below works purely on gen/kill/liveOut.
            m_workset.clear();
            for (size_t instIndex = Adapter::blockSize(block); instIndex--;) {
                Adapter::forEachDef(block, instIndex + 1, [&] (unsigned index) { m_workset.remove(index); });
                Adapter::forEachUse(block, instIndex, [&] (unsigned index) { m_workset.add(index); });
            }
            Adapter::forEachDef(block, 0, [&] (unsigned index) { m_workset.remove(index); });
            for (unsigned index : m_workset)
                setBit(genSet, index);

            // liveOut automatically contains the LateUse's of the terminal.
            Adapter::forEachUse(block, Adapter::blockSize(block), [&] (unsigned index) {
                setBit(liveOutSet, index);
            });

            dirtyBlocks.quickSet(blockIndex);
            worklist.append(blockIndex);
        }

        while (!worklist.isEmpty()) {
            unsigned blockIndex = worklist.takeLast();
            bool cleared = dirtyBlocks.quickClear(blockIndex);
            ASSERT_UNUSED(cleared, cleared);

            auto block = m_cfg.node(blockIndex);
            ASSERT(block);

            auto liveInSet = setFor(liveInMatrix, blockIndex);
            auto liveOutSet = setFor(liveOutMatrix, blockIndex);
            auto genSet = setFor(genMatrix, blockIndex);
            auto killSet = setFor(killMatrix, blockIndex);

            // liveIn = gen | (liveOut & ~kill)
            uint64_t changed = 0;
            for (size_t i = 0; i < wordsPerSet; ++i) {
                uint64_t newLiveIn = genSet[i] | (liveOutSet[i] & ~killSet[i]);
                uint64_t oldLiveIn = liveInSet[i];
                liveInSet[i] = newLiveIn;
                changed |= newLiveIn ^ oldLiveIn;
            }
            if (!changed)
                continue;

            // Propagate this block's liveIn into each predecessor's liveOut, re-queuing a predecessor
            // only when its liveOut actually grew.
            for (auto predecessor : m_cfg.predecessors(block)) {
                auto predLiveOut = setFor(liveOutMatrix, predecessor->index());
                uint64_t predChanged = 0;
                for (size_t i = 0; i < wordsPerSet; ++i) {
                    uint64_t oldLiveOut = predLiveOut[i];
                    uint64_t newLiveOut = oldLiveOut | liveInSet[i];
                    predLiveOut[i] = newLiveOut;
                    predChanged |= newLiveOut ^ oldLiveOut;
                }
                if (predChanged && !dirtyBlocks.quickSet(predecessor->index()))
                    worklist.append(predecessor->index());
            }
        }

        // Materialize the sorted IndexVector representation that the public API uses. WTF::forEachSetBit
        // yields ascending indices, so the resulting vectors are already sorted.
        for (unsigned blockIndex = 0; blockIndex < numNodes; ++blockIndex) {
            auto block = m_cfg.node(blockIndex);
            if (!block)
                continue;
            WTF::forEachSetBit(std::span<const uint64_t> { setFor(liveInMatrix, blockIndex) }, [&] (size_t index) { m_liveAtHead[block].append(index); });
            WTF::forEachSetBit(std::span<const uint64_t> { setFor(liveOutMatrix, blockIndex) }, [&] (size_t index) { m_liveAtTail[block].append(index); });
        }
    }

    // Sparse fallback for functions whose dense matrices would be too large. Memory is proportional
    // to live values, at the cost of working bit by bit (SparseBitVector has no bulk word ops).
    void computeSparse()
    {
        Adapter::prepareToCompute();

        unsigned numNodes = m_cfg.numNodes();

        Vector<SparseBitVector<>> tailBits(numNodes);
        Vector<SparseBitVector<>> headBits(numNodes);

        BitVector dirtyBlocks(numNodes);
        Vector<unsigned, 64> worklist;
        for (unsigned blockIndex = 0; blockIndex < numNodes; ++blockIndex) {
            auto block = m_cfg.node(blockIndex);
            if (!block)
                continue;

            // The liveAtTail of each block automatically contains the LateUse's of the terminal.
            auto& liveAtTail = tailBits[blockIndex];
            Adapter::forEachUse(
                block, Adapter::blockSize(block),
                [&] (unsigned index) {
                    liveAtTail.set(index);
                });

            dirtyBlocks.quickSet(blockIndex);
            worklist.append(blockIndex);
        }

        Vector<unsigned, 64> delta;
        while (!worklist.isEmpty()) {
            unsigned blockIndex = worklist.takeLast();
            bool cleared = dirtyBlocks.quickClear(blockIndex);
            ASSERT_UNUSED(cleared, cleared);

            auto block = m_cfg.node(blockIndex);
            ASSERT(block);

            m_workset.clear();
            tailBits[blockIndex].forEachSetBit(
                [&] (size_t index) {
                    m_workset.add(index);
                });
            for (size_t instIndex = Adapter::blockSize(block); instIndex--;) {
                Adapter::forEachDef(block, instIndex + 1, [&] (unsigned index) { m_workset.remove(index); });
                Adapter::forEachUse(block, instIndex, [&] (unsigned index) { m_workset.add(index); });
            }
            Adapter::forEachDef(block, 0, [&] (unsigned index) { m_workset.remove(index); });

            auto& liveAtHead = headBits[blockIndex];
            delta.shrink(0);
            for (unsigned index : m_workset) {
                if (liveAtHead.set(index))
                    delta.append(index);
            }

            if (delta.isEmpty())
                continue;

            for (auto predecessor : m_cfg.predecessors(block)) {
                auto& predTail = tailBits[predecessor->index()];
                bool changed = false;
                for (unsigned index : delta) {
                    if (predTail.set(index))
                        changed = true;
                }
                if (changed && !dirtyBlocks.quickSet(predecessor->index()))
                    worklist.append(predecessor->index());
            }
        }

        for (unsigned blockIndex = 0; blockIndex < numNodes; ++blockIndex) {
            auto block = m_cfg.node(blockIndex);
            if (!block)
                continue;
            headBits[blockIndex].forEachSetBit([&] (size_t index) { m_liveAtHead[block].append(index); });
            tailBits[blockIndex].forEachSetBit([&] (size_t index) { m_liveAtTail[block].append(index); });
        }
    }

private:
    friend class LocalCalc;
    friend class LocalCalc::Iterable;

    CFG& m_cfg;
    Workset m_workset;
    typename CFG::template Map<IndexVector> m_liveAtHead;
    typename CFG::template Map<IndexVector> m_liveAtTail;
};

} // namespace WTF

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
