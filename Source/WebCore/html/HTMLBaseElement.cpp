/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2008, 2009, 2010 Apple Inc. All rights reserved.
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
#include "HTMLBaseElement.h"

#include "Document.h"
#include "ElementInlines.h"
#include "HTMLNames.h"
#include "TextResourceDecoder.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(HTMLBaseElement);

using namespace HTMLNames;

inline HTMLBaseElement::HTMLBaseElement(const QualifiedName& tagName, Document& document)
    : HTMLElement(tagName, document)
{
    ASSERT(hasTagName(baseTag));
}

Ref<HTMLBaseElement> HTMLBaseElement::create(const QualifiedName& tagName, Document& document)
{
    return adoptRef(*new HTMLBaseElement(tagName, document));
}

void HTMLBaseElement::attributeChanged(const QualifiedName& name, const AtomString& oldValue, const AtomString& newValue, AttributeModificationReason attributeModificationReason)
{
    if (name == hrefAttr || name == targetAttr) {
        if (isConnected())
            document().processBaseElement();
    } else
        HTMLElement::attributeChanged(name, oldValue, newValue, attributeModificationReason);
}

Node::InsertedIntoAncestorResult HTMLBaseElement::insertedIntoAncestor(InsertionType insertionType, ContainerNode& parentOfInsertedTree)
{
    HTMLElement::insertedIntoAncestor(insertionType, parentOfInsertedTree);
    if (insertionType.connectedToDocument)
        document().processBaseElement();
    return InsertedIntoAncestorResult::Done;
}

void HTMLBaseElement::removedFromAncestor(RemovalType removalType, ContainerNode& oldParentOfRemovedTree)
{
    HTMLElement::removedFromAncestor(removalType, oldParentOfRemovedTree);
    if (removalType.disconnectedFromDocument)
        document().processBaseElement();
}

bool HTMLBaseElement::isURLAttribute(const Attribute& attribute) const
{
    return attribute.name().localName() == hrefAttr || HTMLElement::isURLAttribute(attribute);
}

AtomString HTMLBaseElement::target() const
{
    return attributeWithoutSynchronization(targetAttr);
}

// https://html.spec.whatwg.org/#dom-base-href
String HTMLBaseElement::href() const
{
    AtomString url = attributeWithoutSynchronization(hrefAttr);
    if (url.isNull())
        url = emptyAtom();

    auto urlRecord = document().completeURL(url, document().fallbackBaseURL());
    if (!urlRecord.isValid())
        return url;

    return urlRecord.string();
}

}
