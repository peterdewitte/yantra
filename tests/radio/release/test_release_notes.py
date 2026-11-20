# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of tools/ci/release_notes.py (RAD-134). Argument: tools/ci."""
import sys
import unittest

sys.path.insert(0, sys.argv.pop(1))
import release_notes  # noqa: E402

CHANGELOG = """# Changelog

## [Unreleased]
- coming

## [0.2.0] - 2026-12-01
### Added
- second

## v0.1.0 - 2026-11-03
- first

## [0.1.0-rc1]
- candidate
"""


class Notes(unittest.TestCase):
    def test_first_section(self):
        self.assertEqual(release_notes.notes(CHANGELOG, "v0.2.0"), "### Added\n- second\n")

    def test_middle_section_without_brackets(self):
        self.assertEqual(release_notes.notes(CHANGELOG, "v0.1.0"), "- first\n")

    def test_prefix_is_not_a_match(self):
        self.assertEqual(release_notes.notes(CHANGELOG, "0.1.0-rc1"), "- candidate\n")
        self.assertIsNone(release_notes.notes(CHANGELOG, "v0.1"))

    def test_missing(self):
        self.assertIsNone(release_notes.notes(CHANGELOG, "v9.9.9"))

    def test_pre_release_takes_unreleased(self):
        # The product version of a development state (VERSION, PLG-29).
        self.assertEqual(release_notes.notes(CHANGELOG, "v0.3.0-dev"), "- coming\n")
        self.assertIsNone(release_notes.notes(CHANGELOG.replace("## [Unreleased]", "## Later"), "v0.3.0-dev"))


if __name__ == "__main__":
    unittest.main()
