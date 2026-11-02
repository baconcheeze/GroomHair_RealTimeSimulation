#include "DynamicsSession.h"
#include "OrientationFixtures.h"
#include "WindowSession.h"
#include "imgui.h"
#include <fstream>
void DynamicsSession::initialize(ID3D12Device *d, const Submit &submit, guidegpu::State &state, WindowSolver &w,
                                 const std::filesystem::path &path,const DirectX::XMFLOAT4X4& gravityRotation)
{
    window = &w;
    worldToHairRotation=gravityRotation;
    setWorldGravity(worldGravity);
    UploadList keep;
    submit([&](auto *list) { solver.initialize(d, list, state, w, path, keep); });
    initialized = true;
}
void DynamicsSession::modeChanged(int previous, int next, ID3D12Device *d, const Submit &submit)
{
    running = false;
    pendingSteps = 0;
    resetPending = kickPending = false;
    clock = {};
    baseline = std::chrono::steady_clock::now();
    suspended = false;
    message.clear();
    if (next == 3)
    {
        savedFixedSettings = window->settings;
        UploadList keep;
        submit([&](auto *list) { solver.reset(d, list, keep); });
    }
    else if (previous == 3)
    {
        solver.invalidate();
        window->settings = savedFixedSettings;
    }
}
void DynamicsSession::panel(ID3D12Device *d, const Submit &submit)
{
    ImGui::SeparatorText("Fixed-step Guide Dynamics");
    ImGui::TextWrapped("Body/self collision is not enabled. Render hairs remain static; use Guide-only viewing.");
    auto &s = solver.settings;
    const char *labels[] = {"1", "10", "100", "All"};
    int n = s.participating == 1 ? 0 : s.participating == 10 ? 1 : s.participating == 100 ? 2 : 3;
    bool changed = false;
    if (ImGui::Combo("Dynamic participating", &n, labels, 4))
    {
        s.participating = n == 0 ? 1 : n == 1 ? 10 : n == 2 ? 100 : 0;
        changed = true;
    }
    changed |= ImGui::Checkbox("Dynamic Pin Root Position", &s.pinX);
    changed |= ImGui::Checkbox("Dynamic Pin Root Orientation", &s.pinQ);
    changed |= ImGui::InputFloat("Substep h (seconds)", &s.h, .0001f, .001f, "%.7f");
    int sweeps = int(solver.sweeps);
    if (ImGui::SliderInt("Sweeps per substep", &sweeps, 1, 128))
    {
        solver.sweeps = unsigned(sweeps);
        changed = true;
    }
    int guess=solver.initializer==3?1:0;
    const char* guesses[]={"Full prediction (original)","Energy guarded (research candidate)"};
    if(ImGui::Combo("Initial iterate",&guess,guesses,2)){solver.initializer=guess?3u:0u;changed=true;}
    if(guess)ImGui::TextWrapped("Accuracy gate is not met at 32 sweeps. This changes only the initial iterate; y and gravity remain unchanged.");
    bool gravityOn = worldGravity.x != 0 || worldGravity.y != 0 || worldGravity.z != 0;
    if (ImGui::Checkbox("Gravity enabled", &gravityOn))
    {
        setWorldGravity(gravityOn ? Float3{0, -9.81f, 0} : Float3{});
        changed = true;
    }
    if(ImGui::InputFloat3("Gravity (world m/s^2)", &worldGravity.x)){setWorldGravity(worldGravity);changed=true;}
    ImGui::Text("Gravity hair-local: %.5f %.5f %.5f",solver.gravity.x,solver.gravity.y,solver.gravity.z);
    if (changed)
    {
        solver.invalidate();
        running = false;
        pendingSteps = 0;
        clock.remainder = 0;
        message = "Configuration changed: Reset Simulation before stepping.";
    }
    bool valid = std::isfinite(s.h) && s.h > 0 && std::isfinite(1.f / (s.h * s.h)) && 1.f / (s.h * s.h) > 0 &&
                 std::isfinite(solver.gravity.x) && std::isfinite(solver.gravity.y) && std::isfinite(solver.gravity.z);
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Reset Simulation"))
    {
        resetPending = true;
        pendingSteps = 0;
        kickPending = false;
        running = false;
    }
    ImGui::EndDisabled();
    ImGui::Checkbox("Enable expensive sweep diagnostics", &solver.diagnosticSnapshots);
    ImGui::BeginDisabled(!solver.ready() || resetPending);
    if (ImGui::Checkbox("Run Dynamics", &running))
    {
        baseline = std::chrono::steady_clock::now();
        clock.remainder = 0;
    }
    if (ImGui::Button("Step One Substep"))
    {
        pendingSteps = 1;
        running = false;
        clock.remainder = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Step N Substeps"))
    {
        pendingSteps = stepCount;
        running = false;
        clock.remainder = 0;
    }
    int count = int(stepCount);
    if (ImGui::SliderInt("N substeps", &count, 1, 240))
        stepCount = unsigned(count);
    ImGui::InputFloat3("One-shot velocity amplitude (m/s)", &kickAmplitude.x);
    if (ImGui::Button("Apply Smooth Velocity ONCE"))
    {
        kickPending = true;
        running = false;
    }
    if (ImGui::Button("Read Dynamics Diagnostics"))
    {
        try
        {
            UploadList keep;
            if (pendingSteps || kickPending)
                submit([&](auto *list) { record(d, list, keep, 0); });
            message = solver.diagnostics(d, submit, solver.read(d, submit));
        }
        catch (const std::exception &e)
        {
            message = e.what();
            solver.failed = true;
            solver.failure = message;
            pendingSteps = 0;
            running = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::Text("Each substep = %.7f s; Step N advances N*h, not h/N.",s.h);
    ImGui::Text("Queued substeps: %u | observed complete: %u", solver.queuedSteps, solver.observedCompleted);
    ImGui::Text("Simulated time (queued): %.6f s", solver.simulationTime());
    ImGui::Text("Discarded wall time: %.6f s", clock.discarded);
    if (!message.empty())
        ImGui::TextWrapped("%s", message.c_str());
}
void DynamicsSession::record(ID3D12Device *d, ID3D12GraphicsCommandList *list, UploadList &keep, unsigned slot)
{
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - baseline).count();
    baseline = now;
    if (suspended)
    {
        if (running)
            clock.discarded += elapsed;
        elapsed = 0;
        suspended = false;
    }
    if (resetPending)
    {
        solver.reset(d, list, keep);
        clock = {};
        resetPending = false;
        elapsed = 0;
        message.clear();
    }
    if (solver.failed)
    {
        running = false;
        pendingSteps = 0;
    }
    if (kickPending)
    {
        solver.kick(list, kickAmplitude);
        kickPending = false;
    }
    unsigned count = pendingSteps;
    pendingSteps = 0;
    if (!deterministic && running)
    {
        if (solver.ready())
            count += clock.advance(elapsed, solver.settings.h, true);
        else
            running = false;
    }
    if (count)
        solver.step(list, count);
    solver.recordStatusPoll(list, slot);
}

void testDynamicsModeTransitions(DynamicsSession &dynamics, WindowSession &window, OrientationSession &orientation,
                                 const OrientationTestContext &ctx)
{
    auto select = [&](int next) {
        int old = window.mode;
        window.selectMode(next, ctx.device, ctx.submit, orientation);
        dynamics.modeChanged(old, next, ctx.device, ctx.submit);
    };
    select(3);
    ctx.submit([&](auto *list) { dynamics.solver.step(list, 2); });
    dynamics.running = true;
    dynamics.clock.remainder = .001;
    select(2);
    hair::require(!dynamics.running && dynamics.clock.remainder == 0 && !dynamics.solver.ready(),
                  "Leaving dynamics retained runnable state");
    orientation.state.verifyImmutable(ctx.device, ctx.submit, true);
    select(3);
    hair::require(dynamics.solver.queuedSteps == 0 && dynamics.solver.ready(),
                  "Reenter dynamics did not reset physical time");
    orientation.state.verifyImmutable(ctx.device, ctx.submit, true);
    select(0);
    orientation.state.verifyImmutable(ctx.device, ctx.submit, true);
    auto path = ctx.report;
    path.replace_extension("modes.json");
    std::ofstream f(path);
    f << "{\"modeSwitchReset\":true,\"autoCancelled\":true,\"timeReset\":true}";
    f.flush();
    hair::require(bool(f), "Dynamics mode report failure");
}
