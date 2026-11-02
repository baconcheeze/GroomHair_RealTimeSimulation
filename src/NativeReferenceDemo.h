#pragma once
#include "GuideState.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
namespace jointcpu { struct Problem; }
// Shared immutable selection/initialization. No live CPU solve in the GPU path.
std::vector<unsigned> nativeReferenceSelection(const GuideCpuState &, unsigned);
std::unique_ptr<jointcpu::Problem> nativeReferenceProblem(const GuideCpuState &, unsigned,
                                                       const std::filesystem::path &);
struct NativeReferenceSnapshot {
  uint64_t generation = 0, step = 0, kicks = 0;
  double h = 1.f / 120, wall = 0, workSeconds = 0, solveMs = 0,
         maxDisplacement = 0, maxTip = 0, pinError = 0;
  unsigned iterations = 0;
  bool gpu = false; double latencyMs = 0;
  std::chrono::steady_clock::time_point timestamp{};
  bool failed = false;
  std::string status;
  std::vector<unsigned> indices, sourceIds;
  std::vector<std::vector<hair::D>> x, q, v, rest, traces;
};
class NativeReferenceDemo {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  NativeReferenceDemo();
  ~NativeReferenceDemo();
  void initialize(const GuideCpuState &, const std::filesystem::path &,
                  Float3 gravitySolver);
  bool initialized() const;
  void shutdown();
  void reset(unsigned count);
  void run(bool);
  bool running() const;
  void steps(unsigned);
  void kick(Float3 solverMetersPerSecond);
  std::shared_ptr<const NativeReferenceSnapshot> snapshot() const;
  uint64_t generation() const;
  static void save(const std::filesystem::path &,
                   const NativeReferenceSnapshot &);
};
