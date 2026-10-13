import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List

WORKSPACE_ROOT = Path(__file__).resolve().parents[3]
CODE_ROOT = WORKSPACE_ROOT / "Code"
SDK_ROOT = CODE_ROOT / "sdk"
TOOLS_ROOT = WORKSPACE_ROOT / "Tools"

PREBUILT_PATHS = [
    SDK_ROOT / "prebuilts" / "win32" / "bin",
    SDK_ROOT / "prebuilts" / "win32" / "cmake" / "bin",
    SDK_ROOT / "prebuilts" / "win32" / "python3",
    SDK_ROOT / "prebuilts" / "win32" / "gcc-arm-none-eabi" / "bin",
    SDK_ROOT / "tools",
    SDK_ROOT / "tools" / "win32",
]


def log(msg):
    print(f"[release] {msg}")


def add_tool_paths(env):
    prefixes = [str(p) for p in PREBUILT_PATHS if p.exists()]
    env["PATH"] = os.pathsep.join(prefixes + [env.get("PATH", "")])


def resolve_tool(exe_name, fallback):
    for p in PREBUILT_PATHS:
        candidate = p / exe_name
        if candidate.exists():
            return str(candidate)
    return fallback


def run_cmd(cmd, env):
    log("run " + " ".join(cmd))
    subprocess.run(cmd, check=True, env=env)


def compute_module_version(flash_size):
    if flash_size == 2:
        return "ZX800VG_CA"
    return "ZX800VG_SA"


def find_wifi_lib(build_dir):
    candidates = [
        build_dir / "lib" / "libwifi.a",
        build_dir / "lib" / "liblibwifi.a",
        build_dir / "components" / "wifi" / "libwifi.a",
        build_dir / "components" / "wifi" / "liblibwifi.a",
    ]
    for path in candidates:
        if path.exists():
            return path
    matches = sorted(build_dir.glob("**/libwifi*.a"))
    if matches:
        return matches[0]
    raise FileNotFoundError("libwifi archive not found; did the build succeed?")


def build_wifi(build_dir, flash_size):
    build_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["CUSTOMER_NAME"] = "template"
    env["module_version"] = compute_module_version(flash_size)
    env["para_flash_size"] = str(flash_size)
    add_tool_paths(env)

    cmake_bin = resolve_tool("cmake.exe", "cmake")
    ninja_bin = resolve_tool("ninja.exe", "ninja")

    cmake_cmd = [
        cmake_bin,
        "-S",
        str(SDK_ROOT),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCUSTOMER_NAME=template",
    ]
    run_cmd(cmake_cmd, env)
    run_cmd([ninja_bin, "-C", str(build_dir), "libwifi"], env)
    return find_wifi_lib(build_dir)


def ignore_sdk_paths(src, names):
    rel = Path(src).resolve().relative_to(CODE_ROOT)
    ignored = set()

    if rel == Path("sdk/customers"):
        ignored.update([n for n in names if n != "template"])

    if rel.parts[:3] == ("sdk", "components", "wifi"):
        ignored.update({n for n in names if n in {"src", "prebuilt"}})

    if rel == Path("sdk"):
        ignored.update([n for n in names if n == "out" or n.startswith("output_")])
    elif len(rel.parts) >= 2 and rel.parts[0] == "sdk":
        if rel.parts[1] == "out" or rel.parts[1].startswith("output_"):
            ignored.update(names)

    ignored.update({n for n in names if n in {".git", ".vs", "__pycache__"}})
    return sorted(ignored)


def copy_workspace(dst_root):
    code_dst = dst_root / "Code"
    tools_dst = dst_root / "Tools"
    code_dst.parent.mkdir(parents=True, exist_ok=True)

    log(f"Copy Code -> {code_dst}")
    if code_dst.exists():
        shutil.rmtree(code_dst)
    shutil.copytree(CODE_ROOT, code_dst, ignore=ignore_sdk_paths)

    if TOOLS_ROOT.exists():
        log(f"Copy Tools -> {tools_dst}")
        if tools_dst.exists():
            shutil.rmtree(tools_dst)
        shutil.copytree(TOOLS_ROOT, tools_dst)

    readme_src = WORKSPACE_ROOT / "README.md"
    if readme_src.exists():
        shutil.copy2(readme_src, dst_root / "README.md")


def sanitize_aws_block(release_root):
    mqtt_path = release_root / "Code" / "sdk" / "framework" / "device" / "framework_mqtt.c"
    if not mqtt_path.exists():
        return

    with mqtt_path.open("r", encoding="utf-8") as f:
        lines = f.readlines()

    start_idx = end_idx = -1
    for i, line in enumerate(lines):
        if "#ifdef CIS_AWS_MQTT_ENABLED" in line:
            start_idx = i
            break
    if start_idx != -1:
        for j in range(start_idx + 1, len(lines)):
            if lines[j].lstrip().startswith("#endif"):
                end_idx = j
                break

    if start_idx != -1 and end_idx != -1 and end_idx > start_idx:
        log("Sanitizing AWS MQTT block for release")
        replacement = ["/* CIS_AWS_MQTT_ENABLED block stripped for release */\n\n"]
        lines = lines[:start_idx] + replacement + lines[end_idx + 1 :]
        with mqtt_path.open("w", encoding="utf-8") as f:
            f.writelines(lines)


def stage_prebuilt_lib(lib_path, release_root):
    prebuilt_dir = release_root / "Code" / "sdk" / "components" / "wifi" / "prebuilt"
    prebuilt_dir.mkdir(parents=True, exist_ok=True)
    target = prebuilt_dir / lib_path.name
    shutil.copy2(lib_path, target)
    return target


def main(argv):
    parser = argparse.ArgumentParser(description="Build libwifi and assemble a sanitized release package.")
    parser.add_argument("--output", "-o", required=True, help="Release output directory")
    parser.add_argument("--build-dir", default=str(SDK_ROOT / "out" / "wifi_release"), help="CMake build directory")
    parser.add_argument("--flash-size", type=int, choices=[2, 4], default=4, help="Flash size to match module variant")
    parser.add_argument("--skip-build", action="store_true", help="Reuse existing libwifi build")
    parser.add_argument("--force", action="store_true", help="Overwrite existing output directory")
    args = parser.parse_args(argv)

    release_root = Path(args.output).resolve()
    if release_root.exists():
        if not args.force:
            raise SystemExit(f"Output path exists: {release_root}. Use --force to overwrite.")
        log("Cleaning existing output (preserving .git)")
        for child in release_root.iterdir():
            if child.name == ".git":
                continue
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child)
            else:
                child.unlink()
    else:
        release_root.mkdir(parents=True, exist_ok=True)

    build_dir = Path(args.build_dir).resolve()
    if not args.skip_build:
        log("Building libwifi (Release)")
        lib_path = build_wifi(build_dir, args.flash_size)
    else:
        log("Skipping build; locating existing libwifi")
        lib_path = find_wifi_lib(build_dir)

    log(f"libwifi located at {lib_path}")

    log("Copying workspace without customer data")
    copy_workspace(release_root)

    sanitize_aws_block(release_root)

    staged_lib = stage_prebuilt_lib(lib_path, release_root)
    log(f"Staged prebuilt lib -> {staged_lib}")

    log(f"Release ready: {release_root}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
