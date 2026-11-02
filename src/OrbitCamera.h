#pragma once
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include "imgui.h"
struct OrbitCamera {
    DirectX::XMFLOAT3 target{0, 0.5f, 0};
    float yaw = 0.75f, pitch = 0.45f, distance = 9;
    DirectX::XMFLOAT3 initialTarget{0,.5f,0};
    DirectX::XMFLOAT4X4 basis{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    float initialYaw=.75f,initialPitch=.45f,initialDistance=9,minDistance=.3f,maxDistance=200,nearClip=.05f,farClip=500;
    float sceneRadius=5.5f,aspect=1.6f,verticalFov=DirectX::XM_PIDIV4; bool hairScene=false;
    float fitDistance() const { float angle=std::atan(std::tan(verticalFov*.5f)*std::min(1.f,aspect)); return sceneRadius*1.08f/std::sin(angle); }
    void reset() { target=initialTarget; yaw=initialYaw; pitch=initialPitch; distance=initialDistance; }
    void frame() { target=initialTarget; distance=std::clamp(hairScene?fitDistance():13.f,minDistance,maxDistance); }
    void setHair(DirectX::XMFLOAT3 center,float radius,const DirectX::XMFLOAT4X4& axes,float viewAspect) {
        hairScene=true; initialTarget=center; sceneRadius=radius; basis=axes; aspect=viewAspect;
        minDistance=.01f; maxDistance=5; nearClip=.001f; farClip=20;
        initialYaw=-DirectX::XM_PI/12; initialPitch=DirectX::XM_PI/18;
        initialDistance=std::clamp(fitDistance(),minDistance,maxDistance); reset();
    }
    DirectX::XMVECTOR up() const { return DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0,1,0,0),DirectX::XMLoadFloat4x4(&basis)); }
    DirectX::XMVECTOR eye() const {
        using namespace DirectX;
        return XMLoadFloat3(&target) + XMVector3TransformNormal(XMVectorSet(distance * std::cos(pitch)*std::sin(yaw), distance*std::sin(pitch), -distance*std::cos(pitch)*std::cos(yaw), 0),XMLoadFloat4x4(&basis));
    }
    void panKeys(float horizontal,float vertical,float dt,bool fast=false) {
        using namespace DirectX;
        if (!(dt>0) || !std::isfinite(dt)) return;
        auto forward=XMVector3Normalize(XMLoadFloat3(&target)-eye());
        auto right=XMVector3Normalize(XMVector3Cross(up(),forward));
        auto screenUp=XMVector3Cross(forward,right);
        float magnitude=std::max(1.f,std::sqrt(horizontal*horizontal+vertical*vertical));
        float step=std::min(dt,.05f)*std::max(.01f,distance*.35f)*(fast?3.f:1.f)/magnitude;
        XMStoreFloat3(&target,XMLoadFloat3(&target)+(right*horizontal+screenUp*vertical)*step);
    }
    void update(bool allowKeyboard=true) {
        using namespace DirectX;
        auto& io = ImGui::GetIO();
        if (allowKeyboard && !io.WantCaptureKeyboard && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_R)) reset();
            if (ImGui::IsKeyPressed(ImGuiKey_F)) frame();
            float horizontal=float(ImGui::IsKeyDown(ImGuiKey_RightArrow))-float(ImGui::IsKeyDown(ImGuiKey_LeftArrow));
            float vertical=float(ImGui::IsKeyDown(ImGuiKey_UpArrow))-float(ImGui::IsKeyDown(ImGuiKey_DownArrow));
            panKeys(horizontal,vertical,io.DeltaTime,io.KeyShift);
        }
        if (io.WantCaptureMouse) return;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            yaw -= io.MouseDelta.x * 0.006f;
            pitch = std::clamp(pitch + io.MouseDelta.y * 0.006f, -1.48f, 1.48f);
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            auto forward = XMVector3Normalize(XMLoadFloat3(&target) - eye());
            auto right = XMVector3Normalize(XMVector3Cross(up(), forward));
            auto up = XMVector3Cross(forward, right);
            XMStoreFloat3(&target, XMLoadFloat3(&target) + (-right*io.MouseDelta.x + up*io.MouseDelta.y)*(distance*0.0015f));
        }
        distance = std::clamp(distance*std::exp(-io.MouseWheel*0.12f), minDistance, maxDistance);
    }
    DirectX::XMMATRIX matrix(float viewportAspect) const {
        using namespace DirectX;
        return XMMatrixLookAtLH(eye(), XMLoadFloat3(&target), up()) * XMMatrixPerspectiveFovLH(verticalFov, viewportAspect, nearClip, farClip);
    }
};
