#include "NativeReferenceDemo.h"
#include <fstream>
#include <iostream>
#include <set>
using Clock = std::chrono::steady_clock;
template <class P> auto waitFor(NativeReferenceDemo &demo, P predicate) {
  auto start = Clock::now();
  for (;;) {
    auto s = demo.snapshot();
    if (s && s->failed)
      throw std::runtime_error(s->status);
    if (s && s->generation == demo.generation() && predicate(*s))
      return s;
    if (Clock::now() - start > std::chrono::seconds(30))
      throw std::runtime_error("Native worker test timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
int main(int argc, char **argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("NativeReferenceIntegrationTests assets output");
    RestData rest;
    rest.load(argv[1]);
    GuideCpuState cpu;
    cpu.initialize(rest.guide);
    NativeReferenceDemo demo;
    demo.initialize(
        cpu, std::filesystem::path(argv[1]) / "reference-demo",
        worldVectorToHair({0, -9.81f, 0}, rest.worldToHairRotation));
    demo.reset(3);
    auto initial = waitFor(demo, [](auto &s) { return s.step == 0; });
    demo.run(true);
    waitFor(demo, [](auto &s) { return s.step >= 3; });
    demo.run(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto paused = demo.snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (demo.snapshot()->step != paused->step)
      throw std::runtime_error("Pause advanced after settling");
    demo.reset(3);
    auto reset = waitFor(demo, [](auto &s) { return s.step == 0; });
    if (reset->x.size() != 3 || reset->kicks || reset->maxDisplacement)
      throw std::runtime_error("Reset state");
    for (unsigned i = 0; i < 3; i++) {
      if (std::memcmp(reset->x[i].data(), initial->x[i].data(),
                      reset->x[i].size() * sizeof(hair::D)) ||
          std::memcmp(reset->q[i].data(), initial->q[i].data(),
                      reset->q[i].size() * sizeof(hair::D)))
        throw std::runtime_error("Reset x/q equality");
      for (auto v : reset->v[i])
        if (v.norm() != 0)
          throw std::runtime_error("Reset velocity");
    }
    demo.kick({.01f, .02f, .03f});
    demo.steps(4);
    auto a = waitFor(demo, [](auto &s) { return s.step == 4; });
    if (a->kicks != 1 || a->maxDisplacement <= 0 || a->pinError != 0)
      throw std::runtime_error("Kick/motion/pins");
    std::vector<Float3> display;
    for (auto p : a->x[0])
      display.push_back({float(p.x), float(p.y), float(p.z)});
    for (auto &p : display)
      p = {999, 999, 999};
    demo.reset(3);
    waitFor(demo, [](auto &s) { return s.step == 0; });
    demo.kick({.01f, .02f, .03f});
    demo.steps(4);
    auto b = waitFor(demo, [](auto &s) { return s.step == 4; });
    for (unsigned i = 0; i < 3; i++)
      if (std::memcmp(a->x[i].data(), b->x[i].data(),
                      a->x[i].size() * sizeof(hair::D)) ||
          std::memcmp(a->v[i].data(), b->v[i].data(),
                      a->v[i].size() * sizeof(hair::D)))
        throw std::runtime_error("Reset/replay or display feedback");
    std::vector<unsigned> prefix;
    for (unsigned count : {32u, 128u}) {
      demo.reset(count);
      auto s = waitFor(demo, [](auto &p) { return p.step == 0; });
      if (s->x.size() != count ||
          std::set<unsigned>(s->indices.begin(), s->indices.end()).size() !=
              count)
        throw std::runtime_error("Selection count/uniqueness");
      if (!prefix.empty() &&
          !std::equal(prefix.begin(), prefix.end(), s->indices.begin()))
        throw std::runtime_error("Subset prefix unstable");
      prefix = s->indices;
      demo.steps(1);
      s = waitFor(demo, [](auto &p) { return p.step == 1; });
      if (s->pinError || s->x.size() != count)
        throw std::runtime_error("Selected strands/pins");
    }
    demo.shutdown();
    std::filesystem::create_directories(argv[2]);
    std::ofstream f(std::filesystem::path(argv[2]) / "integration.json");
    f << "{\"passed\":true,\"RunPause\":true,\"Step\":true,"
         "\"ResetReplayExact\":true,\"DisplayFloatCopyNotFeedback\":true,"
         "\"FixedRoots\":true,\"Selection32and128\":true}";
    std::cout << "Native integration PASS\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
