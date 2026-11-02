# G64 memory-layout diagnostic

`G64MemoryCandidate` is an isolated CPU-nonlinear/GPU-linear diagnostic. G32 means **32 independent threads per group**, not float32. Launch is fixed at G32/tile2839; no launch tuning is performed here.

```
G64MemoryCandidate.exe codec|ownership|replay|formation|hybrid1|hybrid5|dynamics|benchmark INPUT OUTPUT [baseline|work32|workinput32] [DYNAMICS_CORRECTIONS]
G64CodecAudit.exe INPUT OUTPUT
```

`replay`/`formation` keep the original 0/1/2/3/5 correction trace. `hybrid1` and `hybrid5` select the named budget. `dynamics` runs the existing 3-strand, 16-step screen, with the final CLI correction argument recorded explicitly (default one). `benchmark` uses the fixed three-layout matrix independently of the optional layout argument. All kernels use the preserved FP64 math, strict compiler flags, DSqrt, and per-system order.

`Interleave.h` translates the original CPU Pack to a GPU wire layout. The canonical Pack and decoded readback interfaces remain unchanged. Each software packing tile contains up to 32 consecutive *global* system IDs in supplied order, with per-tile maximum block/DOF/row capacities. Padding is never iterated as mathematical data. A 32-byte integer Tile record, addressed through Meta.pad0, carries the plane bases/capacities. Original integer rows/maps/blocks retain their representation. Status is explicitly separate AoS uint32 data, not transformed double slots.

`work32`: transpose L/C/u/y/du/s/step norms/formed bands/RHS into FP64 planes. `workinput32`: additionally transpose M bands/c/RHS/scales and row b/a[6]. Original integer address offsets stay valid; unused old numerical input slots are reserved holes, zeroed after packing. These holes and all padding are included in memory accounting.

CPU producers currently build the original Pack, followed by an explicit host packing step in prepare. Readback is decoded on the CPU into the original complete scratch representation. These costs are measured separately and in the fresh-input bridge. There is no GPU conversion bridge because both ends of this diagnostic are CPU-owned; no production bridge is installed. A future GPU assembler does not yet generate this layout.

`Codec.cpp` tests exact arbitrary-bit and finite round trips. The independent `CodecAudit.cpp` inspects actual encoded metadata/descriptors, unique physical numerical addresses, exact paired double bits, integer byte preservation and status separation. `Ownership.cpp` checks physical padding/guards plus decoded inactive systems, deliberate failure isolation, and reset/replay. Optional integer stage counters are used only by ownership PSOs, never by timed PSOs.

A whole solve is a single direct-queue command list: Factor -> UAV barrier -> Initial -> UAV barrier -> Correct(s) -> UAV barrier -> readback. Inputs stay resident between corrections; factors are reused. Resource growth is allowed only during preparation after the previous fence completes. Timed resident samples are preallocated/warmed up. The full bridge includes the current CPU conversion/allocation costs and does not claim end-to-end GPU dynamics.

Build via the local CMake project with VS2022 x64 and `/m:1`; see root `G64_MEMORY_LAYOUT.md` for exact commands, measured results and hashes. `experiment.py` is optional offline orchestration; native execution has no Python/IPC dependency. `summary.py` evaluates existing outputs offline.

The existing QD 2.3.24 license/provenance is retained under `third_party/qd-2.3.24`, and copied into staged runtimes. It is used by the CPU reference/scorer, not as shader double-double arithmetic. No new dependency or precision method was introduced. Unrelated auxiliary files and all production/baseline files remain untouched.
