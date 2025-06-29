/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
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

class StatusLabel extends LayoutItem
{

    constructor(layoutDelegate)
    {
        super({
            element: `<div class="status-label"></div>`,
            layoutDelegate
        });

        this._text = "";
        this.minimumWidth = 120;
        this.idealMinimumWidth = this.minimumWidth;
    }

    // Public

    get text()
    {
        return this._text;
    }

    set text(text)
    {
        if (text === this._text)
            return;

        this._text = text;
        this.markDirtyProperty("text");

        if (this.layoutDelegate)
            this.layoutDelegate.needsLayout = true;
    }

    get enabled()
    {
        return this._text !== "";
    }

    // Protected

    commitProperty(propertyName)
    {
        if (propertyName === "text")
            this.element.textContent = this._text;
        else
            super.commitProperty(propertyName);
    }

}
