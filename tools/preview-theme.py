#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render Plasmid offscreen using disposable settings and a private session bus."""

import argparse
import configparser
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("theme", nargs="?", default="breeze-light", help="Installed Plasma style directory name")
    parser.add_argument("--icons", help="Installed icon theme; defaults to breeze or breeze-dark")
    parser.add_argument("--colors", type=Path, help="Color scheme file, for styles without their own colors")
    parser.add_argument("--binary", type=Path, default=root / "build/plasmid-launcher")
    parser.add_argument("--output", type=Path, help="PNG path; defaults to build/theme-previews/<theme>.png")
    args = parser.parse_args()
    if args.theme in (".", "..") or "/" in args.theme or not args.theme:
        parser.error("theme must be an installed style's directory name")
    if not args.binary.is_file():
        parser.error("build Plasmid first, or pass --binary")
    if not shutil.which("dbus-run-session"):
        parser.error("dbus-run-session is required")

    user_data = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))).resolve()
    data_dirs = [user_data] + [Path(p).resolve() for p in os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(":") if p]
    style = next((p / "plasma/desktoptheme" / args.theme for p in data_dirs
                  if (p / "plasma/desktoptheme" / args.theme).is_dir()), None)
    if style is None:
        parser.error(f"installed Plasma style not found: {args.theme}")
    icons = args.icons or ("breeze-dark" if "dark" in args.theme.lower() else "breeze")
    if not any((p / "icons" / icons / "index.theme").is_file() for p in data_dirs):
        parser.error(f"installed icon theme not found: {icons}")

    # Match Launcher::readTheme(): the style's own colors take precedence.
    colors = style / "colors"
    if not colors.is_file():
        colors = args.colors or Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "kdeglobals"
    if not colors.is_file():
        parser.error("style has no colors; pass --colors /path/to/a/scheme.colors")
    palette = configparser.ConfigParser(interpolation=None, strict=False)
    palette.optionxform = str
    palette.read(colors, encoding="utf-8")
    if not palette.has_section("Icons"):
        palette.add_section("Icons")
    palette.set("Icons", "Theme", icons)
    output = (args.output or root / "build/theme-previews" / f"{args.theme}.png").resolve()

    with tempfile.TemporaryDirectory(prefix="plasmid-theme-preview-") as directory:
        temporary = Path(directory)
        environment = os.environ.copy()
        for variable, folder in [("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                                 ("XDG_CACHE_HOME", "cache"), ("XDG_STATE_HOME", "state"),
                                 ("XDG_RUNTIME_DIR", "runtime")]:
            target = temporary / folder
            target.mkdir(mode=0o700)
            environment[variable] = str(target)
        config = temporary / "config"
        (config / "plasmarc").write_text(f"[Theme]\nname={args.theme}\n", encoding="utf-8")
        with (config / "kdeglobals").open("w", encoding="utf-8") as handle:
            palette.write(handle, space_around_delimiters=False)
        downloads = temporary / "Downloads"
        downloads.mkdir()
        (config / "user-dirs.dirs").write_text(f'XDG_DOWNLOAD_DIR="{downloads}"\n', encoding="utf-8")
        # User-installed themes/apps remain discoverable as read-only data roots.
        environment["XDG_DATA_DIRS"] = ":".join(map(str, data_dirs))
        environment["QT_QPA_PLATFORM"] = "offscreen"
        environment["QT_QUICK_BACKEND"] = "software"
        environment["QT_QPA_PLATFORMTHEME"] = "generic"
        environment["QT_STYLE_OVERRIDE"] = "Fusion"
        for variable in ("DISPLAY", "WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS", "QT_WAYLAND_SHELL_INTEGRATION"):
            environment.pop(variable, None)
        capture = temporary / "preview.png"
        command = ["dbus-run-session", "--", str(args.binary.resolve()), "--screenshot", str(capture)]
        with subprocess.Popen(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, start_new_session=True) as process:
            try:
                stdout, stderr = process.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                # Stop the renderer and private bus as well as their wrapper.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.communicate()
                parser.exit(1, "Preview timed out.\n")
            if process.returncode or not capture.is_file():
                parser.exit(1, f"Preview failed:\n{stdout}{stderr}")
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(capture, output)
    print(f"{args.theme} / {icons}: {output}")


if __name__ == "__main__":
    main()
