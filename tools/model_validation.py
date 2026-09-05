#!/usr/bin/env python3
"""Run the HBFSim model-validation gate and emit reproducible reports."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DEFAULT_TESTS = (
    "spec_profile",
    "spec_response",
    "spec_system_boundary",
    "spec_channel",
    "spec_axi",
    "spec_status",
    "spec_dlu",
    "spec_semantic",
    "spec_media_mapping",
    "spec_read_cache",
    "component_boundaries",
    "model_validation",
    "stripe_mapping",
    "parallelism_group",
    "copy_engine",
    "host_gc",
    "refresh_manager",
    "wear",
    "resources",
    "timing",
)


def git_sha() -> str:
    result = subprocess.run(
        ["git", "-C", str(REPO), "rev-parse", "HEAD"],
        text=True, capture_output=True, check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def run_test(build_dir: Path, name: str) -> dict[str, object]:
    started = datetime.now(timezone.utc)
    process = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "--output-on-failure",
         "--no-tests=error", "-R", f"^{name}$"],
        text=True, capture_output=True, check=False,
    )
    ended = datetime.now(timezone.utc)
    return {
        "name": name,
        "status": "pass" if process.returncode == 0 else "fail",
        "returncode": process.returncode,
        "duration_seconds": (ended - started).total_seconds(),
        "stdout": process.stdout,
        "stderr": process.stderr,
    }


def write_markdown(path: Path, report: dict[str, object]) -> None:
    lines = [
        "# HBFSim Model Validation Report",
        "",
        f"- Overall: **{str(report['status']).upper()}**",
        f"- Git SHA: `{report['git_sha']}`",
        f"- Generated: `{report['generated_at_utc']}`",
        "",
        "| Check | Status | Duration (s) |",
        "|---|---:|---:|",
    ]
    for result in report["checks"]:
        lines.append(
            f"| {result['name']} | {str(result['status']).upper()} | "
            f"{float(result['duration_seconds']):.3f} |"
        )
    lines.extend([
        "",
        "The gate covers Spec Profile/response/component boundaries, "
        "completion semantics, Channel media placement, Bank Read Cache, "
        "plane/request-size scaling, sequential stripe and generation "
        "invariants, pipelined copy, steady-state Host GC, deadline refresh, "
        "wear/retirement, resource accounting, and timing.",
        "",
    ])
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=REPO / "build")
    parser.add_argument("--output-dir", type=Path,
                        default=REPO / "results" / "model_validation")
    parser.add_argument("--tests", nargs="*", default=list(DEFAULT_TESTS))
    args = parser.parse_args()

    build_dir = args.build_dir.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    if not (build_dir / "CTestTestfile.cmake").exists():
        print(f"model_validation: build directory is not configured: {build_dir}",
              file=sys.stderr)
        return 2
    output_dir.mkdir(parents=True, exist_ok=True)
    checks = [run_test(build_dir, name) for name in args.tests]
    report = {
        "schema_version": 1,
        "suite_version": "0.2.7",
        "status": "pass" if all(check["status"] == "pass"
                                  for check in checks) else "fail",
        "git_sha": git_sha(),
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "build_dir": str(build_dir),
        "checks": checks,
    }
    (output_dir / "validation_report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    write_markdown(output_dir / "validation_report.md", report)
    print(f"Model validation {report['status']}: {output_dir}")
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
