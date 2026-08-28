#!/usr/bin/env python3
# PSRadio - Packaged RmlUi asset regression test.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]


class UiAssetTests(unittest.TestCase):
    def test_rml_ui_assets_and_metadata_are_consistent(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "check_ui.py")],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("validated", result.stdout)


if __name__ == "__main__":
    unittest.main()
