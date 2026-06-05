#!/usr/bin/env python3
# ==============================================================
# format_code.py — 对 app/ 和 bsp/ 目录执行 clang-format
# 用法:
#   python format_code.py              # 原地格式化
#   python format_code.py --check      # 仅检查，不修改文件
#   python format_code.py --check --diff  # 检查并显示差异
# ==============================================================

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

# ---- 终端颜色 ----
class Color:
    RED    = "\033[91m"
    GREEN  = "\033[92m"
    YELLOW = "\033[93m"
    CYAN   = "\033[96m"
    MAGENTA = "\033[95m"
    RESET  = "\033[0m"

def _c(text: str, color: str) -> str:
    return f"{color}{text}{Color.RESET}"

# ---- 配置 ----
SCRIPT_DIR = Path(__file__).resolve().parent
TARGET_DIRS = ["app", "bsp", "components"]
EXTENSIONS  = ["*.c", "*.h"]
CONFIG_FILE = ".clang-format"


def find_clang_format() -> str:
    """查找 clang-format 可执行文件"""
    cf = shutil.which("clang-format")
    if cf is None:
        print(_c("[ERROR] clang-format not found in PATH!", Color.RED))
        print(_c("        Install: winget install LLVM.LLVM  or  apt install clang-format", Color.YELLOW))
        sys.exit(1)

    # 验证能运行
    try:
        result = subprocess.run([cf, "--version"], capture_output=True, text=True,
                              encoding="utf-8", errors="replace", check=True)
        print(_c(f"[INFO] clang-format: {cf}", Color.CYAN))
        print(_c(f"[INFO] {result.stdout.strip()}", Color.CYAN))
    except subprocess.CalledProcessError as e:
        print(_c(f"[ERROR] clang-format not runnable: {e}", Color.RED))
        sys.exit(1)

    return cf


def check_config() -> None:
    """检查 .clang-format 配置文件"""
    config_path = SCRIPT_DIR / CONFIG_FILE
    if not config_path.exists():
        print(_c(f"[ERROR] {CONFIG_FILE} not found in project root!", Color.RED))
        sys.exit(1)
    print(_c(f"[INFO] Using config: {CONFIG_FILE}", Color.CYAN))


def collect_files() -> list[Path]:
    """收集 app/ 和 bsp/ 下所有 .c/.h 文件"""
    files: list[Path] = []
    for d in TARGET_DIRS:
        target = SCRIPT_DIR / d
        if not target.is_dir():
            print(_c(f"[WARN] Directory '{d}' not found, skipping.", Color.YELLOW))
            continue
        for ext in EXTENSIONS:
            files.extend(target.rglob(ext))

    if not files:
        print(_c(f"[WARN] No .c/.h files found in {', '.join(TARGET_DIRS)}.", Color.YELLOW))
        sys.exit(0)

    print(_c(f"[INFO] Found {len(files)} file(s) to process.\n", Color.CYAN))
    return sorted(files)


def relative_path(file: Path) -> str:
    """返回相对于项目根目录的路径"""
    try:
        return str(file.relative_to(SCRIPT_DIR))
    except ValueError:
        return str(file)


def run_format(cf: str, files: list[Path]) -> int:
    """格式化模式：原地修改"""
    changed = 0
    failed = 0
    for f in files:
        rel = relative_path(f)
        print(_c(f"[FMT] {rel}", Color.MAGENTA))
        try:
            subprocess.run([cf, "-i", "-style=file", str(f)], check=True, capture_output=True,
                           encoding="utf-8", errors="replace")
            changed += 1
        except subprocess.CalledProcessError as e:
            print(_c(f"      ERROR formatting {rel}", Color.RED))
            print(e.stderr)  # 已用 encoding='utf-8' errors='replace'
            failed += 1

    print()
    print("=" * 50)
    print(_c(f" Format Complete: {changed} formatted, {failed} error(s)", Color.CYAN))
    return failed


def run_check(cf: str, files: list[Path], show_diff: bool = False) -> int:
    """检查模式：比较格式化前后，不修改文件"""
    passed = 0
    need_fmt = 0
    failed = 0

    for f in files:
        rel = relative_path(f)

        try:
            original = f.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            original = f.read_text(encoding="latin-1")

        try:
            result = subprocess.run(
                [cf, "-style=file", str(f)],
                capture_output=True, text=True, check=True,
                encoding="utf-8", errors="replace"
            )
            formatted = result.stdout
        except subprocess.CalledProcessError as e:
            print(_c(f"[FAIL] {rel}  (clang-format error)", Color.RED))
            print(e.stderr)
            failed += 1
            continue

        if original == formatted:
            print(_c(f"[ OK ] {rel}", Color.GREEN))
            passed += 1
        else:
            print(_c(f"[DIFF] {rel}  (needs formatting)", Color.YELLOW))
            need_fmt += 1
            if show_diff:
                _print_diff(original, formatted)

    print()
    print("=" * 50)
    print(_c(f" Check Complete: {passed} ok, {need_fmt} need format, {failed} error(s)", Color.CYAN))
    if need_fmt > 0:
        print(_c(" Run 'python format_code.py' to auto-fix all files.", Color.YELLOW))
    else:
        print(_c(" All files comply with the formatting rules!", Color.GREEN))
    return need_fmt + failed


def _print_diff(original: str, formatted: str) -> None:
    """打印逐行差异"""
    import difflib
    # 优先使用 unified diff
    diff = difflib.unified_diff(
        original.splitlines(keepends=True),
        formatted.splitlines(keepends=True),
        fromfile="before", tofile="after"
    )
    for line in diff:
        line = line.rstrip("\n")
        if line.startswith("---") or line.startswith("+++"):
            continue
        if line.startswith("@@"):
            print(_c(line, Color.CYAN))
        elif line.startswith("-"):
            print(_c(line, Color.RED))
        elif line.startswith("+"):
            print(_c(line, Color.GREEN))
        else:
            print(line)


def main():
    parser = argparse.ArgumentParser(
        description="Format C/C++ source files in app/ and bsp/ using clang-format"
    )
    parser.add_argument(
        "--check", action="store_true",
        help="Check only, do not modify files"
    )
    parser.add_argument(
        "--diff", action="store_true",
        help="Show detailed diff (only with --check)"
    )
    args = parser.parse_args()

    os.chdir(SCRIPT_DIR)

    cf = find_clang_format()
    check_config()
    files = collect_files()

    if args.check:
        exit_code = run_check(cf, files, show_diff=args.diff)
    else:
        exit_code = run_format(cf, files)

    sys.exit(0 if exit_code == 0 else 1)


if __name__ == "__main__":
    main()
