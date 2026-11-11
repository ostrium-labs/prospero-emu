#!/usr/bin/env python3
"""Validate a Prospero/KytyPS5 shader dump as a replay corpus.

The emulator writes one JSON manifest per compiled shader when
--graphics-debug-dump is enabled. This script checks that every manifest is
well formed, that its referenced files exist, and that every SPIR-V blob passes
spirv-val.

Usage:
    python3 tools/prospero/corpus_check.py --root _Shaders
    python3 tools/prospero/corpus_check.py --root _Shaders --require-replay
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

SUPPORTED_FORMAT = 1
REQUIRED_KEYS = (
    "format",
    "stage",
    "shader_hash",
    "module_id",
    "code_file",
    "spirv_file",
)


@dataclass
class Report:
    total: int = 0
    validated: int = 0
    not_replayable: int = 0
    errors: list[str] = field(default_factory=list)

    def error(self, path: Path, message: str) -> None:
        self.errors.append(f"{path}: {message}")


def find_spirv_val(explicit: str | None) -> str | None:
    if explicit:
        return explicit
    return shutil.which("spirv-val")


def validate_spirv(spirv_val: str, path: Path) -> str | None:
    result = subprocess.run(
        [spirv_val, "--target-env", "vulkan1.3", str(path)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode == 0:
        return None
    detail = (result.stderr or result.stdout).strip()
    return detail or f"spirv-val exited {result.returncode}"


def load_manifest(manifest: Path, report: Report) -> dict | None:
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        report.error(manifest, f"cannot read manifest: {error}")
        return None

    if not isinstance(data, dict):
        report.error(manifest, "manifest is not a JSON object")
        return None

    missing = [key for key in REQUIRED_KEYS if key not in data]
    if missing:
        report.error(manifest, f"missing keys: {', '.join(missing)}")
        return None

    if data["format"] != SUPPORTED_FORMAT:
        report.error(manifest, f"unsupported format {data['format']!r}")
        return None

    return data


def check_manifest(
    root: Path,
    manifest: Path,
    data: dict,
    report: Report,
    spirv_val: str | None,
    require_replay: bool,
) -> None:
    if data.get("standalone_replay") is not True:
        report.not_replayable += 1
        if require_replay:
            report.error(manifest, "not standalone-replayable (graphics stage?)")

    for key in ("code_file", "spirv_file"):
        referenced = root / str(data[key])
        if not referenced.is_file():
            report.error(manifest, f"{key} does not exist: {referenced}")

    ir_file = data.get("ir_file")
    if ir_file and not (root / str(ir_file)).is_file():
        report.error(manifest, f"ir_file does not exist: {root / str(ir_file)}")

    spirv = root / str(data["spirv_file"])
    if spirv_val is None or not spirv.is_file():
        return

    error = validate_spirv(spirv_val, spirv)
    if error:
        report.error(manifest, f"SPIR-V validation failed: {error}")
    else:
        report.validated += 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("_Shaders"))
    parser.add_argument("--spirv-val", dest="spirv_val", default=None)
    parser.add_argument(
        "--require-replay",
        action="store_true",
        help="fail on manifests that cannot be replayed without a game",
    )
    args = parser.parse_args()

    root = args.root
    if not root.is_dir():
        print(f"error: not a directory: {root}", file=sys.stderr)
        return 2

    manifests = sorted(root.glob("*.json"))
    if not manifests:
        print(f"error: no manifests under {root}", file=sys.stderr)
        return 2

    spirv_val = find_spirv_val(args.spirv_val)
    report = Report()

    for manifest in manifests:
        report.total += 1
        data = load_manifest(manifest, report)
        if data is None:
            continue
        check_manifest(root, manifest, data, report, spirv_val, args.require_replay)

    if spirv_val is None:
        print("warning: spirv-val not found; SPIR-V binaries were not validated", file=sys.stderr)

    print(
        f"manifests={report.total} validated={report.validated} "
        f"not_replayable={report.not_replayable} errors={len(report.errors)}"
    )
    for error in report.errors:
        print(f"  {error}", file=sys.stderr)

    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
