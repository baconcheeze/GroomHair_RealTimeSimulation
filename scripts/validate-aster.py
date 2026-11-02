"""Bounded Aster content checks. No retired custom solver comparisons."""
import hashlib
import json
import math
import subprocess
import sys
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts/aster/delivered"
OUT.mkdir(parents=True, exist_ok=True)
runs = []
for config, name, options in ([] if "--evaluate-only" in sys.argv else [
    ("Release", "motion", ["--steps", "240", "--capture-every", "15", "--reset-check", "--view", "bust"]),
    ("Debug", "motion-hulls", ["--steps", "120", "--capture-every", "30", "--reset-check", "--hulls", "--guides"]),
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
    assert before["character"]=="Aster" and before["guides"]==1600 and before["render"]==16000
    assert before["hulls"]==5
    stats = {}
    for key in ["guideTips", "renderTips", "scalpVertices", "hullWorldColumns"]:
        curve = [[math.dist(a, b) for a, b in zip(before[key], state[key])] for state in states]
        ds = curve[-1]
        stats[key] = {"count": len(ds), "finalRms": math.sqrt(sum(d*d for d in ds)/len(ds)),
                      "peakCheckpointRms": max(math.sqrt(sum(d*d for d in row)/len(row)) for row in curve),
                      "peakDifference": max(max(row) for row in curve)}
        # The Aster animation loops at frame 240; movement is checked over all checkpoints.
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

# Compare actual head/hull world poses to the matching persisted animation,
# independently of the application renderer and without a solver reference.
data = (ROOT / "assets/aster/character.bin").read_bytes()
offset = 0
def read_u32():
    global offset
    value = struct.unpack_from("<I", data, offset)[0]
    offset += 4
    return value
head_joint = read_u32()
vertices = read_u32(); offset += vertices*12
triangles = read_u32(); offset += triangles*12
joint_count = read_u32(); offset += joint_count*64
weights = read_u32(); offset += weights*vertices*8
frame_count = read_u32()
animation = [struct.unpack_from("<6f", data, offset+i*24) for i in range(frame_count*joint_count)]
offset += frame_count*joint_count*24
hull_joints = []
for i in range(read_u32()):
    hull_joints.append(read_u32())
    count = read_u32(); offset += count*12
assert offset == len(data)
def matrix(pose):
    tx,ty,tz,x,y,z = pose
    w = math.sqrt(max(0., 1-x*x-y*y-z*z))
    return [1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w), 0,
            2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w), 0,
            2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y), 0, tx,ty,tz,1]
pose_error = hull_error = 0.
for config,name in [("Release","motion"),("Debug","motion-hulls")]:
    for path in (OUT/config/name).glob("step-*.json"):
        state = json.loads(path.read_text())
        frame = state["animationFrame"]
        expected = matrix(animation[frame*joint_count+head_joint])
        pose_error = max(pose_error, max(abs(a-b) for a,b in zip(expected,state["hairWorldColumns"])))
        for joint, actual in zip(hull_joints, state["hullWorldColumns"]):
            expected = matrix(animation[frame*joint_count+joint])
            hull_error = max(hull_error,max(abs(a-b) for a,b in zip(expected,actual)))
assert pose_error < 2e-6 and hull_error < 2e-6
(OUT/"pose-validation.json").write_text(json.dumps(dict(maxHairWorldComponentError=pose_error,
    maxHullWorldComponentError=hull_error,source="persisted Aster animation matrices",passed=True),indent=2))
print(json.dumps(results, indent=2), flush=True)
