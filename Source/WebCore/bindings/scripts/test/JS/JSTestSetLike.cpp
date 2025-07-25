/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "JSTestSetLike.h"

#include "ActiveDOMObject.h"
#include "ContextDestructionObserverInlines.h"
#include "ExtendedDOMClientIsoSubspaces.h"
#include "ExtendedDOMIsoSubspaces.h"
#include "JSDOMAttribute.h"
#include "JSDOMBinding.h"
#include "JSDOMConstructorNotConstructable.h"
#include "JSDOMConvertAny.h"
#include "JSDOMConvertStrings.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMGlobalObjectInlines.h"
#include "JSDOMOperation.h"
#include "JSDOMSetLike.h"
#include "JSDOMWrapperCache.h"
#include "ScriptExecutionContext.h"
#include "WebCoreJSClientData.h"
#include <JavaScriptCore/BuiltinNames.h>
#include <JavaScriptCore/FunctionPrototype.h>
#include <JavaScriptCore/HeapAnalyzer.h>
#include <JavaScriptCore/JSCInlines.h>
#include <JavaScriptCore/JSDestructibleObjectHeapCellType.h>
#include <JavaScriptCore/SlotVisitorMacros.h>
#include <JavaScriptCore/SubspaceInlines.h>
#include <wtf/GetPtr.h>
#include <wtf/PointerPreparations.h>
#include <wtf/URL.h>
#include <wtf/text/MakeString.h>

namespace WebCore {
using namespace JSC;

// Functions

static JSC_DECLARE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_has);
static JSC_DECLARE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_entries);
static JSC_DECLARE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_keys);
static JSC_DECLARE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_values);
static JSC_DECLARE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_forEach);
static JSC_DECLARE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_add);
static JSC_DECLARE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_clear);
static JSC_DECLARE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_delete);

// Attributes

static JSC_DECLARE_CUSTOM_GETTER(jsTestSetLikeConstructor);
static JSC_DECLARE_CUSTOM_GETTER(jsTestSetLike_size);

class JSTestSetLikePrototype final : public JSC::JSNonFinalObject {
public:
    using Base = JSC::JSNonFinalObject;
    static JSTestSetLikePrototype* create(JSC::VM& vm, JSDOMGlobalObject* globalObject, JSC::Structure* structure)
    {
        JSTestSetLikePrototype* ptr = new (NotNull, JSC::allocateCell<JSTestSetLikePrototype>(vm)) JSTestSetLikePrototype(vm, globalObject, structure);
        ptr->finishCreation(vm);
        return ptr;
    }

    DECLARE_INFO;
    template<typename CellType, JSC::SubspaceAccess>
    static JSC::GCClient::IsoSubspace* subspaceFor(JSC::VM& vm)
    {
        STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(JSTestSetLikePrototype, Base);
        return &vm.plainObjectSpace();
    }
    static JSC::Structure* createStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSValue prototype)
    {
        return JSC::Structure::create(vm, globalObject, prototype, JSC::TypeInfo(JSC::ObjectType, StructureFlags), info());
    }

private:
    JSTestSetLikePrototype(JSC::VM& vm, JSC::JSGlobalObject*, JSC::Structure* structure)
        : JSC::JSNonFinalObject(vm, structure)
    {
    }

    void finishCreation(JSC::VM&);
};
STATIC_ASSERT_ISO_SUBSPACE_SHARABLE(JSTestSetLikePrototype, JSTestSetLikePrototype::Base);

using JSTestSetLikeDOMConstructor = JSDOMConstructorNotConstructable<JSTestSetLike>;

template<> const ClassInfo JSTestSetLikeDOMConstructor::s_info = { "TestSetLike"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestSetLikeDOMConstructor) };

template<> JSValue JSTestSetLikeDOMConstructor::prototypeForStructure(JSC::VM& vm, const JSDOMGlobalObject& globalObject)
{
    UNUSED_PARAM(vm);
    return globalObject.functionPrototype();
}

template<> void JSTestSetLikeDOMConstructor::initializeProperties(VM& vm, JSDOMGlobalObject& globalObject)
{
    putDirect(vm, vm.propertyNames->length, jsNumber(0), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    JSString* nameString = jsNontrivialString(vm, "TestSetLike"_s);
    m_originalName.set(vm, this, nameString);
    putDirect(vm, vm.propertyNames->name, nameString, JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    putDirect(vm, vm.propertyNames->prototype, JSTestSetLike::prototype(vm, globalObject), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum | JSC::PropertyAttribute::DontDelete);
}

/* Hash table for prototype */

static const std::array<HashTableValue, 10> JSTestSetLikePrototypeTableValues {
    HashTableValue { "constructor"_s, static_cast<unsigned>(PropertyAttribute::DontEnum), NoIntrinsic, { HashTableValue::GetterSetterType, jsTestSetLikeConstructor, 0 } },
    HashTableValue { "size"_s, JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::CustomAccessor, NoIntrinsic, { HashTableValue::GetterSetterType, jsTestSetLike_size, 0 } },
    HashTableValue { "has"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsTestSetLikePrototypeFunction_has, 1 } },
    HashTableValue { "entries"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsTestSetLikePrototypeFunction_entries, 0 } },
    HashTableValue { "keys"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsTestSetLikePrototypeFunction_keys, 0 } },
    HashTableValue { "values"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsTestSetLikePrototypeFunction_values, 0 } },
    HashTableValue { "forEach"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsTestSetLikePrototypeFunction_forEach, 1 } },
    HashTableValue { "add"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsTestSetLikePrototypeFunction_add, 1 } },
    HashTableValue { "clear"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsTestSetLikePrototypeFunction_clear, 0 } },
    HashTableValue { "delete"_s, static_cast<unsigned>(JSC::PropertyAttribute::Function), NoIntrinsic, { HashTableValue::NativeFunctionType, jsTestSetLikePrototypeFunction_delete, 1 } },
};

const ClassInfo JSTestSetLikePrototype::s_info = { "TestSetLike"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestSetLikePrototype) };

void JSTestSetLikePrototype::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    reifyStaticProperties(vm, JSTestSetLike::info(), JSTestSetLikePrototypeTableValues, *this);
    putDirect(vm, vm.propertyNames->iteratorSymbol, getDirect(vm, vm.propertyNames->builtinNames().valuesPublicName()), static_cast<unsigned>(JSC::PropertyAttribute::DontEnum));
    JSC_TO_STRING_TAG_WITHOUT_TRANSITION();
}

const ClassInfo JSTestSetLike::s_info = { "TestSetLike"_s, &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestSetLike) };

JSTestSetLike::JSTestSetLike(Structure* structure, JSDOMGlobalObject& globalObject, Ref<TestSetLike>&& impl)
    : JSDOMWrapper<TestSetLike>(structure, globalObject, WTFMove(impl))
{
}

static_assert(!std::is_base_of<ActiveDOMObject, TestSetLike>::value, "Interface is not marked as [ActiveDOMObject] even though implementation class subclasses ActiveDOMObject.");

JSObject* JSTestSetLike::createPrototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    auto* structure = JSTestSetLikePrototype::createStructure(vm, &globalObject, globalObject.objectPrototype());
    structure->setMayBePrototype(true);
    return JSTestSetLikePrototype::create(vm, &globalObject, structure);
}

JSObject* JSTestSetLike::prototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    return getDOMPrototype<JSTestSetLike>(vm, globalObject);
}

JSValue JSTestSetLike::getConstructor(VM& vm, const JSGlobalObject* globalObject)
{
    return getDOMConstructor<JSTestSetLikeDOMConstructor, DOMConstructorID::TestSetLike>(vm, *jsCast<const JSDOMGlobalObject*>(globalObject));
}

void JSTestSetLike::destroy(JSC::JSCell* cell)
{
    JSTestSetLike* thisObject = static_cast<JSTestSetLike*>(cell);
    thisObject->JSTestSetLike::~JSTestSetLike();
}

JSC_DEFINE_CUSTOM_GETTER(jsTestSetLikeConstructor, (JSGlobalObject* lexicalGlobalObject, EncodedJSValue thisValue, PropertyName))
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    auto* prototype = jsDynamicCast<JSTestSetLikePrototype*>(JSValue::decode(thisValue));
    if (!prototype) [[unlikely]]
        return throwVMTypeError(lexicalGlobalObject, throwScope);
    return JSValue::encode(JSTestSetLike::getConstructor(vm, prototype->globalObject()));
}

static inline JSValue jsTestSetLike_sizeGetter(JSGlobalObject& lexicalGlobalObject, JSTestSetLike& thisObject)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(&lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    RELEASE_AND_RETURN(throwScope, (toJS<IDLAny>(lexicalGlobalObject, throwScope, forwardSizeToSetLike(lexicalGlobalObject, thisObject))));
}

JSC_DEFINE_CUSTOM_GETTER(jsTestSetLike_size, (JSGlobalObject* lexicalGlobalObject, EncodedJSValue thisValue, PropertyName attributeName))
{
    return IDLAttribute<JSTestSetLike>::get<jsTestSetLike_sizeGetter>(*lexicalGlobalObject, thisValue, attributeName);
}

static inline JSC::EncodedJSValue jsTestSetLikePrototypeFunction_hasBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSTestSetLike>::ClassParameter castedThis)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    UNUSED_PARAM(throwScope);
    UNUSED_PARAM(callFrame);
    if (callFrame->argumentCount() < 1) [[unlikely]]
        return throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));
    EnsureStillAliveScope argument0 = callFrame->uncheckedArgument(0);
    auto keyConversionResult = convert<IDLDOMString>(*lexicalGlobalObject, argument0.value());
    if (keyConversionResult.hasException(throwScope)) [[unlikely]]
       return encodedJSValue();
    RELEASE_AND_RETURN(throwScope, JSValue::encode(toJS<IDLAny>(*lexicalGlobalObject, throwScope, forwardHasToSetLike(*lexicalGlobalObject, *callFrame, *castedThis, keyConversionResult.releaseReturnValue()))));
}

JSC_DEFINE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_has, (JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSTestSetLike>::call<jsTestSetLikePrototypeFunction_hasBody>(*lexicalGlobalObject, *callFrame, "has");
}

static inline JSC::EncodedJSValue jsTestSetLikePrototypeFunction_entriesBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSTestSetLike>::ClassParameter castedThis)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    UNUSED_PARAM(throwScope);
    UNUSED_PARAM(callFrame);
    RELEASE_AND_RETURN(throwScope, JSValue::encode(toJS<IDLAny>(*lexicalGlobalObject, throwScope, forwardEntriesToSetLike(*lexicalGlobalObject, *callFrame, *castedThis))));
}

JSC_DEFINE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_entries, (JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSTestSetLike>::call<jsTestSetLikePrototypeFunction_entriesBody>(*lexicalGlobalObject, *callFrame, "entries");
}

static inline JSC::EncodedJSValue jsTestSetLikePrototypeFunction_keysBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSTestSetLike>::ClassParameter castedThis)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    UNUSED_PARAM(throwScope);
    UNUSED_PARAM(callFrame);
    RELEASE_AND_RETURN(throwScope, JSValue::encode(toJS<IDLAny>(*lexicalGlobalObject, throwScope, forwardKeysToSetLike(*lexicalGlobalObject, *callFrame, *castedThis))));
}

JSC_DEFINE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_keys, (JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSTestSetLike>::call<jsTestSetLikePrototypeFunction_keysBody>(*lexicalGlobalObject, *callFrame, "keys");
}

static inline JSC::EncodedJSValue jsTestSetLikePrototypeFunction_valuesBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSTestSetLike>::ClassParameter castedThis)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    UNUSED_PARAM(throwScope);
    UNUSED_PARAM(callFrame);
    RELEASE_AND_RETURN(throwScope, JSValue::encode(toJS<IDLAny>(*lexicalGlobalObject, throwScope, forwardValuesToSetLike(*lexicalGlobalObject, *callFrame, *castedThis))));
}

JSC_DEFINE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_values, (JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSTestSetLike>::call<jsTestSetLikePrototypeFunction_valuesBody>(*lexicalGlobalObject, *callFrame, "values");
}

static inline JSC::EncodedJSValue jsTestSetLikePrototypeFunction_forEachBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSTestSetLike>::ClassParameter castedThis)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    UNUSED_PARAM(throwScope);
    UNUSED_PARAM(callFrame);
    if (callFrame->argumentCount() < 1) [[unlikely]]
        return throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));
    EnsureStillAliveScope argument0 = callFrame->uncheckedArgument(0);
    auto callbackConversionResult = convert<IDLAny>(*lexicalGlobalObject, argument0.value());
    if (callbackConversionResult.hasException(throwScope)) [[unlikely]]
       return encodedJSValue();
    RELEASE_AND_RETURN(throwScope, JSValue::encode(toJS<IDLAny>(*lexicalGlobalObject, throwScope, forwardForEachToSetLike(*lexicalGlobalObject, *callFrame, *castedThis, callbackConversionResult.releaseReturnValue()))));
}

JSC_DEFINE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_forEach, (JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSTestSetLike>::call<jsTestSetLikePrototypeFunction_forEachBody>(*lexicalGlobalObject, *callFrame, "forEach");
}

static inline JSC::EncodedJSValue jsTestSetLikePrototypeFunction_addBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSTestSetLike>::ClassParameter castedThis)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    UNUSED_PARAM(throwScope);
    UNUSED_PARAM(callFrame);
    if (callFrame->argumentCount() < 1) [[unlikely]]
        return throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));
    EnsureStillAliveScope argument0 = callFrame->uncheckedArgument(0);
    auto keyConversionResult = convert<IDLDOMString>(*lexicalGlobalObject, argument0.value());
    if (keyConversionResult.hasException(throwScope)) [[unlikely]]
       return encodedJSValue();
    RELEASE_AND_RETURN(throwScope, JSValue::encode(toJS<IDLAny>(*lexicalGlobalObject, throwScope, forwardAddToSetLike(*lexicalGlobalObject, *callFrame, *castedThis, keyConversionResult.releaseReturnValue()))));
}

JSC_DEFINE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_add, (JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSTestSetLike>::call<jsTestSetLikePrototypeFunction_addBody>(*lexicalGlobalObject, *callFrame, "add");
}

static inline JSC::EncodedJSValue jsTestSetLikePrototypeFunction_clearBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSTestSetLike>::ClassParameter castedThis)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    UNUSED_PARAM(throwScope);
    UNUSED_PARAM(callFrame);
    RELEASE_AND_RETURN(throwScope, JSValue::encode(toJS<IDLAny>(*lexicalGlobalObject, throwScope, forwardClearToSetLike(*lexicalGlobalObject, *callFrame, *castedThis))));
}

JSC_DEFINE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_clear, (JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSTestSetLike>::call<jsTestSetLikePrototypeFunction_clearBody>(*lexicalGlobalObject, *callFrame, "clear");
}

static inline JSC::EncodedJSValue jsTestSetLikePrototypeFunction_deleteBody(JSC::JSGlobalObject* lexicalGlobalObject, JSC::CallFrame* callFrame, typename IDLOperation<JSTestSetLike>::ClassParameter castedThis)
{
    SUPPRESS_UNCOUNTED_LOCAL auto& vm = JSC::getVM(lexicalGlobalObject);
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    UNUSED_PARAM(throwScope);
    UNUSED_PARAM(callFrame);
    if (callFrame->argumentCount() < 1) [[unlikely]]
        return throwVMError(lexicalGlobalObject, throwScope, createNotEnoughArgumentsError(lexicalGlobalObject));
    EnsureStillAliveScope argument0 = callFrame->uncheckedArgument(0);
    auto keyConversionResult = convert<IDLDOMString>(*lexicalGlobalObject, argument0.value());
    if (keyConversionResult.hasException(throwScope)) [[unlikely]]
       return encodedJSValue();
    RELEASE_AND_RETURN(throwScope, JSValue::encode(toJS<IDLAny>(*lexicalGlobalObject, throwScope, forwardDeleteToSetLike(*lexicalGlobalObject, *callFrame, *castedThis, keyConversionResult.releaseReturnValue()))));
}

JSC_DEFINE_HOST_FUNCTION(jsTestSetLikePrototypeFunction_delete, (JSGlobalObject* lexicalGlobalObject, CallFrame* callFrame))
{
    return IDLOperation<JSTestSetLike>::call<jsTestSetLikePrototypeFunction_deleteBody>(*lexicalGlobalObject, *callFrame, "delete");
}

JSC::GCClient::IsoSubspace* JSTestSetLike::subspaceForImpl(JSC::VM& vm)
{
    return WebCore::subspaceForImpl<JSTestSetLike, UseCustomHeapCellType::No>(vm, "JSTestSetLike"_s,
        [] (auto& spaces) { return spaces.m_clientSubspaceForTestSetLike.get(); },
        [] (auto& spaces, auto&& space) { spaces.m_clientSubspaceForTestSetLike = std::forward<decltype(space)>(space); },
        [] (auto& spaces) { return spaces.m_subspaceForTestSetLike.get(); },
        [] (auto& spaces, auto&& space) { spaces.m_subspaceForTestSetLike = std::forward<decltype(space)>(space); }
    );
}

void JSTestSetLike::analyzeHeap(JSCell* cell, HeapAnalyzer& analyzer)
{
    auto* thisObject = jsCast<JSTestSetLike*>(cell);
    analyzer.setWrappedObjectForCell(cell, &thisObject->wrapped());
    if (RefPtr context = thisObject->scriptExecutionContext())
        analyzer.setLabelForCell(cell, makeString("url "_s, context->url().string()));
    Base::analyzeHeap(cell, analyzer);
}

bool JSTestSetLikeOwner::isReachableFromOpaqueRoots(JSC::Handle<JSC::Unknown> handle, void*, AbstractSlotVisitor& visitor, ASCIILiteral* reason)
{
    UNUSED_PARAM(handle);
    UNUSED_PARAM(visitor);
    UNUSED_PARAM(reason);
    return false;
}

void JSTestSetLikeOwner::finalize(JSC::Handle<JSC::Unknown> handle, void* context)
{
    auto* jsTestSetLike = static_cast<JSTestSetLike*>(handle.slot()->asCell());
    auto& world = *static_cast<DOMWrapperWorld*>(context);
    uncacheWrapper(world, jsTestSetLike->protectedWrapped().ptr(), jsTestSetLike);
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN
#if ENABLE(BINDING_INTEGRITY)
#if PLATFORM(WIN)
#pragma warning(disable: 4483)
extern "C" { extern void (*const __identifier("??_7TestSetLike@WebCore@@6B@")[])(); }
#else
extern "C" { extern void* _ZTVN7WebCore11TestSetLikeE[]; }
#endif
template<std::same_as<TestSetLike> T>
static inline void verifyVTable(TestSetLike* ptr) 
{
    if constexpr (std::is_polymorphic_v<T>) {
        const void* actualVTablePointer = getVTablePointer<T>(ptr);
#if PLATFORM(WIN)
        void* expectedVTablePointer = __identifier("??_7TestSetLike@WebCore@@6B@");
#else
        void* expectedVTablePointer = &_ZTVN7WebCore11TestSetLikeE[2];
#endif

        // If you hit this assertion you either have a use after free bug, or
        // TestSetLike has subclasses. If TestSetLike has subclasses that get passed
        // to toJS() we currently require TestSetLike you to opt out of binding hardening
        // by adding the SkipVTableValidation attribute to the interface IDL definition
        RELEASE_ASSERT(actualVTablePointer == expectedVTablePointer);
    }
}
#endif
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

JSC::JSValue toJSNewlyCreated(JSC::JSGlobalObject*, JSDOMGlobalObject* globalObject, Ref<TestSetLike>&& impl)
{
#if ENABLE(BINDING_INTEGRITY)
    verifyVTable<TestSetLike>(impl.ptr());
#endif
    return createWrapper<TestSetLike>(globalObject, WTFMove(impl));
}

JSC::JSValue toJS(JSC::JSGlobalObject* lexicalGlobalObject, JSDOMGlobalObject* globalObject, TestSetLike& impl)
{
    return wrap(lexicalGlobalObject, globalObject, impl);
}

TestSetLike* JSTestSetLike::toWrapped(JSC::VM&, JSC::JSValue value)
{
    if (auto* wrapper = jsDynamicCast<JSTestSetLike*>(value))
        return &wrapper->wrapped();
    return nullptr;
}

}
