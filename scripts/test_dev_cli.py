#!/usr/bin/env python3
"""Unit tests for the repo-local ./dev command wrapper."""

from __future__ import annotations

import argparse
import subprocess
import unittest
from unittest import mock

import dev


class DevCliTests(unittest.TestCase):
    def test_fullclean_is_a_top_level_command(self) -> None:
        args = dev.build_parser().parse_args(["fullclean"])

        self.assertIs(args.func, dev.cmd_fullclean)
        self.assertIs(args.ensure, dev.ensure_idf)

    def test_flash_recovers_from_idf_python_env_mismatch(self) -> None:
        calls: list[list[str]] = []
        mismatch = (
            "'/Users/spencer/.espressif/python_env/idf5.4_py3.14_env/bin/python' "
            "is currently active in the environment while the project was configured "
            "with '/Users/spencer/.espressif/python_env/idf5.4_py3.13_env/bin/python'. "
            "Run 'idf.py fullclean' to start again.\n"
        )
        results = [
            subprocess.CompletedProcess(["idf.py", "-b", "460800", "flash"], 1, "", mismatch),
            subprocess.CompletedProcess(["idf.py", "fullclean"], 0, "cleaned\n", ""),
            subprocess.CompletedProcess(["idf.py", "-b", "460800", "flash"], 0, "flashed\n", ""),
        ]

        def fake_run(cmd: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
            calls.append(cmd)
            return results.pop(0)

        args = argparse.Namespace(port=None, baud=460800)
        with mock.patch("dev.subprocess.run", side_effect=fake_run):
            self.assertEqual(dev.cmd_flash(args), 0)

        self.assertEqual(
            calls,
            [
                ["idf.py", "-b", "460800", "flash"],
                ["idf.py", "fullclean"],
                ["idf.py", "-b", "460800", "flash"],
            ],
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
