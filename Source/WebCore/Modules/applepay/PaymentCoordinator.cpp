/*
 * Copyright (C) 2015-2020 Apple Inc. All rights reserved.
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
#include "PaymentCoordinator.h"

#if ENABLE(APPLE_PAY)

#include "ApplePayCouponCodeUpdate.h"
#include "ApplePayPaymentAuthorizationResult.h"
#include "ApplePayPaymentMethodUpdate.h"
#include "ApplePayShippingContactUpdate.h"
#include "ApplePayShippingMethod.h"
#include "ApplePayShippingMethodUpdate.h"
#include "Document.h"
#include "ExceptionDetails.h"
#include "LinkIconCollector.h"
#include "Logging.h"
#include "Page.h"
#include "PaymentCoordinatorClient.h"
#include "PaymentSession.h"
#include "UserContentProvider.h"
#include <JavaScriptCore/ConsoleTypes.h>
#include <wtf/CompletionHandler.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/URL.h>

#define PAYMENT_COORDINATOR_RELEASE_LOG_ERROR(fmt, ...) RELEASE_LOG_ERROR(ApplePay, "%p - PaymentCoordinator::" fmt, this, ##__VA_ARGS__)
#define PAYMENT_COORDINATOR_RELEASE_LOG(fmt, ...) RELEASE_LOG(ApplePay, "%p - PaymentCoordinator::" fmt, this, ##__VA_ARGS__)
#define PAYMENT_COORDINATOR_RELEASE_LOG_WITH_THIS(thisPointer, fmt, ...) RELEASE_LOG(ApplePay, "%p - PaymentCoordinator::" fmt, thisPointer, ##__VA_ARGS__)

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(PaymentCoordinator);

Ref<PaymentCoordinator> PaymentCoordinator::create(Ref<PaymentCoordinatorClient>&& client)
{
    return adoptRef(*new PaymentCoordinator(WTFMove(client)));
}

PaymentCoordinator::PaymentCoordinator(Ref<PaymentCoordinatorClient>&& client)
    : m_client(WTFMove(client))
{
}

PaymentCoordinator::~PaymentCoordinator() = default;

bool PaymentCoordinator::supportsVersion(Document&, unsigned version) const
{
    auto supportsVersion = m_client->supportsVersion(version);
    PAYMENT_COORDINATOR_RELEASE_LOG("supportsVersion(%d) -> %d", version, supportsVersion);
    return supportsVersion;
}

bool PaymentCoordinator::canMakePayments()
{
    auto canMakePayments = m_client->canMakePayments();
    PAYMENT_COORDINATOR_RELEASE_LOG("canMakePayments() -> %d", canMakePayments);
    return canMakePayments;
}

void PaymentCoordinator::canMakePaymentsWithActiveCard(Document& document, const String& merchantIdentifier, Function<void(bool)>&& completionHandler)
{
    m_client->canMakePaymentsWithActiveCard(merchantIdentifier, document.domain(), [weakThis = WeakPtr { *this }, document = WeakPtr<Document, WeakPtrImplWithEventTargetData> { document }, completionHandler = WTFMove(completionHandler)](bool canMakePayments) {
        if (!weakThis)
            return completionHandler(false);

        PAYMENT_COORDINATOR_RELEASE_LOG_WITH_THIS(weakThis.get(), "canMakePaymentsWithActiveCard() -> %d", canMakePayments);

        if (!canMakePayments)
            return completionHandler(false);

        if (!document)
            return completionHandler(false);

        completionHandler(true);
    });
}

void PaymentCoordinator::openPaymentSetup(Document& document, const String& merchantIdentifier, Function<void(bool)>&& completionHandler)
{
    PAYMENT_COORDINATOR_RELEASE_LOG("openPaymentSetup()");
    m_client->openPaymentSetup(merchantIdentifier, document.domain(), WTFMove(completionHandler));
}

bool PaymentCoordinator::beginPaymentSession(Document& document, PaymentSession& paymentSession, const ApplePaySessionPaymentRequest& paymentRequest)
{
    ASSERT(!m_activeSession);

    RefPtr page = document.page();
    if (!page)
        return false;

    auto linkIconURLs = LinkIconCollector { document }.iconsOfTypes({ LinkIconType::TouchIcon, LinkIconType::TouchPrecomposedIcon }).map([](auto& icon) {
        return icon.url;
    });
    auto showPaymentUI = m_client->showPaymentUI(page->mainFrameURL(), WTFMove(linkIconURLs), paymentRequest);
    PAYMENT_COORDINATOR_RELEASE_LOG("beginPaymentSession() -> %d", showPaymentUI);
    if (!showPaymentUI)
        return false;

#if ENABLE(APPLE_PAY_SHIPPING_CONTACT_EDITING_MODE)
    if (paymentRequest.shippingContactEditingMode() == ApplePayShippingContactEditingMode::Enabled)
        document.addConsoleMessage(MessageSource::PaymentRequest, MessageLevel::Warning, "`enabled` is a deprecated value for `shippingContactEditingMode`. Please use `available` instead."_s);
#endif

    m_activeSession = paymentSession;
    return true;
}

void PaymentCoordinator::completeMerchantValidation(const PaymentMerchantSession& paymentMerchantSession)
{
    ASSERT(m_activeSession);
    PAYMENT_COORDINATOR_RELEASE_LOG("completeMerchantValidation()");
    m_client->completeMerchantValidation(paymentMerchantSession);
}

void PaymentCoordinator::completeShippingMethodSelection(std::optional<ApplePayShippingMethodUpdate>&& update)
{
    ASSERT(m_activeSession);
    PAYMENT_COORDINATOR_RELEASE_LOG("completeShippingMethodSelection()");
    m_client->completeShippingMethodSelection(WTFMove(update));
}

void PaymentCoordinator::completeShippingContactSelection(std::optional<ApplePayShippingContactUpdate>&& update)
{
    ASSERT(m_activeSession);
    PAYMENT_COORDINATOR_RELEASE_LOG("completeShippingContactSelection()");
    m_client->completeShippingContactSelection(WTFMove(update));
}

void PaymentCoordinator::completePaymentMethodSelection(std::optional<ApplePayPaymentMethodUpdate>&& update)
{
    ASSERT(m_activeSession);
    PAYMENT_COORDINATOR_RELEASE_LOG("completePaymentMethodSelection()");
    m_client->completePaymentMethodSelection(WTFMove(update));
}

#if ENABLE(APPLE_PAY_COUPON_CODE)

void PaymentCoordinator::completeCouponCodeChange(std::optional<ApplePayCouponCodeUpdate>&& update)
{
    ASSERT(m_activeSession);
    PAYMENT_COORDINATOR_RELEASE_LOG("completeCouponCodeChange()");
    m_client->completeCouponCodeChange(WTFMove(update));
}

#endif // ENABLE(APPLE_PAY_COUPON_CODE)

void PaymentCoordinator::completePaymentSession(ApplePayPaymentAuthorizationResult&& result)
{
    ASSERT(m_activeSession);

    bool isFinalState = result.isFinalState();
    PAYMENT_COORDINATOR_RELEASE_LOG("completePaymentSession() (isFinalState: %d)", isFinalState);
    m_client->completePaymentSession(WTFMove(result));

    if (!isFinalState)
        return;

    m_activeSession = nullptr;
}

void PaymentCoordinator::abortPaymentSession()
{
    ASSERT(m_activeSession);
    PAYMENT_COORDINATOR_RELEASE_LOG("abortPaymentSession()");
    m_client->abortPaymentSession();
    m_activeSession = nullptr;
}

void PaymentCoordinator::cancelPaymentSession()
{
    ASSERT(m_activeSession);
    PAYMENT_COORDINATOR_RELEASE_LOG("cancelPaymentSession()");
    m_client->cancelPaymentSession();
}

void PaymentCoordinator::validateMerchant(URL&& validationURL)
{
    RefPtr activeSession = m_activeSession;
    if (!activeSession) {
        // It's possible that the payment has been aborted already.
        return;
    }

    PAYMENT_COORDINATOR_RELEASE_LOG("validateMerchant()");
    activeSession->validateMerchant(WTFMove(validationURL));
}

void PaymentCoordinator::didAuthorizePayment(const Payment& payment)
{
    RefPtr activeSession = m_activeSession;
    if (!activeSession) {
        // It's possible that the payment has been aborted already.
        return;
    }

    PAYMENT_COORDINATOR_RELEASE_LOG("didAuthorizePayment()");
    activeSession->didAuthorizePayment(payment);
}

void PaymentCoordinator::didSelectPaymentMethod(const PaymentMethod& paymentMethod)
{
    RefPtr activeSession = m_activeSession;
    if (!activeSession) {
        // It's possible that the payment has been aborted already.
        return;
    }

    PAYMENT_COORDINATOR_RELEASE_LOG("didSelectPaymentMethod()");
    activeSession->didSelectPaymentMethod(paymentMethod);
}

void PaymentCoordinator::didSelectShippingMethod(const ApplePayShippingMethod& shippingMethod)
{
    RefPtr activeSession = m_activeSession;
    if (!activeSession) {
        // It's possible that the payment has been aborted already.
        return;
    }

    PAYMENT_COORDINATOR_RELEASE_LOG("didSelectShippingMethod()");
    activeSession->didSelectShippingMethod(shippingMethod);
}

void PaymentCoordinator::didSelectShippingContact(const PaymentContact& shippingContact)
{
    RefPtr activeSession = m_activeSession;
    if (!activeSession) {
        // It's possible that the payment has been aborted already.
        return;
    }

    PAYMENT_COORDINATOR_RELEASE_LOG("didSelectShippingContact()");
    activeSession->didSelectShippingContact(shippingContact);
}

#if ENABLE(APPLE_PAY_COUPON_CODE)

void PaymentCoordinator::didChangeCouponCode(String&& couponCode)
{
    RefPtr activeSession = m_activeSession;
    if (!activeSession) {
        // It's possible that the payment has been aborted already.
        return;
    }

    PAYMENT_COORDINATOR_RELEASE_LOG("didChangeCouponCode()");
    activeSession->didChangeCouponCode(WTFMove(couponCode));
}

#endif // ENABLE(APPLE_PAY_COUPON_CODE)

void PaymentCoordinator::didCancelPaymentSession(PaymentSessionError&& error)
{
    RefPtr activeSession = m_activeSession;
    if (!activeSession) {
        // It's possible that the payment has been aborted already.
        return;
    }

    PAYMENT_COORDINATOR_RELEASE_LOG("didCancelPaymentSession()");
    activeSession->didCancelPaymentSession(WTFMove(error));
    m_activeSession = nullptr;
}

std::optional<String> PaymentCoordinator::validatedPaymentNetwork(Document&, unsigned version, const String& paymentNetwork) const
{
    if (version < 2 && equalLettersIgnoringASCIICase(paymentNetwork, "jcb"_s))
        return std::nullopt;

    if (version < 3 && equalIgnoringASCIICase(paymentNetwork, "carteBancaire"_s))
        return std::nullopt;

    return m_client->validatedPaymentNetwork(paymentNetwork);
}

void PaymentCoordinator::getSetupFeatures(const ApplePaySetupConfiguration& configuration, const URL& url, CompletionHandler<void(Vector<Ref<ApplePaySetupFeature>>&&)>&& completionHandler)
{
    PAYMENT_COORDINATOR_RELEASE_LOG("getSetupFeatures()");
    m_client->getSetupFeatures(configuration, url, [weakThis = WeakPtr { *this }, completionHandler = WTFMove(completionHandler)](Vector<Ref<ApplePaySetupFeature>>&& features) mutable {
        if (!weakThis)
            return;
        PAYMENT_COORDINATOR_RELEASE_LOG_WITH_THIS(weakThis.get(), "getSetupFeatures() completed (features: %zu)", features.size());
        completionHandler(WTFMove(features));
    });
}

void PaymentCoordinator::beginApplePaySetup(const ApplePaySetupConfiguration& configuration, const URL& url, Vector<Ref<ApplePaySetupFeature>>&& features, CompletionHandler<void(bool)>&& completionHandler)
{
    PAYMENT_COORDINATOR_RELEASE_LOG("beginApplePaySetup()");
    m_client->beginApplePaySetup(configuration, url, WTFMove(features), [weakThis = WeakPtr { *this }, completionHandler = WTFMove(completionHandler)](bool success) mutable {
        if (!weakThis)
            return;
        PAYMENT_COORDINATOR_RELEASE_LOG_WITH_THIS(weakThis.get(), "beginApplePaySetup() completed (success: %d)", success);
        completionHandler(success);
    });
}

void PaymentCoordinator::endApplePaySetup()
{
    PAYMENT_COORDINATOR_RELEASE_LOG("endApplePaySetup()");
    m_client->endApplePaySetup();
}

} // namespace WebCore

#undef PAYMENT_COORDINATOR_RELEASE_LOG_ERROR
#undef PAYMENT_COORDINATOR_RELEASE_LOG

#endif // ENABLE(APPLE_PAY)
