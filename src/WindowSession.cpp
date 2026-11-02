#include "WindowSession.h"
#include "OrientationFixtures.h"
#include "imgui.h"
#include <fstream>
void WindowSession::selectMode(int selected, ID3D12Device *device, const Submit &submit,
                               OrientationSession &orientation)
{
    hair::require(selected >= 0 && selected <= 4, "Invalid execution mode");
    orientation.cancelQueued();
    autoRelax = false;
    solver.invalidate();
    resetPending = true;
    preparePending.clear();
    perturbPending = runPending = 0;
    lastDiagnostic.clear();
    mode = selected;
    // Complete the reset before the newly selected panel can submit diagnostics or work.
    UploadList keep;
    submit([&](auto *list) { record(device, list, keep, orientation, false); });
}
void WindowSession::initialize(ID3D12Device *device, const Submit &submit, guidegpu::State &state,
                               const std::filesystem::path &shaders)
{
    UploadList keep;
    submit([&](auto *list) { solver.initialize(device, list, state, shaders, keep); });
    initialized = true;
}
void WindowSession::panel(ID3D12Device *device, const Submit &submit, OrientationSession &orientation)
{
    const char *names[] = {"Static Preview", "Orientation Only", "Coupled Window Relaxation", "Guide Dynamics", "Native Reference Demo"};
    int selected = mode;
    if (ImGui::Combo("Execution mode", &selected, names, 5))
        selectMode(selected, device, submit, orientation);
    if (mode != 2)
        return;
    ImGui::SeparatorText("Fixed-target window relaxation (NO time stepping)");
    ImGui::TextWrapped(
        "Guide positions move. Render hairs stay static. Prepare once; sweeps reuse the SAME target and h.");
    auto &s = solver.settings;
    const char *labels[] = {"1", "10", "100", "All"};
    int selection = s.participating == 1 ? 0 : s.participating == 10 ? 1 : s.participating == 100 ? 2 : 3;
    bool changed = false;
    if (ImGui::Combo("Window participating", &selection, labels, 4))
    {
        s.participating = selection == 0 ? 1 : selection == 1 ? 10 : selection == 2 ? 100 : 0;
        changed = true;
        resetPending = true;
    }
    if (ImGui::Checkbox("Pin Root Position", &s.pinX))
    {
        changed = true;
        resetPending = true;
    }
    if (ImGui::Checkbox("Pin Root Orientation", &s.pinQ))
    {
        changed = true;
        resetPending = true;
    }
    if (ImGui::InputFloat("Fixed h (seconds)", &s.h, .0001f, .001f, "%.6f"))
        changed = true;
    if (ImGui::InputFloat("Window baseSS", &s.ss, .1f, 1, "%.6g"))
        changed = true;
    if (ImGui::InputFloat("Window baseBT", &s.bt, .000001f, .00001f, "%.6g"))
        changed = true;
    if (changed)
    {
        solver.invalidate();
        autoRelax = false;
        runPending = perturbPending = 0;
        preparePending.clear();
    }
    ImGui::TextUnformatted(solver.ready() ? "Problem prepared" : "UNPREPARED / STALE: prepare before solving");
    const float invH2 = 1.f / (s.h * s.h);
    const bool validParameters = std::isfinite(s.h) && s.h > 0 && std::isfinite(invH2) && invH2 > 0 &&
                                 std::isfinite(s.ss) && s.ss > 0 && std::isfinite(s.bt) && s.bt > 0;
    if (!validParameters)
        ImGui::TextUnformatted("h, baseSS and baseBT must be finite and positive.");
    ImGui::BeginDisabled(!validParameters);
    if (ImGui::Button("Prepare Rest-Target Problem"))
        preparePending = "rest";
    if (ImGui::Button("Prepare Displaced-Target Problem"))
        preparePending = "displaced";
    ImGui::EndDisabled();
    if (ImGui::Button("Reset Current Guide State"))
    {
        resetPending = true;
        preparePending.clear();
        autoRelax = false;
        runPending = perturbPending = 0;
    }
    ImGui::BeginDisabled(!solver.ready());
    if (ImGui::Button("Perturb Positions"))
    {
        perturbPending = 1;
        autoRelax = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Perturb Orientations"))
    {
        perturbPending = 2;
        autoRelax = false;
    }
    if (ImGui::Button("Perturb Both"))
    {
        perturbPending = 3;
        autoRelax = false;
    }
    int n = int(sweeps);
    if (ImGui::SliderInt("Window sweeps", &n, 1, 4096))
        sweeps = unsigned(n);
    if (ImGui::Button("Run One Window Sweep"))
        runPending = 1;
    ImGui::SameLine();
    if (ImGui::Button("Run N Window Sweeps"))
        runPending = sweeps;
    ImGui::Checkbox("Auto Relax Current Problem", &autoRelax);
    if (ImGui::Button("Read Window Diagnostics"))
    {
        try
        {
            UploadList keep;
            if (resetPending || !preparePending.empty() || perturbPending || runPending)
                submit([&](auto *list) { record(device, list, keep, orientation, false); });
            lastDiagnostic = solver.diagnostics(device, submit, solver.read(device, submit));
        }
        catch (const std::exception &e)
        {
            lastDiagnostic = e.what();
            autoRelax = false;
        }
    }
    ImGui::EndDisabled();
    if (!lastDiagnostic.empty())
        ImGui::TextWrapped("%s", lastDiagnostic.c_str());
}
void testWindowModeTransitions(WindowSession &window, OrientationSession &orientation,
                               const OrientationTestContext &ctx)
{
    window.selectMode(2, ctx.device, ctx.submit, orientation);
    window.solver.settings = {1, 1.f / 120, 1, 1e-5f, true, true};
    UploadList keep;
    ctx.submit([&](auto *list) {
        window.solver.prepare(ctx.device, list, windowmath::targets(orientation.cpu, "rest"), "rest", keep);
        window.solver.perturb(list, 3);
        window.solver.run(list, 2);
    });
    window.autoRelax = true;
    orientation.solver.settings.autoSolve = true;
    window.selectMode(1, ctx.device, ctx.submit, orientation);
    hair::require(!window.autoRelax && !orientation.solver.settings.autoSolve && !window.solver.ready(),
                  "Mode switch retained Auto or prepared problem");
    // This is the same immediate diagnostic path reachable in the new panel's first frame.
    orientation.state.verifyImmutable(ctx.device, ctx.submit, true);
    auto snapshot = readOrientation(orientation.state, orientation.solver, ctx.device, ctx.submit);
    auto diagnostic = measureOrientation(orientation.cpu, orientation.solver, snapshot);
    hair::require(diagnostic.energy() < 1e-10, "Mode switch diagnostics observed stale coupled state");
    orientation.solver.settings.autoSolve = true;
    window.selectMode(0, ctx.device, ctx.submit, orientation);
    hair::require(!orientation.solver.settings.autoSolve, "Static mode retained orientation Auto");
    orientation.state.verifyImmutable(ctx.device, ctx.submit, true);
    auto path = ctx.report;
    path.replace_extension("mode-transitions.json");
    std::ofstream report(path);
    report << "{\"coupledToOrientationResetBeforeDiagnostics\":true,\"autoCancelled\":true,\"staticResetExact\":true}";
    report.flush();
    hair::require(bool(report), "Mode transition report write failed");
}
void WindowSession::record(ID3D12Device *device, ID3D12GraphicsCommandList *list, UploadList &keep,
                           OrientationSession &orientation, bool allowAuto)
{
    if (resetPending)
    {
        solver.resetCurrent(device, list, keep);
        orientation.solver.resetStatus(device, list, keep);
        resetPending = false;
        lastDiagnostic.clear();
    }
    if (mode != 2)
        return;
    if (!preparePending.empty())
    {
        auto y = windowmath::targets(orientation.cpu, preparePending);
        solver.prepare(device, list, y, preparePending, keep);
        preparePending.clear();
        lastDiagnostic.clear();
    }
    if (perturbPending)
    {
        solver.perturb(list, perturbPending);
        perturbPending = 0;
        lastDiagnostic.clear();
    }
    if (runPending)
    {
        solver.run(list, runPending);
        runPending = 0;
    }
    else if (allowAuto && autoRelax)
    {
        if (solver.ready())
            solver.run(list, sweeps);
        else
            autoRelax = false;
    }
}
