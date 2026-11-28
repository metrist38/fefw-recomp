# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the Decky plugin's runtime pause flag helper (main.py)."""
import os
import stat
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

DECKY_DIR = Path(__file__).resolve().parent.parent


def _load_main_module():
    stub_decky = types.ModuleType("decky")
    stub_decky.logger = mock.Mock()
    stub_decky.DECKY_PLUGIN_SETTINGS_DIR = "/tmp"
    stub_decky.DECKY_USER_HOME = "/tmp"
    stub_decky.DECKY_PLUGIN_VERSION = "0.0.0-test"
    stub_decky.DECKY_PLUGIN_RUNTIME_DIR = "/tmp"
    stub_launcher = types.ModuleType("fefw_launcher")
    stub_launcher.Controller = mock.Mock()
    stub_launcher.LauncherError = Exception
    with mock.patch.dict(sys.modules, {"decky": stub_decky, "fefw_launcher": stub_launcher}):
        sys.path.insert(0, str(DECKY_DIR))
        try:
            import importlib
            if "main" in sys.modules:
                del sys.modules["main"]
            return importlib.import_module("main")
        finally:
            sys.path.remove(str(DECKY_DIR))


class PauseFlagTests(unittest.TestCase):
    def test_set_and_clear_pause_flag(self):
        mod = _load_main_module()
        with tempfile.TemporaryDirectory() as tmp:
            flag = os.path.join(tmp, "fefw-recomp-pause")
            with mock.patch.object(mod, "PAUSE_FLAG_PATH", flag):
                mod._set_pause_flag(False)
                self.assertFalse(os.path.exists(flag))

                mod._set_pause_flag(True)
                self.assertTrue(os.path.exists(flag))
                mode = stat.S_IMODE(os.stat(flag).st_mode)
                self.assertEqual(mode, 0o644)

                mod._set_pause_flag(False)
                self.assertFalse(os.path.exists(flag))


if __name__ == "__main__":
    unittest.main()
