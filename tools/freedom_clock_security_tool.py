#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import textwrap
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SKETCH_PATH = REPO_ROOT / "Freedom_Clock_HeltecVME213.ino"
PARTITIONS_PATH = REPO_ROOT / "partitions.csv"
WORKDIR = REPO_ROOT / "provisioning-workdir"
RELEASES_DIR = WORKDIR / "releases"
VAULT_DIR = WORKDIR / "vault"
BUNDLES_DIR = WORKDIR / "bundles"
PUBLIC_UPDATES_DIR = WORKDIR / "public-updates"
BOOTLOADER_PROJECT_DIR = WORKDIR / "idf-secure-boot-project"
BOOTLOADERS_DIR = WORKDIR / "bootloaders"
BUILD_ROOT = WORKDIR / "arduino-build"
DEFAULT_IDF_ROOT = WORKDIR / "esp-idf"
DOCS_DIR = REPO_ROOT / "docs"
RELEASE_NOTES_DIR = DOCS_DIR / "releases"
LOCAL_GITHUB_TOKEN_PATH = Path.home() / ".freedom-clock" / "github-token"

BOARD_FQBN = "Heltec-esp32:esp32:heltec_vision_master_e_213"
ARDUINO_CLI_DEFAULT = Path(
    "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
)
ARDUINO_LIBRARIES_DEFAULT = Path.home() / "Documents" / "Arduino" / "libraries"
HELTEC_PACKAGE_ROOT = Path.home() / "Library" / "Arduino15" / "packages" / "Heltec-esp32"
ESPTOOL_ROOT = HELTEC_PACKAGE_ROOT / "tools" / "esptool_py" / "5.2.0"
ESPTOOL_BIN = ESPTOOL_ROOT / "esptool"
ESPEFUSE_BIN = ESPTOOL_ROOT / "espefuse"
ESPSECURE_BIN = ESPTOOL_ROOT / "espsecure"
ARDUINO_PLATFORM_ROOT = HELTEC_PACKAGE_ROOT / "hardware" / "esp32" / "3.3.8"
BOOT_APP0_BIN = ARDUINO_PLATFORM_ROOT / "tools" / "partitions" / "boot_app0.bin"
CHIP = "esp32s3"

FIRMWARE_VERSION_RE = re.compile(
    r'FIRMWARE_VERSION\[\]\s*=\s*"(?P<version>[^"]+)"'
)

SECURE_BOOT_PROJECT_CMAKE = """\
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(freedom_clock_secure_boot)
"""

SECURE_BOOT_PROJECT_MAIN_CMAKE = """\
idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")
"""

SECURE_BOOT_PROJECT_MAIN_C = """\
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
"""

SECURE_BOOT_PROJECT_SDKCONFIG_DEFAULTS = """\
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHMODE="qio"
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHFREQ="80m"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
CONFIG_BOOTLOADER_OFFSET_IN_FLASH=0x0
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000
CONFIG_SECURE_BOOT=y
CONFIG_SECURE_BOOT_V2_ENABLED=y
CONFIG_SECURE_BOOTLOADER_REFLASHABLE=y
# CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES is not set
"""


class ToolError(RuntimeError):
    pass


def ensure_workspace() -> None:
    for path in [WORKDIR, RELEASES_DIR, VAULT_DIR, BUNDLES_DIR, PUBLIC_UPDATES_DIR, BOOTLOADERS_DIR, BUILD_ROOT, DOCS_DIR, RELEASE_NOTES_DIR]:
        path.mkdir(parents=True, exist_ok=True)


def timestamp_slug() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def print_step(message: str) -> None:
    print(f"\n==> {message}")


def run_command(
    cmd: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    capture_output: bool = True,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    printable = " ".join(shlex.quote(part) for part in cmd)
    print(f"$ {printable}")
    result = subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        text=True,
        capture_output=capture_output,
    )
    if check and result.returncode != 0:
        stderr = result.stderr.strip() if result.stderr else ""
        stdout = result.stdout.strip() if result.stdout else ""
        details = stderr or stdout or f"Command exited with {result.returncode}"
        raise ToolError(f"Command failed: {printable}\n{details}")
    return result


def run_bash(script: str, *, cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    return run_command(
        ["/bin/bash", "-lc", script],
        cwd=cwd,
        check=check,
    )


def resolve_arduino_cli() -> Path:
    env_override = os.environ.get("FREEDOM_CLOCK_ARDUINO_CLI")
    if env_override:
        path = Path(env_override).expanduser()
        if path.exists():
            return path
    if ARDUINO_CLI_DEFAULT.exists():
        return ARDUINO_CLI_DEFAULT
    which_path = shutil.which("arduino-cli")
    if which_path:
        return Path(which_path)
    raise ToolError("arduino-cli was not found. Install Arduino IDE or set FREEDOM_CLOCK_ARDUINO_CLI.")


def require_file(path: Path, description: str) -> Path:
    if not path.exists():
        raise ToolError(f"{description} was not found at {path}")
    return path


def current_firmware_version() -> str:
    match = FIRMWARE_VERSION_RE.search(SKETCH_PATH.read_text())
    if not match:
        return "UNKNOWN"
    return match.group("version")


def serial_ports() -> list[str]:
    patterns = ["/dev/cu.usb*", "/dev/cu.wch*", "/dev/cu.SLAB_*", "/dev/cu.*"]
    found: list[str] = []
    for pattern in patterns:
        for entry in sorted(Path("/dev").glob(pattern.replace("/dev/", ""))):
            value = str(entry)
            if value not in found:
                found.append(value)
    return found


def platform_bootloader_candidates() -> list[Path]:
    sdk_bin_dir = HELTEC_PACKAGE_ROOT / "tools" / "esp32-arduino-libs"
    return sorted(sdk_bin_dir.glob("idf-release_*/esp32s3/bin/bootloader_qio_80m.elf"))


def latest_release_dir() -> Path:
    ensure_workspace()
    releases = [path for path in RELEASES_DIR.iterdir() if path.is_dir()]
    if not releases:
        raise ToolError("No release bundle exists yet. Run build-release first.")
    return max(releases, key=lambda path: path.stat().st_mtime)


def latest_manual_update_dir() -> Path:
    ensure_workspace()
    releases = [path for path in PUBLIC_UPDATES_DIR.iterdir() if path.is_dir()]
    if not releases:
        raise ToolError("No manual update package exists yet. Run build-manual-update first.")
    return max(releases, key=lambda path: path.stat().st_mtime)


def github_repo_slug() -> str:
    result = run_command(
        ["git", "config", "--get", "remote.origin.url"],
        cwd=REPO_ROOT,
    )
    remote = (result.stdout or "").strip()
    match = re.search(r"github\.com[:/](?P<owner>[^/]+)/(?P<repo>[^/.]+)(?:\.git)?$", remote)
    if not match:
        raise ToolError(f"Could not parse GitHub owner/repo from origin URL: {remote or 'missing'}")
    return f"{match.group('owner')}/{match.group('repo')}"


def parse_flash_args(flash_args_path: Path) -> dict[str, str]:
    lines = [line.strip() for line in flash_args_path.read_text().splitlines() if line.strip()]
    offsets: dict[str, str] = {}
    for line in lines[1:]:
        parts = line.split()
        if len(parts) != 2:
            continue
        offset, name = parts
        offsets[name] = offset
    return offsets


def github_token() -> str:
    token = (
        os.environ.get("FREEDOM_CLOCK_GITHUB_TOKEN")
        or os.environ.get("GITHUB_TOKEN")
        or os.environ.get("GH_TOKEN")
    )
    if not token and LOCAL_GITHUB_TOKEN_PATH.exists():
        token = LOCAL_GITHUB_TOKEN_PATH.read_text().strip()
    if not token:
        raise ToolError(
            "GitHub token not found. Set FREEDOM_CLOCK_GITHUB_TOKEN, GITHUB_TOKEN, GH_TOKEN, or store it at ~/.freedom-clock/github-token."
        )
    return token


def github_api_request(
    method: str,
    url: str,
    *,
    token: str,
    payload: dict | list | None = None,
    binary: bytes | None = None,
    content_type: str = "application/vnd.github+json",
) -> tuple[int, dict | list | str]:
    if shutil.which("curl"):
        return github_api_request_with_curl(
            method,
            url,
            token=token,
            payload=payload,
            binary=binary,
            content_type=content_type,
        )

    if payload is not None and binary is not None:
        raise ToolError("Pass either JSON payload or binary payload, not both.")

    data: bytes | None = None
    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "FreedomClockSecurityTool/2026",
    }
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json; charset=utf-8"
    elif binary is not None:
        data = binary
        headers["Content-Type"] = content_type

    request = urllib.request.Request(url, data=data, method=method, headers=headers)

    try:
        with urllib.request.urlopen(request) as response:
            raw = response.read()
            body_text = raw.decode("utf-8", errors="replace") if raw else ""
            if not body_text:
                return response.status, ""
            try:
                return response.status, json.loads(body_text)
            except json.JSONDecodeError:
                return response.status, body_text
    except urllib.error.HTTPError as exc:
        raw = exc.read()
        body_text = raw.decode("utf-8", errors="replace") if raw else ""
        if body_text:
            try:
                parsed = json.loads(body_text)
            except json.JSONDecodeError:
                parsed = body_text
        else:
            parsed = ""
        return exc.code, parsed
    except urllib.error.URLError as exc:
        raise ToolError(f"GitHub API request failed: {exc}") from exc


def github_api_error_message(body: dict | list | str) -> str:
    if isinstance(body, dict):
        message = str(body.get("message") or "Unknown GitHub API error")
        errors = body.get("errors")
        if errors:
            return f"{message} ({errors})"
        return message
    if isinstance(body, list):
        return json.dumps(body)
    return str(body) if body else "Unknown GitHub API error"


def github_api_request_with_curl(
    method: str,
    url: str,
    *,
    token: str,
    payload: dict | list | None = None,
    binary: bytes | None = None,
    content_type: str = "application/vnd.github+json",
) -> tuple[int, dict | list | str]:
    curl_bin = shutil.which("curl")
    if not curl_bin:
        raise ToolError("curl was not found.")
    if payload is not None and binary is not None:
        raise ToolError("Pass either JSON payload or binary payload, not both.")

    data: bytes | None = None
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        content_type = "application/json; charset=utf-8"
    elif binary is not None:
        data = binary

    status_marker = "__FREEDOM_CLOCK_HTTP_STATUS__:"
    config_lines = [
        "silent",
        "show-error",
        "location",
        f'request = "{method}"',
        f'url = "{url}"',
        'header = "Accept: application/vnd.github+json"',
        f'header = "Authorization: Bearer {token}"',
        'header = "X-GitHub-Api-Version: 2022-11-28"',
        'header = "User-Agent: FreedomClockSecurityTool/2026"',
        f'header = "Content-Type: {content_type}"',
        f'write-out = "\\n{status_marker}%{{http_code}}"',
    ]
    if data is not None:
        config_lines.append('data-binary = "@-"')

    ensure_workspace()
    config_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            "w",
            encoding="utf-8",
            prefix="github-curl-",
            suffix=".conf",
            dir=WORKDIR,
            delete=False,
        ) as handle:
            config_path = Path(handle.name)
            handle.write("\n".join(config_lines) + "\n")
        config_path.chmod(0o600)

        result = subprocess.run(
            [curl_bin, "--config", str(config_path)],
            input=data,
            capture_output=True,
        )
    finally:
        if config_path and config_path.exists():
            config_path.unlink()

    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace").strip()
        raise ToolError(f"GitHub API request failed: {stderr or 'curl exited with an error'}")

    raw = result.stdout
    marker_bytes = ("\n" + status_marker).encode("utf-8")
    if marker_bytes not in raw:
        raise ToolError("GitHub API response did not include an HTTP status marker.")
    body_bytes, status_bytes = raw.rsplit(marker_bytes, 1)
    status = int(status_bytes.decode("utf-8", errors="replace").strip())
    body_text = body_bytes.decode("utf-8", errors="replace") if body_bytes else ""
    if not body_text:
        return status, ""
    try:
        return status, json.loads(body_text)
    except json.JSONDecodeError:
        return status, body_text


def detect_tools() -> dict[str, str | bool | list[str]]:
    return {
        "repo_root": str(REPO_ROOT),
        "sketch_exists": SKETCH_PATH.exists(),
        "partitions_exists": PARTITIONS_PATH.exists(),
        "python": sys.executable,
        "arduino_cli": str(resolve_arduino_cli()) if True else "",
        "esptool": str(require_file(ESPTOOL_BIN, "esptool")),
        "espefuse": str(require_file(ESPEFUSE_BIN, "espefuse")),
        "espsecure": str(require_file(ESPSECURE_BIN, "espsecure")),
        "boot_app0": str(require_file(BOOT_APP0_BIN, "boot_app0.bin")),
        "idf_py_on_path": shutil.which("idf.py") or "",
        "idf_bootstrap_dir": str(DEFAULT_IDF_ROOT),
        "secure_boot_project": str(BOOTLOADER_PROJECT_DIR),
        "workspace": str(WORKDIR),
        "ports": serial_ports(),
        "firmware_version": current_firmware_version(),
        "bootloader_elf_candidates": [str(path) for path in platform_bootloader_candidates()],
    }


def build_release(args: argparse.Namespace) -> Path:
    ensure_workspace()
    arduino_cli = resolve_arduino_cli()
    build_name = args.release_name or timestamp_slug()
    build_dir = BUILD_ROOT / build_name
    release_dir = RELEASES_DIR / build_name
    build_dir.mkdir(parents=True, exist_ok=True)
    release_dir.mkdir(parents=True, exist_ok=True)

    print_step("Compiling the Arduino sketch")
    run_command(
        [
            str(arduino_cli),
            "compile",
            "--fqbn",
            BOARD_FQBN,
            "--libraries",
            str(ARDUINO_LIBRARIES_DEFAULT),
            "--build-path",
            str(build_dir),
            str(REPO_ROOT),
        ]
    )

    artifact_map = {
        "app.bin": build_dir / "Freedom_Clock_HeltecVME213.ino.bin",
        "bootloader.bin": build_dir / "Freedom_Clock_HeltecVME213.ino.bootloader.bin",
        "partitions.bin": build_dir / "Freedom_Clock_HeltecVME213.ino.partitions.bin",
        "boot_app0.bin": BOOT_APP0_BIN,
        "flash_args.txt": build_dir / "flash_args",
        "sdkconfig": build_dir / "sdkconfig",
        "partitions.csv": build_dir / "partitions.csv",
        "merged.bin": build_dir / "Freedom_Clock_HeltecVME213.ino.merged.bin",
    }
    copied: dict[str, dict[str, str | int]] = {}
    print_step(f"Copying release artifacts into {release_dir}")
    for output_name, source in artifact_map.items():
        require_file(source, output_name)
        destination = release_dir / output_name
        shutil.copy2(source, destination)
        copied[output_name] = {
            "sha256": sha256_file(destination),
            "size_bytes": destination.stat().st_size,
        }

    offsets = parse_flash_args(release_dir / "flash_args.txt")
    manifest = {
        "created_at_utc": timestamp_slug(),
        "release_name": build_name,
        "firmware_version": current_firmware_version(),
        "fqbn": BOARD_FQBN,
        "files": copied,
        "offsets": {
            "bootloader": offsets.get("Freedom_Clock_HeltecVME213.ino.bootloader.bin", "0x0"),
            "partitions": offsets.get("Freedom_Clock_HeltecVME213.ino.partitions.bin", "0x8000"),
            "boot_app0": offsets.get("boot_app0.bin", "0xe000"),
            "app": offsets.get("Freedom_Clock_HeltecVME213.ino.bin", "0x10000"),
        },
    }
    (release_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Release ready: {release_dir}")
    return release_dir


def generate_keys(args: argparse.Namespace) -> Path:
    ensure_workspace()
    device_id = args.device_id
    if not device_id:
        raise ToolError("--device-id is required for key generation.")

    global_dir = VAULT_DIR / "global"
    device_dir = VAULT_DIR / "devices" / device_id
    global_dir.mkdir(parents=True, exist_ok=True)
    device_dir.mkdir(parents=True, exist_ok=True)

    signing_key = global_dir / "secure_boot_signing_key.pem"
    signing_digest = global_dir / "secure_boot_signing_key_digest.bin"
    flash_key = device_dir / "flash_encryption_key.bin"

    force = getattr(args, "force", False)

    if not signing_key.exists() or force:
        print_step("Generating Secure Boot V2 signing key")
        run_command(
            [
                str(ESPSECURE_BIN),
                "generate-signing-key",
                "--version",
                "2",
                "--scheme",
                "rsa3072",
                str(signing_key),
            ]
        )
    if not signing_digest.exists() or force:
        print_step("Generating Secure Boot V2 public-key digest")
        run_command(
            [
                str(ESPSECURE_BIN),
                "digest-sbv2-public-key",
                "--keyfile",
                str(signing_key),
                "--output",
                str(signing_digest),
            ]
        )
    if not flash_key.exists() or force:
        print_step(f"Generating per-device flash-encryption key for {device_id}")
        run_command(
            [
                str(ESPSECURE_BIN),
                "generate-flash-encryption-key",
                "--keylen",
                "512",
                str(flash_key),
            ]
        )

    manifest = {
        "device_id": device_id,
        "created_at_utc": timestamp_slug(),
        "secure_boot_key_sha256": sha256_file(signing_key),
        "secure_boot_digest_sha256": sha256_file(signing_digest),
        "flash_encryption_key_sha256": sha256_file(flash_key),
    }
    (device_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Key material ready under: {device_dir}")
    return device_dir


def scaffold_secure_boot_project(args: argparse.Namespace) -> Path:
    ensure_workspace()
    print_step("Scaffolding the ESP-IDF secure-boot helper project")
    (BOOTLOADER_PROJECT_DIR / "main").mkdir(parents=True, exist_ok=True)
    (BOOTLOADER_PROJECT_DIR / "CMakeLists.txt").write_text(SECURE_BOOT_PROJECT_CMAKE)
    (BOOTLOADER_PROJECT_DIR / "main" / "CMakeLists.txt").write_text(SECURE_BOOT_PROJECT_MAIN_CMAKE)
    (BOOTLOADER_PROJECT_DIR / "main" / "main.c").write_text(SECURE_BOOT_PROJECT_MAIN_C)
    (BOOTLOADER_PROJECT_DIR / "sdkconfig.defaults").write_text(SECURE_BOOT_PROJECT_SDKCONFIG_DEFAULTS)
    shutil.copy2(PARTITIONS_PATH, BOOTLOADER_PROJECT_DIR / "partitions.csv")
    project_readme = textwrap.dedent(
        """\
        This helper project exists only to build a Secure Boot V2 capable second-stage bootloader.

        The Freedom Clock Arduino sketch is still compiled separately with Arduino CLI.
        The production tool signs the Arduino app image and pairs it with the bootloader built here.
        """
    )
    (BOOTLOADER_PROJECT_DIR / "README.md").write_text(project_readme)
    print(f"Helper project ready: {BOOTLOADER_PROJECT_DIR}")
    return BOOTLOADER_PROJECT_DIR


def bootstrap_idf(args: argparse.Namespace) -> Path:
    ensure_workspace()
    idf_root = Path(args.idf_root).expanduser() if args.idf_root else DEFAULT_IDF_ROOT
    git = shutil.which("git")
    if not git:
        raise ToolError("git is required to bootstrap ESP-IDF.")

    if not idf_root.exists():
        print_step(f"Cloning ESP-IDF into {idf_root}")
        run_command(
            [
                git,
                "clone",
                "--depth",
                "1",
                "--branch",
                args.idf_ref,
                "https://github.com/espressif/esp-idf.git",
                str(idf_root),
            ]
        )
    install_script = idf_root / "install.sh"
    require_file(install_script, "ESP-IDF install.sh")
    print_step("Installing the ESP-IDF Python environment")
    run_bash(f"cd {shlex.quote(str(idf_root))} && ./install.sh esp32s3")
    print(f"ESP-IDF bootstrap complete: {idf_root}")
    return idf_root


def resolve_idf_root(user_supplied: str | None) -> Path:
    env_idf = os.environ.get("IDF_PATH")
    if user_supplied:
        return Path(user_supplied).expanduser()
    if env_idf:
        return Path(env_idf).expanduser()
    return DEFAULT_IDF_ROOT


def build_secure_boot_project(args: argparse.Namespace) -> Path:
    ensure_workspace()
    scaffold_secure_boot_project(args)
    idf_root = resolve_idf_root(args.idf_root)
    export_script = idf_root / "export.sh"
    if not export_script.exists():
        raise ToolError(
            f"ESP-IDF is not ready at {idf_root}. Run:\n"
            f"  ./FreedomClockSecurityTool.command bootstrap-idf\n"
            f"and then rerun build-secure-boot-project."
        )

    print_step("Building the Secure Boot V2 helper bootloader with ESP-IDF")
    script = textwrap.dedent(
        f"""\
        export IDF_PATH={shlex.quote(str(idf_root))}
        source {shlex.quote(str(export_script))} >/dev/null
        cd {shlex.quote(str(BOOTLOADER_PROJECT_DIR))}
        idf.py set-target esp32s3 build
        """
    )
    run_bash(script)

    built_bootloader = BOOTLOADER_PROJECT_DIR / "build" / "bootloader" / "bootloader.bin"
    require_file(built_bootloader, "built secure-boot bootloader")
    output_bootloader = BOOTLOADERS_DIR / "secure_boot_v2_bootloader.bin"
    shutil.copy2(built_bootloader, output_bootloader)
    print(f"Secure-boot bootloader ready: {output_bootloader}")
    return output_bootloader


def prepare_bundle(args: argparse.Namespace) -> Path:
    ensure_workspace()
    release_dir = Path(args.release_dir).expanduser() if args.release_dir else latest_release_dir()
    device_id = args.device_id
    if not device_id:
        raise ToolError("--device-id is required.")

    flash_key = VAULT_DIR / "devices" / device_id / "flash_encryption_key.bin"
    require_file(flash_key, "per-device flash-encryption key")

    mode = args.mode
    bundle_dir = BUNDLES_DIR / device_id / f"{release_dir.name}-{mode}"
    bundle_dir.mkdir(parents=True, exist_ok=True)

    manifest = json.loads((release_dir / "manifest.json").read_text())
    offsets = manifest["offsets"]

    bootloader_source = release_dir / "bootloader.bin"
    signed_app_source = release_dir / "app.bin"

    if mode == "full":
        secure_boot_key = VAULT_DIR / "global" / "secure_boot_signing_key.pem"
        require_file(secure_boot_key, "Secure Boot signing key")
        secure_boot_bootloader = Path(args.secure_boot_bootloader).expanduser() if args.secure_boot_bootloader else BOOTLOADERS_DIR / "secure_boot_v2_bootloader.bin"
        if not secure_boot_bootloader.exists():
            raise ToolError(
                f"Secure Boot bootloader was not found at {secure_boot_bootloader}.\n"
                f"Run:\n"
                f"  ./FreedomClockSecurityTool.command build-secure-boot-project\n"
                f"or pass --secure-boot-bootloader PATH."
            )
        print_step("Signing the secure-boot bootloader")
        signed_bootloader = bundle_dir / "bootloader.signed.bin"
        run_command(
            [
                str(ESPSECURE_BIN),
                "sign-data",
                "--version",
                "2",
                "--keyfile",
                str(secure_boot_key),
                "--output",
                str(signed_bootloader),
                str(secure_boot_bootloader),
            ]
        )
        bootloader_source = signed_bootloader

        print_step("Signing the Arduino application image")
        signed_app = bundle_dir / "app.signed.bin"
        run_command(
            [
                str(ESPSECURE_BIN),
                "sign-data",
                "--version",
                "2",
                "--keyfile",
                str(secure_boot_key),
                "--output",
                str(signed_app),
                str(release_dir / "app.bin"),
            ]
        )
        signed_app_source = signed_app

    print_step("Encrypting flash images for this device")
    files_to_encrypt = [
        ("bootloader.encrypted.bin", bootloader_source, offsets["bootloader"]),
        ("partitions.encrypted.bin", release_dir / "partitions.bin", offsets["partitions"]),
        ("boot_app0.encrypted.bin", release_dir / "boot_app0.bin", offsets["boot_app0"]),
        ("app.encrypted.bin", signed_app_source, offsets["app"]),
    ]
    encrypted_manifest: dict[str, dict[str, str | int]] = {}
    for output_name, source_path, address in files_to_encrypt:
        destination = bundle_dir / output_name
        run_command(
            [
                str(ESPSECURE_BIN),
                "encrypt-flash-data",
                "--aes-xts",
                "--keyfile",
                str(flash_key),
                "--address",
                str(address),
                "--output",
                str(destination),
                str(source_path),
            ]
        )
        encrypted_manifest[output_name] = {
            "source": source_path.name,
            "address": address,
            "sha256": sha256_file(destination),
            "size_bytes": destination.stat().st_size,
        }

    (bundle_dir / "manifest.json").write_text(
        json.dumps(
            {
                "created_at_utc": timestamp_slug(),
                "mode": mode,
                "device_id": device_id,
                "release_dir": str(release_dir),
                "encrypted_files": encrypted_manifest,
            },
            indent=2,
        )
        + "\n"
    )
    print(f"Provisioning bundle ready: {bundle_dir}")
    return bundle_dir


def build_manual_update(args: argparse.Namespace) -> Path:
    ensure_workspace()
    release_dir = build_release(args)
    release_manifest = json.loads((release_dir / "manifest.json").read_text())
    version = release_manifest["firmware_version"]
    output_dir = PUBLIC_UPDATES_DIR / release_dir.name
    output_dir.mkdir(parents=True, exist_ok=True)

    files: dict[str, dict[str, str | int | bool]] = {}

    open_update_path = output_dir / f"FreedomClock-{version}-manual-update-open.bin"
    shutil.copy2(release_dir / "app.bin", open_update_path)
    files["open"] = {
        "filename": open_update_path.name,
        "sha256": sha256_file(open_update_path),
        "size_bytes": open_update_path.stat().st_size,
        "requires_secure_boot": False,
    }

    secure_boot_key = VAULT_DIR / "global" / "secure_boot_signing_key.pem"
    if secure_boot_key.exists():
        secure_update_path = output_dir / f"FreedomClock-{version}-manual-update-secure.bin"
        print_step("Signing the public manual-update image for secure devices")
        run_command(
            [
                str(ESPSECURE_BIN),
                "sign-data",
                "--version",
                "2",
                "--keyfile",
                str(secure_boot_key),
                "--output",
                str(secure_update_path),
                str(release_dir / "app.bin"),
            ]
        )
        files["secure"] = {
            "filename": secure_update_path.name,
            "sha256": sha256_file(secure_update_path),
            "size_bytes": secure_update_path.stat().st_size,
            "requires_secure_boot": True,
        }

    readme_text = textwrap.dedent(
        f"""\
        Freedom Clock manual firmware update package
        ==========================================

        Firmware version: {version}
        Built from release bundle: {release_dir.name}

        Files:
        - {files["open"]["filename"]}: use for normal development boards and devices that are not provisioned with Secure Boot.
        """
    )
    if "secure" in files:
        readme_text += (
            f"- {files['secure']['filename']}: use for production-hardened devices that were provisioned with Secure Boot.\n"
        )
    else:
        readme_text += "- No secure-device package was generated because the Secure Boot signing key is not available on this Mac.\n"
    readme_text += textwrap.dedent(
        """\

        How users install it:
        1. Join the device setup Wi-Fi.
        2. Open http://192.168.4.1
        3. Go to Firmware Update.
        4. Upload the correct .bin file.

        Notes:
        - Saved settings stay on the device.
        - This package is for local setup-page upload, not for the production provisioning tool.
        """
    )
    (output_dir / "README.txt").write_text(readme_text)

    checksums_lines = [
        f"{metadata['sha256']}  {metadata['filename']}"
        for metadata in files.values()
    ]
    (output_dir / "SHA256SUMS.txt").write_text("\n".join(checksums_lines) + "\n")

    manifest = {
        "created_at_utc": timestamp_slug(),
        "firmware_version": version,
        "release_name": release_dir.name,
        "source_release_dir": str(release_dir),
        "files": files,
    }
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Manual update package ready: {output_dir}")
    return output_dir


def resolve_manual_update_dir(args: argparse.Namespace) -> Path:
    if getattr(args, "release_name", None):
        candidate = PUBLIC_UPDATES_DIR / args.release_name
        if not candidate.exists():
            raise ToolError(f"Manual update package not found: {candidate}")
        return candidate
    return latest_manual_update_dir()


def default_release_notes_path(version: str) -> Path:
    return RELEASE_NOTES_DIR / f"v{version}.md"


def release_title_for_version(version: str) -> str:
    return f"Freedom Clock {version}"


def ensure_release_notes_file(path: Path, version: str) -> None:
    if path.exists():
        return
    template = textwrap.dedent(
        f"""\
        # {release_title_for_version(version)}

        ## Highlights

        - Describe the most important user-facing changes here.
        - Mention any firmware update or setup-flow changes.
        - Mention any security, privacy, or reliability improvements.

        ## Installation

        - `FreedomClock-{version}-manual-update-open.bin` for normal devices
        - `FreedomClock-{version}-manual-update-secure.bin` for security-hardened devices
        """
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(template)


def upload_release_asset(upload_url_template: str, asset_path: Path, token: str) -> None:
    upload_base = upload_url_template.split("{", 1)[0]
    upload_url = f"{upload_base}?name={urllib.parse.quote(asset_path.name)}"
    mime_type = mimetypes.guess_type(asset_path.name)[0] or "application/octet-stream"
    status, body = github_api_request(
        "POST",
        upload_url,
        token=token,
        binary=asset_path.read_bytes(),
        content_type=mime_type,
    )
    if status not in (200, 201):
        raise ToolError(f"Asset upload failed for {asset_path.name}: {github_api_error_message(body)}")


def publish_github_release(args: argparse.Namespace) -> None:
    ensure_workspace()
    repo_slug = github_repo_slug()
    manual_dir = resolve_manual_update_dir(args)
    manifest = json.loads((manual_dir / "manifest.json").read_text())
    version = manifest["firmware_version"]
    tag_name = args.tag or f"v{version}"
    release_title = args.title or release_title_for_version(version)
    notes_path = Path(args.notes_file).expanduser() if args.notes_file else default_release_notes_path(version)
    ensure_release_notes_file(notes_path, version)
    release_body = notes_path.read_text().strip()
    if not release_body:
        raise ToolError(f"Release notes file is empty: {notes_path}")

    if not args.confirm_publish:
        print(
            "\nThis command publishes a public GitHub Release and uploads firmware assets.\n"
            "Build and test the manual update package locally first. Then rerun with:\n"
            "  --confirm-publish\n"
        )
        print(f"Release tag: {tag_name}")
        print(f"Release title: {release_title}")
        print(f"Release notes: {notes_path}")
        print(f"Assets folder: {manual_dir}")
        return

    token = github_token()
    print_step(f"Publishing GitHub Release {tag_name} to {repo_slug}")

    api_base = f"https://api.github.com/repos/{repo_slug}/releases"
    release_payload = {
        "tag_name": tag_name,
        "name": release_title,
        "body": release_body,
        "draft": bool(args.draft),
        "prerelease": bool(args.prerelease),
        "generate_release_notes": False,
    }

    status, body = github_api_request("POST", api_base, token=token, payload=release_payload)
    if status in (200, 201):
        release = body
    elif status == 422:
        fetch_status, fetch_body = github_api_request(
            "GET",
            f"{api_base}/tags/{urllib.parse.quote(tag_name)}",
            token=token,
        )
        if fetch_status != 200 or not isinstance(fetch_body, dict):
            raise ToolError(f"Could not fetch existing release for {tag_name}: {github_api_error_message(fetch_body)}")
        release_id = fetch_body["id"]
        patch_status, patch_body = github_api_request(
            "PATCH",
            f"{api_base}/{release_id}",
            token=token,
            payload={
                "name": release_title,
                "body": release_body,
                "draft": bool(args.draft),
                "prerelease": bool(args.prerelease),
            },
        )
        if patch_status != 200 or not isinstance(patch_body, dict):
            raise ToolError(f"Could not update existing release {tag_name}: {github_api_error_message(patch_body)}")
        release = patch_body
    else:
        raise ToolError(f"Could not create GitHub Release {tag_name}: {github_api_error_message(body)}")

    if not isinstance(release, dict):
        raise ToolError("GitHub API returned an unexpected release payload.")

    upload_url = release["upload_url"]
    assets_url = release["assets_url"]
    html_url = release.get("html_url", "")

    existing_assets: dict[str, int] = {}
    assets_status, assets_body = github_api_request("GET", assets_url, token=token)
    if assets_status == 200 and isinstance(assets_body, list):
        for asset in assets_body:
            if isinstance(asset, dict) and "name" in asset and "id" in asset:
                existing_assets[str(asset["name"])] = int(asset["id"])

    asset_paths = [
        manual_dir / metadata["filename"]
        for metadata in manifest["files"].values()
    ]
    for extra_name in ["SHA256SUMS.txt", "manifest.json", "README.txt"]:
        asset_paths.append(manual_dir / extra_name)

    seen_names: set[str] = set()
    for asset_path in asset_paths:
        if not asset_path.exists():
            raise ToolError(f"Release asset not found: {asset_path}")
        if asset_path.name in seen_names:
            continue
        seen_names.add(asset_path.name)
        if asset_path.name in existing_assets:
            delete_status, delete_body = github_api_request(
                "DELETE",
                f"{api_base}/assets/{existing_assets[asset_path.name]}",
                token=token,
            )
            if delete_status not in (204, 200):
                raise ToolError(f"Could not replace existing asset {asset_path.name}: {github_api_error_message(delete_body)}")
        print_step(f"Uploading {asset_path.name}")
        upload_release_asset(upload_url, asset_path, token)

    print(f"GitHub Release published: {html_url or f'https://github.com/{repo_slug}/releases/tag/{tag_name}'}")
    print(f"Release notes source: {notes_path}")
    print(f"Assets uploaded from: {manual_dir}")


def prompt_confirmation(required_text: str, skip_prompt: bool) -> None:
    if skip_prompt:
        return
    print(
        "\nThis step burns permanent eFuses.\n"
        "If you continue, this device stops being a normal development board."
    )
    typed = input(f"Type exactly '{required_text}' to continue:\n> ").strip()
    if typed != required_text:
        raise ToolError("Confirmation text did not match. Aborting before any irreversible step.")


def esptool_write_flash(port: str, encrypted_files: list[tuple[str, Path]]) -> None:
    cmd = [
        str(ESPTOOL_BIN),
        "--chip",
        CHIP,
        "--port",
        port,
        "write-flash",
        "--flash-mode",
        "dio",
        "--flash-freq",
        "80m",
        "--flash-size",
        "8MB",
    ]
    for offset, path in encrypted_files:
        cmd.extend([offset, str(path)])
    run_command(cmd, capture_output=False)


def secure_boot_bootloader_path(args: argparse.Namespace) -> Path:
    return Path(args.secure_boot_bootloader).expanduser() if getattr(args, "secure_boot_bootloader", None) else BOOTLOADERS_DIR / "secure_boot_v2_bootloader.bin"


def ensure_release_and_bundle(args: argparse.Namespace) -> tuple[Path, Path]:
    if getattr(args, "bootstrap_idf", False):
        bootstrap_idf(args)
    if args.mode == "full" and not secure_boot_bootloader_path(args).exists():
        build_secure_boot_project(args)

    release_dir = build_release(args)
    generate_keys(args)
    bundle_dir = prepare_bundle(
        argparse.Namespace(
            release_dir=str(release_dir),
            device_id=args.device_id,
            mode=args.mode,
            secure_boot_bootloader=getattr(args, "secure_boot_bootloader", None),
        )
    )
    return release_dir, bundle_dir


def provisioning_mutation_steps(args: argparse.Namespace, bundle_dir: Path) -> list[tuple[str, list[str]]]:
    flash_key = VAULT_DIR / "devices" / args.device_id / "flash_encryption_key.bin"
    digest_bin = VAULT_DIR / "global" / "secure_boot_signing_key_digest.bin"
    steps: list[tuple[str, list[str]]] = [
        (
            "Erase flash",
            [str(ESPTOOL_BIN), "--chip", CHIP, "--port", args.port, "erase-flash"],
        ),
        (
            "Burn the flash-encryption key",
            [
                str(ESPEFUSE_BIN),
                "--chip",
                CHIP,
                "--port",
                args.port,
                "burn-key",
                "BLOCK_KEY0",
                str(flash_key),
                "XTS_AES_256_KEY",
            ],
        ),
        (
            "Enable flash encryption and harden download paths",
            [
                str(ESPEFUSE_BIN),
                "--chip",
                CHIP,
                "--port",
                args.port,
                "burn-efuse",
                "SPI_BOOT_CRYPT_CNT",
                "7",
                "DIS_DOWNLOAD_ICACHE",
                "0x1",
                "DIS_DOWNLOAD_DCACHE",
                "0x1",
                "SOFT_DIS_JTAG",
                "0x1",
                "DIS_DIRECT_BOOT",
                "0x1",
                "DIS_USB_JTAG",
                "0x1",
                "DIS_DOWNLOAD_MANUAL_ENCRYPT",
                "0x1",
            ],
        ),
    ]

    if args.mode == "full":
        steps.insert(
            2,
            (
                "Burn the Secure Boot V2 public-key digest",
                [
                    str(ESPEFUSE_BIN),
                    "--chip",
                    CHIP,
                    "--port",
                    args.port,
                    "burn-key",
                    "BLOCK_KEY2",
                    str(digest_bin),
                    "SECURE_BOOT_DIGEST0",
                ],
            ),
        )
        steps.append(
            (
                "Enable Secure Boot V2",
                [
                    str(ESPEFUSE_BIN),
                    "--chip",
                    CHIP,
                    "--port",
                    args.port,
                    "burn-efuse",
                    "SECURE_BOOT_EN",
                    "0x1",
                ],
            )
        )
        if args.revoke_unused_digests:
            steps.append(
                (
                    "Revoke the two unused Secure Boot digest slots",
                    [
                        str(ESPEFUSE_BIN),
                        "--chip",
                        CHIP,
                        "--port",
                        args.port,
                        "burn-efuse",
                        "SECURE_BOOT_KEY_REVOKE1",
                        "0x1",
                        "SECURE_BOOT_KEY_REVOKE2",
                        "0x1",
                    ],
                )
            )
        steps.append(
            (
                "Write-protect RD_DIS",
                [
                    str(ESPEFUSE_BIN),
                    "--chip",
                    CHIP,
                    "--port",
                    args.port,
                    "write-protect-efuse",
                    "RD_DIS",
                ],
            )
        )

    write_flash_cmd = [
        str(ESPTOOL_BIN),
        "--chip",
        CHIP,
        "--port",
        args.port,
        "write-flash",
        "--flash-mode",
        "dio",
        "--flash-freq",
        "80m",
        "--flash-size",
        "8MB",
        "0x0",
        str(bundle_dir / "bootloader.encrypted.bin"),
        "0x8000",
        str(bundle_dir / "partitions.encrypted.bin"),
        "0xe000",
        str(bundle_dir / "boot_app0.encrypted.bin"),
        "0x10000",
        str(bundle_dir / "app.encrypted.bin"),
    ]
    steps.append(("Flash the signed and encrypted images", write_flash_cmd))

    if not args.skip_final_secure_download:
        steps.append(
            (
                "Finalize UART security-download mode",
                [
                    str(ESPEFUSE_BIN),
                    "--chip",
                    CHIP,
                    "--port",
                    args.port,
                    "burn-efuse",
                    "ENABLE_SECURITY_DOWNLOAD",
                    "0x1",
                ],
            )
        )

    return steps


def print_provisioning_profile(args: argparse.Namespace) -> None:
    print_step("Provisioning profile")
    print(json.dumps(
        {
            "port": args.port,
            "device_id": args.device_id,
            "mode": args.mode,
            "skip_final_secure_download": args.skip_final_secure_download,
            "revoke_unused_digests": args.revoke_unused_digests,
            "bootstrap_idf": getattr(args, "bootstrap_idf", False),
            "secure_boot_bootloader": str(secure_boot_bootloader_path(args)),
        },
        indent=2,
    ))


def dry_run_provisioning(args: argparse.Namespace) -> None:
    ensure_workspace()
    if not args.port:
        raise ToolError("--port is required.")
    if not args.device_id:
        raise ToolError("--device-id is required.")

    print_step("Dry run: preparing all safe host-side artifacts")
    release_dir, bundle_dir = ensure_release_and_bundle(args)
    print_provisioning_profile(args)

    print_step("Dry run result")
    print(f"Release bundle: {release_dir}")
    print(f"Device bundle: {bundle_dir}")
    print("No board changes were made.")

    print_step("Board mutations that WOULD happen next")
    for index, (label, cmd) in enumerate(provisioning_mutation_steps(args, bundle_dir), start=1):
        printable = " ".join(shlex.quote(part) for part in cmd)
        print(f"{index}. {label}")
        print(f"   $ {printable}")


def provision_staging(args: argparse.Namespace) -> None:
    args.mode = "full"
    args.skip_final_secure_download = True
    args.revoke_unused_digests = False
    print_step("Using the safer staging profile")
    print("This keeps secure-download finalization off and preserves spare Secure Boot digest slots.")
    provision_production(args)


def provision_production(args: argparse.Namespace) -> None:
    ensure_workspace()
    if not args.port:
        raise ToolError("--port is required.")
    if not args.device_id:
        raise ToolError("--device-id is required.")

    prompt_confirmation(
        f"LOCK {args.port} AS A PRODUCTION DEVICE",
        args.yes_i_understand,
    )
    print_provisioning_profile(args)

    _, bundle_dir = ensure_release_and_bundle(args)

    flash_key = VAULT_DIR / "devices" / args.device_id / "flash_encryption_key.bin"
    digest_bin = VAULT_DIR / "global" / "secure_boot_signing_key_digest.bin"

    print_step("Erasing flash")
    run_command(
        [str(ESPTOOL_BIN), "--chip", CHIP, "--port", args.port, "erase-flash"],
        capture_output=False,
    )

    print_step("Burning the flash-encryption key")
    run_command(
        [
            str(ESPEFUSE_BIN),
            "--chip",
            CHIP,
            "--port",
            args.port,
            "burn-key",
            "BLOCK_KEY0",
            str(flash_key),
            "XTS_AES_256_KEY",
        ],
        capture_output=False,
    )

    if args.mode == "full":
        print_step("Burning the Secure Boot V2 public-key digest")
        run_command(
            [
                str(ESPEFUSE_BIN),
                "--chip",
                CHIP,
                "--port",
                args.port,
                "burn-key",
                "BLOCK_KEY2",
                str(digest_bin),
                "SECURE_BOOT_DIGEST0",
            ],
            capture_output=False,
        )

    print_step("Enabling flash encryption and hardening download paths")
    run_command(
        [
            str(ESPEFUSE_BIN),
            "--chip",
            CHIP,
            "--port",
            args.port,
            "burn-efuse",
            "SPI_BOOT_CRYPT_CNT",
            "7",
            "DIS_DOWNLOAD_ICACHE",
            "0x1",
            "DIS_DOWNLOAD_DCACHE",
            "0x1",
            "SOFT_DIS_JTAG",
            "0x1",
            "DIS_DIRECT_BOOT",
            "0x1",
            "DIS_USB_JTAG",
            "0x1",
            "DIS_DOWNLOAD_MANUAL_ENCRYPT",
            "0x1",
        ],
        capture_output=False,
    )

    if args.mode == "full":
        print_step("Enabling Secure Boot V2")
        run_command(
            [
                str(ESPEFUSE_BIN),
                "--chip",
                CHIP,
                "--port",
                args.port,
                "burn-efuse",
                "SECURE_BOOT_EN",
                "0x1",
            ],
            capture_output=False,
        )
        if args.revoke_unused_digests:
            print_step("Revoking the two unused Secure Boot digest slots")
            run_command(
                [
                    str(ESPEFUSE_BIN),
                    "--chip",
                    CHIP,
                    "--port",
                    args.port,
                    "burn-efuse",
                    "SECURE_BOOT_KEY_REVOKE1",
                    "0x1",
                    "SECURE_BOOT_KEY_REVOKE2",
                    "0x1",
                ],
                capture_output=False,
            )
        else:
            print_step("Keeping spare Secure Boot digest slots available for future key rotation")
        print_step("Write-protecting the RD_DIS field as recommended by Espressif")
        run_command(
            [
                str(ESPEFUSE_BIN),
                "--chip",
                CHIP,
                "--port",
                args.port,
                "write-protect-efuse",
                "RD_DIS",
            ],
            capture_output=False,
        )

    print_step("Flashing the signed and encrypted images")
    encrypted_files = [
        ("0x0", bundle_dir / "bootloader.encrypted.bin"),
        ("0x8000", bundle_dir / "partitions.encrypted.bin"),
        ("0xe000", bundle_dir / "boot_app0.encrypted.bin"),
        ("0x10000", bundle_dir / "app.encrypted.bin"),
    ]
    esptool_write_flash(args.port, encrypted_files)

    if not args.skip_final_secure_download:
        print_step("Finalizing UART security-download mode")
        run_command(
            [
                str(ESPEFUSE_BIN),
                "--chip",
                CHIP,
                "--port",
                args.port,
                "burn-efuse",
                "ENABLE_SECURITY_DOWNLOAD",
                "0x1",
            ],
            capture_output=False,
        )
    else:
        print_step("Skipping final secure-download lock for staging use")

    print_step("Reading back the device security summary")
    status(args)


def update_secure_device(args: argparse.Namespace) -> None:
    ensure_workspace()
    if not args.port or not args.device_id:
        raise ToolError("--port and --device-id are required.")

    release_dir = build_release(args)
    generate_keys(args)

    if args.full:
        bundle_dir = prepare_bundle(
            argparse.Namespace(
                release_dir=str(release_dir),
                device_id=args.device_id,
                mode="full",
                secure_boot_bootloader=args.secure_boot_bootloader,
            )
        )
        print_step("Flashing the full signed and encrypted image set")
        files = [
            ("0x0", bundle_dir / "bootloader.encrypted.bin"),
            ("0x8000", bundle_dir / "partitions.encrypted.bin"),
            ("0xe000", bundle_dir / "boot_app0.encrypted.bin"),
            ("0x10000", bundle_dir / "app.encrypted.bin"),
        ]
    else:
        secure_boot_key = VAULT_DIR / "global" / "secure_boot_signing_key.pem"
        flash_key = VAULT_DIR / "devices" / args.device_id / "flash_encryption_key.bin"
        require_file(secure_boot_key, "Secure Boot signing key")
        require_file(flash_key, "per-device flash-encryption key")
        update_dir = BUNDLES_DIR / args.device_id / f"{release_dir.name}-app-update"
        update_dir.mkdir(parents=True, exist_ok=True)

        print_step("Signing the Arduino application image")
        signed_app = update_dir / "app.signed.bin"
        run_command(
            [
                str(ESPSECURE_BIN),
                "sign-data",
                "--version",
                "2",
                "--keyfile",
                str(secure_boot_key),
                "--output",
                str(signed_app),
                str(release_dir / "app.bin"),
            ]
        )

        print_step("Encrypting the application image for this device")
        encrypted_app = update_dir / "app.encrypted.bin"
        run_command(
            [
                str(ESPSECURE_BIN),
                "encrypt-flash-data",
                "--aes-xts",
                "--keyfile",
                str(flash_key),
                "--address",
                "0x10000",
                "--output",
                str(encrypted_app),
                str(signed_app),
            ]
        )
        print_step("Flashing the encrypted application image update")
        files = [("0x10000", encrypted_app)]

    esptool_write_flash(args.port, files)
    print_step("Checking the device security state after the update")
    status(args)


def status(args: argparse.Namespace) -> None:
    if not args.port:
        raise ToolError("--port is required.")
    print_step("Querying security status with esptool")
    get_security = run_command(
        [str(ESPTOOL_BIN), "--chip", CHIP, "--port", args.port, "get-security-info"],
        check=False,
    )
    if get_security.stdout:
        print(get_security.stdout.strip())
    if get_security.stderr:
        print(get_security.stderr.strip())

    print_step("Querying eFuse summary with espefuse")
    efuse = run_command(
        [str(ESPEFUSE_BIN), "--chip", CHIP, "--port", args.port, "summary", "--format", "json"],
        check=False,
    )
    if efuse.returncode == 0 and efuse.stdout.strip():
        try:
            parsed = json.loads(efuse.stdout)
            print(json.dumps(parsed, indent=2))
        except json.JSONDecodeError:
            print(efuse.stdout.strip())
    else:
        print("eFuse summary is not available. This is expected once secure-download mode is fully enabled.")
        if efuse.stderr:
            print(efuse.stderr.strip())


def doctor(_: argparse.Namespace) -> None:
    ensure_workspace()
    print(json.dumps(detect_tools(), indent=2))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Freedom Clock production security tool for macOS hosts."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    doctor_parser = subparsers.add_parser("doctor", help="Check local tooling and repo readiness.")
    doctor_parser.set_defaults(func=doctor)

    build_parser_cmd = subparsers.add_parser("build-release", help="Compile the Arduino sketch and stage release artifacts.")
    build_parser_cmd.add_argument("--release-name", help="Optional release bundle name.")
    build_parser_cmd.set_defaults(func=build_release)

    manual_update_parser = subparsers.add_parser("build-manual-update", help="Compile the sketch and assemble public .bin files for setup-page firmware uploads.")
    manual_update_parser.add_argument("--release-name", help="Optional release bundle name.")
    manual_update_parser.set_defaults(func=build_manual_update)

    publish_release_parser = subparsers.add_parser("publish-github-release", help="Create or update a proper GitHub Release and upload the manual update assets.")
    publish_release_parser.add_argument("--release-name", help="Manual update package directory name under provisioning-workdir/public-updates/. Defaults to the newest one.")
    publish_release_parser.add_argument("--tag", help="Git tag to publish. Defaults to v<firmware-version> from the manual update package.")
    publish_release_parser.add_argument("--title", help="Release title shown on GitHub. Defaults to 'Freedom Clock <version>'.")
    publish_release_parser.add_argument("--notes-file", help="Markdown file to use as the release description. Defaults to docs/releases/v<version>.md.")
    publish_release_parser.add_argument("--draft", action="store_true", help="Create or update the GitHub Release as a draft.")
    publish_release_parser.add_argument("--prerelease", action="store_true", help="Mark the GitHub Release as a prerelease.")
    publish_release_parser.add_argument("--confirm-publish", action="store_true", help="Actually publish to GitHub. Without this flag the command only prints what would be published.")
    publish_release_parser.set_defaults(func=publish_github_release)

    keys_parser = subparsers.add_parser("generate-keys", help="Generate signing and flash-encryption keys.")
    keys_parser.add_argument("--device-id", required=True, help="Stable device label, for example fc-001.")
    keys_parser.add_argument("--force", action="store_true", help="Regenerate keys even if they already exist.")
    keys_parser.set_defaults(func=generate_keys)

    scaffold_parser = subparsers.add_parser("scaffold-secure-boot-project", help="Create the minimal ESP-IDF helper project.")
    scaffold_parser.set_defaults(func=scaffold_secure_boot_project)

    bootstrap_parser = subparsers.add_parser("bootstrap-idf", help="Download and install a local ESP-IDF checkout for the helper bootloader build.")
    bootstrap_parser.add_argument("--idf-root", help="Where to place or reuse the ESP-IDF checkout.")
    bootstrap_parser.add_argument("--idf-ref", default="release/v5.5", help="ESP-IDF branch or tag to clone.")
    bootstrap_parser.set_defaults(func=bootstrap_idf)

    build_sb_parser = subparsers.add_parser("build-secure-boot-project", help="Build the Secure Boot V2 helper bootloader.")
    build_sb_parser.add_argument("--idf-root", help="Existing ESP-IDF checkout to use.")
    build_sb_parser.set_defaults(func=build_secure_boot_project)

    bundle_parser = subparsers.add_parser("prepare-bundle", help="Sign and encrypt a release for one device.")
    bundle_parser.add_argument("--release-dir", help="Existing release bundle directory. Defaults to the newest one.")
    bundle_parser.add_argument("--device-id", required=True, help="Stable device label, for example fc-001.")
    bundle_parser.add_argument("--mode", choices=["full", "flash-only"], default="full", help="full = flash encryption + secure boot, flash-only = encryption without secure boot.")
    bundle_parser.add_argument("--secure-boot-bootloader", help="Optional path to a prebuilt secure-boot bootloader.bin.")
    bundle_parser.set_defaults(func=prepare_bundle)

    status_parser = subparsers.add_parser("status", help="Read the current board security state.")
    status_parser.add_argument("--port", required=True, help="Serial port, for example /dev/cu.usbmodemXXXX.")
    status_parser.set_defaults(func=status)

    dry_run_parser = subparsers.add_parser("dry-run-provisioning", help="Prepare release artifacts and show exactly which irreversible board commands would run, without touching the board.")
    dry_run_parser.add_argument("--port", required=True, help="Serial port, for example /dev/cu.usbmodemXXXX.")
    dry_run_parser.add_argument("--device-id", required=True, help="Stable device label, for example fc-001.")
    dry_run_parser.add_argument("--release-name", help="Optional release bundle name.")
    dry_run_parser.add_argument("--mode", choices=["full", "flash-only"], default="full", help="full = flash encryption + secure boot, flash-only = encryption without secure boot.")
    dry_run_parser.add_argument("--secure-boot-bootloader", help="Optional path to a prebuilt secure-boot bootloader.bin.")
    dry_run_parser.add_argument("--idf-root", help="Existing ESP-IDF checkout to use for the helper bootloader build.")
    dry_run_parser.add_argument("--idf-ref", default="release/v5.5", help="ESP-IDF branch or tag to clone when bootstrapping.")
    dry_run_parser.add_argument("--bootstrap-idf", action="store_true", help="Download and install ESP-IDF automatically before building the helper bootloader.")
    dry_run_parser.add_argument("--skip-final-secure-download", action="store_true", help="Preview a flow that leaves the final secure-download lock off.")
    dry_run_parser.add_argument("--revoke-unused-digests", action="store_true", help="Preview a flow that also revokes the two unused Secure Boot digest slots.")
    dry_run_parser.set_defaults(func=dry_run_provisioning)

    provision_parser = subparsers.add_parser("provision-production", help="One-shot production provisioning: build, sign, encrypt, burn eFuses, and flash.")
    provision_parser.add_argument("--port", required=True, help="Serial port, for example /dev/cu.usbmodemXXXX.")
    provision_parser.add_argument("--device-id", required=True, help="Stable device label, for example fc-001.")
    provision_parser.add_argument("--release-name", help="Optional release bundle name.")
    provision_parser.add_argument("--mode", choices=["full", "flash-only"], default="full", help="full = flash encryption + secure boot, flash-only = encryption without secure boot.")
    provision_parser.add_argument("--secure-boot-bootloader", help="Optional path to a prebuilt secure-boot bootloader.bin.")
    provision_parser.add_argument("--idf-root", help="Existing ESP-IDF checkout to use for the helper bootloader build.")
    provision_parser.add_argument("--idf-ref", default="release/v5.5", help="ESP-IDF branch or tag to clone when bootstrapping.")
    provision_parser.add_argument("--bootstrap-idf", action="store_true", help="Download and install ESP-IDF automatically before building the helper bootloader.")
    provision_parser.add_argument("--skip-final-secure-download", action="store_true", help="Leave secure-download finalization off for staging experiments.")
    provision_parser.add_argument("--revoke-unused-digests", action="store_true", help="Also revoke the two unused Secure Boot digest slots. Safer if you never want key rotation, less recoverable if things go wrong.")
    provision_parser.add_argument("--yes-i-understand", action="store_true", help="Skip the typed eFuse confirmation prompt.")
    provision_parser.set_defaults(func=provision_production)

    staging_parser = subparsers.add_parser("provision-staging", help="Provision a safer first-trial staging board: full security path, but no final secure-download lock and no digest-slot revocation.")
    staging_parser.add_argument("--port", required=True, help="Serial port, for example /dev/cu.usbmodemXXXX.")
    staging_parser.add_argument("--device-id", required=True, help="Stable device label, for example fc-stage-001.")
    staging_parser.add_argument("--release-name", help="Optional release bundle name.")
    staging_parser.add_argument("--secure-boot-bootloader", help="Optional path to a prebuilt secure-boot bootloader.bin.")
    staging_parser.add_argument("--idf-root", help="Existing ESP-IDF checkout to use for the helper bootloader build.")
    staging_parser.add_argument("--idf-ref", default="release/v5.5", help="ESP-IDF branch or tag to clone when bootstrapping.")
    staging_parser.add_argument("--bootstrap-idf", action="store_true", help="Download and install ESP-IDF automatically before building the helper bootloader.")
    staging_parser.add_argument("--yes-i-understand", action="store_true", help="Skip the typed eFuse confirmation prompt.")
    staging_parser.set_defaults(func=provision_staging)

    update_parser = subparsers.add_parser("update-secure-device", help="Build, sign, encrypt, and flash an update onto an already provisioned device.")
    update_parser.add_argument("--port", required=True, help="Serial port, for example /dev/cu.usbmodemXXXX.")
    update_parser.add_argument("--device-id", required=True, help="Stable device label, for example fc-001.")
    update_parser.add_argument("--release-name", help="Optional release bundle name.")
    update_parser.add_argument("--secure-boot-bootloader", help="Optional path to a prebuilt secure-boot bootloader.bin.")
    update_parser.add_argument("--full", action="store_true", help="Flash the whole image set instead of only the application partition.")
    update_parser.set_defaults(func=update_secure_device)

    return parser


def main() -> int:
    try:
        parser = build_parser()
        args = parser.parse_args()
        ensure_workspace()
        args.func(args)
        return 0
    except ToolError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nCancelled.")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
