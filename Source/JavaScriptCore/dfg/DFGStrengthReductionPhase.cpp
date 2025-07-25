/*
 * Copyright (C) 2013-2024 Apple Inc. All rights reserved.
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
#include "DFGStrengthReductionPhase.h"

#if ENABLE(DFG_JIT)

#include "ButterflyInlines.h"
#include "DFGAbstractHeap.h"
#include "DFGClobberize.h"
#include "DFGGraph.h"
#include "DFGInsertionSet.h"
#include "DFGJITCode.h"
#include "DFGPhase.h"
#include "JSObjectInlines.h"
#include "JSWebAssemblyInstance.h"
#include "MathCommon.h"
#include "NumberPrototype.h"
#include "RegExpCache.h"
#include "RegExpObject.h"
#include "StringPrototypeInlines.h"
#include "WasmCallingConvention.h"
#include "WebAssemblyFunction.h"
#include <cstdlib>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>

namespace JSC { namespace DFG {

class StrengthReductionPhase : public Phase {
    static constexpr bool verbose = false;
    
public:
    StrengthReductionPhase(Graph& graph)
        : Phase(graph, "strength reduction"_s)
        , m_insertionSet(graph)
    {
    }
    
    bool run()
    {
        ASSERT(m_graph.m_fixpointState == FixpointNotConverged);
        
        m_changed = false;
        
        for (BlockIndex blockIndex = m_graph.numBlocks(); blockIndex--;) {
            m_block = m_graph.block(blockIndex);
            if (!m_block)
                continue;
            for (m_nodeIndex = 0; m_nodeIndex < m_block->size(); ++m_nodeIndex) {
                m_node = m_block->at(m_nodeIndex);
                handleNode();
            }
            m_insertionSet.execute(m_block);
        }
        
        return m_changed;
    }

private:
    static bool foldPurifyNaN(Edge& edge)
    {
        if (edge->op() == PurifyNaN) {
            edge = edge->child1();
            return true;
        }
        return false;
    }

    static bool foldPurifyNaNOnBinary(Node* node)
    {
        bool result = false;
        if (node->isBinaryUseKind(DoubleRepUse)) {
            result |= foldPurifyNaN(node->child1());
            result |= foldPurifyNaN(node->child2());
        }
        return result;
    }

    static bool foldPurifyNaNOnUnary(Node* node)
    {
        if (node->child1().useKind() == DoubleRepUse)
            return foldPurifyNaN(node->child1());
        return false;
    }

    void handleNode()
    {
        switch (m_node->op()) {
        case Branch:
        case PurifyNaN:
        case DoubleAsInt32:
        case ValueToInt32:
        case GlobalIsNaN:
        case NumberIsNaN:
        case GlobalIsFinite:
        case NumberIsFinite:
        case NumberIsSafeInteger:
        case ParseInt:
        case ToIntegerOrInfinity:
        case ToLength:
        case ArithFRound:
        case ArithF16Round:
        case ArithRound:
        case ArithFloor:
        case ArithCeil:
        case ArithTrunc:
        case ArithSqrt:
        case ArithAbs:
        case ArithNegate:
        case ArithUnary: {
            if (foldPurifyNaNOnUnary(m_node))
                m_changed = true;
            break;
        }

        case NumberToStringWithRadix: {
            if (m_node->child1().useKind() == DoubleRepUse) {
                if (foldPurifyNaN(m_node->child1()))
                    m_changed = true;
            }
            break;
        }

        case ArithBitOr:
            handleCommutativity();

            if (m_node->child1().useKind() != UntypedUse && m_node->child2()->isInt32Constant() && !m_node->child2()->asInt32()) {
                convertToIdentityOverChild1();
                break;
            }
            break;
            
        case ArithBitXor:
        case ArithBitAnd:
            handleCommutativity();
            break;
            
        case ArithBitLShift:
        case ArithBitRShift:
        case ArithBitURShift:
            if (m_node->child1().useKind() != UntypedUse && m_node->child2()->isInt32Constant() && !(m_node->child2()->asInt32() & 0x1f)) {
                convertToIdentityOverChild1();
                break;
            }
            break;
            
        case UInt32ToNumber:
            if (m_node->child1()->op() == ArithBitURShift
                && m_node->child1()->child2()->isInt32Constant()
                && (m_node->child1()->child2()->asInt32() & 0x1f)
                && m_node->arithMode() != Arith::DoOverflow) {
                m_node->convertToIdentity();
                m_changed = true;
                break;
            }
            if (bytecodeCanTruncateInteger(m_node->arithNodeFlags())) {
                m_node->convertToIdentity();
                m_changed = true;
                break;
            }
            break;
            
        case ArithAdd:
            handleCommutativity();
            if (foldPurifyNaNOnBinary(m_node))
                m_changed = true;
            
            if (m_node->child2()->isInt32Constant() && !m_node->child2()->asInt32()) {
                convertToIdentityOverChild1();
                break;
            }
            break;
            
        case ValueMul:
        case ValueBitOr:
        case ValueBitAnd:
        case ValueBitXor: {
            // FIXME: we should maybe support the case where one operand is always HeapBigInt and the other is always BigInt32?
            if (m_node->binaryUseKind() == AnyBigIntUse || m_node->binaryUseKind() == BigInt32Use || m_node->binaryUseKind() == HeapBigIntUse)
                handleCommutativity();
            break;
        }

        case ArithMul: {
            handleCommutativity();
            if (foldPurifyNaNOnBinary(m_node))
                m_changed = true;

            Edge& child2 = m_node->child2();
            if (child2->isNumberConstant() && child2->asNumber() == 2) {
                switch (m_node->binaryUseKind()) {
                case DoubleRepUse:
                    // It is always valuable to get rid of a double multiplication by 2.
                    // We won't have half-register dependencies issues on x86 and we won't have to load the constants.
                    m_node->setOp(ArithAdd);
                    child2.setNode(m_node->child1().node());
                    m_changed = true;
                    break;
#if USE(JSVALUE64)
                case Int52RepUse:
#endif
                case Int32Use:
                    // For integers, we can only convert compatible modes.
                    // ArithAdd does handle do negative zero check for example.
                    if (m_node->arithMode() == Arith::CheckOverflow || m_node->arithMode() == Arith::Unchecked) {
                        m_node->setOp(ArithAdd);
                        child2.setNode(m_node->child1().node());
                        m_changed = true;
                    }
                    break;
                default:
                    break;
                }
            }
            break;
        }
        case ArithSub: {
            if (foldPurifyNaNOnBinary(m_node))
                m_changed = true;

            if (m_node->child2()->isInt32Constant()
                && m_node->isBinaryUseKind(Int32Use)) {
                int32_t value = m_node->child2()->asInt32();
                if (value != INT32_MIN) {
                    m_node->setOp(ArithAdd);
                    m_node->child2().setNode(
                        m_insertionSet.insertConstant(
                            m_nodeIndex, m_node->origin, jsNumber(-value)));
                    m_changed = true;
                    break;
                }
            }
            break;
        }

        case ArithPow:
            if (m_node->child1().useKind() == DoubleRepUse) {
                if (foldPurifyNaN(m_node->child1()))
                    m_changed = true;
            }

            if (m_node->child2()->isNumberConstant()) {
                double yOperandValue = m_node->child2()->asNumber();
                if (yOperandValue == 1) {
                    convertToIdentityOverChild1();
                    m_changed = true;
                } else if (yOperandValue == 2) {
                    m_node->setOp(ArithMul);
                    m_node->child2() = m_node->child1();
                    m_changed = true;
                }
            }
            break;

        case ArithMod: {
            if (foldPurifyNaNOnBinary(m_node))
                m_changed = true;

            // On Integers
            // In: ArithMod(ArithMod(x, const1), const2)
            // Out: Identity(ArithMod(x, const1))
            //     if const1 <= const2.
            if (m_node->binaryUseKind() == Int32Use
                && m_node->child2()->isInt32Constant()
                && m_node->child1()->op() == ArithMod
                && m_node->child1()->binaryUseKind() == Int32Use
                && m_node->child1()->child2()->isInt32Constant()) {

                int32_t const1 = m_node->child1()->child2()->asInt32();
                int32_t const2 = m_node->child2()->asInt32();

                if (const1 == INT_MIN || const2 == INT_MIN)
                    break; // std::abs(INT_MIN) is undefined.

                if (std::abs(const1) <= std::abs(const2))
                    convertToIdentityOverChild1();
            }
            break;
        }

        case ArithDiv: {
            if (foldPurifyNaNOnBinary(m_node))
                m_changed = true;

            // Transform
            //    ArithDiv(x, constant)
            // Into
            //    ArithMul(x, 1 / constant)
            // if the operation has the same result.
            if (m_node->isBinaryUseKind(DoubleRepUse)
                && m_node->child2()->isNumberConstant()) {

                if (std::optional<double> reciprocal = safeReciprocalForDivByConst(m_node->child2()->asNumber())) {
                    Node* reciprocalNode = m_insertionSet.insertConstant(m_nodeIndex, m_node->origin, jsDoubleNumber(*reciprocal), DoubleConstant);
                    m_node->setOp(ArithMul);
                    m_node->child2() = Edge(reciprocalNode, DoubleRepUse);
                    m_changed = true;
                    break;
                }
            }
            break;
        }

        case ValueRep:
        case Int52Rep: {
            // This short-circuits circuitous conversions, like ValueRep(Int52Rep(value)).

            if (m_node->op() == ValueRep) {
                if (foldPurifyNaNOnUnary(m_node))
                    m_changed = true;
            }
            
            // The only speculation that we would do beyond validating that we have a type that
            // can be represented a certain way is an Int32 check that would appear on Int52Rep
            // nodes. For now, if we see this and the final type we want is an Int52, we use it
            // as an excuse not to fold. The only thing we would need is a Int52RepInt32Use kind.
            bool hadInt32Check = false;
            if (m_node->op() == Int52Rep) {
                if (m_node->child1().useKind() != Int32Use)
                    break;
                hadInt32Check = true;
            }
            for (Node* node = m_node->child1().node(); ; node = node->child1().node()) {
                if (canonicalResultRepresentation(node->result()) ==
                    canonicalResultRepresentation(m_node->result())) {
                    m_insertionSet.insertCheck(m_graph, m_nodeIndex, m_node);
                    if (hadInt32Check) {
                        // FIXME: Consider adding Int52RepInt32Use or even DoubleRepInt32Use,
                        // which would be super weird. The latter would only arise in some
                        // seriously circuitous conversions.
                        if (canonicalResultRepresentation(node->result()) != NodeResultJS)
                            break;
                        
                        m_insertionSet.insertCheck(
                            m_nodeIndex, m_node->origin, Edge(node, Int32Use));
                    }
                    m_node->child1() = node->defaultEdge();
                    m_node->convertToIdentity();
                    m_changed = true;
                    break;
                }
                
                switch (node->op()) {
                case Int52Rep:
                    if (node->child1().useKind() != Int32Use)
                        break;
                    hadInt32Check = true;
                    continue;
                    
                case ValueRep:
                    continue;
                    
                default:
                    break;
                }
                break;
            }
            break;
        }
            
        case Flush: {
            ASSERT(m_graph.m_form != SSA);

            if (m_graph.willCatchExceptionInMachineFrame(m_node->origin.semantic)) {
                // FIXME: We should be able to relax this:
                // https://bugs.webkit.org/show_bug.cgi?id=150824
                break;
            }
            
            Node* setLocal = nullptr;
            Operand operand = m_node->operand();
            
            for (unsigned i = m_nodeIndex; i--;) {
                Node* node = m_block->at(i);

                if (node->op() == SetLocal && node->operand() == operand) {
                    setLocal = node;
                    break;
                }

                if (accessesOverlap(m_graph, node, AbstractHeap(Stack, operand)))
                    break;

            }
            
            if (!setLocal)
                break;
            
            // The Flush should become a PhantomLocal at this point. This means that we want the
            // local's value during OSR, but we don't care if the value is stored to the stack. CPS
            // rethreading can canonicalize PhantomLocals for us.
            m_node->convertFlushToPhantomLocal();
            m_graph.dethread();
            m_changed = true;
            break;
        }

        // FIXME: we should probably do this in constant folding but this currently relies on OSR exit history:
        // https://bugs.webkit.org/show_bug.cgi?id=154832
        case OverridesHasInstance: {
            if (!m_node->child2().node()->isCellConstant())
                break;

            if (m_node->child2().node()->asCell() != m_graph.globalObjectFor(m_node->origin.semantic)->functionProtoHasInstanceSymbolFunction()) {
                m_graph.convertToConstant(m_node, jsBoolean(true));
                m_changed = true;

            } else if (!m_graph.hasExitSite(m_node->origin.semantic, BadTypeInfoFlags)) {
                // We optimistically assume that we will not see a function that has a custom instanceof operation as they should be rare.
                m_insertionSet.insertNode(m_nodeIndex, SpecNone, CheckTypeInfoFlags, m_node->origin, OpInfo(ImplementsDefaultHasInstance), Edge(m_node->child1().node(), CellUse));
                m_graph.convertToConstant(m_node, jsBoolean(false));
                m_changed = true;
            }

            break;
        }

        // FIXME: We have a lot of string constant-folding rules here. It would be great to
        // move these to the abstract interpreter once AbstractValue can support LazyJSValue.
        // https://bugs.webkit.org/show_bug.cgi?id=155204

        case ValueAdd: {
            if (m_node->child1()->isConstant()
                && m_node->child2()->isConstant()
                && (!!m_node->child1()->tryGetString(m_graph) || !!m_node->child2()->tryGetString(m_graph))) {
                auto tryGetConstantString = [&] (Node* node) -> String {
                    String string = node->tryGetString(m_graph);
                    if (!string.isEmpty())
                        return string;
                    JSValue value = node->constant()->value();
                    if (!value)
                        return String();
                    if (value.isInt32())
                        return String::number(value.asInt32());
                    if (value.isNumber())
                        return String::number(value.asNumber());
                    if (value.isBoolean())
                        return value.asBoolean() ? "true"_s : "false"_s;
                    if (value.isNull())
                        return "null"_s;
                    if (value.isUndefined())
                        return "undefined"_s;
                    return String();
                };

                String leftString = tryGetConstantString(m_node->child1().node());
                if (!leftString)
                    break;
                String rightString = tryGetConstantString(m_node->child2().node());
                if (!rightString)
                    break;

                StringBuilder builder;
                builder.append(leftString);
                builder.append(rightString);
                convertToLazyJSValue(m_node, LazyJSValue::newString(m_graph, builder.toString()));
                m_changed = true;
                break;
            }

            if (m_node->binaryUseKind() == BigInt32Use || m_node->binaryUseKind() == HeapBigIntUse || m_node->binaryUseKind() == AnyBigIntUse)
                handleCommutativity();

            break;
        }

        case MakeRope:
        case MakeAtomString:
        case StrCat: {
            // Constant folding.
            String string0 = m_node->child1()->tryGetString(m_graph);
            if (!string0) {
                if (m_node->child2() && m_node->child3()) {
                    String string1 = m_node->child2()->tryGetString(m_graph);
                    if (!string1)
                        break;
                    String string2 = m_node->child3()->tryGetString(m_graph);
                    if (!string2)
                        break;

                    StringBuilder builder;
                    builder.append(string1);
                    builder.append(string2);
                    if (!builder.hasOverflowed()) {
                        LazyJSValue value = LazyJSValue::newString(m_graph, builder.toString());
                        auto* constant = m_insertionSet.insertNode(m_nodeIndex, SpecNone, LazyJSConstant, m_node->origin, OpInfo(m_graph.m_lazyJSValues.add(value)));
                        m_node->child2().setNode(constant);
                        m_node->child3() = Edge();
                        m_changed = true;
                    }
                }
                break;
            }

            if (!m_node->child2()) {
                ASSERT(!m_node->child3());
                convertToLazyJSValue(m_node, LazyJSValue::newString(m_graph, string0));
                m_changed = true;
                break;
            }

            String string1 = m_node->child2()->tryGetString(m_graph);
            if (!string1)
                break;

            StringBuilder builder;
            builder.append(string0);
            builder.append(string1);
            if (!m_node->child3()) {
                if (!builder.hasOverflowed()) {
                    convertToLazyJSValue(m_node, LazyJSValue::newString(m_graph, builder.toString()));
                    m_changed = true;
                }
                break;
            }

            String string2 = m_node->child3()->tryGetString(m_graph);
            if (!string2) {
                if (!builder.hasOverflowed()) {
                    LazyJSValue value = LazyJSValue::newString(m_graph, builder.toString());
                    auto* constant = m_insertionSet.insertNode(m_nodeIndex, SpecNone, LazyJSConstant, m_node->origin, OpInfo(m_graph.m_lazyJSValues.add(value)));
                    m_node->child1().setNode(constant);
                    m_node->child2() = m_node->child3();
                    m_node->child3() = Edge();
                    m_changed = true;
                }
                break;
            }

            builder.append(string2);
            if (!builder.hasOverflowed()) {
                convertToLazyJSValue(m_node, LazyJSValue::newString(m_graph, builder.toString()));
                m_changed = true;
            }
            break;
        }

        case ToString:
        case CallStringConstructor: {
            Edge& child1 = m_node->child1();
            switch (child1.useKind()) {
            case Int32Use:
            case Int52RepUse:
            case DoubleRepUse: {
                if (child1->hasConstant()) {
                    JSValue value = child1->constant()->value();
                    if (value) {
                        String result;
                        if (value.isInt32())
                            result = String::number(value.asInt32());
                        else if (value.isNumber())
                            result = String::number(value.asNumber());

                        if (!result.isNull()) {
                            convertToLazyJSValue(m_node, LazyJSValue::newString(m_graph, result));
                            m_changed = true;
                        }
                    }
                }
                break;
            }
            case StringOrOtherUse: {
                if (child1->hasConstant()) {
                    if (JSValue value = child1->constant()->value()) {
                        if (value.isUndefinedOrNull()) {
                            m_graph.convertToConstant(m_node, value.isUndefined() ? vm().smallStrings.undefinedString() : vm().smallStrings.nullString());
                            m_changed = true;
                            break;
                        }
                        if (value.isString()) {
                            m_graph.convertToConstant(m_node, value);
                            m_changed = true;
                            break;
                        }
                    }
                }
                break;
            }

            case StringObjectUse:
            case StringOrStringObjectUse: {
                if (child1->op() == NewStringObject && child1->child1().useKind() == KnownStringUse) {
                    m_node->convertToIdentityOn(child1->child1().node());
                    m_changed = true;
                }
                break;
            }

            case KnownPrimitiveUse:
                break;

            default:
                break;
            }
            break;
        }

        case NumberToStringWithValidRadixConstant: {
            Edge& child1 = m_node->child1();
            if (child1->hasConstant()) {
                JSValue value = child1->constant()->value();
                if (value && value.isNumber()) {
                    String result = toStringWithRadix(value.asNumber(), m_node->validRadixConstant());
                    convertToLazyJSValue(m_node, LazyJSValue::newString(m_graph, result));
                    m_changed = true;
                }
            }
            break;
        }

        case GetArrayLength: {
            if (m_node->arrayMode().type() == Array::Generic
                || m_node->arrayMode().type() == Array::String) {
                String string = m_node->child1()->tryGetString(m_graph);
                if (!!string) {
                    m_graph.convertToConstant(m_node, jsNumber(string.length()));
                    m_changed = true;
                    break;
                }
            }
            break;
        }

        case GetGlobalObject: {
            if (JSObject* object = m_node->child1()->dynamicCastConstant<JSObject*>()) {
                m_graph.convertToConstant(m_node, object->globalObject());
                m_changed = true;
                break;
            }
            break;
        }

        case NewRegExpUntyped: {
            if (m_node->child1().useKind() != StringUse || m_node->child2().useKind() != StringUse)
                break;

            String pattern = m_node->child1()->tryGetString(m_graph);
            if (!pattern)
                break;

            String flagsString = m_node->child2()->tryGetString(m_graph);
            if (!flagsString)
                break;

            auto flags = Yarr::parseFlags(flagsString);
            if (!flags)
                break;

            auto* regExp = vm().regExpCache()->lookup(vm(), pattern, flags.value());
            if (!regExp)
                break;

            m_node->convertToNewRegExp(m_graph.freezeStrong(regExp), m_insertionSet.insertConstantForUse(m_nodeIndex, m_node->origin, jsNumber(0), UntypedUse));
            m_changed = true;
            break;
        }

        case RegExpSearch:
        case RegExpExec:
        case RegExpTest:
        case RegExpMatchFast:
        case RegExpExecNonGlobalOrSticky: {
            JSGlobalObject* globalObject = m_node->child1()->dynamicCastConstant<JSGlobalObject*>();
            if (!globalObject) {
                dataLogLnIf(verbose, "Giving up because no global object.");
                break;
            }

            if (globalObject->isHavingABadTime()) {
                dataLogLnIf(verbose, "Giving up because bad time.");
                break;
            }

            if (m_graph.m_plan.isUnlinked() && globalObject != m_graph.globalObjectFor(m_node->origin.semantic)) {
                dataLogLnIf(verbose, "Giving up because unlinked DFG requires globalObject is the same to the node's origin.");
                break;
            }

            Node* regExpObjectNode = nullptr;
            RegExp* regExp = nullptr;
            bool regExpObjectNodeIsConstant = false;
            if (m_node->op() == RegExpExec || m_node->op() == RegExpTest || m_node->op() == RegExpMatchFast || m_node->op() == RegExpSearch) {
                regExpObjectNode = m_node->child2().node();
                if (RegExpObject* regExpObject = regExpObjectNode->dynamicCastConstant<RegExpObject*>()) {
                    JSGlobalObject* globalObject = regExpObject->globalObject();
                    if (globalObject->isRegExpRecompiled()) {
                        dataLogLnIf(verbose, "Giving up because RegExp recompile happens.");
                        break;
                    }
                    m_graph.watchpoints().addLazily(globalObject->regExpRecompiledWatchpointSet());
                    regExp = regExpObject->regExp();
                    regExpObjectNodeIsConstant = true;
                } else if (regExpObjectNode->op() == NewRegExp) {
                    JSGlobalObject* globalObject = m_graph.globalObjectFor(regExpObjectNode->origin.semantic);
                    if (globalObject->isRegExpRecompiled()) {
                        dataLogLnIf(verbose, "Giving up because RegExp recompile happens.");
                        break;
                    }
                    m_graph.watchpoints().addLazily(globalObject->regExpRecompiledWatchpointSet());
                    regExp = regExpObjectNode->castOperand<RegExp*>();
                } else {
                    dataLogLnIf(verbose, "Giving up because the regexp is unknown.");
                    break;
                }
            } else
                regExp = m_node->castOperand<RegExp*>();

            if (m_node->op() == RegExpMatchFast) {
                if (regExp->global()) {
                    if (regExp->sticky())
                        break;
                    if (m_node->child3().useKind() != StringUse)
                        break;
                    NodeOrigin origin = m_node->origin;
                    m_insertionSet.insertNode(
                        m_nodeIndex, SpecNone, Check, origin, m_node->children.justChecks());
                    m_insertionSet.insertNode(
                        m_nodeIndex, SpecNone, SetRegExpObjectLastIndex, origin,
                        OpInfo(false),
                        Edge(regExpObjectNode, RegExpObjectUse),
                        m_insertionSet.insertConstantForUse(
                            m_nodeIndex, origin, jsNumber(0), UntypedUse));
                    origin = origin.withInvalidExit();
                    m_node->convertToRegExpMatchFastGlobalWithoutChecks(m_graph.freeze(regExp));
                    m_node->origin = origin;
                    m_changed = true;
                    break;
                }

                m_node->setOp(RegExpExec);
                m_changed = true;
                // Continue performing strength reduction onto RegExpExec node.
            }

            ASSERT(m_node->op() != RegExpMatchFast);

            bool needLastIndexTypeCheck = false;
            auto insertLastIndexTypeCheckIfNecessary = [&](NodeOrigin origin) {
                if (needLastIndexTypeCheck) {
                    ASSERT(m_node->op() != RegExpExecNonGlobalOrSticky);
                    Node* lastIndex = m_insertionSet.insertNode(m_nodeIndex, SpecNone, GetRegExpObjectLastIndex, origin, Edge(regExpObjectNode, RegExpObjectUse));
                    m_insertionSet.insertNode(m_nodeIndex, SpecNone, Check, origin, Edge(lastIndex, Int32Use));
                }
            };

            unsigned lastIndex = UINT_MAX;
            if (m_node->op() != RegExpExecNonGlobalOrSticky) {
                // This will only work if we can prove what the value of lastIndex is. To do this
                // safely, we need to execute the insertion set so that we see any previous strength
                // reductions. This is needed for soundness since otherwise the effectfulness of any
                // previous strength reductions would be invisible to us.
                ASSERT(regExpObjectNode);
                executeInsertionSet();
                for (unsigned otherNodeIndex = m_nodeIndex; otherNodeIndex--;) {
                    Node* otherNode = m_block->at(otherNodeIndex);
                    if (otherNode == regExpObjectNode) {
                        if (regExpObjectNodeIsConstant)
                            break;
                        lastIndex = 0;
                        break;
                    }
                    if (otherNode->op() == SetRegExpObjectLastIndex
                        && otherNode->child1() == regExpObjectNode
                        && otherNode->child2()->isInt32Constant()
                        && otherNode->child2()->asInt32() >= 0) {
                        lastIndex = otherNode->child2()->asUInt32();
                        break;
                    }
                    if (writesOverlap(m_graph, otherNode, RegExpObject_lastIndex))
                        break;
                }
                if (lastIndex == UINT_MAX) {
                    // We cannot statically prove lastIndex. But still there is a chance.
                    // If RegExp is not global and not sticky, then only thing we care is ToIntegerOrInfinity(regExp.lastIndex).
                    // Thus, we can emit Int32Use check to protect further when conversion happens.
                    if (regExp->globalOrSticky()) {
                        dataLogLnIf(verbose, "Giving up because the last index is not known.");
                        break;
                    }

                    if (m_graph.hasExitSite(m_node->origin.semantic, BadType)) {
                        dataLogLnIf(verbose, "Giving up because the last index type check may fail.");
                        break;
                    }

                    if (RegExpObject* object = regExpObjectNode->dynamicCastConstant<RegExpObject*>()) {
                        if (!object->getLastIndex().isInt32()) {
                            dataLogLnIf(verbose, "Giving up because the constant RegExpObject's lastIndex is not Int32 already.");
                            break;
                        }
                    }

                    needLastIndexTypeCheck = true;
                }
            }

            if (!regExp->globalOrSticky())
                lastIndex = 0;

            auto foldToConstant = [&] {
                Node* stringNode = nullptr;
                if (m_node->op() == RegExpExecNonGlobalOrSticky)
                    stringNode = m_node->child2().node();
                else
                    stringNode = m_node->child3().node();

                // NOTE: This mostly already protects us from having the compiler execute a regexp
                // operation on a ginormous string by preventing us from getting our hands on ginormous
                // strings in the first place.
                String string = stringNode->tryGetString(m_graph);
                if (!string) {
                    dataLogLnIf(verbose, "Giving up because the string is unknown.");
                    return false;
                }

                FrozenValue* regExpFrozenValue = m_graph.freeze(regExp);

                // Refuse to do things with regular expressions that have a ginormous number of
                // subpatterns.
                unsigned ginormousNumberOfSubPatterns = 1000;
                if (regExp->numSubpatterns() > ginormousNumberOfSubPatterns) {
                    dataLogLnIf(verbose, "Giving up because of pattern limit.");
                    return false;
                }

                if ((m_node->op() == RegExpExec || m_node->op() == RegExpExecNonGlobalOrSticky)) {
                    if (regExp->hasNamedCaptures()) {
                        // FIXME: https://bugs.webkit.org/show_bug.cgi?id=176464
                        // Implement strength reduction optimization for named capture groups.
                        dataLogLnIf(verbose, "Giving up because of named capture groups.");
                        return false;
                    }

                    if (regExp->hasIndices()) {
                        // FIXME: https://bugs.webkit.org/show_bug.cgi?id=220930
                        // Implement strength reduction optimization for RegExp with match indices.
                        dataLogLnIf(verbose, "Giving up because of match indices.");
                        return false;
                    }
                }

                m_graph.watchpoints().addLazily(globalObject->havingABadTimeWatchpointSet());

                Structure* structure = globalObject->regExpMatchesArrayStructure();
                if (structure->indexingType() != ArrayWithContiguous) {
                    // This is further protection against a race with haveABadTime.
                    dataLogLnIf(verbose, "Giving up because the structure has the wrong indexing type.");
                    return false;
                }
                m_graph.registerStructure(structure);

                FrozenValue* globalObjectFrozenValue = m_graph.freeze(globalObject);

                MatchResult result;
                Vector<int> ovector;
                // We have to call the kind of match function that the main thread would have called.
                // Otherwise, we might not have the desired Yarr code compiled, and the match will fail.
                if (m_node->op() == RegExpExec || m_node->op() == RegExpExecNonGlobalOrSticky) {
                    int position;
                    if (!regExp->matchConcurrently(vm(), string, lastIndex, position, ovector)) {
                        dataLogLnIf(verbose, "Giving up because match failed.");
                        return false;
                    }
                    result.start = position;
                    result.end = ovector[1];
                } else {
                    if (!regExp->matchConcurrently(vm(), string, lastIndex, result)) {
                        dataLogLnIf(verbose, "Giving up because match failed.");
                        return false;
                    }
                }

                // We've constant-folded the regexp. Now we're committed to replacing RegExpExec/Test.

                m_changed = true;

                NodeOrigin origin = m_node->origin;

                m_insertionSet.insertNode(m_nodeIndex, SpecNone, Check, origin, m_node->children.justChecks());
                insertLastIndexTypeCheckIfNecessary(origin);

                if (m_node->op() == RegExpExec || m_node->op() == RegExpExecNonGlobalOrSticky) {
                    if (result) {
                        RegisteredStructureSet* structureSet = m_graph.addStructureSet(structure);

                        // Create an array modeling the JS array that we will try to allocate. This is
                        // basically createRegExpMatchesArray but over C++ strings instead of JSStrings.
                        Vector<String> resultArray;
                        resultArray.append(string.substring(result.start, result.end - result.start));
                        for (unsigned i = 1; i <= regExp->numSubpatterns(); ++i) {
                            int start = ovector[2 * i];
                            if (start >= 0)
                                resultArray.append(string.substring(start, ovector[2 * i + 1] - start));
                            else
                                resultArray.append(String());
                        }

                        unsigned publicLength = resultArray.size();
                        unsigned vectorLength =
                            Butterfly::optimalContiguousVectorLength(structure, publicLength);

                        UniquedStringImpl* indexUID = vm().propertyNames->index.impl();
                        UniquedStringImpl* inputUID = vm().propertyNames->input.impl();
                        UniquedStringImpl* groupsUID = vm().propertyNames->groups.impl();
                        unsigned indexIndex = m_graph.identifiers().ensure(indexUID);
                        unsigned inputIndex = m_graph.identifiers().ensure(inputUID);
                        unsigned groupsIndex = m_graph.identifiers().ensure(groupsUID);

                        unsigned firstChild = m_graph.m_varArgChildren.size();
                        m_graph.m_varArgChildren.append(
                            m_insertionSet.insertConstantForUse(
                                m_nodeIndex, origin, structure, KnownCellUse));
                        ObjectMaterializationData* data = m_graph.m_objectMaterializationData.add();

                        m_graph.m_varArgChildren.append(
                            m_insertionSet.insertConstantForUse(
                                m_nodeIndex, origin, jsNumber(publicLength), KnownInt32Use));
                        data->m_properties.append(PublicLengthPLoc);

                        m_graph.m_varArgChildren.append(
                            m_insertionSet.insertConstantForUse(
                                m_nodeIndex, origin, jsNumber(vectorLength), KnownInt32Use));
                        data->m_properties.append(VectorLengthPLoc);

                        m_graph.m_varArgChildren.append(
                            m_insertionSet.insertConstantForUse(
                                m_nodeIndex, origin, jsNumber(result.start), UntypedUse));
                        data->m_properties.append(
                            PromotedLocationDescriptor(NamedPropertyPLoc, indexIndex));

                        m_graph.m_varArgChildren.append(Edge(stringNode, UntypedUse));
                        data->m_properties.append(
                            PromotedLocationDescriptor(NamedPropertyPLoc, inputIndex));

                        // FIXME: https://bugs.webkit.org/show_bug.cgi?id=176464
                        // Implement strength reduction optimization for named capture groups.
                        m_graph.m_varArgChildren.append(
                            m_insertionSet.insertConstantForUse(
                                m_nodeIndex, origin, jsUndefined(), UntypedUse));
                        data->m_properties.append(
                            PromotedLocationDescriptor(NamedPropertyPLoc, groupsIndex));

                        auto materializeString = [&] (const String& string) -> Node* {
                            if (string.isNull()) {
                                return m_insertionSet.insertConstant(
                                    m_nodeIndex, origin, jsUndefined());
                            }
                            if (string.isEmpty()) {
                                return m_insertionSet.insertConstant(
                                    m_nodeIndex, origin, vm().smallStrings.emptyString());
                            }
                            LazyJSValue value = LazyJSValue::newString(m_graph, string);
                            return m_insertionSet.insertNode(
                                m_nodeIndex, SpecNone, LazyJSConstant, origin,
                                OpInfo(m_graph.m_lazyJSValues.add(value)));
                        };

                        for (unsigned i = 0; i < resultArray.size(); ++i) {
                            Node* node = materializeString(resultArray[i]);
                            m_graph.m_varArgChildren.append(Edge(node, UntypedUse));
                            data->m_properties.append(
                                PromotedLocationDescriptor(IndexedPropertyPLoc, i));
                        }

                        Node* resultNode = m_insertionSet.insertNode(
                            m_nodeIndex, SpecArray, Node::VarArg, MaterializeNewObject, origin,
                            OpInfo(structureSet), OpInfo(data), firstChild,
                            m_graph.m_varArgChildren.size() - firstChild);

                        m_node->convertToIdentityOn(resultNode);
                    } else
                        m_graph.convertToConstant(m_node, jsNull());
                } else if (m_node->op() == RegExpTest)
                    m_graph.convertToConstant(m_node, jsBoolean(!!result));
                else {
                    ASSERT(m_node->op() == RegExpSearch);
                    int32_t searchResult = result ? result.start : -1;
                    m_graph.convertToConstant(m_node, jsNumber(searchResult));
                }

                // Whether it's Exec or Test, we need to tell the globalObject and RegExpObject what's up.
                // Because SetRegExpObjectLastIndex may exit and it clobbers exit state, we do that
                // first.

                if (regExp->globalOrSticky()) {
                    ASSERT(regExpObjectNode);
                    m_insertionSet.insertNode(
                        m_nodeIndex, SpecNone, SetRegExpObjectLastIndex, origin,
                        OpInfo(false),
                        Edge(regExpObjectNode, RegExpObjectUse),
                        m_insertionSet.insertConstantForUse(
                            m_nodeIndex, origin, jsNumber(result ? result.end : 0), UntypedUse));

                    origin = origin.withInvalidExit();
                }

                if (result) {
                    unsigned firstChild = m_graph.m_varArgChildren.size();
                    m_graph.m_varArgChildren.append(
                        m_insertionSet.insertConstantForUse(
                            m_nodeIndex, origin, globalObjectFrozenValue, KnownCellUse));
                    m_graph.m_varArgChildren.append(
                        m_insertionSet.insertConstantForUse(
                            m_nodeIndex, origin, regExpFrozenValue, KnownCellUse));
                    m_graph.m_varArgChildren.append(Edge(stringNode, KnownCellUse));
                    m_graph.m_varArgChildren.append(
                        m_insertionSet.insertConstantForUse(
                            m_nodeIndex, origin, jsNumber(result.start), KnownInt32Use));
                    m_graph.m_varArgChildren.append(
                        m_insertionSet.insertConstantForUse(
                            m_nodeIndex, origin, jsNumber(result.end), KnownInt32Use));
                    m_insertionSet.insertNode(
                        m_nodeIndex, SpecNone, Node::VarArg, RecordRegExpCachedResult, origin,
                        OpInfo(), OpInfo(), firstChild, m_graph.m_varArgChildren.size() - firstChild);

                    origin = origin.withInvalidExit();
                }

                m_node->origin = origin;
                return true;
            };

#if ENABLE(YARR_JIT_REGEXP_TEST_INLINE)
            auto convertTestToTestInline = [&] {
                if (m_node->op() != RegExpTest)
                    return false;

                if (regExp->globalOrSticky())
                    return false;

                if (regExp->eitherUnicode())
                    return false;

                auto jitCodeBlock = regExp->getRegExpJITCodeBlock();
                if (!jitCodeBlock)
                    return false;

                auto inlineCodeStats8Bit = jitCodeBlock->get8BitInlineStats();

                if (!inlineCodeStats8Bit.canInline())
                    return false;

                unsigned codeSize = inlineCodeStats8Bit.codeSize();

                if (codeSize > Options::maximumRegExpTestInlineCodesize())
                    return false;

                unsigned alignedFrameSize = WTF::roundUpToMultipleOf<stackAlignmentBytes()>(inlineCodeStats8Bit.stackSize());

                if (alignedFrameSize)
                    m_graph.m_parameterSlots = std::max(m_graph.m_parameterSlots, argumentCountForStackSize(alignedFrameSize));

                NodeOrigin origin = m_node->origin;
                m_insertionSet.insertNode(m_nodeIndex, SpecNone, Check, origin, m_node->children.justChecks());
                insertLastIndexTypeCheckIfNecessary(origin);
                m_node->convertToRegExpTestInline(m_graph.freeze(globalObject), m_graph.freeze(regExp));
                m_changed = true;
                return true;
            };
#endif

            auto convertToStatic = [&] {
                if (m_node->op() != RegExpExec)
                    return false;
                if (regExp->globalOrSticky())
                    return false;
                if (m_node->child3().useKind() != StringUse)
                    return false;

                NodeOrigin origin = m_node->origin;
                m_insertionSet.insertNode(m_nodeIndex, SpecNone, Check, origin, m_node->children.justChecks());
                insertLastIndexTypeCheckIfNecessary(origin);
                m_node->convertToRegExpExecNonGlobalOrStickyWithoutChecks(m_graph.freeze(regExp));
                m_changed = true;
                return true;
            };

            if (foldToConstant())
                break;

#if ENABLE(YARR_JIT_REGEXP_TEST_INLINE)
            if (convertTestToTestInline())
                break;
#endif

            if (convertToStatic())
                break;

            break;
        }

        case StringReplace:
        case StringReplaceAll:
        case StringReplaceRegExp: {
            Node* stringNode = m_node->child1().node();
            String string = stringNode->tryGetString(m_graph);
            if (!string)
                break;

            String replace = m_node->child3()->tryGetString(m_graph);
            if (!replace)
                break;

            Node* regExpObjectNode = m_node->child2().node();
            RegExp* regExp;
            if (RegExpObject* regExpObject = regExpObjectNode->dynamicCastConstant<RegExpObject*>()) {
                JSGlobalObject* globalObject = regExpObject->globalObject();
                if (m_graph.m_plan.isUnlinked() && globalObject != m_graph.globalObjectFor(m_node->origin.semantic)) {
                    dataLogLnIf(verbose, "Giving up because unlinked DFG requires globalObject is the same to the node's origin.");
                    break;
                }
                if (globalObject->isRegExpRecompiled()) {
                    dataLogLnIf(verbose, "Giving up because RegExp recompile happens.");
                    break;
                }
                m_graph.watchpoints().addLazily(globalObject->regExpRecompiledWatchpointSet());
                regExp = regExpObject->regExp();
            } else if (regExpObjectNode->op() == NewRegExp) {
                JSGlobalObject* globalObject = m_graph.globalObjectFor(regExpObjectNode->origin.semantic);
                if (m_graph.m_plan.isUnlinked() && globalObject != m_graph.globalObjectFor(m_node->origin.semantic)) {
                    dataLogLnIf(verbose, "Giving up because unlinked DFG requires globalObject is the same to the node's origin.");
                    break;
                }
                if (globalObject->isRegExpRecompiled()) {
                    dataLogLnIf(verbose, "Giving up because RegExp recompile happens.");
                    break;
                }
                m_graph.watchpoints().addLazily(globalObject->regExpRecompiledWatchpointSet());
                regExp = regExpObjectNode->castOperand<RegExp*>();
            } else {
                dataLogLnIf(verbose, "Giving up because the regexp is unknown.");
                break;
            }

            StringBuilder builder;

            unsigned lastIndex = 0;
            unsigned startPosition = 0;
            bool ok = true;
            do {
                MatchResult result;
                Vector<int> ovector;
                // Model which version of match() is called by the main thread.
                if (replace.isEmpty() && regExp->global()) {
                    if (!regExp->matchConcurrently(vm(), string, startPosition, result)) {
                        ok = false;
                        break;
                    }
                } else {
                    int position;
                    if (!regExp->matchConcurrently(vm(), string, startPosition, position, ovector)) {
                        ok = false;
                        break;
                    }
                    
                    result.start = position;
                    result.end = ovector[1];
                }
                
                if (!result)
                    break;

                unsigned replLen = replace.length();
                if (lastIndex < result.start || replLen) {
                    builder.appendSubstring(string, lastIndex, result.start - lastIndex);
                    if (replLen) {
                        StringBuilder replacement;
                        substituteBackreferences(replacement, replace, string, ovector.span().data(), regExp);
                        builder.append(replacement);
                    }
                }

                lastIndex = result.end;
                startPosition = lastIndex;

                // special case of empty match
                if (result.empty()) {
                    startPosition++;
                    if (startPosition > string.length())
                        break;
                }
            } while (regExp->global());
            if (!ok)
                break;

            // We are committed at this point.
            m_changed = true;

            NodeOrigin origin = m_node->origin;

            m_insertionSet.insertNode(
                m_nodeIndex, SpecNone, Check, origin, m_node->children.justChecks());

            if (regExp->global()) {
                m_insertionSet.insertNode(
                    m_nodeIndex, SpecNone, SetRegExpObjectLastIndex, origin,
                    OpInfo(false),
                    Edge(regExpObjectNode, RegExpObjectUse),
                    m_insertionSet.insertConstantForUse(
                        m_nodeIndex, origin, jsNumber(0), UntypedUse));
                
                origin = origin.withInvalidExit();
            }

            if (!lastIndex && builder.isEmpty())
                m_node->convertToIdentityOn(stringNode);
            else {
                builder.appendSubstring(string, lastIndex);
                m_node->convertToLazyJSConstant(m_graph, LazyJSValue::newString(m_graph, builder.toString()));
            }

            m_node->origin = origin;
            break;
        }

        case StringReplaceString: {
            Node* stringNode = m_node->child1().node();
            String string = stringNode->tryGetString(m_graph);
            if (!string)
                break;

            String searchString = m_node->child2()->tryGetString(m_graph);
            if (!searchString)
                break;

            String replace = m_node->child3()->tryGetString(m_graph);
            if (!replace)
                break;

            size_t matchStart = string.find(searchString);
            if (matchStart == notFound) {
                m_changed = true;
                m_insertionSet.insertNode(m_nodeIndex, SpecNone, Check, m_node->origin, m_node->children.justChecks());
                m_node->convertToIdentityOn(stringNode);
                break;
            }

            size_t searchStringLength = searchString.length();
            size_t matchEnd = matchStart + searchStringLength;
            String result = tryMakeReplacedString<StringReplaceSubstitutions::Yes>(string, replace, matchStart, matchEnd);
            if (!result) [[unlikely]]
                break;

            m_changed = true;
            m_insertionSet.insertNode(m_nodeIndex, SpecNone, Check, m_node->origin, m_node->children.justChecks());
            m_node->convertToLazyJSConstant(m_graph, LazyJSValue::newString(m_graph, WTFMove(result)));
            break;
        }

        case StringSubstring:
        case StringSlice: {
            Node* stringNode = m_node->child1().node();

            if (!m_node->child2()->isInt32Constant())
                break;

            int32_t startValue = m_node->child2()->asInt32();
            std::optional<int32_t> endValue = std::nullopt;
            if (m_node->child3()) {
                if (!m_node->child3()->isInt32Constant())
                    break;
                endValue = m_node->child3()->asInt32();
                if (endValue.value() == startValue) {
                    // Regardless of whatever the string is, it generates empty string (Even if both are negative index).
                    m_changed = true;
                    m_insertionSet.insertNode(m_nodeIndex, SpecNone, Check, m_node->origin, m_node->children.justChecks());
                    m_node->convertToLazyJSConstant(m_graph, LazyJSValue::newString(m_graph, emptyString()));
                    break;
                }
            }

            String string = stringNode->tryGetString(m_graph);
            if (!string)
                break;

            int32_t length = string.length();
            int32_t start = 0;
            int32_t end = 0;
            if (m_node->op() == StringSubstring)
                std::tie(start, end) = extractSubstringOffsets(length, startValue, endValue);
            else
                std::tie(start, end) = extractSliceOffsets(length, startValue, endValue);

            m_changed = true;
            m_insertionSet.insertNode(m_nodeIndex, SpecNone, Check, m_node->origin, m_node->children.justChecks());
            if (!start && end == length) {
                m_node->convertToIdentityOn(stringNode);
                break;
            }
            m_node->convertToLazyJSConstant(m_graph, LazyJSValue::newString(m_graph, string.substring(start, end - start)));
            break;
        }

        case GetByVal:
        case GetByValMegamorphic: {
            Edge& baseEdge = m_graph.child(m_node, 0);
            Edge& keyEdge = m_graph.child(m_node, 1);
            if (baseEdge.useKind() == ObjectUse && m_node->arrayMode().type() == Array::Generic) {
                if (keyEdge->op() == MakeRope) {
                    keyEdge->setOp(MakeAtomString);
                    m_changed = true;
                }
            }
            break;
        }

        case PutByVal:
        case PutByValDirect:
        case PutByValAlias:
        case PutByValMegamorphic: {
            Edge& baseEdge = m_graph.child(m_node, 0);
            Edge& keyEdge = m_graph.child(m_node, 1);
            switch (m_node->arrayMode().modeForPut().type()) {
            case Array::Generic: {
                if (baseEdge.useKind() == CellUse || baseEdge.useKind() == KnownCellUse) {
                    if (keyEdge->op() == MakeRope) {
                        keyEdge->setOp(MakeAtomString);
                        m_changed = true;
                    }
                }
                break;
            }
            case Array::Float16Array:
            case Array::Float32Array:
            case Array::Float64Array: {
                if (m_node->op() == PutByVal || m_node->op() == PutByValDirect || m_node->op() == PutByValAlias) {
                    Edge& valueEdge = m_graph.child(m_node, 2);
                    if (valueEdge.useKind() == DoubleRepUse) {
                        if (foldPurifyNaN(valueEdge))
                            m_changed = true;
                    }
                }
                break;
            }

            case Array::Uint8Array:
            case Array::Uint16Array:
            case Array::Uint32Array: {
                if (m_node->op() == PutByVal || m_node->op() == PutByValDirect || m_node->op() == PutByValAlias) {
                    Edge& valueEdge = m_graph.child(m_node, 2);
                    if (valueEdge.useKind() == Int32Use) {
                        if (valueEdge->op() == UInt32ToNumber && valueEdge->child1().useKind() == Int32Use) {
                            valueEdge = valueEdge->child1();
                            m_changed = true;
                        }
                    }
                }
                break;
            }

            default:
                break;
            }
            break;
        }

        case InByVal:
        case InByValMegamorphic: {
            Edge& baseEdge = m_graph.child(m_node, 0);
            Edge& keyEdge = m_graph.child(m_node, 1);
            if (baseEdge.useKind() == CellUse) {
                if (keyEdge->op() == MakeRope) {
                    keyEdge->setOp(MakeAtomString);
                    m_changed = true;
                }
            }
            break;
        }

        case HasOwnProperty: {
            Edge& keyEdge = m_graph.child(m_node, 1);
            if (keyEdge->op() == MakeRope) {
                keyEdge->setOp(MakeAtomString);
                m_changed = true;
            }
            break;
        }

        case CompareStrictEq:
        case SameValue:
        case CompareEq:
        case CompareLess:
        case CompareLessEq:
        case CompareGreater:
        case CompareGreaterEq: {
            if (m_node->child1().useKind() == UntypedUse && m_node->child2().useKind() == UntypedUse) {
                if (m_node->op() == CompareEq || m_node->op() == CompareStrictEq || m_node->op() == SameValue) {
                    if (Node::shouldSpeculateBoolean(m_node->child1().node(), m_node->child2().node())) {
                        m_node->child1().setUseKind(BooleanUse);
                        m_node->child2().setUseKind(BooleanUse);
                        if (m_node->op() == SameValue)
                            m_node->setOpAndDefaultFlags(CompareStrictEq);
                        m_node->clearFlags(NodeMustGenerate);
                        m_changed = true;
                        break;
                    }
                }

                if (Node::shouldSpeculateInt32(m_node->child1().node(), m_node->child2().node())) {
                    m_node->child1().setUseKind(Int32Use);
                    m_node->child2().setUseKind(Int32Use);
                    if (m_node->op() == SameValue)
                        m_node->setOpAndDefaultFlags(CompareStrictEq);
                    m_node->clearFlags(NodeMustGenerate);
                    m_changed = true;
                    break;
                }
            }

            if (foldPurifyNaNOnBinary(m_node))
                m_changed = true;
            break;
        }

        case CheckInBounds: {
            auto isInt32OrKnownInt32Use = [](UseKind useKind) {
                return useKind == Int32Use || useKind == KnownInt32Use;
            };

            if (!isInt32OrKnownInt32Use(m_node->child1().useKind()) || !isInt32OrKnownInt32Use(m_node->child2().useKind()))
                break;

            if (m_node->child2()->isInt32Constant()) {
                int32_t length = m_node->child2()->asInt32();
                if (length < 0)
                    break;

                switch (m_node->child1()->op()) {
                case ArithBitRShift: {
                    if (!m_node->child1()->isBinaryUseKind(Int32Use))
                        break;
                    if (m_node->child1()->child2()->isInt32Constant()) {
                        int32_t shiftAmount = m_node->child1()->child2()->asInt32();
                        if (shiftAmount < 0 || shiftAmount > 31)
                            break;
                        auto result = static_cast<int64_t>(length) << shiftAmount;
                        if (result > INT32_MAX)
                            break;
                        m_node->child1() = Edge(m_node->child1()->child1().node(), Int32Use);
                        m_node->child2() = Edge(m_insertionSet.insertConstant(m_nodeIndex, m_node->origin, jsNumber(static_cast<int32_t>(result))), KnownInt32Use);
                        m_changed = true;
                        break;
                    }
                    break;
                }
                default:
                    break;
                }
            }
            break;
        }

        case Call:
        case Construct:
        case TailCallInlinedCaller:
        case TailCall: {
            ExecutableBase* executable = nullptr;
            Edge callee = m_graph.varArgChild(m_node, 0);
            CallVariant callVariant;
            JSFunction* function = callee->dynamicCastConstant<JSFunction*>();
            if (function) {
                executable = function->executable();
                callVariant = CallVariant(function);
            } else if (callee->isFunctionAllocation()) {
                executable = callee->castOperand<FunctionExecutable*>();
                callVariant = CallVariant(executable);
            }
            
            if (!executable)
                break;

            if (m_graph.m_plan.isUnlinked())
                break;

            if (m_graph.m_plan.isFTL()) {
                if (Options::useDataICInFTL())
                    break;
            }

#if ENABLE(WEBASSEMBLY)
            // FIXME: Support wasm IC.
            // DirectCall to wasm function has suboptimal implementation. We avoid using DirectCall if we know that function is a wasm function.
            // https://bugs.webkit.org/show_bug.cgi?id=220339
            if (executable->intrinsic() == WasmFunctionIntrinsic && !Options::forceICFailure()) {
                if (m_node->op() != Call) // FIXME: We should support tail-call.
                    break;
                if (!function)
                    break;
                auto* wasmFunction = jsDynamicCast<WebAssemblyFunction*>(function);
                if (!wasmFunction)
                    break;
                const auto& signature = Wasm::TypeInformation::getFunctionSignature(wasmFunction->typeIndex());
                if (signature.argumentsOrResultsIncludeV128() || signature.argumentsOrResultsIncludeExnref())
                    break;

                unsigned numPassedArgs = m_node->numChildren() - /* |callee| and |this| */ 2;
                if (signature.argumentCount() > numPassedArgs)
                    break;

                if (!signature.returnsVoid() && signature.returnCount() != 1)
                    break;

                bool success = true;
                for (unsigned index = 0; index < signature.argumentCount(); ++index) {
                    auto type = signature.argumentType(index);
                    Edge argument = m_graph.varArgChild(m_node, 2 + index);
                    switch (type.kind) {
                    case Wasm::TypeKind::I32: {
                        if (!argument->shouldSpeculateInt32())
                            success = false;
                        break;
                    }
                    case Wasm::TypeKind::I64: {
                        if (!argument->shouldSpeculateHeapBigInt())
                            success = false;
                        break;
                    }
                    case Wasm::TypeKind::Ref:
                    case Wasm::TypeKind::RefNull:
                    case Wasm::TypeKind::Funcref:
                    case Wasm::TypeKind::Externref: {
                        if (!Wasm::isExternref(type) || !type.isNullable())
                            success = false;
                        break;
                    }
                    case Wasm::TypeKind::F32:
                    case Wasm::TypeKind::F64: {
                        if (!argument->shouldSpeculateNumber())
                            success = false;
                        break;
                    }
                    case Wasm::TypeKind::V128:
                    default: {
                        success = false;
                        break;
                    }
                    }
                }

                if (!signature.returnsVoid()) {
                    ASSERT(signature.returnCount() == 1);
                    auto type = signature.returnType(0);
                    switch (type.kind) {
                    case Wasm::TypeKind::I32:
                    case Wasm::TypeKind::I64:
                    case Wasm::TypeKind::Ref:
                    case Wasm::TypeKind::RefNull:
                    case Wasm::TypeKind::Funcref:
                    case Wasm::TypeKind::Externref:
                    case Wasm::TypeKind::F32:
                    case Wasm::TypeKind::F64: {
                        break;
                    }
                    case Wasm::TypeKind::V128:
                    default: {
                        success = false;
                        break;
                    }
                    }
                }

                auto indexForChecks = [&]() -> std::optional<unsigned> {
                    unsigned index = m_nodeIndex;
                    while (!m_block->at(index)->origin.exitOK) {
                        if (index)
                            return std::nullopt;
                        index--;
                    }
                    return index;
                };

                auto checkIndexValue = indexForChecks();
                if (!checkIndexValue)
                    break;

                if (!success || !is64Bit() || !m_graph.m_plan.isFTL())
                    break;

                unsigned numAllocatedArgs = static_cast<unsigned>(signature.argumentCount()) + /* |this| for wasm */ 1;
                m_graph.m_parameterSlots = std::max(m_graph.m_parameterSlots, Graph::parameterSlotsForArgCount(numAllocatedArgs));

                unsigned checkIndex = checkIndexValue.value();
                for (unsigned index = 0; index < signature.argumentCount(); ++index) {
                    auto type = signature.argumentType(index);
                    Edge argument = m_graph.varArgChild(m_node, 2 + index);
                    Node* argumentNode = argument.node();
                    switch (type.kind) {
                    case Wasm::TypeKind::I32: {
                        m_insertionSet.insertCheck(checkIndex, m_node->origin, Edge(argumentNode, Int32Use));
                        m_graph.varArgChild(m_node, 2 + index) = Edge(argumentNode, KnownInt32Use);
                        break;
                    }
                    case Wasm::TypeKind::I64: {
                        m_insertionSet.insertCheck(checkIndex, m_node->origin, Edge(argumentNode, HeapBigIntUse));
                        m_graph.varArgChild(m_node, 2 + index) = Edge(argumentNode, KnownCellUse);
                        break;
                    }
                    case Wasm::TypeKind::Ref:
                    case Wasm::TypeKind::RefNull:
                    case Wasm::TypeKind::Funcref:
                    case Wasm::TypeKind::Externref: {
                        break;
                    }
                    case Wasm::TypeKind::F32:
                    case Wasm::TypeKind::F64: {
                        UseKind useKind;
                        if (argument->shouldSpeculateDoubleReal())
                            useKind = RealNumberUse;
                        else if (argument->shouldSpeculateNumber())
                            useKind = NumberUse;
                        else
                            useKind = NotCellNorBigIntUse;
                        Node* result = m_insertionSet.insertNode(checkIndex, SpecBytecodeDouble, DoubleRep, m_node->origin, Edge(argumentNode, useKind));
                        m_graph.varArgChild(m_node, 2 + index) = Edge(result, DoubleRepUse);
                        break;
                    }
                    case Wasm::TypeKind::V128:
                    default:
                        RELEASE_ASSERT_NOT_REACHED();
                    }
                }

                if (!signature.returnsVoid()) {
                    auto type = signature.returnType(0);
                    switch (type.kind) {
                    case Wasm::TypeKind::I32: {
                        m_node->setResult(NodeResultInt32);
                        break;
                    }
                    case Wasm::TypeKind::I64: {
                        break;
                    }
                    case Wasm::TypeKind::Ref:
                    case Wasm::TypeKind::RefNull:
                    case Wasm::TypeKind::Funcref:
                    case Wasm::TypeKind::Externref: {
                        break;
                    }
                    case Wasm::TypeKind::F32:
                    case Wasm::TypeKind::F64: {
                        break;
                    }
                    case Wasm::TypeKind::V128:
                    default: {
                        break;
                    }
                    }
                }

                m_node->convertToCallWasm(m_graph.freeze(wasmFunction));
                break;
            }
#endif

            // We gave up inlining a wrapped function, but still, we can inline bound function's wrapper by extracting it.
            // This also wipes bound-function thunk call which is suboptimal compared to directly calling a wrapped function here.
            if (executable->intrinsic() == BoundFunctionCallIntrinsic && function && (m_node->op() == Call || m_node->op() == TailCall || m_node->op() == TailCallInlinedCaller)) {
                JSBoundFunction* boundFunction = jsCast<JSBoundFunction*>(function);
                if (JSFunction* targetFunction = jsDynamicCast<JSFunction*>(boundFunction->targetFunction())) {
                    auto* targetExecutable = targetFunction->executable();
                    if ((boundFunction->boundArgsLength() + m_node->numChildren()) <= Options::maximumDirectCallStackSize()) {
                        if (FunctionExecutable* functionExecutable = jsDynamicCast<FunctionExecutable*>(targetExecutable)) {
                            // We need to update m_parameterSlots before we get to the backend, but we don't
                            // want to do too much of this.
                            unsigned numAllocatedArgs = static_cast<unsigned>(functionExecutable->parameterCount()) + 1;
                            if (numAllocatedArgs <= Options::maximumDirectCallStackSize())
                                m_graph.m_parameterSlots = std::max(m_graph.m_parameterSlots, Graph::parameterSlotsForArgCount(numAllocatedArgs));
                        }

                        unsigned firstChild = m_graph.m_varArgChildren.size();
                        m_graph.m_varArgChildren.append(m_insertionSet.insertConstant(m_nodeIndex, m_node->origin, targetFunction)); // |callee|.
                        m_graph.m_varArgChildren.append(m_insertionSet.insertConstant(m_nodeIndex, m_node->origin, boundFunction->boundThis())); // |this|

                        boundFunction->forEachBoundArg([&](JSValue argument) {
                            m_graph.m_varArgChildren.append(m_insertionSet.insertConstant(m_nodeIndex, m_node->origin, argument));
                            return IterationStatus::Continue;
                        });

                        // First one is |callee|, second one is |this|.
                        for (unsigned index = 2; index < m_node->numChildren(); ++index)
                            m_graph.m_varArgChildren.append(m_graph.child(m_node, index));

                        m_node->children = AdjacencyList(AdjacencyList::Variable, firstChild, m_graph.m_varArgChildren.size() - firstChild);
                        m_graph.m_parameterSlots = std::max(m_graph.m_parameterSlots, Graph::parameterSlotsForArgCount(m_node->numChildren() - 1));

                        m_graph.m_plan.recordedStatuses().addCallLinkStatus(m_node->origin.semantic, CallLinkStatus(CallVariant(targetExecutable)));
                        m_node->convertToDirectCall(m_graph.freeze(targetExecutable));
                        m_changed = true;
                        break;
                    }
                }
            }

            if (FunctionExecutable* functionExecutable = jsDynamicCast<FunctionExecutable*>(executable)) {
                if (m_node->op() == Construct && functionExecutable->constructAbility() == ConstructAbility::CannotConstruct)
                    break;

                // We need to update m_parameterSlots before we get to the backend, but we don't
                // want to do too much of this.
                unsigned numAllocatedArgs =
                    static_cast<unsigned>(functionExecutable->parameterCount()) + 1;
                
                if (numAllocatedArgs <= Options::maximumDirectCallStackSize()) {
                    m_graph.m_parameterSlots = std::max(
                        m_graph.m_parameterSlots,
                        Graph::parameterSlotsForArgCount(numAllocatedArgs));
                }
            }

            m_graph.m_plan.recordedStatuses().addCallLinkStatus(m_node->origin.semantic, CallLinkStatus(callVariant));

            m_node->convertToDirectCall(m_graph.freeze(executable));
            m_changed = true;
            break;
        }

        default:
            break;
        }
    }
            
    void convertToIdentityOverChild(unsigned childIndex)
    {
        ASSERT(!(m_node->flags() & NodeHasVarArgs));
        m_insertionSet.insertCheck(m_graph, m_nodeIndex, m_node);
        m_node->children.removeEdge(childIndex ^ 1);
        m_node->convertToIdentity();
        m_changed = true;
    }
    
    void convertToIdentityOverChild1()
    {
        convertToIdentityOverChild(0);
    }
    
    void convertToIdentityOverChild2()
    {
        convertToIdentityOverChild(1);
    }

    void convertToLazyJSValue(Node* node, LazyJSValue value)
    {
        m_insertionSet.insertCheck(m_graph, m_nodeIndex, node);
        node->convertToLazyJSConstant(m_graph, value);
    }
    
    void handleCommutativity()
    {
        // It's definitely not sound to swap the lhs and rhs when we may be performing effectful
        // calls on the lhs/rhs for valueOf.
        if (m_node->child1().useKind() == UntypedUse || m_node->child2().useKind() == UntypedUse)
            return;

        // If the right side is a constant then there is nothing left to do.
        if (m_node->child2()->hasConstant())
            return;
        
        // This case ensures that optimizations that look for x + const don't also have
        // to look for const + x.
        if (m_node->child1()->hasConstant() && !m_node->child1()->asJSValue().isCell()) {
            std::swap(m_node->child1(), m_node->child2());
            m_changed = true;
            return;
        }
        
        // This case ensures that CSE is commutativity-aware.
        if (m_node->child1().node() > m_node->child2().node()) {
            std::swap(m_node->child1(), m_node->child2());
            m_changed = true;
            return;
        }
    }

    void executeInsertionSet()
    {
        m_nodeIndex += m_insertionSet.execute(m_block);
    }
    
    InsertionSet m_insertionSet;
    BasicBlock* m_block;
    unsigned m_nodeIndex;
    Node* m_node;
    bool m_changed;
};
    
bool performStrengthReduction(Graph& graph)
{
    return runPhase<StrengthReductionPhase>(graph);
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)

