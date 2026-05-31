#!/usr/bin/env python3
#
# Copyright (C) 2026 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Run the C preprocessor on a JavaScriptCore builtin .js source file.

The output is a preprocessed .js file consumed by generate-js-builtins.py.
A Make-style depfile is also emitted so the build system can re-preprocess
any builtin whose transitively-included headers change.

This script is invoked from both Source/JavaScriptCore/CMakeLists.txt and
Source/JavaScriptCore/DerivedSources.make. The caller names the actual
compiler via --driver ("clang", "gcc", or "clang-cl"), which determines
the flag syntax we emit — clang-cl requires MSVC-style flags and routes
GCC-only flags through `/clang:`, while clang and gcc both accept the
same GCC-style flag set.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile


DRIVER_CLANG = "clang"
DRIVER_GCC = "gcc"
DRIVER_CLANG_CL = "clang-cl"
_DRIVER_CHOICES = (DRIVER_CLANG, DRIVER_GCC, DRIVER_CLANG_CL)


# Matches the first `/* ... */` block at the top of a builtin .js source
# file. The match is non-greedy, so it stops at the first `*/`, which is
# exactly the end of the leading copyright/license comment.
_LEADING_COMMENT_REGEXP = re.compile(r"\s*/\*.*?\*/", re.DOTALL)

# Builtin license blocks are short; reading a small prefix is enough to
# capture the leading /* ... */ and avoids slurping multi-hundred-KB sources.
_LEADING_COMMENT_READ_BYTES = 4096


def extract_leading_comment(input_path):
    """Return the original source file's leading /* ... */ comment.

    wkbuiltins/builtins_model.py extracts copyright info via
    multilineCommentRegExp.findall(text)[0] — i.e. it parses the FIRST
    multiline comment of the preprocessed file and slices it at
    "Redistribution". With `-include BuiltinsMacros.h` and `-CC` (preserve
    comments), the BuiltinsMacros.h license block lands in the output
    before the source's own block, so without intervention the source's
    per-file copyright years would be dropped. Prepending the source's
    license guarantees it is the first block the parser sees.
    """
    with open(input_path, "r", encoding="utf-8") as f:
        text = f.read(_LEADING_COMMENT_READ_BYTES)
    match = _LEADING_COMMENT_REGEXP.match(text)
    return match.group(0) if match else ""


# GCC-style flags that take a separate file/dir argument. CMake may feed
# these to us via `extra` (e.g. `-imacros wtf/Platform.h`). clang-cl's
# MSVC-compat driver does not always accept them, so we wrap each such
# pair in `/clang:` to route it through the clang frontend.
_GCC_FLAGS_WITH_ARG = frozenset((
    "-imacros", "-include", "-isystem", "-idirafter",
    "-iquote", "-iframework", "-isysroot",
))


def translate_extra_for_clang_cl(extra):
    """Wrap GCC-only flags in /clang: so clang-cl forwards them to clang.

    Flags that are native to cl.exe (e.g. `/EHa-`, `/W4`) pass through
    unchanged; GCC-style `-I`/`-D` are also accepted by clang-cl directly.
    """
    translated = []
    i = 0
    while i < len(extra):
        flag = extra[i]
        if flag in _GCC_FLAGS_WITH_ARG and i + 1 < len(extra):
            translated.append("/clang:" + flag)
            translated.append("/clang:" + extra[i + 1])
            i += 2
            continue
        translated.append(flag)
        i += 1
    return translated


def build_command(args, extra):
    """Construct the preprocessor command line for the selected driver.

    The command always writes preprocessed text to stdout. We capture stdout
    in main() so we can prepend the source's own license block before writing
    the final output (see extract_leading_comment for why). All three drivers
    share this stdout-capture path for uniformity.
    """
    # Tell wtf/Platform.h (and any other header pulled in via -imacros) that
    # we're scanning purely for macro text replacement, so emission-only
    # constructs like `#pragma strict_gs_check(on)` should be skipped.
    text_mode_define = "-DWTF_PREPROCESSING_FOR_TEXT_REPLACEMENT_ONLY=1"
    if args.driver == DRIVER_CLANG_CL:
        cmd = [
            args.compiler,
            "/nologo",
            "/EP",              # preprocess to stdout without linemarkers
            "/C",               # preserve comments
            "/TP",              # treat input as C++
            "/FI", args.header,  # force-include BuiltinsMacros.h
        ]
        if args.depfile:
            # `-MD`/`-MT` in clang-cl mean `/MD`/`/MT` (CRT selection), so
            # route the GCC-style dep flags through /clang: to the frontend.
            cmd += [
                "/clang:-MD",
                "/clang:-MF", "/clang:" + args.depfile,
                "/clang:-MT", "/clang:" + args.output,
            ]
        cmd += translate_extra_for_clang_cl(extra)
        # `-w` suppresses every warning, so anything `-Werror` could promote
        # is already silenced. Placed last to override `-W...` from `extra`.
        # The input is JavaScript, not C/C++, so warnings from the C/C++
        # tokenizer are spurious (e.g. `''` is a valid empty JS string but
        # the C lexer flags it as an "empty character constant"). We only
        # care about preprocessor directives.
        cmd += ["-w", text_mode_define]
        cmd += [args.input]
    else:
        # clang and gcc accept the same GCC-style flag set.
        cmd = [
            args.compiler,
            "-E", "-P", "-CC",
            "-x", "c++",
            "-include", args.header,
        ]
        if args.depfile:
            cmd += ["-MD", "-MF", args.depfile, "-MT", args.output]
        cmd += extra
        # `-w` suppresses every warning, so anything `-Werror` could promote
        # is already silenced. Placed last to override `-W...` from `extra`.
        # The input is JavaScript, not C/C++, so warnings from the C/C++
        # tokenizer are spurious (e.g. `''` is a valid empty JS string but
        # the C lexer flags it as an "empty character constant"). We only
        # care about preprocessor directives.
        cmd += ["-w", text_mode_define]
        cmd += [args.input]
    return cmd


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--compiler", required=True,
                        help="Path to the C++ compiler (clang, gcc, or clang-cl).")
    parser.add_argument("--driver", choices=_DRIVER_CHOICES,
                        default=DRIVER_CLANG,
                        help="Which compiler we're invoking. 'clang' and "
                             "'gcc' share the same GCC-style flag set; "
                             "'clang-cl' uses MSVC-compat syntax.")
    parser.add_argument("--header", required=True,
                        help="Macro header injected via -include/-FI before the input.")
    parser.add_argument("--input", required=True,
                        help="Path to the builtin .js source file.")
    parser.add_argument("--output", required=True,
                        help="Path where the preprocessed .js file is written.")
    parser.add_argument("--depfile", default=None,
                        help="Optional Make-style depfile path (-MF).")

    # Anything after `--` is passed verbatim to the compiler. Splitting argv
    # ourselves is more predictable than argparse.REMAINDER, which keeps the
    # `--` separator in the parsed list across some Python versions and not
    # others.
    argv = sys.argv[1:]
    if "--" in argv:
        sep = argv.index("--")
        parser_argv = argv[:sep]
        extra = argv[sep + 1:]
    else:
        parser_argv = argv
        extra = []
    args = parser.parse_args(parser_argv)

    # The build systems create the top-level preprocessed-builtins/ directory,
    # but each output may live in a per-source subdirectory (e.g. builtins/,
    # inspector/) to avoid basename collisions across source dirs. Create
    # only the leaf as needed; the depfile is always alongside the output.
    output_dir = os.path.dirname(args.output) or "."
    os.makedirs(output_dir, exist_ok=True)

    leading_comment = extract_leading_comment(args.input)

    cmd = build_command(args, extra)
    result = subprocess.run(cmd, check=True, stdout=subprocess.PIPE)
    payload = result.stdout
    if leading_comment:
        payload = leading_comment.encode("utf-8") + b"\n" + payload

    # Write to a sibling temp file and rename on success so partially-written
    # output never fools dependency tracking.
    fd, tmp_path = tempfile.mkstemp(
        prefix=os.path.basename(args.output),
        suffix=".tmp",
        dir=output_dir,
    )
    replaced = False
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(payload)
        os.replace(tmp_path, args.output)
        replaced = True
    finally:
        if not replaced:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as exc:
        sys.exit(exc.returncode)
