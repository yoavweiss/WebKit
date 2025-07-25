/*
 *  Copyright (C) 1999-2002 Harri Porten (porten@kde.org)
 *  Copyright (C) 2001 Peter Kelly (pmk@post.com)
 *  Copyright (C) 2003-2023 Apple Inc. All rights reserved.
 *  Copyright (C) 2007 Cameron Zwarich (cwzwarich@uwaterloo.ca)
 *  Copyright (C) 2007 Maks Orlovich
 *  Copyright (C) 2015 Canon Inc. All rights reserved.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include "JSFunction.h"

#include "AsyncGeneratorPrototype.h"
#include "BuiltinNames.h"
#include "CatchScope.h"
#include "CommonIdentifiers.h"
#include "CallFrame.h"
#include "FunctionExecutableInlines.h"
#include "GeneratorPrototype.h"
#include "JSBoundFunction.h"
#include "JSCInlines.h"
#include "JSGlobalObject.h"
#include "JSRemoteFunction.h"
#include "ObjectConstructor.h"
#include "ObjectPrototype.h"
#include "PropertyNameArray.h"
#include "StackVisitor.h"
#include "TypeError.h"
#include "VMTrapsInlines.h"
#if ENABLE(WEBASSEMBLY)
#include "WebAssemblyFunction.h"
#endif

namespace JSC {

JSC_DEFINE_HOST_FUNCTION(callHostFunctionAsConstructor, (JSGlobalObject* globalObject, CallFrame* callFrame))
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    return throwVMError(globalObject, scope, createNotAConstructorError(globalObject, callFrame->jsCallee()));
}

const ClassInfo JSFunction::s_info = { "Function"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSFunction) };
const ClassInfo JSStrictFunction::s_info = { "Function"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSStrictFunction) };
const ClassInfo JSSloppyFunction::s_info = { "Function"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSSloppyFunction) };
const ClassInfo JSArrowFunction::s_info = { "Function"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSArrowFunction) };

bool JSFunction::isHostFunctionNonInline() const
{
    return isHostFunction();
}

Structure* JSFunction::selectStructureForNewFuncExp(JSGlobalObject* globalObject, FunctionExecutable* executable)
{
    ASSERT(!executable->isHostFunction());
    bool isBuiltin = executable->isBuiltinFunction();
    // Arrow functions will never have a prototype, so no need to check
    if (executable->isArrowFunction())
        return globalObject->arrowFunctionStructure(isBuiltin);
    if (executable->isInStrictContext()) {
        if (executable->hasPrototypeProperty())
            return globalObject->strictFunctionStructure(isBuiltin);
        return globalObject->strictMethodStructure(isBuiltin);
    }
    if (executable->hasPrototypeProperty())
        return globalObject->sloppyFunctionStructure(isBuiltin);
    return globalObject->sloppyMethodStructure(isBuiltin);
}

JSFunction* JSFunction::create(VM& vm, JSGlobalObject* globalObject, FunctionExecutable* executable, JSScope* scope)
{
    return create(vm, globalObject, executable, scope, selectStructureForNewFuncExp(globalObject, executable));
}

JSFunction* JSFunction::create(VM& vm, JSGlobalObject*, FunctionExecutable* executable, JSScope* scope, Structure* structure)
{
    JSFunction* result = createImpl(vm, executable, scope, structure);
    executable->notifyCreation(vm, result, "Allocating a function");
    return result;
}

JSFunction* JSFunction::create(VM& vm, JSGlobalObject* globalObject, unsigned length, const String& name, NativeFunction nativeFunction, ImplementationVisibility implementationVisibility, Intrinsic intrinsic, NativeFunction nativeConstructor, const DOMJIT::Signature* signature)
{
    NativeExecutable* executable = vm.getHostFunction(nativeFunction, implementationVisibility, intrinsic, nativeConstructor, signature, name);
    Structure* structure = globalObject->hostFunctionStructure();
    JSFunction* function = new (NotNull, allocateCell<JSFunction>(vm)) JSFunction(vm, executable, globalObject, structure);
    // Can't do this during initialization because getHostFunction might do a GC allocation.
    function->finishCreation(vm, executable, length, name);
    return function;
}

JSFunction::JSFunction(VM& vm, NativeExecutable* executable, JSGlobalObject* globalObject, Structure* structure)
    : Base(vm, globalObject, structure)
    , m_executableOrRareData(std::bit_cast<uintptr_t>(executable))
{
    assertTypeInfoFlagInvariants();
    ASSERT(structure->globalObject() == globalObject);
}

#if ASSERT_ENABLED
void JSFunction::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    ASSERT(jsDynamicCast<JSFunction*>(this));
    ASSERT(type() == JSFunctionType);
    // JSCell::{getCallData,getConstructData} relies on the following conditions.
    ASSERT(methodTable()->getConstructData == &JSFunction::getConstructData);
    ASSERT(methodTable()->getCallData == &JSFunction::getCallData);
}
#endif

void JSFunction::finishCreation(VM& vm, NativeExecutable*, unsigned length, const String& name)
{
    Base::finishCreation(vm);
    ASSERT(inherits(info()));
    ASSERT(type() == JSFunctionType);
    // JSCell::{getCallData,getConstructData} relies on the following conditions.
    ASSERT(methodTable()->getConstructData == &JSFunction::getConstructData);
    ASSERT(methodTable()->getCallData == &JSFunction::getCallData);

    // JSBoundFunction/JSRemoteFunction instances use finishCreation(VM&) overload and lazily allocate their name string / length.
    ASSERT(!this->inherits<JSBoundFunction>() && !this->inherits<JSRemoteFunction>());

    putDirect(vm, vm.propertyNames->length, jsNumber(length), PropertyAttribute::ReadOnly | PropertyAttribute::DontEnum);
    if (!name.isNull())
        putDirect(vm, vm.propertyNames->name, jsString(vm, name), PropertyAttribute::ReadOnly | PropertyAttribute::DontEnum);
}

FunctionRareData* JSFunction::allocateRareData(VM& vm)
{
    uintptr_t executableOrRareData = m_executableOrRareData;
    ASSERT(!(executableOrRareData & rareDataTag));
    FunctionRareData* rareData = FunctionRareData::create(vm, std::bit_cast<ExecutableBase*>(executableOrRareData));
    executableOrRareData = std::bit_cast<uintptr_t>(rareData) | rareDataTag;

    // A DFG compilation thread may be trying to read the rare data
    // We want to ensure that it sees it properly allocated
    WTF::storeStoreFence();

    m_executableOrRareData = executableOrRareData;
    vm.writeBarrier(this, rareData);

    return rareData;
}

JSObject* JSFunction::prototypeForConstruction(VM& vm, JSGlobalObject* globalObject)
{
    // This code assumes getting the prototype is not effectful. That's only
    // true when we can use the allocation profile.
    ASSERT(canUseAllocationProfiles());
    DeferTermination deferScope(vm);
    auto scope = DECLARE_CATCH_SCOPE(vm);
    JSValue prototype = get(globalObject, vm.propertyNames->prototype);
    scope.releaseAssertNoException();
    if (prototype.isObject()) [[likely]]
        return asObject(prototype);
    if (isHostOrBuiltinFunction())
        return this->globalObject()->objectPrototype();

    JSGlobalObject* scopeGlobalObject = this->scope()->globalObject();
    // https://tc39.github.io/ecma262/#sec-generator-function-definitions-runtime-semantics-evaluatebody
    if (isGeneratorWrapperParseMode(jsExecutable()->parseMode()))
        return scopeGlobalObject->generatorPrototype();
    // https://tc39.github.io/ecma262/#sec-asyncgenerator-definitions-evaluatebody
    if (isAsyncGeneratorWrapperParseMode(jsExecutable()->parseMode()))
        return scopeGlobalObject->asyncGeneratorPrototype();
    return scopeGlobalObject->objectPrototype();
}

FunctionRareData* JSFunction::allocateAndInitializeRareData(JSGlobalObject* globalObject, size_t inlineCapacity)
{
    uintptr_t executableOrRareData = m_executableOrRareData;
    ASSERT(!(executableOrRareData & rareDataTag));
    ASSERT(canUseAllocationProfiles());
    VM& vm = globalObject->vm();
    JSObject* prototype = prototypeForConstruction(vm, globalObject);
    FunctionRareData* rareData = FunctionRareData::create(vm, std::bit_cast<ExecutableBase*>(executableOrRareData));
    rareData->initializeObjectAllocationProfile(vm, this->globalObject(), prototype, inlineCapacity, this);
    executableOrRareData = std::bit_cast<uintptr_t>(rareData) | rareDataTag;

    // A DFG compilation thread may be trying to read the rare data
    // We want to ensure that it sees it properly allocated
    WTF::storeStoreFence();

    m_executableOrRareData = executableOrRareData;
    vm.writeBarrier(this, rareData);

    return rareData;
}

FunctionRareData* JSFunction::initializeRareData(JSGlobalObject* globalObject, size_t inlineCapacity)
{
    uintptr_t executableOrRareData = m_executableOrRareData;
    ASSERT(executableOrRareData & rareDataTag);
    ASSERT(canUseAllocationProfiles());
    VM& vm = globalObject->vm();
    JSObject* prototype = prototypeForConstruction(vm, globalObject);
    FunctionRareData* rareData = std::bit_cast<FunctionRareData*>(executableOrRareData & ~rareDataTag);
    rareData->initializeObjectAllocationProfile(vm, this->globalObject(), prototype, inlineCapacity, this);
    return rareData;
}

String JSFunction::name(VM& vm)
{
    if (isHostFunction()) {
        if (this->inherits<JSBoundFunction>())
            return jsCast<JSBoundFunction*>(this)->nameString();
        NativeExecutable* executable = jsCast<NativeExecutable*>(this->executable());
        return executable->name();
    }
    const Identifier identifier = jsExecutable()->name();
    if (identifier == vm.propertyNames->starDefaultPrivateName)
        return emptyString();
    return identifier.string();
}

String JSFunction::nameWithoutGC(VM& vm)
{
    AssertNoGC assertNoGC;
    if (isHostFunction()) {
        if (this->inherits<JSBoundFunction>())
            return jsCast<JSBoundFunction*>(this)->nameStringWithoutGC(vm);
        NativeExecutable* executable = jsCast<NativeExecutable*>(this->executable());
        return executable->name();
    }
    const Identifier identifier = jsExecutable()->name();
    if (identifier == vm.propertyNames->starDefaultPrivateName)
        return emptyString();
    return identifier.string();
}

String JSFunction::displayName(VM& vm)
{
    JSValue displayName = getDirect(vm, vm.propertyNames->displayName);
    
    if (displayName && isJSString(displayName))
        return asString(displayName)->tryGetValue();
    
    return String();
}

const String JSFunction::calculatedDisplayName(VM& vm)
{
    const String explicitName = displayName(vm);
    
    if (!explicitName.isEmpty())
        return explicitName;
    
    const String actualName = name(vm);
    if (!actualName.isEmpty() || isHostOrBuiltinFunction())
        return actualName;

    return jsExecutable()->ecmaName().string();
}

JSString* JSFunction::toString(JSGlobalObject* globalObject)
{
    VM& vm = getVM(globalObject);
    if (inherits<JSBoundFunction>()) {
        JSBoundFunction* function = jsCast<JSBoundFunction*>(this);
        auto scope = DECLARE_THROW_SCOPE(vm);
        JSValue string = jsMakeNontrivialString(globalObject, "function "_s, function->nameString(), "() {\n    [native code]\n}"_s);
        RETURN_IF_EXCEPTION(scope, nullptr);
        return asString(string);
    } else if (inherits<JSRemoteFunction>()) {
        JSRemoteFunction* function = jsCast<JSRemoteFunction*>(this);
        auto scope = DECLARE_THROW_SCOPE(vm);
        JSValue string = jsMakeNontrivialString(globalObject, "function "_s, function->nameString(), "() {\n    [native code]\n}"_s);
        RETURN_IF_EXCEPTION(scope, nullptr);
        return asString(string);
    }

    if (isHostFunction())
        return static_cast<NativeExecutable*>(executable())->toString(globalObject);
    return jsExecutable()->toString(globalObject);
}

const SourceCode* JSFunction::sourceCode() const
{
    if (isHostOrBuiltinFunction())
        return nullptr;
    return &jsExecutable()->source();
}
    
template<typename Visitor>
void JSFunction::visitChildrenImpl(JSCell* cell, Visitor& visitor)
{
    JSFunction* thisObject = jsCast<JSFunction*>(cell);
    ASSERT_GC_OBJECT_INHERITS(thisObject, info());
    Base::visitChildren(thisObject, visitor);

    visitor.appendUnbarriered(std::bit_cast<JSCell*>(std::bit_cast<uintptr_t>(thisObject->m_executableOrRareData) & ~rareDataTag));
}

DEFINE_VISIT_CHILDREN(JSFunction);

CallData JSFunction::getCallData(JSCell* cell)
{
    // Keep this function OK for invocation from concurrent compilers.
    CallData callData;

    JSFunction* thisObject = jsCast<JSFunction*>(cell);
    if (thisObject->isHostFunction()) {
        callData.type = CallData::Type::Native;
        callData.native.function = thisObject->nativeFunction();
        callData.native.isBoundFunction = thisObject->inherits<JSBoundFunction>();
        callData.native.isWasm = false;
#if ENABLE(WEBASSEMBLY)
        callData.native.isWasm = thisObject->inherits<WebAssemblyFunction>();
#endif
    } else {
        callData.type = CallData::Type::JS;
        callData.js.functionExecutable = thisObject->jsExecutable();
        callData.js.scope = thisObject->scope();
    }

    return callData;
}

static constexpr unsigned prototypeAttributesForNonClass = PropertyAttribute::DontEnum | PropertyAttribute::DontDelete;

static inline JSObject* constructPrototypeObject(JSGlobalObject* globalObject, JSFunction* thisObject)
{
    VM& vm = globalObject->vm();
    JSGlobalObject* scopeGlobalObject = thisObject->scope()->globalObject();
    // Unlike Function instances, the prototype object of GeneratorFunction instances lacks own "constructor" property.
    // https://tc39.es/ecma262/#sec-runtime-semantics-instantiategeneratorfunctionobject (step 6)
    if (isGeneratorWrapperParseMode(thisObject->jsExecutable()->parseMode()))
        return constructEmptyObject(globalObject, scopeGlobalObject->generatorPrototype());
    // Unlike Function instances, the prototype object of AsyncGeneratorFunction instances lacks own "constructor" property.
    // https://tc39.es/ecma262/#sec-runtime-semantics-instantiateasyncgeneratorfunctionobject (step 6)
    if (isAsyncGeneratorWrapperParseMode(thisObject->jsExecutable()->parseMode()))
        return constructEmptyObject(globalObject, scopeGlobalObject->asyncGeneratorPrototype());

    JSObject* prototype = constructEmptyObject(globalObject, scopeGlobalObject->objectPrototype());
    prototype->putDirect(vm, vm.propertyNames->constructor, thisObject, static_cast<unsigned>(PropertyAttribute::DontEnum));
    return prototype;
}

bool JSFunction::getOwnPropertySlot(JSObject* object, JSGlobalObject* globalObject, PropertyName propertyName, PropertySlot& slot)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSFunction* thisObject = jsCast<JSFunction*>(object);

    if (propertyName == vm.propertyNames->prototype) {
        if (thisObject->mayHaveNonReifiedPrototype()) {
            unsigned attributes;
            PropertyOffset offset = thisObject->getDirectOffset(vm, propertyName, attributes);
            if (!isValidOffset(offset)) {
                // For class constructors, prototype object is initialized from bytecode via defineOwnProperty().
                ASSERT(!thisObject->jsExecutable()->isClassConstructorFunction());
                thisObject->putDirect(vm, propertyName, constructPrototypeObject(globalObject, thisObject), prototypeAttributesForNonClass);
                offset = thisObject->getDirectOffset(vm, vm.propertyNames->prototype, attributes);
                ASSERT(isValidOffset(offset));
            }
            slot.setValue(thisObject, attributes, thisObject->getDirect(offset), offset);
            return true;
        }
    }

    thisObject->reifyLazyPropertyIfNeeded<JSFunction::SetHasModifiedLengthOrName::No>(vm, globalObject, propertyName);
    RETURN_IF_EXCEPTION(scope, false);

    RELEASE_AND_RETURN(scope, Base::getOwnPropertySlot(thisObject, globalObject, propertyName, slot));
}

void JSFunction::getOwnSpecialPropertyNames(JSObject* object, JSGlobalObject* globalObject, PropertyNameArray& propertyNames, DontEnumPropertiesMode mode)
{
    JSFunction* thisObject = jsCast<JSFunction*>(object);
    VM& vm = globalObject->vm();
    auto scope = DECLARE_CATCH_SCOPE(vm);

    if (mode == DontEnumPropertiesMode::Include) {
        bool hasLength = thisObject->hasOwnProperty(globalObject, vm.propertyNames->length);
        if (scope.exception()) [[unlikely]] {
            hasLength = false;
            scope.clearException();
        }
        if (!thisObject->hasReifiedLength() || hasLength)
            propertyNames.add(vm.propertyNames->length);
        bool hasName = thisObject->hasOwnProperty(globalObject, vm.propertyNames->name);
        if (scope.exception()) [[unlikely]] {
            hasName = false;
            scope.clearException();
        }
        if (!thisObject->hasReifiedName() || hasName)
            propertyNames.add(vm.propertyNames->name);
        if (!thisObject->isHostOrBuiltinFunction() && thisObject->jsExecutable()->hasPrototypeProperty())
            propertyNames.add(vm.propertyNames->prototype);
    } else if (mode == DontEnumPropertiesMode::Exclude) {
        PropertyDescriptor descriptor;

        thisObject->getOwnPropertyDescriptor(globalObject, vm.propertyNames->length, descriptor);
        if (scope.exception()) [[unlikely]]
            scope.clearException();
        else if (descriptor.enumerable())
            propertyNames.add(vm.propertyNames->length);

        thisObject->getOwnPropertyDescriptor(globalObject, vm.propertyNames->name, descriptor);
        if (scope.exception()) [[unlikely]]
            scope.clearException();
        else if (descriptor.enumerable())
            propertyNames.add(vm.propertyNames->name);
    }
}

bool JSFunction::put(JSCell* cell, JSGlobalObject* globalObject, PropertyName propertyName, JSValue value, PutPropertySlot& slot)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSFunction* thisObject = jsCast<JSFunction*>(cell);

    if (propertyName == vm.propertyNames->prototype) {
        slot.disableCaching();
        if (FunctionRareData* rareData = thisObject->rareData())
            rareData->clear("Store to prototype property of a function");
        if (thisObject->mayHaveNonReifiedPrototype()) {
            if (!isValidOffset(thisObject->getDirectOffset(vm, propertyName))) {
                // For class constructors, prototype object is initialized from bytecode via defineOwnProperty().
                ASSERT(!thisObject->jsExecutable()->isClassConstructorFunction());
                if (slot.thisValue() != thisObject) [[unlikely]]
                    RELEASE_AND_RETURN(scope, JSObject::definePropertyOnReceiver(globalObject, propertyName, value, slot));
                thisObject->putDirect(vm, propertyName, value, prototypeAttributesForNonClass);
                return true;
            }
            RELEASE_AND_RETURN(scope, Base::put(thisObject, globalObject, propertyName, value, slot));
        }
    }

    PropertyStatus propertyType = thisObject->reifyLazyPropertyIfNeeded<>(vm, globalObject, propertyName);
    RETURN_IF_EXCEPTION(scope, false);
    if (isLazy(propertyType))
        slot.disableCaching();
    RELEASE_AND_RETURN(scope, Base::put(thisObject, globalObject, propertyName, value, slot));
}

bool JSFunction::deleteProperty(JSCell* cell, JSGlobalObject* globalObject, PropertyName propertyName, DeletePropertySlot& slot)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);
    JSFunction* thisObject = jsCast<JSFunction*>(cell);

    PropertyStatus propertyType = thisObject->reifyLazyPropertyIfNeeded<>(vm, globalObject, propertyName);
    RETURN_IF_EXCEPTION(scope, false);
    if (isLazy(propertyType))
        slot.disableCaching();
    RELEASE_AND_RETURN(scope, Base::deleteProperty(thisObject, globalObject, propertyName, slot));
}

bool JSFunction::defineOwnProperty(JSObject* object, JSGlobalObject* globalObject, PropertyName propertyName, const PropertyDescriptor& descriptor, bool throwException)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSFunction* thisObject = jsCast<JSFunction*>(object);


    if (propertyName == vm.propertyNames->prototype) {
        if (FunctionRareData* rareData = thisObject->rareData())
            rareData->clear("Store to prototype property of a function");
    }

    if (propertyName == vm.propertyNames->prototype && thisObject->mayHaveNonReifiedPrototype()) {
        if (!isValidOffset(thisObject->getDirectOffset(vm, propertyName))) {
            if (thisObject->jsExecutable()->isClassConstructorFunction()) {
                // Fast path for prototype object initialization from bytecode that avoids calling into getOwnPropertySlot().
                ASSERT(descriptor.isDataDescriptor());
                thisObject->putDirect(vm, propertyName, descriptor.value(), descriptor.attributes());
                return true;
            }
            thisObject->putDirect(vm, propertyName, constructPrototypeObject(globalObject, thisObject), prototypeAttributesForNonClass);
        }
    } else {
        thisObject->reifyLazyPropertyIfNeeded<>(vm, globalObject, propertyName);
        RETURN_IF_EXCEPTION(scope, false);
    }

    RELEASE_AND_RETURN(scope, Base::defineOwnProperty(object, globalObject, propertyName, descriptor, throwException));
}

// ECMA 13.2.2 [[Construct]]
CallData JSFunction::getConstructData(JSCell* cell)
{
    // Keep this function OK for invocation from concurrent compilers.
    CallData constructData;

    JSFunction* thisObject = jsCast<JSFunction*>(cell);
    if (thisObject->isHostFunction()) {
        if (thisObject->inherits<JSBoundFunction>()) {
            if (jsCast<JSBoundFunction*>(thisObject)->canConstruct()) {
                constructData.type = CallData::Type::Native;
                constructData.native.function = thisObject->nativeConstructor();
                constructData.native.isBoundFunction = true;
                constructData.native.isWasm = false;
            }
        } else if (thisObject->nativeConstructor() != callHostFunctionAsConstructor) {
            constructData.type = CallData::Type::Native;
            constructData.native.function = thisObject->nativeConstructor();
        }
    } else {
        FunctionExecutable* functionExecutable = thisObject->jsExecutable();
        if (functionExecutable->constructAbility() != ConstructAbility::CannotConstruct) {
            constructData.type = CallData::Type::JS;
            constructData.js.functionExecutable = functionExecutable;
            constructData.js.scope = thisObject->scope();
        }
    }

    return constructData;
}

String getCalculatedDisplayName(VM& vm, JSObject* object)
{
    if (!jsDynamicCast<JSFunction*>(object) && !jsDynamicCast<InternalFunction*>(object))
        return emptyString();

    Structure* structure = object->structure();
    unsigned attributes;
    // This function may be called when the mutator isn't running and we are lazily generating a stack trace.
    PropertyOffset offset = structure->getConcurrently(vm.propertyNames->displayName.impl(), attributes);
    if (offset != invalidOffset && !(attributes & (PropertyAttribute::Accessor | PropertyAttribute::CustomAccessorOrValue))) {
        JSValue displayName = object->getDirect(offset);
        if (displayName && displayName.isString())
            return asString(displayName)->tryGetValueWithoutGC();
    }

    if (auto* function = jsDynamicCast<JSFunction*>(object)) {
        String actualName = function->nameWithoutGC(vm);
        if (!actualName.isEmpty() || function->isHostOrBuiltinFunction())
            return actualName;

        return function->jsExecutable()->ecmaName().string();
    }
    if (auto* function = jsDynamicCast<InternalFunction*>(object))
        return function->name();

    return emptyString();
}

void JSFunction::setFunctionName(JSGlobalObject* globalObject, JSValue value)
{
    VM& vm = globalObject->vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    // The "name" property may have been already been defined as part of a property list in an
    // object literal (and therefore reified).
    if (hasReifiedName())
        return;

    ASSERT(!isHostFunction());
    ASSERT(jsExecutable()->ecmaName().isNull());
    String name;
    if (value.isSymbol()) {
        PrivateName privateName = asSymbol(value)->privateName();
        SymbolImpl& uid = privateName.uid();
        if (uid.isNullSymbol())
            name = emptyString();
        else {
            name = makeNameWithOutOfMemoryCheck(globalObject, scope, "Function "_s, '[', String(&uid), ']');
            RETURN_IF_EXCEPTION(scope, void());
        }
    } else {
        ASSERT(value.isString());
        name = asString(value)->value(globalObject);
        RETURN_IF_EXCEPTION(scope, void());
    }
    RELEASE_AND_RETURN(scope, (void)reifyName(vm, globalObject, name));
}

void JSFunction::reifyLength(VM& vm)
{
    FunctionRareData* rareData = this->ensureRareData(vm);

    ASSERT(!hasReifiedLength());
    double length = originalLength(vm);
    JSValue initialValue = jsNumber(length);
    unsigned initialAttributes = PropertyAttribute::DontEnum | PropertyAttribute::ReadOnly;
    const Identifier& identifier = vm.propertyNames->length;
    rareData->setHasReifiedLength();
    putDirect(vm, identifier, initialValue, initialAttributes);
}

JSFunction::PropertyStatus JSFunction::reifyName(VM& vm, JSGlobalObject* globalObject)
{
    const Identifier& ecmaName = jsExecutable()->ecmaName();
    String name;
    // https://tc39.github.io/ecma262/#sec-exports-runtime-semantics-evaluation
    // When the ident is "*default*", we need to set "default" for the ecma name.
    // This "*default*" name is never shown to users.
    if (ecmaName == vm.propertyNames->starDefaultPrivateName)
        name = vm.propertyNames->defaultKeyword.string();
    else
        name = ecmaName.string();
    return reifyName(vm, globalObject, name);
}

JSFunction::PropertyStatus JSFunction::reifyName(VM& vm, JSGlobalObject* globalObject, String name)
{
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    FunctionRareData* rareData = this->ensureRareData(vm);

    ASSERT(!hasReifiedName());
    ASSERT(!isHostFunction());
    unsigned initialAttributes = PropertyAttribute::DontEnum | PropertyAttribute::ReadOnly;
    const Identifier& propID = vm.propertyNames->name;

    if (jsExecutable()->isGetter())
        name = makeNameWithOutOfMemoryCheck(globalObject, throwScope, "Getter "_s, "get "_s, name);
    else if (jsExecutable()->isSetter())
        name = makeNameWithOutOfMemoryCheck(globalObject, throwScope, "Setter "_s, "set "_s, name);
    RETURN_IF_EXCEPTION(throwScope, PropertyStatus::Lazy);

    rareData->setHasReifiedName();
    putDirect(vm, propID, jsString(vm, WTFMove(name)), initialAttributes);
    return PropertyStatus::Reified;
}

template <JSFunction::SetHasModifiedLengthOrName set>
JSFunction::PropertyStatus JSFunction::reifyLazyPropertyIfNeeded(VM& vm, JSGlobalObject* globalObject, PropertyName propertyName)
{
    JSFunction::PropertyStatus status;
    if (isHostOrBuiltinFunction())
        status = reifyLazyPropertyForHostOrBuiltinIfNeeded(vm, globalObject, propertyName);
    else if (PropertyStatus lazyPrototype = reifyLazyPrototypeIfNeeded(vm, globalObject, propertyName); isLazy(lazyPrototype))
        status = lazyPrototype;
    else if (PropertyStatus lazyLength = reifyLazyLengthIfNeeded(vm, globalObject, propertyName); isLazy(lazyLength))
        status = lazyLength;
    else if (PropertyStatus lazyName = reifyLazyNameIfNeeded(vm, globalObject, propertyName); isLazy(lazyName))
        status = lazyName;
    else
        status = PropertyStatus::Eager;

    if constexpr (set == SetHasModifiedLengthOrName::Yes) {
        if (isNonBoundHostFunction() || !structure()->didTransition())
            return status;
        bool isLengthProperty = propertyName == vm.propertyNames->length;
        bool isNameProperty = propertyName == vm.propertyNames->name;
        if (!isLengthProperty && !isNameProperty)
            return status;
        FunctionRareData* rareData = ensureRareData(vm);
        if (isLengthProperty)
            rareData->setHasModifiedLengthForBoundOrNonHostFunction();
        else
            rareData->setHasModifiedNameForBoundOrNonHostFunction();
    }

    return status;
}

JSFunction::PropertyStatus JSFunction::reifyLazyPropertyForHostOrBuiltinIfNeeded(VM& vm, JSGlobalObject* globalObject, PropertyName propertyName)
{
    ASSERT(isHostOrBuiltinFunction());
    if (isBuiltinFunction() || this->inherits<JSBoundFunction>() || this->inherits<JSRemoteFunction>()) {
        PropertyStatus lazyLength = reifyLazyLengthIfNeeded(vm, globalObject, propertyName);
        if (isLazy(lazyLength))
            return lazyLength;
    }
    return reifyLazyBoundNameIfNeeded(vm, globalObject, propertyName);
}

JSFunction::PropertyStatus JSFunction::reifyLazyPrototypeIfNeeded(VM& vm, JSGlobalObject* globalObject, PropertyName propertyName)
{
    if (propertyName == vm.propertyNames->prototype && mayHaveNonReifiedPrototype()) {
        if (!getDirect(vm, propertyName)) {
            // For class constructors, prototype object is initialized from bytecode via defineOwnProperty().
            ASSERT(!jsExecutable()->isClassConstructorFunction());
            putDirect(vm, propertyName, constructPrototypeObject(globalObject, this), prototypeAttributesForNonClass);
            return PropertyStatus::Reified;
        }
        return PropertyStatus::Lazy;
    }
    return PropertyStatus::Eager;
}

JSFunction::PropertyStatus JSFunction::reifyLazyLengthIfNeeded(VM& vm, JSGlobalObject*, PropertyName propertyName)
{
    if (propertyName == vm.propertyNames->length) {
        if (!hasReifiedLength()) {
            reifyLength(vm);
            return PropertyStatus::Reified;
        }
        return PropertyStatus::Lazy;
    }
    return PropertyStatus::Eager;
}

JSFunction::PropertyStatus JSFunction::reifyLazyNameIfNeeded(VM& vm, JSGlobalObject* globalObject, PropertyName propertyName)
{
    if (propertyName == vm.propertyNames->name) {
        if (!hasReifiedName())
            return reifyName(vm, globalObject);
        return PropertyStatus::Lazy;
    }
    return PropertyStatus::Eager;
}

JSFunction::PropertyStatus JSFunction::reifyLazyBoundNameIfNeeded(VM& vm, JSGlobalObject* globalObject, PropertyName propertyName)
{
    auto scope = DECLARE_THROW_SCOPE(vm);

    const Identifier& nameIdent = vm.propertyNames->name;
    if (propertyName != nameIdent)
        return PropertyStatus::Eager;

    if (hasReifiedName())
        return PropertyStatus::Lazy;

    if (isBuiltinFunction())
        RELEASE_AND_RETURN(scope, reifyName(vm, globalObject));
    else if (this->inherits<JSBoundFunction>()) {
        FunctionRareData* rareData = this->ensureRareData(vm);
        JSString* name = jsCast<JSBoundFunction*>(this)->name();
        JSString* string = jsString(globalObject, vm.smallStrings.boundPrefixString(), name);
        RETURN_IF_EXCEPTION(scope, PropertyStatus::Lazy);
        unsigned initialAttributes = PropertyAttribute::DontEnum | PropertyAttribute::ReadOnly;
        rareData->setHasReifiedName();
        putDirect(vm, nameIdent, string, initialAttributes);
    } else if (this->inherits<JSRemoteFunction>()) {
        FunctionRareData* rareData = this->ensureRareData(vm);
        JSString* name = jsCast<JSRemoteFunction*>(this)->nameMayBeNull();
        if (!name)
            name = jsEmptyString(vm);
        unsigned initialAttributes = PropertyAttribute::DontEnum | PropertyAttribute::ReadOnly;
        rareData->setHasReifiedName();
        putDirect(vm, nameIdent, name, initialAttributes);
    }
    return PropertyStatus::Reified;
}

#if ASSERT_ENABLED
void JSFunction::assertTypeInfoFlagInvariants()
{
    // If you change this, you'll need to update speculationFromClassInfoInheritance.
    const ClassInfo* info = classInfo();
    if (!(inlineTypeFlags() & ImplementsDefaultHasInstance))
        RELEASE_ASSERT(info == JSBoundFunction::info());
    else
        RELEASE_ASSERT(info != JSBoundFunction::info());
}
#endif // ASSERT_ENABLED

} // namespace JSC
