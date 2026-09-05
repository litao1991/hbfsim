#!/usr/bin/env python3
"""Run reproducible HBFSim parameter sweeps using only Python stdlib."""

from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def git_sha() -> str:
    result = subprocess.run(
        ["git", "-C", str(REPO), "rev-parse", "HEAD"],
        text=True, capture_output=True, check=False
    )
    return result.stdout.strip() if result.returncode == 0 else "unknown"


def resolve_path(value: str) -> Path:
    path = Path(value).expanduser()
    return path if path.is_absolute() else (REPO / path).resolve()


def scalar(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if value is None:
        raise ValueError("sweep values cannot be null")
    return str(value)


def override_yaml(overrides: dict[str, object]) -> str:
    tree: dict[str, object] = {}
    for dotted, value in overrides.items():
        parts = dotted.split(".")
        if not parts or any(not part for part in parts):
            raise ValueError(f"invalid dotted key: {dotted}")
        node = tree
        for part in parts[:-1]:
            child = node.setdefault(part, {})
            if not isinstance(child, dict):
                raise ValueError(f"conflicting sweep key: {dotted}")
            node = child
        node[parts[-1]] = value

    lines: list[str] = []

    def emit(node: dict[str, object], depth: int) -> None:
        for key in sorted(node):
            value = node[key]
            prefix = "  " * depth + key + ":"
            if isinstance(value, dict):
                lines.append(prefix)
                emit(value, depth + 1)
            else:
                lines.append(prefix + " " + scalar(value))

    emit(tree, 0)
    return "\n".join(lines) + "\n"


def safe_name(index: int, values: dict[str, object]) -> str:
    parts = [f"run-{index:04d}"]
    for key, value in sorted(values.items()):
        token = re.sub(r"[^A-Za-z0-9_.-]+", "-", f"{key}={value}")
        parts.append(token[-48:])
    return "__".join(parts)


def read_summary(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    with path.open(newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            result[row["metric"]] = row["value"]
    return result


def run_one(binary: Path, base_text: str, trace: Path, root: Path,
            index: int, values: dict[str, object], revision: str,
            trace_hash: str, dry_run: bool) -> dict[str, object]:
    run_id = safe_name(index, values)
    output = root / run_id
    output.mkdir(parents=True, exist_ok=True)
    generated = output / "input_config.yaml"
    all_overrides = dict(values)
    all_overrides["statistics.output_dir"] = str(output)
    generated.write_text(
        base_text.rstrip() + "\n\n# experiment_runner overrides\n" +
        override_yaml(all_overrides), encoding="utf-8"
    )
    command = [str(binary), str(generated), str(trace)]
    started = datetime.now(timezone.utc).isoformat()
    if dry_run:
        return {"run_id": run_id, "parameters": values, "command": command,
                "status": "dry-run", "returncode": 0, "summary": {}}
    process = subprocess.run(command, text=True, capture_output=True,
                             check=False)
    ended = datetime.now(timezone.utc).isoformat()
    summary_path = output / "summary.csv"
    summary = read_summary(summary_path) if summary_path.exists() else {}
    resolved = output / "resolved_config.yaml"
    if not resolved.exists():
        resolved.write_text(generated.read_text(encoding="utf-8"),
                            encoding="utf-8")
    metadata = {
        "schema_version": 1,
        "run_id": run_id,
        "status": "ok" if process.returncode == 0 else "failed",
        "returncode": process.returncode,
        "started_at_utc": started,
        "ended_at_utc": ended,
        "git_sha": revision,
        "trace": str(trace),
        "trace_sha256": trace_hash,
        "input_config_sha256": digest(generated),
        "resolved_config_sha256": digest(resolved),
        "parameters": values,
        "command": command,
        "stdout": process.stdout,
        "stderr": process.stderr,
    }
    (output / "experiment_metadata.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8"
    )
    return {"run_id": run_id, "parameters": values,
            "status": metadata["status"], "returncode": process.returncode,
            "summary": summary}


def write_table(path: Path, runs: list[dict[str, object]]) -> None:
    parameter_keys = sorted({key for run in runs
                             for key in run["parameters"]})
    metric_keys = sorted({key for run in runs for key in run["summary"]})
    with path.open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(
            target, fieldnames=["run_id", "status"] + parameter_keys +
            metric_keys
        )
        writer.writeheader()
        for run in runs:
            row = {"run_id": run["run_id"], "status": run["status"]}
            row.update(run["parameters"])
            row.update(run["summary"])
            writer.writerow(row)


def write_svg(path: Path, metric: str, runs: list[dict[str, object]]) -> bool:
    points: list[tuple[int, float, str]] = []
    for index, run in enumerate(runs):
        try:
            points.append((index, float(run["summary"][metric]),
                           str(run["run_id"])))
        except (KeyError, TypeError, ValueError):
            pass
    if not points:
        return False
    width, height = 960, 420
    left, right, top, bottom = 80, 30, 45, 70
    values = [point[1] for point in points]
    low, high = min(values), max(values)
    if high == low:
        high = low + 1.0
    x_span = max(1, len(points) - 1)
    coords = []
    for position, value, _ in points:
        x = left + (width - left - right) * position / x_span
        y = top + (height - top - bottom) * (high - value) / (high - low)
        coords.append((x, y, value))
    polyline = " ".join(f"{x:.1f},{y:.1f}" for x, y, _ in coords)
    circles = "\n".join(
        f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4"><title>{value:g}</title></circle>'
        for x, y, value in coords
    )
    escaped = metric.replace("&", "&amp;").replace("<", "&lt;")
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="white"/>
<text x="{width / 2}" y="25" text-anchor="middle" font-family="sans-serif" font-size="18">{escaped}</text>
<line x1="{left}" y1="{top}" x2="{left}" y2="{height-bottom}" stroke="#555"/>
<line x1="{left}" y1="{height-bottom}" x2="{width-right}" y2="{height-bottom}" stroke="#555"/>
<text x="15" y="{top}" font-family="sans-serif" font-size="12">{high:g}</text>
<text x="15" y="{height-bottom}" font-family="sans-serif" font-size="12">{low:g}</text>
<text x="{width / 2}" y="{height-20}" text-anchor="middle" font-family="sans-serif" font-size="12">run index</text>
<polyline points="{polyline}" fill="none" stroke="#2563eb" stroke-width="2"/>
<g fill="#2563eb">{circles}</g>
</svg>\n'''
    path.write_text(svg, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("spec", type=Path)
    parser.add_argument("--binary", type=Path,
                        default=REPO / "build" / "hbfsim")
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    spec = json.loads(args.spec.read_text(encoding="utf-8"))
    binary = args.binary.expanduser().resolve()
    base = resolve_path(spec["base_config"])
    trace = resolve_path(spec["trace"])
    root = resolve_path(spec.get("output_root", "results/sweep"))
    root.mkdir(parents=True, exist_ok=True)
    axes = spec.get("parameters", {})
    keys = sorted(axes)
    combinations = [dict(zip(keys, values)) for values in itertools.product(
        *(axes[key] for key in keys)
    )] if keys else [{}]
    revision = git_sha()
    trace_hash = digest(trace)
    base_text = base.read_text(encoding="utf-8")
    runs: list[dict[str, object]] = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        futures = [executor.submit(run_one, binary, base_text, trace, root,
                                   index, values, revision, trace_hash,
                                   args.dry_run)
                   for index, values in enumerate(combinations)]
        for future in as_completed(futures):
            runs.append(future.result())
    runs.sort(key=lambda run: str(run["run_id"]))
    write_table(root / "sweep_summary.csv", runs)
    plot_dir = root / "plots"
    plot_dir.mkdir(exist_ok=True)
    metrics = spec.get("plot_metrics", [
        "p99_latency_ns", "effective_bandwidth_GBps",
        "stripe_write_amplification"
    ])
    plots = [metric for metric in metrics
             if write_svg(plot_dir / f"{metric}.svg", metric, runs)]
    manifest = {
        "schema_version": 1,
        "git_sha": revision,
        "trace": str(trace),
        "trace_sha256": trace_hash,
        "base_config": str(base),
        "base_config_sha256": digest(base),
        "run_count": len(runs),
        "plots": plots,
        "runs": [{"run_id": run["run_id"], "status": run["status"],
                  "parameters": run["parameters"]} for run in runs],
    }
    (root / "sweep_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8"
    )
    failures = [run for run in runs if run["returncode"] != 0]
    print(f"Completed {len(runs)} runs; {len(failures)} failed; results: {root}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
