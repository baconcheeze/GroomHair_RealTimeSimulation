#pragma once
#include "OrientationSolver.h"
#include <limits>
struct OrientationDiagnostic {
    double ss{},bt{},maxBatch=std::numeric_limits<double>::quiet_NaN(),maxLast=std::numeric_limits<double>::quiet_NaN(),norm{},root{},stationarity{},normalizedStationarity{},spectral{},normalizedSpectral{};
    uint64_t ordinary{},fallback{},hard{},rejected{},failed{};unsigned ties{};
    double energy() const {return ss+bt;}
    std::string json() const;
};
struct OrientationSnapshot {std::vector<hair::F> q;std::vector<uint32_t> errors;std::vector<SolveStats> stats;};
OrientationSnapshot readOrientation(guidegpu::State& state,OrientationSolver& solver,ID3D12Device* device,const Submit& submit);
OrientationDiagnostic measureOrientation(const GuideCpuState& cpu,const OrientationSolver& solver,const OrientationSnapshot& snapshot,const std::vector<hair::F>* before=nullptr);
