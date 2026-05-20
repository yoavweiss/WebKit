# Copyright (C) 2026 Igalia S.L.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#    * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#    * Neither the name of the copyright holder nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
import sys
import unittest

from webkitpy.port.linux_container_sdk_utils import _strip_unix_path_prefix


class ContainerSDKHelpersTest(unittest.TestCase):

    def test_dbus_standard_path(self):
        path = "/run/user/1000/bus"
        DBUS_SESSION_BUS_ADDRESS = f"unix:path={path}"
        path_resolved = _strip_unix_path_prefix(DBUS_SESSION_BUS_ADDRESS)
        self.assertEqual(path, path_resolved)

    def test_dbus_autolaunch_path(self):
        path = "/tmp/dbus-OxJnk3YDFt"
        DBUS_SESSION_BUS_ADDRESS = f"unix:path={path},guid=cd2293c3498d62c28d9e98816a0d8b27"
        path_resolved = _strip_unix_path_prefix(DBUS_SESSION_BUS_ADDRESS)
        self.assertEqual(path, path_resolved)

    def test_pulse_server_path(self):
        path = "/run/user/1000/pulse/native"
        PULSE_SERVER = f"unix:{path}"
        path_resolved = _strip_unix_path_prefix(PULSE_SERVER)
        self.assertEqual(path, path_resolved)

    def test_at_spi_bus_path(self):
        path = "/run/user/1000/at-spi/bus"
        AT_SPI_BUS_ADDRESS = f"unix:path={path},guid=b6602b80724b3d489a0e9c9c6a0d8b2d"
        path_resolved = _strip_unix_path_prefix(AT_SPI_BUS_ADDRESS)
        self.assertEqual(path, path_resolved)
