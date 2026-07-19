#!/usr/bin/env python3
"""Generate the dashboard's Material Design Icons subset fonts + glyph header.

Pins (bump deliberately — codepoints are stable within a major MDI release,
but the header is regenerated from the pinned package so drift is impossible):
  @mdi/font 7.4.47      glyph codepoints (scss/_variables.scss) + webfont TTF
  lv_font_conv 1.5.3    LVGL font compiler, run via npx

Outputs (committed to the repo; regeneration should be diff-stable):
  main/assets/ui_font_mdi_32.c   all dashboard glyphs        (chips, stat rows)
  main/assets/ui_font_mdi_48.c   room badges + media transport (large surfaces)
  main/ui/ui_icons.h             LV_FONT_DECLARE + UTF-8 glyph string macros

Usage:  python3 scripts/gen_mdi_font.py        (needs node/npm on PATH)
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MDI_PACKAGE = "@mdi/font@7.4.47"
LV_FONT_CONV = "lv_font_conv@1.5.3"

REPO = Path(__file__).resolve().parent.parent
ASSETS = REPO / "main" / "assets"
ICONS_HEADER = REPO / "main" / "ui" / "ui_icons.h"

# (macro, mdi icon name, also-in-48px-set)
# The 48 px face only carries glyphs drawn large (room-card badges, media
# transport buttons) to keep its flash cost down; everything renders at 32 px.
ICONS = [
    ("UI_ICON_HOME",            "home",                 False),
    ("UI_ICON_HOME_ROOF",       "home-roof",            True),
    ("UI_ICON_BED",             "bed",                  True),
    ("UI_ICON_CHAIR",           "chair-rolling",        True),
    ("UI_ICON_SOFA",            "sofa",                 True),
    ("UI_ICON_FLOOR_LAMP",      "floor-lamp",           False),
    ("UI_ICON_DESK_LAMP",       "desk-lamp",            False),
    ("UI_ICON_CEILING_LIGHT",   "ceiling-light",        False),
    ("UI_ICON_OUTDOOR_LAMP",    "outdoor-lamp",         False),
    ("UI_ICON_WALL_SCONCE",     "wall-sconce",          False),
    ("UI_ICON_IMAGE",           "image",                True),
    ("UI_ICON_SERVER",          "server",               True),
    ("UI_ICON_STRING_LIGHTS",   "string-lights",        True),
    ("UI_ICON_LIGHT_GROUP_OFF", "lightbulb-group-off",  True),
    ("UI_ICON_LIGHTBULB",       "lightbulb",            False),
    ("UI_ICON_LIGHTBULB_OFF",   "lightbulb-off",        False),
    ("UI_ICON_THERMOMETER",     "thermometer",          True),  # hero temp badge
    ("UI_ICON_WATER_PCT",       "water-percent",        False),
    ("UI_ICON_MOLECULE_CO2",    "molecule-co2",         False),
    ("UI_ICON_LED_STRIP",       "led-strip-variant",    False),
    ("UI_ICON_BRIGHTNESS",      "brightness-6",         False),
    ("UI_ICON_PLAY",            "play",                 True),
    ("UI_ICON_PAUSE",           "pause",                True),
    ("UI_ICON_PLAY_PAUSE",      "play-pause",           True),
    ("UI_ICON_MUSIC",           "music",                True),
    ("UI_ICON_ALBUM",           "album",                False),
    ("UI_ICON_MIC",             "microphone",           False),
    ("UI_ICON_SPEAKER",         "speaker",              True),
    ("UI_ICON_SNOWFLAKE",       "snowflake",            False),
    ("UI_ICON_POWER",           "power",                False),
]


def run(cmd: list[str], **kwargs) -> None:
    print("+", " ".join(str(c) for c in cmd))
    subprocess.run(cmd, check=True, **kwargs)


def load_codepoints(scss: Path) -> dict[str, int]:
    """Parse the $mdi-icons scss map:  "home-roof": F1D8B,"""
    table: dict[str, int] = {}
    for match in re.finditer(r'"([a-z0-9-]+)":\s*([0-9A-F]+)', scss.read_text()):
        table[match.group(1)] = int(match.group(2), 16)
    if len(table) < 1000:
        sys.exit(f"error: parsed only {len(table)} icons from {scss} — format changed?")
    return table


def gen_font(ttf: Path, out: Path, size: int, codepoints: list[int]) -> None:
    cmd = [
        "npx", "--yes", LV_FONT_CONV,
        "--font", str(ttf),
        "--format", "lvgl",
        "--bpp", "4",
        "--size", str(size),
        "--no-compress",
        "-o", str(out),
    ]
    for cp in codepoints:
        cmd += ["-r", f"0x{cp:X}"]
    run(cmd)

    # Post-process the generated C file:
    #  - The "Opts:" banner embeds the throwaway npm-install path and the
    #    absolute output path; pin both to stable strings so regeneration never
    #    churns the committed diff (regardless of machine or checkout location).
    #  - lv_font_conv's include guard falls back to "lvgl/lvgl.h", which does
    #    not resolve against the ESP-IDF managed component; the repo convention
    #    (see ui_font_font2.c) is a plain "lvgl.h" include.
    text = out.read_text()
    text = text.replace(str(ttf), "@mdi/font/fonts/materialdesignicons-webfont.ttf")
    text = text.replace(str(out), str(out.relative_to(REPO)))
    text = text.replace(
        '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif',
        '#include "lvgl.h"',
    )
    out.write_text(text)


def utf8_escape(cp: int) -> str:
    return "".join(f"\\x{b:02X}" for b in chr(cp).encode("utf-8"))


def gen_header(resolved: list[tuple[str, str, bool, int]]) -> None:
    lines = [
        "#ifndef UI_ICONS_H",
        "#define UI_ICONS_H",
        "",
        "/* GENERATED FILE — do not edit by hand.",
        " *",
        f" * Material Design Icons subset ({MDI_PACKAGE}) compiled to LVGL fonts by",
        " * scripts/gen_mdi_font.py. Regenerate with:",
        " *   python3 scripts/gen_mdi_font.py",
        " *",
        " * ui_font_mdi_32 carries every glyph below; ui_font_mdi_48 only the ones",
        " * marked [48] (room badges + media transport). MDI codepoints live in",
        " * Unicode plane 15 (U+Fxxxx), hence the 4-byte UTF-8 literals. */",
        "",
        '#include "lvgl.h"',
        "",
        "LV_FONT_DECLARE(ui_font_mdi_32);",
        "LV_FONT_DECLARE(ui_font_mdi_48);",
        "",
    ]
    for macro, name, in_48, cp in resolved:
        tag = " [48]" if in_48 else ""
        lines.append(f'#define {macro:<24} "{utf8_escape(cp)}" /* U+{cp:X} mdi:{name}{tag} */')
    lines += ["", "#endif /* UI_ICONS_H */", ""]
    ICONS_HEADER.write_text("\n".join(lines))
    print(f"wrote {ICONS_HEADER.relative_to(REPO)} ({len(resolved)} glyphs)")


def main() -> None:
    for tool in ("npm", "npx"):
        if shutil.which(tool) is None:
            sys.exit(f"error: {tool} not found on PATH (node is required)")

    with tempfile.TemporaryDirectory(prefix="mdi-font-") as tmp:
        run(["npm", "install", "--prefix", tmp, MDI_PACKAGE,
             "--no-save", "--no-audit", "--no-fund", "--silent"])
        pkg = Path(tmp) / "node_modules" / "@mdi" / "font"
        ttf = pkg / "fonts" / "materialdesignicons-webfont.ttf"
        table = load_codepoints(pkg / "scss" / "_variables.scss")

        missing = [name for _, name, _ in ICONS if name not in table]
        if missing:
            sys.exit(f"error: icon name(s) not in {MDI_PACKAGE}: {', '.join(missing)}")

        resolved = [(macro, name, in_48, table[name]) for macro, name, in_48 in ICONS]
        gen_font(ttf, ASSETS / "ui_font_mdi_32.c", 32, [cp for *_, cp in resolved])
        gen_font(ttf, ASSETS / "ui_font_mdi_48.c", 48,
                 [cp for _, _, in_48, cp in resolved if in_48])

    gen_header(resolved)
    for name in ("ui_font_mdi_32.c", "ui_font_mdi_48.c"):
        path = ASSETS / name
        print(f"wrote {path.relative_to(REPO)} ({path.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
