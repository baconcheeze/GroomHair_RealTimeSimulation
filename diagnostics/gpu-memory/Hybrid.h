#pragma once
#include "Layout.h"
namespace nativels
{
Result hybridMinimize(Workspace &, const GuideCpuState &, windowmath::State &, const std::vector<D> &,
                      const windowmath::Settings &, unsigned count = 5, Mode mode = Mode::NativeExtended,
                      double ptol = 1e-11, double qtol = 1e-9);
}
namespace gpuls
{
extern Engine *hybridEngine;
struct HybridRecord
{
    double gpu, upload, roundtrip;
    unsigned n, corrections, code;
};
extern std::vector<HybridRecord> hybridRecords;
int hybridSuite(Context &, const fs::path &, const fs::path &, unsigned count);
int hybridDynamics(Context &, const fs::path &, const fs::path &);
int batchBenchmark(Context &, const fs::path &, const fs::path &);
} // namespace gpuls
