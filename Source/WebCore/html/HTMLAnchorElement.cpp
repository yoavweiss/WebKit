/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2000 Simon Hausmann <hausmann@kde.org>
 * Copyright (C) 2003-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *           (C) 2006 Graham Dennis (graham.dennis@gmail.com)
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
#include "HTMLAnchorElement.h"

#include "Chrome.h"
#include "ChromeClient.h"
#include "ContainerNodeInlines.h"
#include "DOMTokenList.h"
#include "ElementAncestorIteratorInlines.h"
#include "EventHandler.h"
#include "EventNames.h"
#include "FrameLoader.h"
#include "FrameLoaderTypes.h"
#include "FrameSelection.h"
#include "HTMLCanvasElement.h"
#include "HTMLImageElement.h"
#include "HTMLParserIdioms.h"
#include "HTMLPictureElement.h"
#include "KeyboardEvent.h"
#include "LoaderStrategy.h"
#include "LocalFrame.h"
#include "LocalFrameLoaderClient.h"
#include "MouseEvent.h"
#include "OriginAccessPatterns.h"
#include "PingLoader.h"
#include "PlatformStrategies.h"
#include "PrivateClickMeasurement.h"
#include "RegistrableDomain.h"
#include "RenderImage.h"
#include "ResourceRequest.h"
#include "SVGImage.h"
#include "ScriptController.h"
#include "SecurityOrigin.h"
#include "SecurityPolicy.h"
#include "Settings.h"
#include "SystemPreviewInfo.h"
#include "URLKeepingBlobAlive.h"
#include "UserGestureIndicator.h"
#include <JavaScriptCore/ConsoleTypes.h>
#include <wtf/RuntimeApplicationChecks.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/WeakHashMap.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>

#if PLATFORM(COCOA)
#include "DataDetection.h"
#endif

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(HTMLAnchorElement);

using namespace HTMLNames;

HTMLAnchorElement::HTMLAnchorElement(const QualifiedName& tagName, Document& document)
    : HTMLElement(tagName, document)
{
}

Ref<HTMLAnchorElement> HTMLAnchorElement::create(Document& document)
{
    return adoptRef(*new HTMLAnchorElement(aTag, document));
}

Ref<HTMLAnchorElement> HTMLAnchorElement::create(const QualifiedName& tagName, Document& document)
{
    return adoptRef(*new HTMLAnchorElement(tagName, document));
}

HTMLAnchorElement::~HTMLAnchorElement() = default;

bool HTMLAnchorElement::supportsFocus() const
{
    if (hasEditableStyle())
        return HTMLElement::supportsFocus();
    // If not a link we should still be able to focus the element if it has tabIndex.
    return isLink() || HTMLElement::supportsFocus();
}

bool HTMLAnchorElement::isMouseFocusable() const
{
#if !(PLATFORM(GTK) || PLATFORM(WPE))
    // Only allow links with tabIndex or contentEditable to be mouse focusable.
    if (isLink())
        return HTMLElement::supportsFocus();
#endif

    return HTMLElement::isMouseFocusable();
}

bool HTMLAnchorElement::isInteractiveContent() const
{
    return isLink();
}

bool HTMLAnchorElement::isKeyboardFocusable(const FocusEventData& focusEventData) const
{
    if (!isFocusable())
        return false;

    // Anchor is focusable if the base element supports focus and is focusable.
    if (isFocusable() && Element::supportsFocus())
        return HTMLElement::isKeyboardFocusable(focusEventData);

    RefPtr frame = document().frame();
    if (!frame)
        return false;

    if (isLink() && !frame->eventHandler().tabsToLinks(focusEventData))
        return false;
    return HTMLElement::isKeyboardFocusable(focusEventData);
}

static void appendServerMapMousePosition(StringBuilder& url, Event& event)
{
    auto* mouseEvent = dynamicDowncast<MouseEvent>(event);
    if (!mouseEvent)
        return;

    RefPtr imageElement = dynamicDowncast<HTMLImageElement>(mouseEvent->target());
    if (!imageElement)
        return;

    if (!imageElement->isServerMap())
        return;

    CheckedPtr renderer = dynamicDowncast<RenderImage>(imageElement->renderer());
    if (!renderer)
        return;

    // FIXME: This should probably pass UseTransforms in the OptionSet<MapCoordinatesMode>.
    auto absolutePosition = renderer->absoluteToLocal(FloatPoint(mouseEvent->pageX(), mouseEvent->pageY()));
    url.append('?', std::lround(absolutePosition.x()), ',', std::lround(absolutePosition.y()));
}

void HTMLAnchorElement::defaultEventHandler(Event& event)
{
    if (isLink()) {
        if (focused() && isEnterKeyKeydownEvent(event) && treatLinkAsLiveForEventType(NonMouseEvent)) {
            event.setDefaultHandled();
            dispatchSimulatedClick(&event);
            return;
        }

        if (MouseEvent::canTriggerActivationBehavior(event) && treatLinkAsLiveForEventType(eventType(event))) {
            handleClick(event);
            return;
        }

        if (hasEditableStyle()) {
            // This keeps track of the editable block that the selection was in (if it was in one) just before the link was clicked
            // for the LiveWhenNotFocused editable link behavior
            auto& eventNames = WebCore::eventNames();
            if (auto* mouseEvent = dynamicDowncast<MouseEvent>(event); event.type() == eventNames.mousedownEvent && mouseEvent && mouseEvent->button() != MouseButton::Right && document().frame()) {
                setRootEditableElementForSelectionOnMouseDown(document().frame()->selection().selection().rootEditableElement());
                m_wasShiftKeyDownOnMouseDown = mouseEvent->shiftKey();
            } else if (event.type() == eventNames.mouseoverEvent) {
                // These are cleared on mouseover and not mouseout because their values are needed for drag events,
                // but drag events happen after mouse out events.
                clearRootEditableElementForSelectionOnMouseDown();
                m_wasShiftKeyDownOnMouseDown = false;
            }
        }
    }

    HTMLElement::defaultEventHandler(event);
}

void HTMLAnchorElement::setActive(bool down, Style::InvalidationScope invalidationScope)
{
    if (down && hasEditableStyle()) {
        switch (document().settings().editableLinkBehavior()) {
        case EditableLinkBehavior::Default:
        case EditableLinkBehavior::AlwaysLive:
            break;

        // Don't set the link to be active if the current selection is in the same editable block as this link.
        case EditableLinkBehavior::LiveWhenNotFocused:
            if (down && document().frame() && document().frame()->selection().selection().rootEditableElement() == rootEditableElement())
                return;
            break;
        
        case EditableLinkBehavior::NeverLive:
        case EditableLinkBehavior::OnlyLiveWithShiftKey:
            return;

        }
    }
    
    HTMLElement::setActive(down, invalidationScope);
}

void HTMLAnchorElement::attributeChanged(const QualifiedName& name, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason attributeModificationReason)
{
    HTMLElement::attributeChanged(name, oldValue, newValue, attributeModificationReason);

    if (name == hrefAttr)
        setIsLink(!newValue.isNull() && !shouldProhibitLinks(this));
    else if (name == relAttr) {
        // Update HTMLAnchorElement::relList() if more rel attributes values are supported.
        static MainThreadNeverDestroyed<const AtomString> noReferrer("noreferrer"_s);
        static MainThreadNeverDestroyed<const AtomString> noOpener("noopener"_s);
        static MainThreadNeverDestroyed<const AtomString> opener("opener"_s);
        SpaceSplitString relValue(newValue, SpaceSplitString::ShouldFoldCase::Yes);
        if (relValue.contains(noReferrer))
            m_linkRelations.add(Relation::NoReferrer);
        if (relValue.contains(noOpener))
            m_linkRelations.add(Relation::NoOpener);
        if (relValue.contains(opener))
            m_linkRelations.add(Relation::Opener);
        if (m_relList)
            m_relList->associatedAttributeValueChanged();
    } else if (name == nameAttr)
        protectedDocument()->processInternalResourceLinks(this);
}

bool HTMLAnchorElement::isURLAttribute(const Attribute& attribute) const
{
    return attribute.name().localName() == hrefAttr || HTMLElement::isURLAttribute(attribute);
}

bool HTMLAnchorElement::canStartSelection() const
{
    if (!isLink())
        return HTMLElement::canStartSelection();
    return hasEditableStyle();
}

bool HTMLAnchorElement::draggable() const
{
    const AtomString& value = attributeWithoutSynchronization(draggableAttr);
    if (equalLettersIgnoringASCIICase(value, "true"_s))
        return true;
    if (equalLettersIgnoringASCIICase(value, "false"_s))
        return false;
    return hasAttributeWithoutSynchronization(hrefAttr);
}

URL HTMLAnchorElement::href() const
{
    return protectedDocument()->completeURL(attributeWithoutSynchronization(hrefAttr));
}

bool HTMLAnchorElement::hasRel(Relation relation) const
{
    return m_linkRelations.contains(relation);
}

DOMTokenList& HTMLAnchorElement::relList()
{
    if (!m_relList) {
        lazyInitialize(m_relList, makeUniqueWithoutRefCountedCheck<DOMTokenList>(*this, HTMLNames::relAttr, [](Document& document, StringView token) {
#if USE(SYSTEM_PREVIEW)
            if (equalLettersIgnoringASCIICase(token, "ar"_s))
                return document.settings().systemPreviewEnabled();
#else
            UNUSED_PARAM(document);
#endif
            return equalLettersIgnoringASCIICase(token, "noreferrer"_s) || equalLettersIgnoringASCIICase(token, "noopener"_s) || equalLettersIgnoringASCIICase(token, "opener"_s);
        }));
    }
    return *m_relList;
}

const AtomString& HTMLAnchorElement::name() const
{
    return getNameAttribute();
}

int HTMLAnchorElement::defaultTabIndex() const
{
    return 0;
}

AtomString HTMLAnchorElement::target() const
{
    return attributeWithoutSynchronization(targetAttr);
}

String HTMLAnchorElement::origin() const
{
    auto url = href();
    if (!url.isValid())
        return emptyString();
    return SecurityOrigin::create(url)->toString();
}

void HTMLAnchorElement::setProtocol(StringView value)
{
    if (auto url = href(); !url.isValid())
        return;
    URLDecomposition::setProtocol(value);
}

String HTMLAnchorElement::text()
{
    return textContent();
}

void HTMLAnchorElement::setText(String&& text)
{
    setTextContent(WTFMove(text));
}

bool HTMLAnchorElement::isLiveLink() const
{
    return isLink() && treatLinkAsLiveForEventType(m_wasShiftKeyDownOnMouseDown ? MouseEventWithShiftKey : MouseEventWithoutShiftKey);
}

void HTMLAnchorElement::sendPings(const URL& destinationURL)
{
    if (!document().frame())
        return;

    const auto& pingValue = attributeWithoutSynchronization(pingAttr);
    if (pingValue.isNull())
        return;

    SpaceSplitString pingURLs(pingValue, SpaceSplitString::ShouldFoldCase::No);
    for (auto& pingURL : pingURLs)
        PingLoader::sendPing(*document().frame(), document().completeURL(pingURL), destinationURL);
}

#if USE(SYSTEM_PREVIEW)
bool HTMLAnchorElement::isSystemPreviewLink()
{
    if (!document().settings().systemPreviewEnabled())
        return false;

    static MainThreadNeverDestroyed<const AtomString> systemPreviewRelValue("ar"_s);

    if (!relList().contains(systemPreviewRelValue))
        return false;

    if (auto* child = firstElementChild()) {
        if (is<HTMLImageElement>(child) || is<HTMLPictureElement>(child)) {
            auto numChildren = childElementCount();
            // FIXME: We've documented that it should be the only child, but some early demos have two children.
            return numChildren == 1 || numChildren == 2;
        }
    }

    return false;
}
#endif

std::optional<URL> HTMLAnchorElement::attributionDestinationURLForPCM() const
{
    URL destinationURL { attributeWithoutSynchronization(attributiondestinationAttr) };
    if (destinationURL.isValid() && destinationURL.protocolIsInHTTPFamily())
        return destinationURL;

    protectedDocument()->addConsoleMessage(MessageSource::Other, MessageLevel::Warning, "attributiondestination could not be converted to a valid HTTP-family URL."_s);
    return std::nullopt;
}

std::optional<RegistrableDomain> HTMLAnchorElement::mainDocumentRegistrableDomainForPCM() const
{
    Ref document = this->document();
    if (RefPtr page = document->page()) {
        if (auto mainFrameURL = page->mainFrameURL(); !mainFrameURL.isEmpty())
            return RegistrableDomain(mainFrameURL);
    }

    document->addConsoleMessage(MessageSource::Other, MessageLevel::Warning, "Could not find a main document to use as source site for Private Click Measurement."_s);
    return std::nullopt;
}

std::optional<PCM::EphemeralNonce> HTMLAnchorElement::attributionSourceNonceForPCM() const
{
    auto attributionSourceNonceAttr = attributeWithoutSynchronization(attributionsourcenonceAttr);
    if (attributionSourceNonceAttr.isEmpty())
        return std::nullopt;

    auto ephemeralNonce = PCM::EphemeralNonce { attributionSourceNonceAttr };
    if (!ephemeralNonce.isValid()) {
        protectedDocument()->addConsoleMessage(MessageSource::Other, MessageLevel::Warning, "attributionsourcenonce was not valid."_s);
        return std::nullopt;
    }

    return ephemeralNonce;
}

std::optional<PrivateClickMeasurement> HTMLAnchorElement::parsePrivateClickMeasurementForSKAdNetwork(const URL& hrefURL) const
{
    if (!document().settings().sKAttributionEnabled())
        return std::nullopt;

    using SourceID = PrivateClickMeasurement::SourceID;
    using SourceSite = PCM::SourceSite;
    using AttributionDestinationSite = PCM::AttributionDestinationSite;

    auto adamID = PrivateClickMeasurement::appStoreURLAdamID(hrefURL);
    if (!adamID)
        return std::nullopt;

    auto attributionDestinationDomain = attributionDestinationURLForPCM();
    if (!attributionDestinationDomain)
        return std::nullopt;

    auto mainDocumentRegistrableDomain = mainDocumentRegistrableDomainForPCM();
    if (!mainDocumentRegistrableDomain)
        return std::nullopt;

    auto attributionSourceNonce = attributionSourceNonceForPCM();
    if (!attributionSourceNonce)
        return std::nullopt;

#if PLATFORM(COCOA)
    auto bundleID = applicationBundleIdentifier();
#else
    String bundleID;
#endif

    auto privateClickMeasurement = PrivateClickMeasurement {
        SourceID(0),
        SourceSite(WTFMove(*mainDocumentRegistrableDomain)),
        AttributionDestinationSite(*attributionDestinationDomain),
        bundleID,
        WallTime::now(),
        PCM::AttributionEphemeral::No
    };
    privateClickMeasurement.setEphemeralSourceNonce(WTFMove(*attributionSourceNonce));
    privateClickMeasurement.setAdamID(*adamID);
    return privateClickMeasurement;
}

std::optional<PrivateClickMeasurement> HTMLAnchorElement::parsePrivateClickMeasurement(const URL& hrefURL) const
{
    using SourceID = PrivateClickMeasurement::SourceID;
    using SourceSite = PCM::SourceSite;
    using AttributionDestinationSite = PCM::AttributionDestinationSite;

    RefPtr page = document().page();
    if (!page || !document().settings().privateClickMeasurementEnabled() || !UserGestureIndicator::processingUserGesture())
        return std::nullopt;

    if (auto pcm = parsePrivateClickMeasurementForSKAdNetwork(hrefURL))
        return pcm;

    auto hasAttributionSourceIDAttr = hasAttributeWithoutSynchronization(attributionsourceidAttr);
    auto hasAttributionDestinationAttr = hasAttributeWithoutSynchronization(attributiondestinationAttr);
    if (!hasAttributionSourceIDAttr && !hasAttributionDestinationAttr)
        return std::nullopt;

    auto attributionSourceIDAttr = attributeWithoutSynchronization(attributionsourceidAttr);
    auto attributionDestinationAttr = attributeWithoutSynchronization(attributiondestinationAttr);

    if (!hasAttributionSourceIDAttr || !hasAttributionDestinationAttr || attributionSourceIDAttr.isEmpty() || attributionDestinationAttr.isEmpty()) {
        protectedDocument()->addConsoleMessage(MessageSource::Other, MessageLevel::Warning, "Both attributionsourceid and attributiondestination need to be set for Private Click Measurement to work."_s);
        return std::nullopt;
    }

    auto attributionSourceID = parseHTMLNonNegativeInteger(attributionSourceIDAttr);
    if (!attributionSourceID) {
        protectedDocument()->addConsoleMessage(MessageSource::Other, MessageLevel::Warning, "attributionsourceid is not a non-negative integer which is required for Private Click Measurement."_s);
        return std::nullopt;
    }
    
    if (attributionSourceID.value() > std::numeric_limits<uint8_t>::max()) {
        protectedDocument()->addConsoleMessage(MessageSource::Other, MessageLevel::Warning, makeString("attributionsourceid must have a non-negative value less than or equal to "_s, std::numeric_limits<uint8_t>::max(), " for Private Click Measurement."_s));
        return std::nullopt;
    }

    URL destinationURL { attributionDestinationAttr };
    if (!destinationURL.isValid() || !destinationURL.protocolIsInHTTPFamily()) {
        protectedDocument()->addConsoleMessage(MessageSource::Other, MessageLevel::Warning, "attributiondestination could not be converted to a valid HTTP-family URL."_s);
        return std::nullopt;
    }

    auto& mainURL = page->mainFrameURL();
    if (mainURL.isEmpty()) {
        protectedDocument()->addConsoleMessage(MessageSource::Other, MessageLevel::Warning, "Could not find a main document to use as source site for Private Click Measurement."_s);
        return std::nullopt;
    }

    RegistrableDomain mainDocumentRegistrableDomain = RegistrableDomain { mainURL };
    if (mainDocumentRegistrableDomain.matches(destinationURL)) {
        protectedDocument()->addConsoleMessage(MessageSource::Other, MessageLevel::Warning, "attributiondestination can not be the same site as the current website."_s);
        return std::nullopt;
    }

#if PLATFORM(COCOA)
    auto bundleID = applicationBundleIdentifier();
#else
    String bundleID;
#endif

    PrivateClickMeasurement privateClickMeasurement {
        SourceID(attributionSourceID.value()),
        SourceSite(WTFMove(mainDocumentRegistrableDomain)),
        AttributionDestinationSite(destinationURL),
        bundleID,
        WallTime::now(),
        page->sessionID().isEphemeral() ? PCM::AttributionEphemeral::Yes : PCM::AttributionEphemeral::No
    };

    if (auto ephemeralNonce = attributionSourceNonceForPCM())
        privateClickMeasurement.setEphemeralSourceNonce(WTFMove(*ephemeralNonce));

    return privateClickMeasurement;
}

void HTMLAnchorElement::handleClick(Event& event)
{
    event.setDefaultHandled();

    Ref document = this->document();
    RefPtr frame { document->frame() };
    if (!frame)
        return;

    if (!hasTagName(aTag) && !isConnected())
        return;

    StringBuilder url;
    url.append(attributeWithoutSynchronization(hrefAttr).string().trim(isASCIIWhitespace));
    appendServerMapMousePosition(url, event);
    URL completedURL = document->completeURL(url.toString());

#if ENABLE(DATA_DETECTION) && PLATFORM(IOS_FAMILY)
    if (DataDetection::canPresentDataDetectorsUIForElement(*this)) {
        if (RefPtr page = document->page()) {
            if (page->chrome().client().showDataDetectorsUIForElement(*this, event))
                return;
        }
    }
#endif

    AtomString downloadAttribute;
    if (document->settings().downloadAttributeEnabled()) {
        // Ignore the download attribute completely if the href URL is cross origin.
        bool isSameOrigin = completedURL.protocolIsData() || document->protectedSecurityOrigin()->canRequest(completedURL, OriginAccessPatternsForWebProcess::singleton());
        if (isSameOrigin)
            downloadAttribute = AtomString { ResourceResponse::sanitizeSuggestedFilename(attributeWithoutSynchronization(downloadAttr)) };
        else if (hasAttributeWithoutSynchronization(downloadAttr))
            protectedDocument()->addConsoleMessage(MessageSource::Security, MessageLevel::Warning, "The download attribute on anchor was ignored because its href URL has a different security origin."_s);
    }

    SystemPreviewInfo systemPreviewInfo;
#if USE(SYSTEM_PREVIEW)
    systemPreviewInfo.isPreview = isSystemPreviewLink() && document->settings().systemPreviewEnabled();

    if (systemPreviewInfo.isPreview) {
        systemPreviewInfo.element.nodeIdentifier = nodeIdentifier();
        systemPreviewInfo.element.documentIdentifier = document->identifier();
        systemPreviewInfo.element.webPageIdentifier = document->pageID();
        if (auto* child = firstElementChild())
            systemPreviewInfo.previewRect = child->boundsInRootViewSpace();

        if (RefPtr page = document->page())
            page->beginSystemPreview(completedURL, document->topOrigin().data(), WTFMove(systemPreviewInfo), [keepBlobAlive = URLKeepingBlobAlive(completedURL, document->topOrigin().data())] { });
        return;
    }
#endif

    auto referrerPolicy = hasRel(Relation::NoReferrer) ? ReferrerPolicy::NoReferrer : this->referrerPolicy();

    auto effectiveTarget = this->effectiveTarget();
    NewFrameOpenerPolicy newFrameOpenerPolicy = NewFrameOpenerPolicy::Allow;
    if (hasRel(Relation::NoOpener) || hasRel(Relation::NoReferrer) || (!hasRel(Relation::Opener) && isBlankTargetFrameName(effectiveTarget) && !completedURL.protocolIsJavaScript()))
        newFrameOpenerPolicy = NewFrameOpenerPolicy::Suppress;

    auto privateClickMeasurement = parsePrivateClickMeasurement(completedURL);
    // A matching triggering event needs to happen before an attribution report can be sent.
    // Thus, URLs should be empty for now.
    ASSERT(!privateClickMeasurement || (privateClickMeasurement->attributionReportClickSourceURL().isNull() && privateClickMeasurement->attributionReportClickDestinationURL().isNull()));
    
    frame->loader().changeLocation(completedURL, effectiveTarget, &event, referrerPolicy, document->shouldOpenExternalURLsPolicyToPropagate(), newFrameOpenerPolicy, downloadAttribute, WTFMove(privateClickMeasurement), NavigationHistoryBehavior::Push, this);

    sendPings(completedURL);

    // Preconnect to the link's target for improved page load time.
    if (completedURL.protocolIsInHTTPFamily() && document->settings().linkPreconnectEnabled() && ((frame->isMainFrame() && isSelfTargetFrameName(effectiveTarget)) || isBlankTargetFrameName(effectiveTarget))) {
        auto storageCredentialsPolicy = frame->page() && frame->page()->canUseCredentialStorage() ? StoredCredentialsPolicy::Use : StoredCredentialsPolicy::DoNotUse;
        platformStrategies()->loaderStrategy()->preconnectTo(frame->loader(), ResourceRequest { WTFMove(completedURL) }, storageCredentialsPolicy, LoaderStrategy::ShouldPreconnectAsFirstParty::Yes, [] (ResourceError) { });
    }
}

// Falls back to using <base> element's target if the anchor does not have one.
AtomString HTMLAnchorElement::effectiveTarget() const
{
    auto effectiveTarget = target();
    if (effectiveTarget.isEmpty())
        effectiveTarget = document().baseTarget();
    return makeTargetBlankIfHasDanglingMarkup(effectiveTarget);
}

HTMLAnchorElement::EventType HTMLAnchorElement::eventType(Event& event)
{
    if (auto* mouseEvent = dynamicDowncast<MouseEvent>(event))
        return mouseEvent->shiftKey() ? MouseEventWithShiftKey : MouseEventWithoutShiftKey;
    return NonMouseEvent;
}

bool HTMLAnchorElement::treatLinkAsLiveForEventType(EventType eventType) const
{
    if (!hasEditableStyle())
        return true;

    switch (document().settings().editableLinkBehavior()) {
    case EditableLinkBehavior::Default:
    case EditableLinkBehavior::AlwaysLive:
        return true;

    case EditableLinkBehavior::NeverLive:
        return false;

    // If the selection prior to clicking on this link resided in the same editable block as this link,
    // and the shift key isn't pressed, we don't want to follow the link.
    case EditableLinkBehavior::LiveWhenNotFocused:
        return eventType == MouseEventWithShiftKey || (eventType == MouseEventWithoutShiftKey && rootEditableElementForSelectionOnMouseDown() != rootEditableElement());

    case EditableLinkBehavior::OnlyLiveWithShiftKey:
        return eventType == MouseEventWithShiftKey;
    }

    ASSERT_NOT_REACHED();
    return false;
}

bool isEnterKeyKeydownEvent(Event& event)
{
    if (event.type() != eventNames().keydownEvent)
        return false;
    auto* keyboardEvent = dynamicDowncast<KeyboardEvent>(event);
    return keyboardEvent && keyboardEvent->keyIdentifier() == "Enter"_s;
}

bool shouldProhibitLinks(Element* element)
{
    return isInSVGImage(element);
}

bool HTMLAnchorElement::willRespondToMouseClickEventsWithEditability(Editability editability) const
{
    return isLink() || HTMLElement::willRespondToMouseClickEventsWithEditability(editability);
}

static auto& rootEditableElementMap()
{
    static NeverDestroyed<WeakHashMap<HTMLAnchorElement, WeakPtr<Element, WeakPtrImplWithEventTargetData>, WeakPtrImplWithEventTargetData>> map;
    return map.get();
}

Element* HTMLAnchorElement::rootEditableElementForSelectionOnMouseDown() const
{
    if (!m_hasRootEditableElementForSelectionOnMouseDown)
        return 0;
    return rootEditableElementMap().get(*this).get();
}

void HTMLAnchorElement::clearRootEditableElementForSelectionOnMouseDown()
{
    if (!m_hasRootEditableElementForSelectionOnMouseDown)
        return;
    rootEditableElementMap().remove(*this);
    m_hasRootEditableElementForSelectionOnMouseDown = false;
}

void HTMLAnchorElement::setRootEditableElementForSelectionOnMouseDown(Element* element)
{
    if (!element) {
        clearRootEditableElementForSelectionOnMouseDown();
        return;
    }

    rootEditableElementMap().set(*this, element);
    m_hasRootEditableElementForSelectionOnMouseDown = true;
}

String HTMLAnchorElement::referrerPolicyForBindings() const
{
    return referrerPolicyToString(referrerPolicy());
}

ReferrerPolicy HTMLAnchorElement::referrerPolicy() const
{
    return parseReferrerPolicy(attributeWithoutSynchronization(referrerpolicyAttr), ReferrerPolicySource::ReferrerPolicyAttribute).value_or(ReferrerPolicy::EmptyString);
}

Node::InsertedIntoAncestorResult HTMLAnchorElement::insertedIntoAncestor(InsertionType insertionType, ContainerNode& parentOfInsertedTree)
{
    auto result = HTMLElement::insertedIntoAncestor(insertionType, parentOfInsertedTree);
    document().processInternalResourceLinks(this);
    return result;
}

void HTMLAnchorElement::setFullURL(const URL& fullURL)
{
    setAttributeWithoutSynchronization(hrefAttr, AtomString { fullURL.string() });
}

}
