#include "NativeReferenceDemo.h"
#include "../diagnostics/joint/Problem.h"
#include "../diagnostics/native-ls/NativeLS.h"
#include <fstream>
#include <iomanip>
#include <sstream>
namespace {
using hair::D;
using State = windowmath::State;
using Clock = std::chrono::steady_clock;
double guessEnergy(const GuideCpuState &c, const State &s,
                   const std::vector<D> &y,
                   const windowmath::Settings &settings) {
  double e = 0, h = settings.h;
  for (unsigned j = 0; j < s.x.size(); j++) {
    D d = s.x[j] - y[j];
    e += .5 * (c.masses[j] / (h * h)) * d.dot(d);
    if (j < c.lengths.size()) {
      D css = (s.x[j + 1] - s.x[j]) / c.lengths[j] -
              hair::rotate(s.q[j], D{0, 0, 1});
      e += .5 * (double(settings.ss) * c.lengths[j]) * css.dot(css);
    }
  }
  return e;
}
struct Entry {
  unsigned index;
  std::unique_ptr<jointcpu::Problem> p;
  std::unique_ptr<nativels::Workspace> w;
  State rest, state, inertial;
  std::vector<D> v, target;
  std::vector<D> trace;
  Entry(unsigned i, std::unique_ptr<jointcpu::Problem> problem)
      : index(i), p(std::move(problem)) {
    rest = windowmath::rest(p->cpu);
    state = inertial = rest;
    v.resize(rest.x.size());
    target.resize(v.size());
    w = std::make_unique<nativels::Workspace>(p->cpu, p->set);
    trace.push_back(rest.x.back());
  }
  // Exact rest-start guarded wrapper from
  // diagnostics/native-ls/Bridge.cpp::simulate.
  unsigned step(D gravity) {
    auto previous = state;
    double h = p->set.h;
    inertial = previous;
    for (unsigned j = 0; j < v.size(); j++) {
      bool pin = j == 0 && p->set.pinX;
      target[j] =
          pin ? rest.x[j] : previous.x[j] + h * v[j] + (h * h) * gravity;
      state.x[j] = target[j];
      inertial.x[j] = pin ? rest.x[j] : previous.x[j] + h * v[j];
    }
    if (p->set.pinQ) {
      state.q[0] = rest.q[0];
      inertial.q[0] = rest.q[0];
    }
    double full = guessEnergy(p->cpu, state, target, p->set),
           other = guessEnergy(p->cpu, inertial, target, p->set);
    if (std::isfinite(other) && other < full)
      state = inertial;
    double before =
        nativels::objective(p->cpu, state, target, p->set, w->layout);
    auto r = nativels::minimize(*w, p->cpu, state, target, p->set, 5,
                                nativels::Mode::NativeExtended, 1e-11, 1e-9);
    hair::require(r.diagnostics.qualified && !r.factorFailure,
                  "Native reference did not qualify");
    hair::require(r.diagnostics.objective <=
                      before + 1e-22 + 1e-10 * std::abs(before),
                  "Native objective increased");
    for (unsigned j = 0; j < v.size(); j++) {
      v[j] = j == 0 ? D{} : (state.x[j] - previous.x[j]) / h;
      hair::require(std::isfinite(state.x[j].norm()) &&
                        std::isfinite(v[j].norm()),
                    "Nonfinite native x/v");
    }
    for (auto q : state.q)
      hair::require(std::isfinite(q.norm()) && std::abs(q.norm() - 1) < 2e-4,
                    "Nonfinite/nonunit native q");
    hair::require((state.x[0] - rest.x[0]).norm() == 0 &&
                      (state.q[0] - rest.q[0]).norm() == 0 && v[0].norm() == 0,
                  "Pinned native root changed");
    trace.push_back(state.x.back());
    if (trace.size() > 128)
      trace.erase(trace.begin());
    return r.accepted;
  }
};
std::vector<unsigned> select(const GuideCpuState &c, unsigned count) {
  hair::require(count == 3 || count == 32 || count == 128,
                "Native demo count must be 3/32/128");
  auto &ids = c.source->sourceIds;
  std::vector<unsigned> result;
  for (unsigned id : {9852u, 14u, 59u}) {
    auto it = std::find(ids.begin(), ids.end(), id);
    hair::require(it != ids.end(), "Tested source guide missing");
    result.push_back(unsigned(it - ids.begin()));
  }
  std::vector<double> nearest(ids.size(), DBL_MAX);
  for (unsigned k = 0; k < count; k++) {
    if (k < result.size()) {
      auto r = c.ranges[result[k]];
      D root{c.source->positions[r.firstVertex].x,
             c.source->positions[r.firstVertex].y,
             c.source->positions[r.firstVertex].z};
      for (unsigned i = 0; i < ids.size(); i++) {
        auto p = c.source->positions[c.ranges[i].firstVertex];
        D d = D{p.x, p.y, p.z} - root;
        nearest[i] = std::min(nearest[i], d.dot(d));
      }
    } else {
      unsigned best = 0;
      double distance = -1;
      for (unsigned i = 0; i < ids.size(); i++)
        if (std::find(result.begin(), result.end(), i) == result.end() &&
            nearest[i] > distance) {
          best = i;
          distance = nearest[i];
        }
      hair::require(distance >= 0, "Not enough guides");
      result.push_back(best);
      auto a = c.source->positions[c.ranges[best].firstVertex];
      for (unsigned i = 0; i < ids.size(); i++) {
        auto b = c.source->positions[c.ranges[i].firstVertex];
        D d{b.x - a.x, b.y - a.y, b.z - a.z};
        nearest[i] = std::min(nearest[i], d.dot(d));
      }
    }
  }
  return result;
}
std::unique_ptr<jointcpu::Problem>
problem(const GuideCpuState &c, unsigned i,
        const std::filesystem::path &fixtures) {
  auto id = c.source->sourceIds[i];
  auto r = c.ranges[i];
  if (id == 9852 || id == 14 || id == 59) {
    auto p =
        jointcpu::loadProblem(fixtures / (id == 9852 ? "primary.jwp"
                                          : id == 14 ? "heldout-slot0.jwp"
                                                     : "heldout-slot2.jwp"));
    hair::require(p->id == id && p->source.positions.size() == r.vertexCount,
                  "Reference fixture ID/shape mismatch");
    for (unsigned j = 0; j < r.vertexCount; j++)
      hair::require(std::memcmp(&p->source.positions[j],
                                &c.source->positions[r.firstVertex + j],
                                12) == 0,
                    "Reference fixture does not match groom positions");
    hair::require(p->set.pinX && p->set.pinQ,
                  "Reference demo requires fixed roots");
    return p;
  }
  auto p = std::make_unique<jointcpu::Problem>();
  p->id = id;
  p->source.positions.assign(c.source->positions.begin() + r.firstVertex,
                             c.source->positions.begin() + r.firstVertex +
                                 r.vertexCount);
  p->source.offsets = {0, r.vertexCount};
  p->source.sourceIds = {id};
  p->source.derive();
  auto &d = p->cpu;
  d.ranges = {{0, r.vertexCount, 0, r.segmentCount, 0, r.bendCount}};
  d.orientations.assign(c.orientations.begin() + r.firstSegment,
                        c.orientations.begin() + r.firstSegment +
                            r.segmentCount);
  d.bends.assign(c.bends.begin() + r.firstBend,
                 c.bends.begin() + r.firstBend + r.bendCount);
  d.lengths.assign(c.lengths.begin() + r.firstSegment,
                   c.lengths.begin() + r.firstSegment + r.segmentCount);
  d.radii.assign(c.radii.begin() + r.firstSegment,
                 c.radii.begin() + r.firstSegment + r.segmentCount);
  d.masses.assign(c.masses.begin() + r.firstVertex,
                  c.masses.begin() + r.firstVertex + r.vertexCount);
  d.velocities.resize(r.vertexCount);
  p->bind();
  return p;
}
} // namespace
std::vector<unsigned> nativeReferenceSelection(const GuideCpuState &c, unsigned n) {
  return select(c, n);
}
std::unique_ptr<jointcpu::Problem> nativeReferenceProblem(const GuideCpuState &c, unsigned i,
                                                        const std::filesystem::path &p) {
  return problem(c, i, p);
}
struct NativeReferenceDemo::Impl {
  const GuideCpuState *cpu;
  std::filesystem::path fixtures;
  D gravity;
  std::thread thread;
  std::mutex mutex;
  std::condition_variable cv;
  std::atomic<bool> stop = false, running = false;
  std::atomic<uint64_t> generation = 1;
  unsigned count = 3, pending = 0;
  D pendingKick;
  bool hasKick = false;
  std::atomic<std::shared_ptr<const NativeReferenceSnapshot>> published;
  void loop() {
    std::vector<std::unique_ptr<Entry>> entries;
    uint64_t local = 0, step = 0, kicks = 0;
    double work = 0, lastMs = 0;
    unsigned iterations = 0;
    auto epoch = Clock::now();
    bool failed = false;
    std::string status;
    auto publish = [&] {
      auto s = std::make_shared<NativeReferenceSnapshot>();
      s->generation = local;
      s->step = step;
      s->kicks = kicks;
      s->workSeconds = work;
      s->solveMs = lastMs;
      s->iterations = iterations;
      s->timestamp = Clock::now();
      s->wall = std::chrono::duration<double>(s->timestamp - epoch).count();
      s->failed = failed;
      s->status = status;
      for (auto &e : entries) {
        s->h = e->p->set.h;
        s->indices.push_back(e->index);
        s->sourceIds.push_back(e->p->id);
        s->x.push_back(e->state.x);
        s->q.push_back(e->state.q);
        s->v.push_back(e->v);
        s->rest.push_back(e->rest.x);
        s->traces.push_back(e->trace);
        s->pinError =
            std::max(s->pinError, (e->state.x[0] - e->rest.x[0]).norm());
        for (unsigned j = 0; j < e->state.x.size(); j++)
          s->maxDisplacement = std::max(s->maxDisplacement,
                                        (e->state.x[j] - e->rest.x[j]).norm());
        s->maxTip =
            std::max(s->maxTip, (e->state.x.back() - e->rest.x.back()).norm());
      }
      published.store(s);
    };
    while (!stop) {
      try {
        if (local != generation.load()) {
          unsigned requested;
          {
            std::lock_guard lock(mutex);
            requested = count;
            local = generation.load();
            pending = 0;
            hasKick = false;
          }
          entries.clear();
          for (unsigned i : select(*cpu, requested)) {
            if (stop)
              break;
            entries.push_back(
                std::make_unique<Entry>(i, problem(*cpu, i, fixtures)));
          }
          step = kicks = 0;
          work = lastMs = 0;
          failed = false;
          iterations = 0;
          status = "Ready (paused). CPU double; no collision.";
          epoch = Clock::now();
          publish();
        }
        bool doStep = false;
        {
          std::unique_lock lock(mutex);
          cv.wait(lock, [&] {
            return stop || generation != local || hasKick || pending || running;
          });
          if (stop)
            break;
          if (generation != local)
            continue;
          if (hasKick) {
            D impulse = pendingKick;
            hasKick = false;
            for (auto &e : entries)
              for (unsigned j = 1; j < e->v.size(); j++) {
                double u = double(j) / (e->v.size() - 1);
                e->v[j] = e->v[j] + std::sin(1.57079632679 * u) * impulse;
              }
            kicks++;
            publish();
          }
          doStep = !failed && (running || pending);
          if (pending)
            pending--;
        }
        if (!doStep)
          continue;
        auto begin = Clock::now();
        iterations = 0;
        bool aborted = false;
        for (auto &e : entries) {
          if (stop || generation != local) {
            aborted = true;
            break;
          }
          try {
            iterations += e->step(gravity);
          } catch (const std::exception &ex) {
            throw std::runtime_error("source " + std::to_string(e->p->id) +
                                     ": " + ex.what());
          }
        }
        if (aborted)
          continue;
        lastMs = nativels::ms(begin);
        work += lastMs / 1000;
        step++;
        status = "Completed fixed substep; roots fixed; all selected strands "
                 "solved.";
        publish();
        if (running && lastMs < entries.front()->p->set.h * 1000) {
          std::unique_lock lock(mutex);
          cv.wait_for(lock,
                      std::chrono::duration<double>(
                          double(entries.front()->p->set.h) - lastMs / 1000),
                      [&] { return stop || !running || generation != local; });
        }
      } catch (const std::exception &e) {
        failed = true;
        running = false;
        {
          std::lock_guard lock(mutex);
          pending = 0;
          hasKick = false;
        }
        status = e.what();
        auto old = published.load();
        auto s = old ? std::make_shared<NativeReferenceSnapshot>(*old)
                     : std::make_shared<NativeReferenceSnapshot>();
        s->failed = true;
        s->status = status;
        s->generation = local;
        published.store(s);
      }
    }
  }
};
NativeReferenceDemo::NativeReferenceDemo() = default;
NativeReferenceDemo::~NativeReferenceDemo() { shutdown(); }
void NativeReferenceDemo::initialize(const GuideCpuState &c,
                                     const std::filesystem::path &fixtures,
                                     Float3 g) {
  if (impl)
    return;
  impl = std::make_unique<Impl>();
  impl->cpu = &c;
  impl->fixtures = fixtures;
  impl->gravity = {g.x, g.y, g.z};
  impl->thread = std::thread([this] { impl->loop(); });
}
bool NativeReferenceDemo::initialized() const { return bool(impl); }
void NativeReferenceDemo::shutdown() {
  if (impl) {
    impl->stop = true;
    impl->cv.notify_all();
    if (impl->thread.joinable())
      impl->thread.join();
    impl.reset();
  }
}
void NativeReferenceDemo::reset(unsigned n) {
  if (!impl)
    return;
  impl->running = false;
  {
    std::lock_guard lock(impl->mutex);
    impl->count = n;
    impl->pending = 0;
    impl->hasKick = false;
    impl->generation++;
  }
  impl->cv.notify_all();
}
void NativeReferenceDemo::run(bool on) {
  if (!impl)
    return;
  auto s = snapshot();
  if (on && s && s->failed)
    return;
  impl->running = on;
  if (!on) {
    std::lock_guard lock(impl->mutex);
    impl->pending = 0;
  }
  impl->cv.notify_all();
}
bool NativeReferenceDemo::running() const { return impl && impl->running; }
void NativeReferenceDemo::steps(unsigned n) {
  if (!impl)
    return;
  auto s = snapshot();
  if (s && s->failed)
    return;
  impl->running = false;
  {
    std::lock_guard lock(impl->mutex);
    impl->pending += n;
  }
  impl->cv.notify_all();
}
void NativeReferenceDemo::kick(Float3 v) {
  if (!impl)
    return;
  auto s = snapshot();
  if (s && s->failed)
    return;
  {
    std::lock_guard lock(impl->mutex);
    impl->pendingKick = {v.x, v.y, v.z};
    impl->hasKick = true;
  }
  impl->cv.notify_all();
}
std::shared_ptr<const NativeReferenceSnapshot>
NativeReferenceDemo::snapshot() const {
  return impl ? impl->published.load() : nullptr;
}
uint64_t NativeReferenceDemo::generation() const {
  return impl ? impl->generation.load() : 0;
}
void NativeReferenceDemo::save(const std::filesystem::path &path,
                               const NativeReferenceSnapshot &s) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream f(path);
  f << std::setprecision(17) << "{\"step\":" << s.step
    << ",\"generation\":" << s.generation << ",\"h\":" << s.h
    << ",\"simulatedSeconds\":" << s.step * s.h << ",\"wallSeconds\":" << s.wall
    << (s.gpu?",\"GPUworkSeconds\":":",\"CPUworkSeconds\":") << s.workSeconds
    << ",\"backend\":\"" << (s.gpu?"gpu":"cpu") << "\",\"substepLatencyMs\":" << s.latencyMs
    << ",\"lastBatchMs\":" << s.solveMs << ",\"active\":" << s.x.size()
    << ",\"kicks\":" << s.kicks << ",\"pinError\":" << s.pinError
    << ",\"maxDisplacement\":" << s.maxDisplacement
    << ",\"maxTip\":" << s.maxTip << ",\"failed\":" << s.failed
    << ",\"strands\":[";
  for (unsigned i = 0; i < s.x.size(); i++) {
    if (i)
      f << ',';
    f << "{\"sourceId\":" << s.sourceIds[i]
      << ",\"guideIndex\":" << s.indices[i];
    for (auto field : {std::pair{"x", &s.x[i]}, std::pair{"q", &s.q[i]},
                       std::pair{"v", &s.v[i]}}) {
      f << ",\"" << field.first << "\":[";
      for (unsigned j = 0; j < field.second->size(); j++) {
        if (j)
          f << ',';
        auto a = (*field.second)[j];
        f << '[' << a.x << ',' << a.y << ',' << a.z;
        if (field.first == std::string("q"))
          f << ',' << a.w;
        f << ']';
      }
      f << ']';
    }
    f << '}';
  }
  f << "]}";
  f.flush();
  hair::require(bool(f), "Native reference snapshot write failed");
}
