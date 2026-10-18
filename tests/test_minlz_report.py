#!/usr/bin/env python3
"""CPU policy checks for the C/Go performance gate."""

import sys
import unittest
import argparse
import tempfile
from pathlib import Path
from unittest.mock import patch
from subprocess import CompletedProcess

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import minlz_report


class ReportCpuGateTests(unittest.TestCase):
    def test_full_report_accepts_missing_level_filter(self):
        with patch.object(sys, "argv", ["minlz_report.py"]):
            args = minlz_report.parse_args()
        self.assertIsNone(args.level)

    def test_level_filter_uses_c_api_enum(self):
        for level in (-1, 0, 1, 2, 3):
            self.assertEqual(minlz_report.parse_level(str(level)), str(level))
        for level in ("-2", "-3", "balanced"):
            with self.assertRaises(argparse.ArgumentTypeError):
                minlz_report.parse_level(level)

    def test_operation_filter_is_forwarded(self):
        with patch.object(sys, "argv", ["minlz_report.py", "--ops", "index"]):
            args = minlz_report.parse_args()
        with patch.object(minlz_report, "perf_exe", return_value=Path("minlz_perf")), \
             patch.object(minlz_report, "build_go_helper"):
            cmd, _ = minlz_report._perf_command(args, Path("."))
        self.assertEqual(cmd[cmd.index("--ops") + 1], "index")

    def test_known_noisy_row_is_confirmed_despite_full_win(self):
        noisy = {
            "operation": "block-encode", "corpus": "logjson-1m", "level": "-2",
            "speedup": 1.145, "size_delta": 0,
        }
        ordinary = {
            "operation": "block-decode", "corpus": "logjson-1m", "level": "-2",
            "speedup": 1.145, "size_delta": 0,
        }
        targets = minlz_report.official_confirmation_targets(
            [noisy, ordinary], "full", 1.03
        )
        self.assertEqual(targets, [noisy])

    def test_gate_rejects_empty_or_incomplete_matrix(self):
        empty = {"speed_rows": 0, "size_rows": 0}
        focused = {"speed_rows": 2, "size_rows": 2}
        full = {"speed_rows": 240, "size_rows": 240}
        self.assertTrue(minlz_report.gate_coverage_failures(empty, False))
        self.assertEqual(minlz_report.gate_coverage_failures(focused, False), [])
        self.assertTrue(minlz_report.gate_coverage_failures(focused, True))
        self.assertEqual(minlz_report.gate_coverage_failures(full, True), [])

    def test_failed_confirmation_overrides_initial_speed_win(self):
        row = {
            "operation": "block-encode", "corpus": "logjson-1m", "level": "-2",
            "speedup": 1.069, "size_delta": 0,
        }
        confirmation = minlz_report.Confirmation(
            "block-encode", "logjson-1m", "-2", 1.069, 0.971, 0, 0,
            "fail", "confirmed Go speed win",
        )
        self.assertEqual(minlz_report.gate_failures([row], "full"), [])
        failures = minlz_report.official_gate_failures([row], "full", [confirmation])
        self.assertEqual(len(failures), 1)
        self.assertIn("confirmed Go speed win", failures[0])

    def test_forced_scalar_backends_use_go_v2_without_avx(self):
        for backend in ("portable", "sse42", "asm"):
            with self.subTest(backend=backend):
                env = {
                    "MINLZ_FORCE_CPU": backend,
                    "GOAMD64": "v3",
                    "GODEBUG": "asyncpreemptoff=1,cpu.avx=on,cpu.avx2=on",
                }
                minlz_report._match_go_cpu_to_forced_backend(env)
                self.assertEqual(env["GOAMD64"], "v2")
                self.assertEqual(
                    env["GODEBUG"],
                    "asyncpreemptoff=1,cpu.avx=off,cpu.avx2=off,cpu.avx512f=off,"
                    "cpu.avx512cd=off,cpu.avx512bw=off,cpu.avx512dq=off,cpu.avx512vl=off",
                )

    def test_linux_no_avx_gate_masks_libc_and_keeps_other_tunables(self):
        env = {
            "MINLZ_FORCE_CPU": "sse42",
            "GLIBC_TUNABLES": "glibc.malloc.tcache_count=2:glibc.cpu.hwcaps=AVX2,-ERMS",
        }
        with patch.object(minlz_report.sys, "platform", "linux"):
            minlz_report._match_go_cpu_to_forced_backend(env)
            first = env["GLIBC_TUNABLES"]
            minlz_report._match_go_cpu_to_forced_backend(env)
        self.assertEqual(env["GLIBC_TUNABLES"], first)
        self.assertIn("glibc.malloc.tcache_count=2", first)
        self.assertIn("-ERMS", first)
        masks = first.split("glibc.cpu.hwcaps=", 1)[1].split(",")
        self.assertNotIn("AVX2", masks)
        for feature in ("AVX", "AVX2", "AVX512F", "AVX512VL"):
            self.assertIn("-" + feature, masks)

    def test_windows_no_avx_gate_does_not_set_glibc_tunables(self):
        env = {"MINLZ_FORCE_CPU": "asm"}
        with patch.object(minlz_report.sys, "platform", "win32"):
            minlz_report._match_go_cpu_to_forced_backend(env)
        self.assertNotIn("GLIBC_TUNABLES", env)

    def test_default_backend_keeps_go_cpu_policy(self):
        env = {"GOAMD64": "v3", "GODEBUG": "cpu.avx=on", "GLIBC_TUNABLES": "glibc.cpu.hwcaps=AVX2"}
        minlz_report._match_go_cpu_to_forced_backend(env)
        self.assertEqual(env, {"GOAMD64": "v3", "GODEBUG": "cpu.avx=on", "GLIBC_TUNABLES": "glibc.cpu.hwcaps=AVX2"})

    def test_cpu_metadata_records_effective_policy(self):
        with patch.dict(minlz_report.os.environ, {"MINLZ_FORCE_CPU": "sse42"}, clear=True), \
             patch.object(minlz_report.sys, "platform", "linux"):
            policy = minlz_report.cpu_gate_metadata()
        self.assertEqual(policy["backend"], "sse42")
        self.assertEqual(policy["go_amd64"], "v2")
        self.assertIn("cpu.avx512f=off", policy["go_debug"])
        self.assertIn("-AVX2", policy["libc_tunables"])

    def test_reports_include_escaped_cpu_policy(self):
        meta = {
            "generated": "test", "command": "minlz_perf", "samples": 7,
            "rounds": 3, "iterations": "auto", "gate_profile": "full",
            "affinity": "2",
            "gate_status": "FAIL", "gate_failures": ["confirmed Go speed win"],
            "build_policy": {"experimental_l2_search": "ON", "workspace_hugepages": "OFF",
                             "readseeker_buffer_transfer": "ON"},
            "cpu_policy": {
                "backend": "sse42", "go_amd64": "v2", "go_debug": "<test>",
                "libc_tunables": "glibc.cpu.hwcaps=-AVX2",
            },
        }
        with patch.object(Path, "write_text") as write:
            minlz_report.write_markdown(Path("report.md"), [], meta)
            minlz_report.write_html(Path("report.html"), [], meta)
        markdown = write.call_args_list[0].args[0]
        report_html = write.call_args_list[1].args[0]
        self.assertIn("C backend: `sse42`; affinity: `2`", markdown)
        self.assertIn("Final gate: **FAIL**; blocking conditions: 1", markdown)
        self.assertIn('class="gate fail"', report_html)
        self.assertIn("glibc.cpu.hwcaps=-AVX2", report_html)
        self.assertIn("&lt;test&gt;", report_html)
        self.assertNotIn("<test>", report_html)
        self.assertIn("Experimental L2 search: `ON`", markdown)
        self.assertIn("Experimental L2 search: <code>ON</code>", report_html)
        self.assertIn("Workspace hugepage hints: `OFF`", markdown)
        self.assertIn("Workspace hugepage hints: <code>OFF</code>", report_html)
        self.assertIn("Readseeker buffer transfer: `ON`", markdown)
        self.assertIn("Readseeker buffer transfer: <code>ON</code>", report_html)

    def test_build_metadata_does_not_assume_production_defaults(self):
        with patch.object(Path, "read_text", side_effect=FileNotFoundError):
            self.assertEqual(minlz_report.build_gate_metadata(Path("."), "build"),
                             {"experimental_l2_search": "unknown", "workspace_hugepages": "unknown",
                              "readseeker_buffer_transfer": "unknown"})
        for flag in ("ON", "OFF", "unexpected"):
            cache = f"MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH:BOOL={flag}\n"
            with patch.object(Path, "read_text", return_value=cache):
                value = minlz_report.build_gate_metadata(Path("."), "build")
            self.assertEqual(value["experimental_l2_search"],
                             flag if flag != "unexpected" else "unknown")
            self.assertEqual(value["workspace_hugepages"], "unknown")
            self.assertEqual(value["readseeker_buffer_transfer"], "unknown")

    def test_hugepage_metadata_is_independent_of_l2_candidate(self):
        cache = ("MINLZ_ENABLE_HUGEPAGE_WORKSPACE:BOOL=ON\n"
                 "MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH:BOOL=OFF\n")
        with patch.object(Path, "read_text", return_value=cache):
            values = minlz_report.build_gate_metadata(Path("."), "build")
        self.assertEqual(values, {"workspace_hugepages": "ON", "experimental_l2_search": "OFF",
                                  "readseeker_buffer_transfer": "unknown"})

    def test_transfer_metadata_is_independent_of_encoder_options(self):
        cache = "MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER:BOOL=ON\n"
        with patch.object(Path, "read_text", return_value=cache):
            values = minlz_report.build_gate_metadata(Path("."), "build")
        self.assertEqual(values["readseeker_buffer_transfer"], "ON")
        self.assertEqual(values["workspace_hugepages"], "unknown")
        self.assertEqual(values["experimental_l2_search"], "unknown")

    def test_go_helper_builds_are_separate_by_amd64_level(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "go").mkdir()
            (root / "go" / "go.mod").write_text("module example.org/minlz\n")
            (root / "compat" / "minlz-go-compat").mkdir(parents=True)
            source = root / "compat" / "minlz-go-compat" / "main.go"
            source.write_text('package main\nimport "github.com/minio/minlz"\n')
            env_v2 = {"GOAMD64": "v2"}
            env_v3 = {"GOAMD64": "v3"}
            with patch.object(minlz_report.subprocess, "run") as run:
                v2 = minlz_report.build_go_helper(root, str(root / "go"), env_v2, "build")
                v3 = minlz_report.build_go_helper(root, str(root / "go"), env_v3, "build")
            self.assertNotEqual(v2, v3)
            self.assertIn("-v2", Path(v2).name)
            self.assertIn("-v3", Path(v3).name)
            self.assertEqual(run.call_count, 2)
            selected = (root / "build" / "compat" / "main.go").read_text()
            self.assertIn('"example.org/minlz"', selected)
            self.assertNotIn('"github.com/minio/minlz"', selected)
            self.assertEqual(source.read_text(), 'package main\nimport "github.com/minio/minlz"\n')

    def test_filtered_rounds_alternate_go_order(self):
        csv_output = (
            "kind,corpus,level,raw_bytes,encoded_bytes,ratio,mbps_min,mbps_median,mbps_max\n"
            "block-encode,logjson-1m,2,1000000,180160,0.18016,850,860,870\n"
        )
        result = CompletedProcess([], 0, csv_output, "")
        cmd = ["minlz_perf", "--corpus", "logjson-1m", "--level", "2"]
        with patch.object(minlz_report.subprocess, "run", return_value=result) as run:
            _, stderr = minlz_report._run_command_rounds(
                cmd, {}, Path("."), 3, False, True
            )
        self.assertEqual(run.call_args_list[0].args[0], cmd)
        self.assertEqual(run.call_args_list[1].args[0], cmd + ["--go-first"])
        self.assertEqual(run.call_args_list[2].args[0], cmd)
        self.assertIn("round 2/3 order=go-first", stderr)


if __name__ == "__main__":
    unittest.main()
