"""Bounded migration checks; launches only the new Jolt-only application."""
import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts/jolt-migration/final"
OUT.mkdir(parents=True, exist_ok=True)
runs = []
for config, name, options in ([] if "--evaluate-only" in sys.argv else [
    ("Release", "motion", ["--steps", "360", "--capture-every", "30", "--reset-check"]),
    ("Debug", "motion-hulls", ["--steps", "60", "--capture-every", "15", "--reset-check", "--hulls", "--guides"]),
    ("Release", "controls", ["--control-check"]),
    ("Debug", "controls", ["--control-check"]),
    ("Release", "paused", ["--paused", "--frames", "8"]),
    ("Release", "fixed-head", ["--no-animation", "--steps", "60", "--capture-every", "30"]),
]):
    exe = ROOT / f"build/jolt-only/bin/{config}/HairLabDX12.exe"
    target = OUT / config / name
    command = [str(exe), *options, "--capture-dir", str(target), "--log", str(target / "app.log")]
    process = subprocess.Popen(command, cwd=ROOT)
    print("START", config, name, process.pid, flush=True)
    try:
        code = process.wait(timeout=120)
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"Incomplete bounded test; inspect task process {process.pid}")
    record = dict(configuration=config, name=name, exit=code, command=command,
                  exeSHA256=hashlib.sha256(exe.read_bytes()).hexdigest())
    runs.append(record)
    (OUT / "runs.json").write_text(json.dumps(runs, indent=2))
    print("END", config, name, code, flush=True)
    if code:
        raise RuntimeError(f"Application migration test failed: {target}")

results = {}
for config, name in [("Release", "motion"), ("Debug", "motion-hulls")]:
    path = OUT / config / name
    before = json.loads((path / "step-0000.json").read_text())
    states = [json.loads(p.read_text()) for p in sorted(path.glob("step-*.json"))]
    last = states[-1]
    stats = {}
    for key in ["guideTips", "renderTips", "scalpVertices", "hullWorldColumns"]:
        curve = [[math.dist(a, b) for a, b in zip(before[key], state[key])] for state in states]
        ds = curve[-1]
        stats[key] = {"count": len(ds), "finalRms": math.sqrt(sum(d*d for d in ds)/len(ds)),
                      "peakCheckpointRms": max(math.sqrt(sum(d*d for d in row)/len(row)) for row in curve),
                      "peakDifference": max(max(row) for row in curve)}
        # The original animation loops at frame 360; movement is checked over all checkpoints.
        assert stats[key]["peakDifference"] > 0, f"No actual motion in {key}"
    for state in states:
        assert state["finite"] and state["maxRootAttachmentErrorMeters"] < 1e-5
    reset = json.loads((path / "reset-0000.json").read_text())
    assert reset == before, "Reset did not restore original scene state"
    assert (path / "reset-0000.png").read_bytes() == (path / "step-0000.png").read_bytes()
    stats["resetExact"] = True
    stats["maxRootAttachmentErrorMeters"] = max(s["maxRootAttachmentErrorMeters"] for s in states)
    results[config] = stats
for config in ["Release", "Debug"]:
    path = OUT / config / "controls"
    a = json.loads((path / "idle-before.json").read_text())
    b = json.loads((path / "idle-after.json").read_text())
    assert a == b, "Pause changes state"
    a = json.loads((path / "camera-before.json").read_text())
    b = json.loads((path / "camera-after.json").read_text())
    assert a == b, "Camera changes physics"
    assert json.loads((path / "single-step.json").read_text())["step"] == 1
    assert json.loads((path / "control-reset.json").read_text())["step"] == 0
results["controllerAPI"] = "pause, step, run/pause, reset, camera state isolation passed in Debug/Release"
(OUT / "validation.json").write_text(json.dumps(results, indent=2))
print(json.dumps(results, indent=2), flush=True)
