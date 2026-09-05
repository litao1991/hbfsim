#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> int:
    runner = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        config = root / "base.yaml"
        trace = root / "trace.csv"
        fake = root / "fake_hbfsim.py"
        output = root / "output"
        config.write_text("statistics:\n  output_dir: ignored\n", encoding="utf-8")
        trace.write_text("timestamp_ns,op,address,size,stream\n0,R,0,4,0\n",
                         encoding="utf-8")
        fake.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib,re,sys\n"
            "text=pathlib.Path(sys.argv[1]).read_text()\n"
            "out=re.findall(r'output_dir:\\s*(.+)',text)[-1].strip()\n"
            "path=pathlib.Path(out); path.mkdir(parents=True,exist_ok=True)\n"
            "path.joinpath('summary.csv').write_text('metric,value\\np99_latency_ns,42\\neffective_bandwidth_GBps,7\\n')\n"
            "path.joinpath('resolved_config.yaml').write_text(text)\n",
            encoding="utf-8")
        fake.chmod(0o755)
        spec = root / "sweep.json"
        spec.write_text(json.dumps({
            "base_config": str(config), "trace": str(trace),
            "output_root": str(output),
            "parameters": {"scheduler.max_consecutive_reads": [1, 2]},
            "plot_metrics": ["p99_latency_ns"]
        }), encoding="utf-8")
        subprocess.run([sys.executable, str(runner), str(spec),
                        "--binary", str(fake), "--jobs", "2"], check=True)
        assert (output / "sweep_summary.csv").exists()
        assert (output / "sweep_manifest.json").exists()
        assert (output / "plots" / "p99_latency_ns.svg").exists()
        runs = sorted(output.glob("run-*"))
        assert len(runs) == 2
        assert all((run / "experiment_metadata.json").exists() for run in runs)
        assert all((run / "resolved_config.yaml").exists() for run in runs)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
