#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Core/Array.h>
#include <Jolt/Math/Mat44.h>
#include <Jolt/Math/Quat.h>
#include <filesystem>
namespace hairlab {
class CharacterLocomotion {
public:
    void Load(const std::filesystem::path& dir, bool dance = false);
    void Reset();
    void Advance(JPH::Vec3Arg input,float dt);
    JPH::Array<JPH::Mat44> Pose(const JPH::Array<JPH::Mat44>& base,bool enabled) const;
    JPH::Vec3 position=JPH::Vec3::sZero(),velocity=JPH::Vec3::sZero();
    float yaw=0,phase=0,walkBlend=0,distance=0,floorBindY=0;
    float speed=1.0f,acceleration=3.5f,braking=5.0f;
    float clipDuration=0,clipFps=0,clipStride=0;
    uint32_t clipFrames=0;
    bool danceClip=false;
private:
    JPH::Array<int32_t> parents;
    uint32_t joints[9]{};
    JPH::Array<JPH::Quat> clipRotations;
    JPH::Array<JPH::Vec3> clipRoots;
};
}
