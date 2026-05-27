#!/usr/bin/env python3
"""Scan for first-order architecture drift in the ESP32S3 screen firmware.

The scan is intentionally conservative for Stage 1: it blocks new unsafe
patterns while allowlisting current known debt that should be retired in later
stages.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCE_EXTS = {".c", ".h", ".cpp", ".hpp", ".cc"}


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    rule: str
    message: str

    def format(self) -> str:
        rel = self.path.relative_to(ROOT)
        return f"{rel}:{self.line}: {self.rule}: {self.message}"


# Keep this small. Each entry is current debt that Stage 1 documents but does
# not fix. New code should not be added here without a written reason.
ALLOWLIST: set[tuple[str, str]] = {
    # app_main currently owns the boot-time lv_port_init() call. Stage 1 records
    # this boot sequence shape without moving runtime initialization code.
    ("main/main.c", "lvgl-outside-view"),
    ("main/app/indicator_ha_model.c", "model-ui-include"),
    ("main/view_data.h", "shared-bsp-include"),
}


EVENT_COMMENT_ALLOWLIST: set[str] = {
    "VIEW_EVENT_SENSOR_TEMP_HISTORY",
    "VIEW_EVENT_SENSOR_HUMIDITY_HISTORY",
    "VIEW_EVENT_SENSOR_TVOC_HISTORY",
    "VIEW_EVENT_SENSOR_CO2_HISTORY",
    "VIEW_EVENT_WIFI_CFG_DELETE",
    # Existing Home Assistant address-display event lacks a payload comment.
    # Stage 1 should block new undocumented events without editing runtime C.
    "VIEW_EVENT_HA_ADDR_DISPLAY",
}


VIEW_FILE_PATTERNS = (
    re.compile(r"(^|/)indicator_.*_view\.c$"),
    re.compile(r"(^|/)indicator_view\.c$"),
    re.compile(r"(^|/)lv_port\.[ch]$"),
    re.compile(r"(^|/)ui(/|$)"),
)


MODEL_FILE_PATTERNS = (
    re.compile(r"(^|/)indicator_.*_model\.c$"),
    re.compile(r"(^|/)indicator_mqtt\.c$"),
    re.compile(r"(^|/)esp32_rp2040\.c$"),
    re.compile(r"(^|/)indicator_cmd\.c$"),
    re.compile(r"(^|/)indicator_storage_nvs\.c$"),
    re.compile(r"(^|/)indicator_btn\.c$"),
)


LVGL_CALL_RE = re.compile(r"\blv_[a-zA-Z0-9_]+\s*\(")
EVENT_ENUM_RE = re.compile(r"^\s*(VIEW_EVENT_[A-Z0-9_]+)\s*(?:=.*?)?,?\s*(?://\s*(.*))?$")


def rel_key(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def is_allowlisted(path: Path, rule: str) -> bool:
    return (rel_key(path), rule) in ALLOWLIST


def iter_source_files() -> list[Path]:
    roots = [ROOT / "main"]
    files: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.suffix in SOURCE_EXTS:
                files.append(path)
    return sorted(files)


def is_view_file(path: Path) -> bool:
    rel = rel_key(path)
    return any(pattern.search(rel) for pattern in VIEW_FILE_PATTERNS)


def is_model_file(path: Path) -> bool:
    rel = rel_key(path)
    return any(pattern.search(rel) for pattern in MODEL_FILE_PATTERNS)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def add_finding(findings: list[Finding], path: Path, line: int, rule: str, message: str) -> None:
    if not is_allowlisted(path, rule):
        findings.append(Finding(path, line, rule, message))


def scan_model_ui_includes(path: Path, text: str, findings: list[Finding]) -> None:
    if not is_model_file(path):
        return
    for match in re.finditer(r'#\s*include\s+"ui\.h"', text):
        add_finding(
            findings,
            path,
            line_number(text, match.start()),
            "model-ui-include",
            "model/controller files should not gain direct SquareLine UI ownership",
        )


def scan_lvgl_calls(path: Path, text: str, findings: list[Finding]) -> None:
    if is_view_file(path):
        return
    for match in LVGL_CALL_RE.finditer(text):
        add_finding(
            findings,
            path,
            line_number(text, match.start()),
            "lvgl-outside-view",
            "LVGL calls belong in view/UI files or the LVGL port layer",
        )


def scan_shared_bsp_includes(path: Path, text: str, findings: list[Finding]) -> None:
    rel = rel_key(path)
    if rel not in {"main/view_data.h"} and not rel.endswith("app_events.h"):
        return
    for match in re.finditer(r"#\s*include\s+[<\"]bsp_", text):
        add_finding(
            findings,
            path,
            line_number(text, match.start()),
            "shared-bsp-include",
            "shared event/data headers should not depend on BSP headers",
        )


def scan_event_payload_comments(path: Path, text: str, findings: list[Finding]) -> None:
    if rel_key(path) not in {"main/view_data.h", "main/app/app_events.h"}:
        return
    for idx, line in enumerate(text.splitlines(), start=1):
        match = EVENT_ENUM_RE.match(line)
        if not match:
            continue
        name, comment = match.groups()
        if name == "VIEW_EVENT_ALL":
            continue
        if name in EVENT_COMMENT_ALLOWLIST:
            continue
        if not comment or not comment.strip():
            add_finding(
                findings,
                path,
                idx,
                "event-payload-comment",
                f"{name} needs an inline payload/lifetime comment",
            )


def scan_service_callbacks(path: Path, text: str, findings: list[Finding]) -> None:
    for match in re.finditer(r"\b[a-zA-Z0-9_]+_register_cb\s*\(", text):
        add_finding(
            findings,
            path,
            line_number(text, match.start()),
            "service-register-callback",
            "prefer the shared event loop over service-local callback registration",
        )


def scan() -> list[Finding]:
    findings: list[Finding] = []
    for path in iter_source_files():
        text = path.read_text(errors="ignore")
        scan_model_ui_includes(path, text, findings)
        scan_lvgl_calls(path, text, findings)
        scan_shared_bsp_includes(path, text, findings)
        scan_event_payload_comments(path, text, findings)
        scan_service_callbacks(path, text, findings)
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list-allowlist", action="store_true", help="print current known-debt allowlist")
    args = parser.parse_args()

    if args.list_allowlist:
        for path, rule in sorted(ALLOWLIST):
            print(f"{path}: {rule}")
        return 0

    findings = scan()
    if findings:
        print("architecture_scan: failed", file=sys.stderr)
        for finding in findings:
            print(f"  {finding.format()}", file=sys.stderr)
        return 1

    print("architecture_scan: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
