# Copyright (C) 2025 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import tempfile
from pathlib import Path
from unittest import TestCase

from .sdkdb import SDKDB
from .macho import APIReport

# Fixtures:
F = Path('/libdoesntexist.dylib')
F_Hash = 1234567890
F_NonNormalized = Path('/foo/../libdoesntexist.dylib')
R = APIReport(
    file=F, arch='arm64e',
    exports={'_WKDoesntExistLibraryVersion', '_OBJC_CLASS_$_WKDoesntExist'},
    methods={'initWithData:'}
)


class TestSDKDB(TestCase):
    def setUp(self):
        self.dbfile = tempfile.NamedTemporaryFile(prefix='TestSDKDB-')
        self.sdkdb = SDKDB(Path(self.dbfile.name))

    def test_ingests_api_report(self):
        # Given a file added to the cache:
        with self.sdkdb:
            self.assertFalse(self.sdkdb._cache_hit_preparing_to_insert(F, F_Hash))
            self.sdkdb._add_api_report(R, F)

        # Its symbols, classes, and implemented selectors should be added.
        self.assertTrue(self.sdkdb.symbol('_WKDoesntExistLibraryVersion'))
        self.assertTrue(self.sdkdb.symbol('_OBJC_CLASS_$_WKDoesntExist'))
        self.assertTrue(self.sdkdb.objc_class('WKDoesntExist'))
        self.assertTrue(self.sdkdb.objc_selector('initWithData:'))

    def test_only_finds_declarations_from_used_inputs(self):
        # Given a file added to the cache:
        with self.sdkdb:
            self.assertFalse(self.sdkdb._cache_hit_preparing_to_insert(F, F_Hash))
            self.sdkdb._add_api_report(R, F)

        # When a new connection is opened to the persisted data...
        self.sdkdb = SDKDB(Path(self.dbfile.name))

        # ...it should not find symbols from the file...
        self.assertFalse(self.sdkdb.symbol('_WKDoesntExistLibraryVersion'))

        # ...until the file is again added as in input.
        self.assertTrue(self.sdkdb._cache_hit_preparing_to_insert(F, F_Hash))
        self.assertTrue(self.sdkdb.symbol('_WKDoesntExistLibraryVersion'))

    def test_path_normalized_when_cache_hit(self):
        with self.sdkdb:
            self.assertFalse(self.sdkdb._cache_hit_preparing_to_insert(F, F_Hash))
            self.assertTrue(self.sdkdb._cache_hit_preparing_to_insert(F_NonNormalized, F_Hash))

    def test_path_normalized_when_cache_miss(self):
        with self.sdkdb:
            self.assertFalse(self.sdkdb._cache_hit_preparing_to_insert(F_NonNormalized, F_Hash))
            self.assertTrue(self.sdkdb._cache_hit_preparing_to_insert(F, F_Hash))
