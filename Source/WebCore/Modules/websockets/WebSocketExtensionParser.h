/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <wtf/HashMap.h>
#include <wtf/text/StringHash.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class WebSocketExtensionParser {
public:
    // FIXME: What character encoding are we parsing? Specify LChar, char8_t, char16_t, or something else here.
    explicit WebSocketExtensionParser(std::span<const uint8_t> data)
        : m_data(data)
    {
    }
    bool finished();
    bool parsedSuccessfully();

    // FIXME: What character encoding are we parsing? Specify LChar, char8_t, char16_t, or something else here.
    bool parseExtension(String& extensionToken, HashMap<String, String>& parameters);

private:
    const String& currentToken() { return m_currentToken; }

    // The following member functions basically follow the grammar defined
    // in Section 2.2 of RFC 2616.
    bool consumeToken();
    bool consumeQuotedString();
    bool consumeQuotedStringOrToken();
    bool consumeCharacter(char);

    void skipSpaces();

    std::span<const uint8_t> m_data;
    String m_currentToken;
    bool m_didFailParsing { false };
};

} // namespace WebCore
