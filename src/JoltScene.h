#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Compute/ComputeSystem.h>
#include <Jolt/Physics/Hair/Hair.h>
#include <Jolt/Physics/Hair/HairShaders.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <filesystem>
#include "CharacterLocomotion.h"
namespace hairlab {
using namespace JPH;
class JoltScene {
public:
  ~JoltScene() { delete mHair; }
  void Initialize();
  void Update(float delta);
  Hair *hair() const { return mHair; }
  const HairSettings &settings() const { return *mHairSettings; }
  uint frame() const { return mFrame; }
  bool danceClip=false;
  uint guideReduction=1;
  Array<HairSettings::Material> materialOverrides;
  uint frameCount() const { return uint(mFaceAnimation.size()); }
  bool animate = true;
  bool hairSimulationEnabled = true;
  bool previousHairSimulationEnabled = true;
  bool walkingEnabled = true;
  bool profileEnabled = false;
  double profileHullSkinMs=0,profileHullRefitMs=0,profileHullTotalMs=0,profileHairRecordMs=0,profileHairWaitMs=0;
  uint32 profileHullRefits=0;
  JPH::Vec3 moveInput=JPH::Vec3::sZero();
  CharacterLocomotion locomotion;
  const Array<Mat44>& worldPose() const { return mWorldPose; }
  Vec3 headWorldPosition=Vec3::sZero(),headWorldVelocity=Vec3::sZero(),headWorldAcceleration=Vec3::sZero();
  Vec3 headPreviousPosition=Vec3::sZero(),headPreviousVelocity=Vec3::sZero();
  PhysicsSystem *mPhysicsSystem{};
  BodyInterface *mBodyInterface{};
  ComputeSystem *mComputeSystem{};
  ComputeQueue *mComputeQueue{};
  static std::filesystem::path assets;
  bool bob = true;
  bool metaHuman = false;
  using Gradient = HairSettings::Gradient;

  inline static bool sLimitMaxStrands = true;
  inline static uint sMaxStrands = 32000u;
  inline static float sSimulationStrandsPercentage = 10.0f;
  inline static uint sOverrideVerticesPerStrand = 32;
  inline static uint sNumSolverIterationsPerSecond =
      HairSettings::cDefaultIterationsPerSecond;
  inline static bool sEnableCollision = true;
  inline static bool sEnableLRA = true;
  inline static float sLinearDamping = 6.0f;
  inline static float sAirResistance = 1.0f;
  inline static float sLongAirResistance = 2.0f;
  inline static float sLongAngularDamping = 3.0f;
  inline static float sAngularDamping = 6.0f;
  inline static float sFriction = 0.2f;
  inline static float sMaxLinearVelocity = 10.0f;
  inline static float sMaxAngularVelocity = 50.0f;
  inline static float sBendComplianceExponent = -7.0f;    // 1.0e-7f
  inline static float sStretchComplianceExponent = -8.0f; // 1.0e-8f
  inline static float sInertiaMultiplier = 10.0f;
  inline static Gradient sHairRadius{0.001f, 0.001f};
  inline static Gradient sWorldTransformInfluence{0.0f, 1.0f};
  inline static Gradient sGravityFactor{0.1f, 1.0f, 0.2f, 0.8f};
  inline static float sGravityPreloadFactor = 1.0f;
  inline static Gradient sGridVelocityFactor{0.25f, 0.1f};
  inline static Gradient sGlobalPose{0.01f, 0.00025f, 0.0f, 0.65f};
  inline static Gradient sSkinGlobalPose{1.0f, 0.06f, 0.0f, 0.45f};
  inline static Gradient sFringeGlobalPose{.4f, .4f, 0.f, .75f};
  inline static Gradient sFringeSkinGlobalPose{1.f, .35f, 0.f, .65f};
  inline static float sFringeLinearDamping = 9.f;
  inline static float sFringeAngularDamping = 10.f;
  inline static float sGridDensityForceFactor = 0.0f;
#ifdef JPH_DEBUG_RENDERER
  inline static uint sDrawSimulationStrandBegin = 0;
  inline static uint sDrawSimulationStrandCount = UINT_MAX;
  inline static bool sDrawRods = false;
  inline static bool sDrawUnloadedRods = false;
  inline static bool sDrawVertexVelocity = false;
  inline static bool sDrawAngularVelocity = false;
  inline static bool sDrawOrientations = false;
  inline static bool sDrawNeutralDensity = false;
  inline static bool sDrawGridDensity = false;
  inline static bool sDrawGridVelocity = false;
  inline static bool sDrawSkinPoints = false;
  inline static Hair::ERenderStrandColor sRenderStrandColor =
      Hair::ERenderStrandColor::PerSimulatedStrand;
  inline static bool sDrawInitialGravity = false;
#endif // JPH_DEBUG_RENDERER
  inline static bool sDrawRenderStrands = true;
  inline static bool sDrawHeadMesh = true;

  uint32 mHeadJointIdx = 0;
  Array<Array<Mat44>> mFaceAnimation;
  struct AttachedBody {
    uint32 mJointIdx;
    BodyID mBodyID;
  };
  Array<AttachedBody> mAttachedBodies;
  uint32 bodyHullCount = 0;
  Array<Float4> mBodyPrimitives;
  Array<uint32> mBodyPrimitiveSources;
  Ref<HairSettings> mHairSettings = nullptr;
  HairShaders mHairShaders;
  Hair *mHair = nullptr;
  uint mFrame = 0;
private:
  Array<Mat44> mWorldPose;
  void PublishPose(float delta,bool teleport);
};
} // namespace hairlab
