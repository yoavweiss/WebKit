/*
 * Copyright (C) 2017-2019 Apple Inc. All rights reserved.
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
#include "PaymentRequest.h"

#if ENABLE(PAYMENT_REQUEST)

#include "ApplePayPaymentHandler.h"
#include "Document.h"
#include "EventNames.h"
#include "EventTargetInlines.h"
#include "FrameDestructionObserverInlines.h"
#include "JSDOMPromise.h"
#include "JSDOMPromiseDeferred.h"
#include "JSPaymentDetailsUpdate.h"
#include "JSPaymentResponse.h"
#include "Page.h"
#include "PaymentAddress.h"
#include "PaymentCoordinator.h"
#include "PaymentCurrencyAmount.h"
#include "PaymentDetailsInit.h"
#include "PaymentHandler.h"
#include "PaymentMethodChangeEvent.h"
#include "PaymentMethodData.h"
#include "PaymentOptions.h"
#include "PaymentRequestUpdateEvent.h"
#include "PaymentRequestUtilities.h"
#include "PaymentValidationErrors.h"
#include "ScriptController.h"
#include <JavaScriptCore/ConsoleTypes.h>
#include <JavaScriptCore/JSONObject.h>
#include <JavaScriptCore/ThrowScope.h>
#include <wtf/ASCIICType.h>
#include <wtf/RunLoop.h>
#include <wtf/Scope.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/UUID.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(PaymentRequest);

// Implements the IsWellFormedCurrencyCode abstract operation from ECMA 402
// https://tc39.github.io/ecma402/#sec-iswellformedcurrencycode
static bool isWellFormedCurrencyCode(const String& currency)
{
    if (currency.length() == 3)
        return currency.containsOnly<isASCIIAlpha>();
    return false;
}

template<typename T>
static ExceptionOr<String> checkAndCanonicalizeData(ScriptExecutionContext& context, T& value)
{
    String serializedData;
    if (value.data) {
        auto* globalObject = context.globalObject();
        auto scope = DECLARE_THROW_SCOPE(globalObject->vm());
        serializedData = JSONStringify(globalObject, value.data.get(), 0);
        if (scope.exception())
            return Exception { ExceptionCode::ExistingExceptionError };
        value.data.clear();
    }
    return { WTFMove(serializedData) };
}

// Implements the "check and canonicalize amount" validity checker
// https://www.w3.org/TR/payment-request/#dfn-check-and-canonicalize-amount
static ExceptionOr<void> checkAndCanonicalizeAmount(PaymentCurrencyAmount& amount)
{
    if (!isWellFormedCurrencyCode(amount.currency))
        return Exception { ExceptionCode::RangeError, makeString('"', amount.currency, "\" is not a valid currency code."_s) };

    if (!isValidDecimalMonetaryValue(amount.value))
        return Exception { ExceptionCode::TypeError, makeString('"', amount.value, "\" is not a valid decimal monetary value."_s) };

    amount.currency = amount.currency.convertToASCIIUppercase();
    return { };
}

enum class NegativeAmountAllowed : bool { No, Yes };
static ExceptionOr<void> checkAndCanonicalizePaymentItem(PaymentItem& item, NegativeAmountAllowed negativeAmountAllowed)
{
    auto exception = checkAndCanonicalizeAmount(item.amount);
    if (exception.hasException())
        return exception.releaseException();

    if (negativeAmountAllowed == NegativeAmountAllowed::No && item.amount.value[0] == '-')
        return Exception { ExceptionCode::TypeError, "Total currency values cannot be negative."_s };

    return { };
}

// Implements the "check and canonicalize total" validity checker
// https://www.w3.org/TR/payment-request/#dfn-check-and-canonicalize-total
static ExceptionOr<void> checkAndCanonicalizeTotal(PaymentItem& total)
{
    return checkAndCanonicalizePaymentItem(total, NegativeAmountAllowed::No);
}

// Implements "validate a standardized payment method identifier"
// https://www.w3.org/TR/payment-method-id/#validity-0
static bool isValidStandardizedPaymentMethodIdentifier(StringView identifier)
{
    enum class State {
        Start,
        Hyphen,
        LowerAlpha,
        Digit,
    };

    auto state = State::Start;
    for (auto character : identifier.codeUnits()) {
        switch (state) {
        case State::Start:
        case State::Hyphen:
            if (isASCIILower(character)) {
                state = State::LowerAlpha;
                break;
            }

            return false;

        case State::LowerAlpha:
        case State::Digit:
            if (isASCIILower(character)) {
                state = State::LowerAlpha;
                break;
            }

            if (isASCIIDigit(character)) {
                state = State::Digit;
                break;
            }

            if (character == '-') {
                state = State::Hyphen;
                break;
            }

            return false;
        }
    }

    return state == State::LowerAlpha || state == State::Digit;
}

// Implements "validate a URL-based payment method identifier"
// https://www.w3.org/TR/payment-method-id/#validation
static bool isValidURLBasedPaymentMethodIdentifier(const URL& url)
{
    return url.protocolIs("https"_s) && !url.hasCredentials();
}

// Implements "validate a payment method identifier"
// https://www.w3.org/TR/payment-method-id/#validity
std::optional<PaymentRequest::MethodIdentifier> convertAndValidatePaymentMethodIdentifier(const String& identifier)
{
    URL url { identifier };
    if (!url.isValid()) {
        if (isValidStandardizedPaymentMethodIdentifier(identifier))
            return { identifier };
        return std::nullopt;
    }

    if (isValidURLBasedPaymentMethodIdentifier(url))
        return { WTFMove(url) };

    return std::nullopt;
}

enum class IsUpdate {
    No,
    Yes,
};

static ExceptionOr<std::tuple<String, Vector<String>>> checkAndCanonicalizeDetails(ScriptExecutionContext& context, PaymentDetailsBase& details, bool requestShipping, IsUpdate isUpdate)
{
    if (details.displayItems) {
        for (auto& item : *details.displayItems) {
            auto paymentItemResult = checkAndCanonicalizePaymentItem(item, NegativeAmountAllowed::Yes);
            if (paymentItemResult.hasException())
                return paymentItemResult.releaseException();
        }
    }

    String selectedShippingOption;
    if (requestShipping) {
        if (details.shippingOptions) {
            HashSet<String> seenShippingOptionIDs;
            bool didLog = false;
            for (auto& shippingOption : *details.shippingOptions) {
                auto exception = checkAndCanonicalizeAmount(shippingOption.amount);
                if (exception.hasException())
                    return exception.releaseException();

                auto addResult = seenShippingOptionIDs.add(shippingOption.id);
                if (!addResult.isNewEntry)
                    return Exception { ExceptionCode::TypeError, "Shipping option IDs must be unique."_s };

#if ENABLE(PAYMENT_REQUEST_SELECTED_SHIPPING_OPTION)
                if (shippingOption.selected)
                    selectedShippingOption = shippingOption.id;
                UNUSED_PARAM(context);
                UNUSED_VARIABLE(didLog);
#else
                if (!selectedShippingOption)
                    selectedShippingOption = shippingOption.id;
                else if (!didLog && shippingOption.selected) {
                    context.addConsoleMessage(JSC::MessageSource::PaymentRequest, JSC::MessageLevel::Warning, "WebKit currently uses the first shipping option even if other shipping options are marked as selected."_s);
                    didLog = true;
                }
#endif
            }
        } else if (isUpdate == IsUpdate::No)
            details.shippingOptions = { { } };
    } else if (isUpdate == IsUpdate::No)
        details.shippingOptions = std::nullopt;

    Vector<String> serializedModifierData;
    if (details.modifiers) {
        serializedModifierData.reserveInitialCapacity(details.modifiers->size());
        for (auto& modifier : *details.modifiers) {
            if (isUpdate == IsUpdate::Yes) {
                auto paymentMethodIdentifier = convertAndValidatePaymentMethodIdentifier(modifier.supportedMethods);
                if (!paymentMethodIdentifier)
                    return Exception { ExceptionCode::RangeError, makeString('"', modifier.supportedMethods, "\" is an invalid payment method identifier."_s) };
            }

            if (modifier.total) {
                auto totalResult = checkAndCanonicalizeTotal(*modifier.total);
                if (totalResult.hasException())
                    return totalResult.releaseException();
            }

            for (auto& item : modifier.additionalDisplayItems) {
                auto paymentItemResult = checkAndCanonicalizePaymentItem(item, NegativeAmountAllowed::Yes);
                if (paymentItemResult.hasException())
                    return paymentItemResult.releaseException();
            }

            String serializedData;
            if (modifier.data) {
                auto dataResult = checkAndCanonicalizeData(context, modifier);
                if (dataResult.hasException())
                    return dataResult.releaseException();

                serializedData = dataResult.releaseReturnValue();
            }
            serializedModifierData.append(WTFMove(serializedData));
        }
    } else if (isUpdate == IsUpdate::No)
        details.modifiers = { { } };

    return std::make_tuple(WTFMove(selectedShippingOption), WTFMove(serializedModifierData));
}

static ExceptionOr<JSC::JSValue> parse(ScriptExecutionContext& context, const String& string)
{
    auto scope = DECLARE_THROW_SCOPE(context.vm());
    JSC::JSValue data = JSONParse(context.globalObject(), string);
    if (scope.exception())
        return Exception { ExceptionCode::ExistingExceptionError };
    return data;
}

static String stringify(const PaymentRequest::MethodIdentifier& identifier)
{
    return WTF::switchOn(identifier,
        [] (const String& string) { return string; },
        [] (const URL& url) { return url.string(); }
    );
}

// Implements the PaymentRequest Constructor
// https://www.w3.org/TR/payment-request/#constructor
ExceptionOr<Ref<PaymentRequest>> PaymentRequest::create(Document& document, Vector<PaymentMethodData>&& methodData, PaymentDetailsInit&& details, PaymentOptions&& options)
{
    auto canCreateSession = PaymentHandler::canCreateSession(document);
    if (canCreateSession.hasException())
        return canCreateSession.releaseException();

    if (details.id.isNull())
        details.id = createVersion4UUIDString();

    if (methodData.isEmpty())
        return Exception { ExceptionCode::TypeError, "At least one payment method is required."_s };

    Vector<Method> serializedMethodData;
    serializedMethodData.reserveInitialCapacity(methodData.size());
    HashSet<String> seenMethodIDs;
    for (auto& paymentMethod : methodData) {
        auto identifier = convertAndValidatePaymentMethodIdentifier(paymentMethod.supportedMethods);
        if (!identifier)
            return Exception { ExceptionCode::RangeError, makeString('"', paymentMethod.supportedMethods, "\" is an invalid payment method identifier."_s) };

        if (!seenMethodIDs.add(stringify(*identifier)))
            return Exception { ExceptionCode::RangeError, "Payment method IDs must be unique."_s };

        String serializedData;
        if (paymentMethod.data) {
            auto scope = DECLARE_THROW_SCOPE(document.globalObject()->vm());
            serializedData = JSONStringify(document.globalObject(), paymentMethod.data.get(), 0);
            if (scope.exception())
                return Exception { ExceptionCode::ExistingExceptionError };
            
            auto parsedDataOrException = parse(document, serializedData);
            if (parsedDataOrException.hasException())
                return parsedDataOrException.releaseException();
            
            auto exception = PaymentHandler::validateData(document, parsedDataOrException.releaseReturnValue(), *identifier);
            if (exception.hasException())
                return exception.releaseException();
        }
        serializedMethodData.append({ WTFMove(*identifier), WTFMove(serializedData) });
    }

    auto totalResult = checkAndCanonicalizeTotal(details.total);
    if (totalResult.hasException())
        return totalResult.releaseException();

    auto detailsResult = checkAndCanonicalizeDetails(document, details, options.requestShipping, IsUpdate::No);
    if (detailsResult.hasException())
        return detailsResult.releaseException();

    auto shippingOptionAndModifierData = detailsResult.releaseReturnValue();
    auto request = adoptRef(*new PaymentRequest(document, WTFMove(options), WTFMove(details), WTFMove(std::get<1>(shippingOptionAndModifierData)), WTFMove(serializedMethodData), WTFMove(std::get<0>(shippingOptionAndModifierData))));
    request->suspendIfNeeded();
    return request;
}

PaymentRequest::PaymentRequest(Document& document, PaymentOptions&& options, PaymentDetailsInit&& details, Vector<String>&& serializedModifierData, Vector<Method>&& serializedMethodData, String&& selectedShippingOption)
    : ActiveDOMObject { document }
    , m_options { WTFMove(options) }
    , m_details { WTFMove(details) }
    , m_serializedModifierData { WTFMove(serializedModifierData) }
    , m_serializedMethodData { WTFMove(serializedMethodData) }
    , m_shippingOption { WTFMove(selectedShippingOption) }
{
}

PaymentRequest::~PaymentRequest()
{
    ASSERT(!hasPendingActivity() || isContextStopped());
    ASSERT(!m_activePaymentHandler);
}

// https://www.w3.org/TR/payment-request/#show-method
void PaymentRequest::show(Document& document, RefPtr<DOMPromise>&& detailsPromise, ShowPromise&& promise)
{
    RefPtr frame = document.frame();
    if (!frame) {
        promise.reject(Exception { ExceptionCode::AbortError });
        return;
    }

    RefPtr window = frame->window();
    if (!window || !window->consumeTransientActivation()) {
        promise.reject(Exception { ExceptionCode::SecurityError, "show() must be triggered by user activation."_s });
        return;
    }

    if (m_state != State::Created) {
        promise.reject(Exception { ExceptionCode::InvalidStateError });
        return;
    }

    if (PaymentHandler::hasActiveSession(document)) {
        promise.reject(Exception { ExceptionCode::AbortError });
        m_state = State::Closed;
        return;
    }

    m_state = State::Interactive;
    ASSERT(!m_showPromise);
    m_showPromise = makeUnique<ShowPromise>(WTFMove(promise));

    RefPtr<PaymentHandler> selectedPaymentHandler;
    for (auto& paymentMethod : m_serializedMethodData) {
        auto data = parse(document, paymentMethod.serializedData);
        if (data.hasException()) {
            settleShowPromise(data.releaseException());
            return;
        }

        auto handler = PaymentHandler::create(document, *this, paymentMethod.identifier);
        if (!handler)
            continue;

        auto result = handler->convertData(document, data.releaseReturnValue());
        if (result.hasException()) {
            settleShowPromise(result.releaseException());
            return;
        }

        if (!selectedPaymentHandler)
            selectedPaymentHandler = WTFMove(handler);
    }

    if (!selectedPaymentHandler) {
        settleShowPromise(Exception { ExceptionCode::NotSupportedError });
        return;
    }

    auto exception = selectedPaymentHandler->show(document);
    if (exception.hasException()) {
        settleShowPromise(exception.releaseException());
        return;
    }

    ASSERT(!m_activePaymentHandler);
    m_activePaymentHandler = PaymentHandlerWithPendingActivity { selectedPaymentHandler.releaseNonNull(), makePendingActivity(*this) };

    if (!detailsPromise)
        return;

    exception = updateWith(UpdateReason::ShowDetailsResolved, detailsPromise.releaseNonNull());
    ASSERT(!exception.hasException());
}

void PaymentRequest::abortWithException(Exception&& exception)
{
    // If state is "closed", then the request has already been aborted.
    if (m_state == State::Closed)
        return;

    ASSERT(m_state == State::Interactive);
    closeActivePaymentHandler();

    if (RefPtr response = m_response)
        response->abortWithException(WTFMove(exception));
    else
        settleShowPromise(WTFMove(exception));
}

RefPtr<PaymentHandler> PaymentRequest::protectedActivePaymentHandler()
{
    return activePaymentHandler();
}

void PaymentRequest::settleShowPromise(ExceptionOr<PaymentResponse&>&& result)
{
    if (auto showPromise = std::exchange(m_showPromise, nullptr))
        showPromise->settle(WTFMove(result));
}

void PaymentRequest::closeActivePaymentHandler()
{
    if (auto activePaymentHandler = std::exchange(m_activePaymentHandler, std::nullopt))
        Ref { activePaymentHandler->paymentHandler }->hide();

    m_isUpdating = false;
    m_state = State::Closed;
}

void PaymentRequest::stop()
{
    closeActivePaymentHandler();
    queueTaskKeepingObjectAlive(*this, TaskSource::Payment, [](auto& request) {
        request.settleShowPromise(Exception { ExceptionCode::AbortError });
    });
}

void PaymentRequest::suspend(ReasonForSuspension reason)
{
    if (reason != ReasonForSuspension::BackForwardCache)
        return;

    if (!m_activePaymentHandler) {
        ASSERT(!m_showPromise);
        ASSERT(m_state != State::Interactive);
        return;
    }

    stop();
}

RefPtr<ScriptExecutionContext> PaymentRequest::protectedScriptExecutionContext() const
{
    return scriptExecutionContext();
}

// https://www.w3.org/TR/payment-request/#abort()-method
void PaymentRequest::abort(AbortPromise&& promise)
{
    if (m_response && m_response->hasRetryPromise()) {
        promise.reject(Exception { ExceptionCode::InvalidStateError });
        return;
    }

    if (m_state != State::Interactive) {
        promise.reject(Exception { ExceptionCode::InvalidStateError });
        return;
    }

    if (m_activePaymentHandler && !protectedActivePaymentHandler()->canAbortSession()) {
        promise.reject(Exception { ExceptionCode::InvalidStateError });
        return;
    }

    abortWithException(Exception { ExceptionCode::AbortError });
    promise.resolve();
}

// https://www.w3.org/TR/payment-request/#canmakepayment()-method
void PaymentRequest::canMakePayment(Document& document, CanMakePaymentPromise&& promise)
{
    if (m_state != State::Created) {
        promise.reject(Exception { ExceptionCode::InvalidStateError });
        return;
    }

    for (auto& paymentMethod : m_serializedMethodData) {
        auto handler = PaymentHandler::create(document, *this, paymentMethod.identifier);
        if (!handler)
            continue;

        handler->canMakePayment(document, [promise = WTFMove(promise)](bool canMakePayment) mutable {
            promise.resolve(canMakePayment);
        });
        return;
    }

    promise.resolve(false);
}

const String& PaymentRequest::id() const
{
    return m_details.id;
}

std::optional<PaymentShippingType> PaymentRequest::shippingType() const
{
    if (m_options.requestShipping)
        return m_options.shippingType;
    return std::nullopt;
}

void PaymentRequest::shippingAddressChanged(Ref<PaymentAddress>&& shippingAddress)
{
    whenDetailsSettled([protectedThis = Ref { *this }, shippingAddress = WTFMove(shippingAddress)]() mutable {
        protectedThis->m_shippingAddress = WTFMove(shippingAddress);
        protectedThis->dispatchAndCheckUpdateEvent(PaymentRequestUpdateEvent::create(eventNames().shippingaddresschangeEvent));
    });
}

void PaymentRequest::shippingOptionChanged(const String& shippingOption)
{
    whenDetailsSettled([protectedThis = Ref { *this }, shippingOption]() mutable {
        protectedThis->m_shippingOption = shippingOption;
        protectedThis->dispatchAndCheckUpdateEvent(PaymentRequestUpdateEvent::create(eventNames().shippingoptionchangeEvent));
    });
}

void PaymentRequest::paymentMethodChanged(const String& methodName, PaymentMethodChangeEvent::MethodDetailsFunction&& methodDetailsFunction)
{
    whenDetailsSettled([protectedThis = Ref { *this }, methodName, methodDetailsFunction = WTFMove(methodDetailsFunction)]() mutable {
        auto& eventName = eventNames().paymentmethodchangeEvent;
        if (protectedThis->hasEventListeners(eventName)) {
            protectedThis->dispatchAndCheckUpdateEvent(PaymentMethodChangeEvent::create(eventName, methodName, WTFMove(methodDetailsFunction)));
            return;
        }

        Ref activePaymentHandler = *protectedThis->activePaymentHandler();
        activePaymentHandler->detailsUpdated(UpdateReason::PaymentMethodChanged, { }, { }, { }, { });
    });
}

ExceptionOr<void> PaymentRequest::updateWith(UpdateReason reason, Ref<DOMPromise>&& promise)
{
    if (m_state != State::Interactive)
        return Exception { ExceptionCode::InvalidStateError };

    if (m_isUpdating)
        return Exception { ExceptionCode::InvalidStateError };

    m_isUpdating = true;

    ASSERT(!m_detailsPromise);
    m_detailsPromise = promise.copyRef();
    promise->whenSettled([protectedThis = Ref { *this }, reason]() {
        protectedThis->settleDetailsPromise(reason);
    });

    return { };
}

ExceptionOr<void> PaymentRequest::completeMerchantValidation(Event& event, Ref<DOMPromise>&& merchantSessionPromise)
{
    if (m_state != State::Interactive)
        return Exception { ExceptionCode::InvalidStateError };

    event.stopPropagation();
    event.stopImmediatePropagation();

    m_merchantSessionPromise = merchantSessionPromise.copyRef();
    merchantSessionPromise->whenSettled([protectedThis = Ref { *this }]() {
        if (protectedThis->m_state != State::Interactive)
            return;

        RefPtr merchantSessionPromise = protectedThis->m_merchantSessionPromise;
        if (merchantSessionPromise->status() == DOMPromise::Status::Rejected) {
            protectedThis->abortWithException(Exception { ExceptionCode::AbortError });
            return;
        }

        Ref activePaymentHandler = *protectedThis->activePaymentHandler();
        auto exception = activePaymentHandler->merchantValidationCompleted(merchantSessionPromise->result());
        if (exception.hasException()) {
            protectedThis->abortWithException(exception.releaseException());
            return;
        }
    });

    return { };
}

void PaymentRequest::dispatchAndCheckUpdateEvent(Ref<PaymentRequestUpdateEvent>&& event)
{
    dispatchEvent(event);

    if (event->didCallUpdateWith())
        return;

    protectedScriptExecutionContext()->addConsoleMessage(JSC::MessageSource::PaymentRequest, JSC::MessageLevel::Warning, makeString("updateWith() should be called synchronously when handling \""_s, event->type(), "\"."_s));
}

void PaymentRequest::settleDetailsPromise(UpdateReason reason)
{
    auto scopeExit = makeScopeExit([&] {
        m_isUpdating = false;
        m_isCancelPending = false;
        m_detailsPromise = nullptr;
    });

    if (m_state != State::Interactive)
        return;

    RefPtr detailsPromise = m_detailsPromise;
    if (m_isCancelPending || detailsPromise->status() == DOMPromise::Status::Rejected) {
        abortWithException(Exception { ExceptionCode::AbortError });
        return;
    }

    Ref activePaymentHandler = *this->activePaymentHandler();

    Ref context = *detailsPromise->scriptExecutionContext();
    auto throwScope = DECLARE_THROW_SCOPE(context->vm());
    auto detailsUpdateConversion = convertDictionary<PaymentDetailsUpdate>(*context->globalObject(), detailsPromise->result());
    if (detailsUpdateConversion.hasException(throwScope)) {
        abortWithException(Exception { ExceptionCode::ExistingExceptionError });
        return;
    }
    auto detailsUpdate = detailsUpdateConversion.releaseReturnValue();

    if (detailsUpdate.total) {
        auto totalResult = checkAndCanonicalizeTotal(*detailsUpdate.total);
        if (totalResult.hasException()) {
            abortWithException(totalResult.releaseException());
            return;
        }
    }

    auto detailsResult = checkAndCanonicalizeDetails(context.get(), detailsUpdate, m_options.requestShipping, IsUpdate::Yes);
    if (detailsResult.hasException()) {
        abortWithException(detailsResult.releaseException());
        return;
    }

    auto shippingOptionAndModifierData = detailsResult.releaseReturnValue();

    if (detailsUpdate.total)
        m_details.total = WTFMove(*detailsUpdate.total);
    if (detailsUpdate.displayItems)
        m_details.displayItems = WTFMove(*detailsUpdate.displayItems);
    if (detailsUpdate.shippingOptions && m_options.requestShipping) {
        m_details.shippingOptions = WTFMove(detailsUpdate.shippingOptions);
        m_shippingOption = WTFMove(std::get<0>(shippingOptionAndModifierData));
    }
    if (detailsUpdate.modifiers) {
        m_details.modifiers = WTFMove(*detailsUpdate.modifiers);
        m_serializedModifierData = WTFMove(std::get<1>(shippingOptionAndModifierData));
    }

    auto result = activePaymentHandler->detailsUpdated(reason, WTFMove(detailsUpdate.error), WTFMove(detailsUpdate.shippingAddressErrors), WTFMove(detailsUpdate.payerErrors), detailsUpdate.paymentMethodErrors.get());
    if (result.hasException()) {
        abortWithException(result.releaseException());
        return;
    }
}

void PaymentRequest::whenDetailsSettled(std::function<void()>&& callback)
{
    auto whenSettledFunction = [protectedThis = Ref { *this }, callback = WTFMove(callback)] {
        ASSERT(protectedThis->m_state == State::Interactive);
        ASSERT(!protectedThis->m_isUpdating);
        ASSERT(!protectedThis->m_isCancelPending);
        ASSERT_UNUSED(protectedThis, protectedThis.ptr());
        callback();
    };

    RefPtr detailsPromise = m_detailsPromise;
    if (!detailsPromise) {
        whenSettledFunction();
        return;
    }

    detailsPromise->whenSettled([protectedThis = Ref { *this }, whenSettledFunction = WTFMove(whenSettledFunction)] {
        if (protectedThis->m_state == State::Interactive)
            whenSettledFunction();
    });
}

void PaymentRequest::accept(const String& methodName, PaymentResponse::DetailsFunction&& detailsFunction)
{
    ASSERT(!m_isUpdating);
    ASSERT(m_state == State::Interactive);

    RefPtr response = m_response;
    bool isRetry = m_response;
    if (!isRetry) {
        response = PaymentResponse::create(protectedScriptExecutionContext().get(), *this);
        m_response = response.copyRef();
        response->setRequestId(m_details.id);
    }

    response->setMethodName(methodName);
    response->setDetailsFunction(WTFMove(detailsFunction));
    response->setShippingAddress(nullptr);
    response->setShippingOption(nullString());
    response->setPayerName(nullString());
    response->setPayerEmail(nullString());
    response->setPayerPhone(nullString());

    if (!isRetry)
        settleShowPromise(*response);
    else {
        ASSERT(response->hasRetryPromise());
        response->settleRetryPromise();
    }

    m_state = State::Closed;
}

void PaymentRequest::accept(const String& methodName, PaymentResponse::DetailsFunction&& detailsFunction, Ref<PaymentAddress>&& shippingAddress, const String& payerName, const String& payerEmail, const String& payerPhone)
{
    ASSERT(!m_isUpdating);
    ASSERT(m_state == State::Interactive);

    RefPtr response = m_response;
    bool isRetry = m_response;
    if (!isRetry) {
        response = PaymentResponse::create(protectedScriptExecutionContext().get(), *this);
        m_response = response.copyRef();
        response->setRequestId(m_details.id);
    }

    response->setMethodName(methodName);
    response->setDetailsFunction(WTFMove(detailsFunction));
    response->setShippingAddress(m_options.requestShipping ? shippingAddress.ptr() : nullptr);
    response->setShippingOption(m_options.requestShipping ? m_shippingOption : String { });
    response->setPayerName(m_options.requestPayerName ? payerName : String { });
    response->setPayerEmail(m_options.requestPayerEmail ? payerEmail : String { });
    response->setPayerPhone(m_options.requestPayerPhone ? payerPhone : String { });

    if (!isRetry)
        settleShowPromise(*response);
    else {
        ASSERT(response->hasRetryPromise());
        response->settleRetryPromise();
    }

    m_state = State::Closed;
}

void PaymentRequest::reject(Exception&& exception)
{
    abortWithException(WTFMove(exception));
}

ExceptionOr<void> PaymentRequest::complete(Document& document, std::optional<PaymentComplete>&& result, String&& serializedData)
{
    ASSERT(m_state == State::Closed);
    if (!m_activePaymentHandler)
        return Exception { ExceptionCode::AbortError };

    Ref activePaymentHandler = *this->activePaymentHandler();
    auto exception = activePaymentHandler->complete(document, WTFMove(result), WTFMove(serializedData));
    if (exception.hasException())
        return exception.releaseException();

    m_activePaymentHandler = std::nullopt;
    return { };
}

ExceptionOr<void> PaymentRequest::retry(PaymentValidationErrors&& errors)
{
    ASSERT(m_state == State::Closed);
    if (!m_activePaymentHandler)
        return Exception { ExceptionCode::AbortError };

    m_state = State::Interactive;

    Ref activePaymentHandler = *this->activePaymentHandler();
    return activePaymentHandler->retry(WTFMove(errors));
}

void PaymentRequest::cancel()
{
    m_activePaymentHandler = std::nullopt;

    if (m_isUpdating) {
        m_isCancelPending = true;
        protectedScriptExecutionContext()->addConsoleMessage(JSC::MessageSource::PaymentRequest, JSC::MessageLevel::Error, "payment request timed out while waiting for Promise given to show() or updateWith() to settle."_s);
        return;
    }

    abortWithException(Exception { ExceptionCode::AbortError });
}

} // namespace WebCore

#endif // ENABLE(PAYMENT_REQUEST)
