/*
 * Copyright (C) 2013 Samsung Electronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "Document.h"
#include "ScriptElement.h"
#include <wtf/WeakRef.h>

namespace WebCore {

class CurrentScriptIncrementer {
    WTF_MAKE_NONCOPYABLE(CurrentScriptIncrementer);
public:
    CurrentScriptIncrementer(Document& document, ScriptElement& scriptElement)
        : m_document(document)
    {
        bool shouldPushNullForCurrentScript = scriptElement.element().isInShadowTree() || scriptElement.scriptType() != ScriptType::Classic;
        protectedDocument()->pushCurrentScript(shouldPushNullForCurrentScript ? nullptr : &scriptElement.element());
    }

    ~CurrentScriptIncrementer()
    {
        protectedDocument()->popCurrentScript();
    }

private:
    Ref<Document> protectedDocument() const { return m_document.get(); }

    WeakRef<Document, WeakPtrImplWithEventTargetData> m_document;
};

} // namespace WebCore
