#include "NativeReferenceView.h"
#include "GuideGpu.h"
void NativeReferenceView::initialize(const GuideCpuState &cpu,
                                     const std::filesystem::path &dir,
                                     const RestData &rest, unsigned n,
                                     bool tip) {
  if (simulation.initialized())
    return;
  gpuCpu=&cpu;gpuDir=dir;
  worldToHair = rest.worldToHairRotation;
  basis = rest.cameraBasis;
  count = n;
  tipFocus = tip;
  focusPending = true;
  simulation.initialize(cpu, dir,
                        worldVectorToHair({0, -9.81f, 0}, worldToHair));
  reset(n);
}
void NativeReferenceView::refresh(OrbitCamera &camera) {
  current = gpuBackend?gpu.snapshot():simulation.snapshot();
  if (current && current->generation == generation() &&
      focusPending && !current->x.empty()) {
    focus(camera, tipFocus);
    focusPending = false;
  }
}
void NativeReferenceView::focus(OrbitCamera &camera, bool tip) {
  if (!current || current->x.empty())
    return;
  Bounds b;
  if (tip) {
    auto p = current->x[0].back();
    b.add({float(p.x - .006), float(p.y - .006), float(p.z - .006)});
    b.add({float(p.x + .006), float(p.y + .006), float(p.z + .006)});
  } else
    for (auto &s : current->x)
      for (auto p : s)
        b.add({float(p.x), float(p.y), float(p.z)});
  camera.setHair(b.center(), b.radius(), basis, camera.aspect);
  if (tip) {
    camera.initialDistance = camera.distance = .04f;
    camera.sceneRadius = .012f;
  }
}
void NativeReferenceView::panel(OrbitCamera &camera) {
  ImGui::SeparatorText("Native Reference Demo");
  int backend=gpuBackend?1:0;const char* backends[]={"CPU Native Reference","GPU Full-Strand - Experimental"};
  if(ImGui::Combo("Reference backend",&backend,backends,2)){setBackend(backend==1);return;}
  ImGui::TextWrapped("Validated full-strand solver. Body/self collision is "
                     "unavailable. Render hairs hidden.");
  auto s = current;
  bool ready = s && s->generation == generation();
  const char *counts[] = {"3 tested real strands", "32 spatial guides",
                          "128 spatial guides"};
  int choice = count == 3 ? 0 : count == 32 ? 1 : 2;
  if (ImGui::Combo("Reference guides", &choice, counts, 3)) {
    count = choice == 0 ? 3 : choice == 1 ? 32 : 128;
    reset(count);
    focusPending = true;
  }
  ImGui::BeginDisabled(!ready || (s && s->failed));
  if (ImGui::Button(running() ? "Pause" : "Run"))
    run(!running());
  ImGui::SameLine();
  if (ImGui::Button("Step"))
    steps(1);
  int n = int(stepN);
  ImGui::SetNextItemWidth(80);
  ImGui::InputInt("N substeps", &n);
  stepN = unsigned(std::clamp(n, 1, 120));
  if (ImGui::Button("Step N"))
    steps(stepN);
  ImGui::InputFloat3("One-shot world kick (m/s)", &kickWorld.x, "%.3f");
  if (ImGui::Button("Apply velocity kick ONCE"))
    kick(worldVectorToHair(kickWorld, worldToHair));
  ImGui::EndDisabled();
  if (ImGui::Button("Reset Reference Simulation"))
    reset(count);
  if (s) {
    ImGui::Text("Active: %zu / requested %u | step %llu", s->x.size(), count,
                s->step);
    ImGui::Text("Simulated: %.6f s | h %.9f s", s->step * s->h, s->h);
    ImGui::Text("Wall since reset (incl. pause): %.2f s",
                s->wall + std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - s->timestamp)
                              .count());
    ImGui::Text(gpuBackend?"GPU work (timestamps): %.3f ms":"Last CPU batch substep: %.3f ms", s->solveMs);
    ImGui::Text(gpuBackend?"Sim time / GPU work: %.3fx":"Sim time / CPU work: %.3fx",
                s->workSeconds > 0 ? s->step * s->h / s->workSeconds : 0);
    if(gpuBackend)ImGui::Text("Observed substep latency: %.3f ms",s->latencyMs);
    if ((gpuBackend?s->latencyMs:s->solveMs) > s->h * 1000)
      ImGui::TextColored({1, .65f, .2f, 1},
                         "SLOWER THAN REAL TIME (h unchanged)");
    ImGui::Text("Max tip displacement: %.4f mm", s->maxTip * 1000);
    ImGui::Text("Max displacement: %.4f mm | kicks %llu",
                s->maxDisplacement * 1000, s->kicks);
    ImGui::TextWrapped("%s", s->status.c_str());
  } else
    ImGui::TextUnformatted("Preparing reference strands on worker...");
  ImGui::Checkbox("Draw native guides", &visible);
  ImGui::Checkbox("Rest ghost (gray)", &ghost);
  ImGui::Checkbox("Actual tip traces (orange)", &traces);
  if (ImGui::Button("Frame selected guides"))
    focus(camera, false);
  if (ImGui::Button("Close up tip: source 9852"))
    focus(camera, true);
  ImGui::TextWrapped("RMB orbit / MMB pan / wheel. Pause stops after the "
                     "in-flight substep. No wall-clock catch-up.");
}
void NativeReferenceView::draw(ID3D12Device *d, ID3D12GraphicsCommandList *l,
                               ID3D12RootSignature *root,
                               ID3D12PipelineState *pso,
                               const DirectX::XMFLOAT4X4 &m, unsigned frame) {
  if(gpuBackend){if(visible)gpu.draw(l,root,pso,m,frame);return;}
  if (!current || !visible)
    return;
  std::vector<Vertex> v;
  auto line = [&](hair::D a, hair::D b, Float3 c) {
    v.push_back({{float(a.x), float(a.y), float(a.z)}, c});
    v.push_back({{float(b.x), float(b.y), float(b.z)}, c});
  };
  for (unsigned i = 0; i < current->x.size(); i++) {
    if (ghost)
      for (unsigned j = 1; j < current->rest[i].size(); j++)
        line(current->rest[i][j - 1], current->rest[i][j], {.25f, .28f, .45f});
    for (unsigned j = 1; j < current->x[i].size(); j++)
      line(current->x[i][j - 1], current->x[i][j], {.05f, 1, 1});
    if (traces)
      for (unsigned j = 1; j < current->traces[i].size(); j++)
        line(current->traces[i][j - 1], current->traces[i][j], {1, .5f, .05f});
  }
  if (v.empty())
    return;
  size_t bytes = v.size() * sizeof(Vertex);
  if (capacity[frame] < bytes) {
    D3D12_HEAP_PROPERTIES h{};
    h.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC b{};
    b.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    b.Width = bytes;
    b.Height = 1;
    b.DepthOrArraySize = b.MipLevels = 1;
    b.SampleDesc.Count = 1;
    b.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    guidegpu::check(d->CreateCommittedResource(
        &h, D3D12_HEAP_FLAG_NONE, &b, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&buffers[frame])));
    capacity[frame] = bytes;
  }
  void *ptr;
  D3D12_RANGE noRead{0, 0};
  guidegpu::check(buffers[frame]->Map(0, &noRead, &ptr));
  std::memcpy(ptr, v.data(), bytes);
  buffers[frame]->Unmap(0, nullptr);
  D3D12_VERTEX_BUFFER_VIEW view{buffers[frame]->GetGPUVirtualAddress(),
                                UINT(bytes), sizeof(Vertex)};
  l->SetGraphicsRootSignature(root);
  l->SetGraphicsRoot32BitConstants(0, 16, &m, 0);
  l->SetPipelineState(pso);
  l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
  l->IASetVertexBuffers(0, 1, &view);
  l->DrawInstanced(UINT(v.size()), 1, 0, 0);
}

void NativeReferenceView::setBackend(bool b){
 run(false);gpuBackend=b;
 if(b){simulation.run(false);if(!gpu.initialized())gpu.initialize(gpuDevice,gpuSubmit,*gpuCpu,gpuDir,worldVectorToHair({0,-9.81f,0},worldToHair),count,gpuFrequency);else gpu.reset(count);}
 else simulation.reset(count);
 current=gpuBackend?gpu.snapshot():simulation.snapshot();focusPending=true;
}
void NativeReferenceView::reset(unsigned n){if(gpuBackend)gpu.reset(n);else simulation.reset(n);}
void NativeReferenceView::run(bool b){if(gpuBackend)gpu.run(b);else simulation.run(b);}
bool NativeReferenceView::running()const{return gpuBackend?gpu.running():simulation.running();}
void NativeReferenceView::steps(unsigned n){if(gpuBackend)gpu.steps(n);else simulation.steps(n);}
void NativeReferenceView::kick(Float3 v){if(gpuBackend)gpu.kick(v);else simulation.kick(v);}
uint64_t NativeReferenceView::generation()const{return gpuBackend?gpu.generation():simulation.generation();}
