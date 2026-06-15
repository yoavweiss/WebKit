/*
 * Copyright (C) 2016 Yusuke Suzuki <utatane.tea@gmail.com>.
 * Copyright (C) 2017 Caio Lima <ticaiolima@gmail.com>.
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

// @internal

@linkTimeConstant
function speciesConstructor(obj, defaultConstructor)
{
    "use strict";

    var constructor = obj.constructor;
    if (constructor === @undefined)
        return defaultConstructor;
    if (!@isObject(constructor))
        @throwTypeError("|this|.constructor is not an Object or undefined");
    constructor = constructor.@@species;
    if (@isUndefinedOrNull(constructor))
        return defaultConstructor;
    if (@isConstructor(constructor))
        return constructor;
    @throwTypeError("|this|.constructor[Symbol.species] is not a constructor");
}

@linkTimeConstant
function advanceStringIndex(string, index, unicode)
{
    // This function implements AdvanceStringIndex described in ES6 21.2.5.2.3.
    "use strict";

    if (!unicode)
        return index + 1;

    if (index + 1 >= string.length)
        return index + 1;

    var first = string.@charCodeAt(index);
    if (first < 0xD800 || first > 0xDBFF)
        return index + 1;

    var second = string.@charCodeAt(index + 1);
    if (second < 0xDC00 || second > 0xDFFF)
        return index + 1;

    return index + 2;
}

@linkTimeConstant
function regExpExec(regexp, str)
{
    "use strict";

    var exec = regexp.exec;
    var builtinExec = @regExpBuiltinExec;
    if (exec !== builtinExec && @isCallable(exec)) {
        var result = exec.@call(regexp, str);
        if (result !== null && !@isObject(result))
            @throwTypeError("The result of a RegExp exec must be null or an object");
        return result;
    }
    return builtinExec.@call(regexp, str);
}
