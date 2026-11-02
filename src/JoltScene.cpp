// Adapted initialization/update from Jolt v5.6.0 HairTest.cpp. MIT, Jorrit
// Rouwe.
#include "JoltScene.h"
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/ObjectStream/ObjectStreamIn.h>
#include <Jolt/ObjectStream/ObjectStreamOut.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Hair/RegisterHair.h>
#include <Jolt/RegisterTypes.h>
using namespace JPH; // Upstream Samples.h supplies this before Layers.h.
#include <Layers.h>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "MetaHumanAttachment.h"
#include "BodyPrimitiveFit.h"
namespace hairlab {
using namespace JPH;
std::filesystem::path JoltScene::assets;
[[noreturn]] static void FatalError(const char *fmt, ...) {
  char s[2048];
  va_list a;
  va_start(a, fmt);
  vsnprintf(s, sizeof(s), fmt, a);
  va_end(a);
  throw std::runtime_error(s);
}
struct AssetStream {
  std::ifstream f;
  AssetStream(const char *n, std::ios::openmode mode)
      : f(JoltScene::assets / n, mode) {
    if (!f)
      FatalError("Missing original asset %s", n);
  }
  std::istream &Get() { return f; }
};
static Array<uint8> ReadData(const char *n) {
  std::ifstream f(JoltScene::assets / n, std::ios::binary | std::ios::ate);
  if (!f)
    FatalError("Missing groom %s", n);
  auto len = f.tellg();
  if (len < 128)
    FatalError("Truncated groom %s", n);
  Array<uint8> d;
  d.resize(size_t(len));
  f.seekg(0);
  f.read((char *)d.data(), len);
  if (!f)
    FatalError("Read failed %s", n);
  return d;
}
void JoltScene::Initialize() {
  // Check groom file exists
  String groom_file = metaHuman ? "metahuman.hair" : bob ? "bob.hair" : "studio.hair";
  String full_path = String((assets / groom_file.c_str()).string());
  if (!std::filesystem::exists(full_path))
    FatalError("Missing original Moca groom: %s", full_path.c_str());

  // Read face mesh and animation
  AssetStream asset_stream("character.bin", std::ios::in | std::ios::binary);
  StreamInWrapper stream(asset_stream.Get());

  // Neck joint index
  stream.Read(mHeadJointIdx);

  // Vertices
  uint32 num_vertices;
  stream.Read(num_vertices);
  Array<Float3> vertices;
  vertices.resize(num_vertices);
  stream.ReadBytes(vertices.data(), sizeof(Float3) * num_vertices);

  // Indices
  uint32 num_indices;
  stream.Read(num_indices);
  Array<IndexedTriangleNoMaterial> indices;
  indices.resize(num_indices);
  stream.ReadBytes(indices.data(),
                   sizeof(IndexedTriangleNoMaterial) * num_indices);

  // Inverse Bind Matrices
  uint32 num_joints;
  stream.Read(num_joints);
  Array<Mat44> inv_bind_pose;
  inv_bind_pose.resize(num_joints);
  stream.ReadBytes(inv_bind_pose.data(), sizeof(Mat44) * num_joints);

  // Skin Weights
  uint num_skin_weights_per_vertex;
  Array<HairSettings::SkinWeight> skin_weights;
  stream.Read(num_skin_weights_per_vertex);
  skin_weights.resize(num_skin_weights_per_vertex * num_vertices);
  stream.ReadBytes(skin_weights.data(), sizeof(HairSettings::SkinWeight) *
                                            num_skin_weights_per_vertex *
                                            num_vertices);

  // Animation
  uint32 num_frames;
  stream.Read(num_frames);
  mFaceAnimation.resize(num_frames);
  for (uint32 frame = 0; frame < num_frames; ++frame) {
    mFaceAnimation[frame].resize(num_joints);
    for (uint32 joint = 0; joint < num_joints; ++joint) {
      Float3 translation, rotation;
      stream.Read(translation);
      stream.Read(rotation);
      Quat rotation_quat(rotation.x, rotation.y, rotation.z,
                         Sqrt(max(0.0f, 1.0f - Vec3(rotation).LengthSq())));
      mFaceAnimation[frame][joint] =
          Mat44::sRotationTranslation(rotation_quat, Vec3(translation));
    }
  }

  // Read collision hulls
  uint32 num_hulls;
  stream.Read(num_hulls);
  for (uint32 i = 0; i < num_hulls; ++i) {
    // Attached to joint
    uint32 joint_index;
    stream.Read(joint_index);

    // Read number of vertices
    uint32 num_hull_vertices;
    stream.Read(num_hull_vertices);

    // Read vertices
    ConvexHullShapeSettings shape_settings;
    shape_settings.SetEmbedded();
    shape_settings.mPoints.resize(num_hull_vertices);
    for (uint32 j = 0; j < num_hull_vertices; ++j)
      stream.Read(shape_settings.mPoints[j]);
    // Replace the old coarse neck/chest primitives with mesh-fitted pieces.
    if (joint_index != mHeadJointIdx)
      continue;
    if(metaHuman && joint_index==mHeadJointIdx) {
      // Separate the skull from its auricles: a convex envelope containing
      // the ears bridges the recess behind them and makes scalp hair hover.
      std::ifstream file(assets/"metahuman.skull",std::ios::binary);
      uint32 h[3]{};file.read(reinterpret_cast<char*>(h),sizeof(h));
      if(!file || h[0]!=0x4c554b53u || h[1]!=1 || h[2]<4 || h[2]>8192)FatalError("Invalid fitted skull hull");
      shape_settings.mPoints.resize(h[2]);shape_settings.mMaxConvexRadius=.0005f;shape_settings.mHullTolerance=.0001f;
      for(auto &p:shape_settings.mPoints) {
        Float3 v;file.read(reinterpret_cast<char*>(&v),sizeof(v));
        if(!file || !std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))FatalError("Invalid skull point");
        p=Vec3(v);
      }
      if(file.peek()!=std::char_traits<char>::eof())FatalError("Unexpected skull hull length");
    }

    Mat44 transform = joint_index != 0xffffffff ? mFaceAnimation[0][joint_index]
                                                : Mat44::sIdentity();
    Mat44 inv_transform = transform.Inversed();
    for (Vec3 &v : shape_settings.mPoints)
      v = inv_transform * v;

    // Create the body
    BodyCreationSettings body(
        &shape_settings, RVec3(transform.GetTranslation()),
        transform.GetQuaternion(), EMotionType::Kinematic, Layers::MOVING);
    BodyID body_id =
        mBodyInterface->CreateAndAddBody(body, EActivation::DontActivate);

    mAttachedBodies.push_back({joint_index, body_id});
  }

  if (metaHuman) {
    // Ear folds protrude beyond the coarse cranial hull. Use small hulls
    // derived from the actual Moca ear mesh, following the same Head pose.
    std::ifstream earFile(assets / "metahuman.ears",std::ios::binary);
    uint32 header[3]{};earFile.read(reinterpret_cast<char*>(header),sizeof(header));
    if (!earFile || header[0]!=0x48524145u || header[1]!=1 || header[2]!=2)
      FatalError("Invalid or missing MetaHuman ear collision data");
    const Mat44 transform=mFaceAnimation[0][mHeadJointIdx];
    const Mat44 inverse=transform.InversedRotationTranslation();
    for (uint32 e=0;e<2;++e) {
      uint32 n=0;earFile.read(reinterpret_cast<char*>(&n),4);
      if (!earFile || n<4 || n>1024)FatalError("Invalid ear hull vertex count");
      ConvexHullShapeSettings shape;shape.SetEmbedded();
      shape.mMaxConvexRadius=.0005f;shape.mHullTolerance=.0001f;
      for (uint32 j=0;j<n;++j) {
        Float3 p;earFile.read(reinterpret_cast<char*>(&p),sizeof(p));
        if (!earFile || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
          FatalError("Invalid ear hull point");
        shape.mPoints.push_back(inverse*Vec3(p));
      }
      auto result=shape.Create();
      if (result.HasError())FatalError("Ear convex hull: %s",result.GetError().c_str());
      BodyCreationSettings body(result.Get(),RVec3(transform.GetTranslation()),transform.GetQuaternion(),EMotionType::Kinematic,Layers::MOVING);
      auto id=mBodyInterface->CreateAndAddBody(body,EActivation::DontActivate);
      if (id.IsInvalid())FatalError("Ear collision body creation failed");
      mAttachedBodies.push_back({mHeadJointIdx,id});
    }
    if (earFile.peek()!=std::char_traits<char>::eof())FatalError("Unexpected ear hull data length");
  }

  // Body/arms: existing skin-only partitions. Fit once; GPU skins endpoints.
  {
    std::ifstream file(assets/"body-colliders.bin",std::ios::binary);
    uint32 h[3]{};file.read(reinterpret_cast<char*>(h),sizeof(h));
    if(!file || h[0]!=0x4c484342u || h[1]!=2 || h[2]==0 || h[2]>64)
      FatalError("Invalid body convex hull asset");
    struct FitSource {uint32 joint;Array<Vec3> points;Array<uint32> skin;};
    Array<FitSource> sources;
    for(uint32 i=0;i<h[2];++i) {
      uint32 joint=0,n=0;file.read(reinterpret_cast<char*>(&joint),4);file.read(reinterpret_cast<char*>(&n),4);
      if(!file || joint>=num_joints || n<4 || n>65536)FatalError("Invalid body hull range");
      ConvexHullShapeSettings shape;shape.SetEmbedded();
      shape.mMaxConvexRadius=0.f;shape.mHullTolerance=.00025f;
      for(uint32 v=0;v<n;++v) {
        Float3 p;file.read(reinterpret_cast<char*>(&p),sizeof(p));
        if(!file || !std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))FatalError("Invalid body hull point");
        shape.mPoints.push_back(Vec3(p));
      }
      uint32 dynamicCount=0;file.read(reinterpret_cast<char*>(&dynamicCount),4);
      if(!file || dynamicCount>65536)FatalError("Invalid skinned seam count");
      Array<uint32> dynamicVertices(dynamicCount);
      for(auto &vertex:dynamicVertices) {
        file.read(reinterpret_cast<char*>(&vertex),4);
        if(!file || vertex>=num_vertices)FatalError("Invalid seam skin vertex");
      }
      sources.push_back({joint,std::move(shape.mPoints),std::move(dynamicVertices)});
    }
    for(uint32 i=0;i<sources.size();++i){
      const auto &source=sources[i];uint32 joint=source.joint;
      unsigned pieces=joint==2?12:(joint==150||joint==170)?6:(joint==151||joint==171)?4:1;
      size_t begin=mBodyPrimitives.size()/6;
      if(pieces>1)FitBodyCapsuleCover(source.points,joint,source.skin,vertices,skin_weights,num_skin_weights_per_vertex,inv_bind_pose,mBodyPrimitives,pieces);
      else FitBodyCapsule(source.points,joint,source.skin,vertices,skin_weights,num_skin_weights_per_vertex,inv_bind_pose,mBodyPrimitives);
      for(size_t p=begin;p<mBodyPrimitives.size()/6;++p)mBodyPrimitiveSources.push_back(i);
    }
    bodyHullCount=uint32(mBodyPrimitives.size()/6);
    if(bodyHullCount>128)FatalError("Body primitive bitmask capacity exceeded");
    if(file.peek()!=std::char_traits<char>::eof())FatalError("Unexpected body hull length");

  }

  // Make mesh relative to neck bind pose
  Mat44 inv_bind_neck = inv_bind_pose[mHeadJointIdx];
  Mat44 bind_neck = inv_bind_neck.Inversed();
  // Primitive endpoints share the same neck-bind input space as scalp vertices.
  for(size_t i=0;i<mBodyPrimitives.size();i+=6)for(uint e=0;e<2;++e){
    auto &p=mBodyPrimitives[i+e];Float3 v;(inv_bind_neck*Vec3(p.x,p.y,p.z)).StoreFloat3(&v);p=Float4(v.x,v.y,v.z,p.w);
  }

  for (Float3 &v : vertices)
    (inv_bind_neck * Vec3(v)).StoreFloat3(&v);
  for (Mat44 &m : inv_bind_pose)
    m = m * bind_neck;

  // Read hair file
  Array<uint8> data = ReadData(groom_file.c_str());
  if (data[0] != 'H' || data[1] != 'A' || data[2] != 'I' || data[3] != 'R')
    FatalError("Invalid hair file");

  uint32 features = *reinterpret_cast<const uint32 *>(&data[12]);
  if ((features & 0b10) != 0b10)
    FatalError("We require points to be defined");

  uint32 num_strands = *reinterpret_cast<const uint32 *>(&data[4]);
  uint32 num_points = *reinterpret_cast<const uint32 *>(&data[8]);
  std::ifstream material_file(assets / (metaHuman ? "metahuman.materials" : bob ? "bob.materials" : "groom.materials"), std::ios::binary);
  uint32 material_count = 0;
  material_file.read(reinterpret_cast<char *>(&material_count), 4);
  if (material_count != num_strands)
    FatalError("Moca groom material count mismatch");
  Array<uint32> strand_materials(material_count);
  material_file.read(reinterpret_cast<char *>(strand_materials.data()),
                     material_count * 4);
  if (!material_file)
    FatalError("Truncated Moca groom materials");
  for (auto material : strand_materials)
    if (material >= (metaHuman ? 8u : 2u))
      FatalError("Invalid Moca material");

  const uint16 *num_segments = nullptr;
  int num_segments_delta = 0;
  const Float3 *points = nullptr;
  if (features & 0b01) {
    // Num segments differs per strand
    num_segments = reinterpret_cast<const uint16 *>(&data[128]);
    num_segments_delta = 1;
    points = reinterpret_cast<const Float3 *>(
        &data[128 + num_strands * sizeof(uint16)]);
  } else {
    // Num segments is constant
    num_segments = reinterpret_cast<const uint16 *>(&data[16]);
    num_segments_delta = 0;
    points = reinterpret_cast<const Float3 *>(&data[128]);
  }

  // Init strands
  if (sLimitMaxStrands && !metaHuman)
    num_strands = std::min(num_strands, sMaxStrands);
  Array<HairSettings::SVertex> hair_vertices;
  hair_vertices.reserve(num_points);
  Array<HairSettings::SStrand> hair_strands;
  hair_strands.reserve(num_strands);
  const Mat44 &neck_transform = mFaceAnimation[0][mHeadJointIdx];
  Mat44 inv_neck_transform = neck_transform.Inversed();
  for (uint32 strand = 0; strand < num_strands; ++strand) {
    // Transform relative to neck
    Array<Vec3> out_points;
    // Authored world-metre positions; same neck-space boundary as scalp.
    for (uint16 point = 0; point < *num_segments + 1; ++point)
      out_points.push_back(inv_neck_transform * Vec3(points[point]));

    // Add the strand to the hair settings
    uint32 first_point = uint32(hair_vertices.size());
    for (uint32 point = 0; point < uint32(out_points.size()); ++point) {
      HairSettings::SVertex v;
      out_points[point].StoreFloat3(&v.mPosition);
      v.mInvMass = point == 0 ? 0.0f : 1.0f;
      hair_vertices.push_back(v);
    }
    hair_strands.push_back(HairSettings::SStrand(
        first_point, uint32(hair_vertices.size()), strand_materials[strand]));

    points += *num_segments + 1;
    num_segments += num_segments_delta;
  }

  // Resample if requested
  // MetaHuman is already sampled and anatomically fitted at this count.
  // Re-resampling it erases the short ear-clearance bends (up to 22mm
  // displacement in the previous fitted asset) and may cross the ear again.
  const bool fittedSamples = metaHuman && std::all_of(hair_strands.begin(),hair_strands.end(),[](const auto &s) {
    return s.VertexCount()==sOverrideVerticesPerStrand;
  });
  if (sOverrideVerticesPerStrand > 1 && !fittedSamples)
    HairSettings::sResample(hair_vertices, hair_strands,
                            sOverrideVerticesPerStrand);

  // Load shaders
  mHairShaders.Init(mComputeSystem);

  // Init hair settings
  mHairSettings = new HairSettings;
  mHairSettings->mScalpVertices = std::move(vertices);
  mHairSettings->mScalpTriangles = std::move(indices);
  mHairSettings->mScalpInverseBindPose = std::move(inv_bind_pose);
  mHairSettings->mScalpSkinWeights = std::move(skin_weights);
  mHairSettings->mScalpNumSkinWeightsPerVertex = num_skin_weights_per_vertex;
  mHairSettings->mNumIterationsPerSecond = sNumSolverIterationsPerSecond;
  HairSettings::Material m;
  m.mEnableCollision = sEnableCollision;
  m.mEnableLRA = sEnableLRA;
  m.mLinearDamping = sLinearDamping;
  m.mAirResistance = sAirResistance;
  m.mAngularDamping = sAngularDamping;
  m.mFriction = sFriction;
  m.mMaxLinearVelocity = sMaxLinearVelocity;
  m.mMaxAngularVelocity = sMaxAngularVelocity;
  m.mGravityFactor = sGravityFactor;
  m.mGravityPreloadFactor = sGravityPreloadFactor;
  m.mBendCompliance = std::pow(10.0f, sBendComplianceExponent);
  m.mStretchCompliance = std::pow(10.0f, sStretchComplianceExponent);
  m.mInertiaMultiplier = sInertiaMultiplier;
  m.mHairRadius = sHairRadius;
  m.mWorldTransformInfluence = sWorldTransformInfluence;
  m.mGridVelocityFactor = sGridVelocityFactor;
  m.mGridDensityForceFactor = sGridDensityForceFactor;
  m.mGlobalPose = sGlobalPose;
  m.mSkinGlobalPose = sSkinGlobalPose;
  m.mMaxLinearVelocity = 10.0f;
  m.mSimulationStrandsFraction = 0.01f * (bob || metaHuman ? 5.0f : sSimulationStrandsPercentage);
  // Keep the longer side/back free, and use a restrained salon hold for the
  // short forehead strands. Both regions still run the upstream simulation.
  if (bob) {
    m.mGlobalPose.mMax *= 2.4f;
    m.mSkinGlobalPose.mMax *= 5.f / 3.f;
    m.mWorldTransformInfluence = {0.f, .8f};
    m.mBendComplianceMultiplier = {1.f, 65.f, 65.f, 1.f};
  }
  mHairSettings->mMaterials.push_back(m);
  if (metaHuman) {
    auto &cap = mHairSettings->mMaterials[0];
    // The swept cap is dressed against the scalp and tied into the tails.
    // Its endpoints must follow the scalp too; loose-tip settings opened
    // holes and let the ear-adjacent cap fold through itself during head turns.
    // Long tails and face framing retain their separate, moving materials.
    cap.mGlobalPose = {1.f, 1.f, 0.f, 1.f};
    cap.mSkinGlobalPose = {1.f, 1.f, 0.f, 1.f};
    // Collision runs after pose retention in Jolt and would displace this
    // deliberately rigid, scalp-following region again.
    cap.mEnableCollision = false;
    cap.mWorldTransformInfluence = {0.f, .15f};
    cap.mLinearDamping = 8.f;
    cap.mAngularDamping = 9.f;
    cap.mHairRadius={0.f,.002f,0.f,.15f};
    const auto fittedCap=cap;
    // Preserve Alembic groups so Jolt parenting never crosses unrelated locks.
    const auto base = m;
    for (uint32 region = 1; region < 8; ++region) {
      auto part = base;
      if (region == 1) {
        part.mGlobalPose = sFringeGlobalPose;
        part.mSkinGlobalPose = sFringeSkinGlobalPose;
        part.mLinearDamping = sFringeLinearDamping;
        part.mAngularDamping = sFringeAngularDamping;
      }
      if (region == 2 || region == 6) {
        // Hold the first free point as well as the follicle. Its measured
        // rest arc fraction is <= 0.05676; release over the next 6% so the
        // remaining ponytail stays free. Includes contact strands.
        part.mGlobalPose = {1.f, 0.f, .06f, .12f};
        part.mSkinGlobalPose = {1.f, .25f, 0.f, 1.f};
        part.mWorldTransformInfluence = {0.f, .65f};
      }
      if (region == 3 || region == 4) {
        part.mGlobalPose = {.02f, .002f, 0.f, .8f};
        part.mSkinGlobalPose = {1.f, .35f, 0.f, .75f};
        part.mWorldTransformInfluence = {0.f, .55f};
        part.mLinearDamping = 8.f;
        part.mAngularDamping = 9.f;
      }
      if (region == 4) {
        part.mSimulationStrandsFraction=1.f;
        part.mGlobalPose={.08f,.025f,0.f,1.f};
        part.mSkinGlobalPose={1.f,.8f,0.f,1.f};
      }
      if(region==3 || region==4) {
        part.mSimulationStrandsFraction=1.f;
        part.mHairRadius={.001f,.003f,0.f,.15f};
      }
      if(region==5){part=fittedCap;part.mSimulationStrandsFraction=1.f;}
      if(region==6)part.mSimulationStrandsFraction=1.f;
      if(region==5 || region==6) {
        // A small contact allowance covers the moving auricle and the chord
        // between samples. Taper from the follicle to avoid lifting its root.
        part.mHairRadius={region==5?0.f:.001f,region==5?.002f:.003f,0.f,.15f};
      }
      if(region==7) {
        part.mSimulationStrandsFraction=1.f;
        part.mGlobalPose={.04f,.01f,0.f,1.f};part.mSkinGlobalPose={1.f,.65f,0.f,1.f};
        part.mHairRadius={.001f,.002f,0.f,.15f};
      }
      // Preserve bangs (1) and side bangs (3). Other non-tail regions
      // follow the skinned pose fully; collision must not displace them after
      // the final ApplyGlobalPose operation.
      if (region == 4 || region == 5 || region == 7) {
        part.mGlobalPose = {1.f, 1.f, 0.f, 1.f};
        part.mSkinGlobalPose = {1.f, 1.f, 0.f, 1.f};
        part.mEnableCollision = false;
      }
      mHairSettings->mMaterials.push_back(part);
    }
  } else {
  if (bob) {
    m.mGlobalPose = sFringeGlobalPose;
    m.mSkinGlobalPose = sFringeSkinGlobalPose;
    m.mLinearDamping = sFringeLinearDamping;
    m.mAngularDamping = sFringeAngularDamping;
    m.mWorldTransformInfluence = {0.f, .55f};
    m.mBendCompliance *= .65f;
    m.mBendComplianceMultiplier = {1.f, 30.f, 30.f, 1.f};
  }
  mHairSettings->mMaterials.push_back(m);
  }
  if(!bob && !metaHuman) {
    // User-approved Long straight defaults; other hairstyles are unchanged.
    auto &side=mHairSettings->mMaterials[0];
    side.mBendCompliance=1.0e-3f;
    side.mAirResistance=sLongAirResistance;
    side.mLinearDamping=6.0f;
    side.mAngularDamping=sLongAngularDamping;
    side.mFriction=.44f;
    side.mGlobalPose.mMin=.0377f;
    side.mGlobalPose.mMax=.0003f;
    auto &bangs=mHairSettings->mMaterials[1];
    bangs.mGlobalPose={1.f,1.f,0.f,1.f};
    bangs.mSkinGlobalPose={1.f,1.f,0.f,1.f};
    bangs.mEnableCollision=false; // Collision follows retention, so disable displacement here.
  }
  mHairSettings->mSimulationBoundsPadding = Vec3::sReplicate(0.1f);
  mHairSettings->mInitialGravity =
      inv_bind_neck.Multiply3x3(mPhysicsSystem->GetGravity());
  if(guideReduction!=1 && guideReduction!=2 && guideReduction!=4 && guideReduction!=8)FatalError("Guide reduction must be 1/2/4/8");
  for(size_t i=0;i<mHairSettings->mMaterials.size();++i){
    auto &material=mHairSettings->mMaterials[i];
    float fraction=material.mSimulationStrandsFraction/float(guideReduction);
    if(materialOverrides.size()==mHairSettings->mMaterials.size())material=materialOverrides[i];
    material.mSimulationStrandsFraction=fraction;
  }
  mHairSettings->InitRenderAndSimulationStrands(hair_vertices, hair_strands);
  if(metaHuman)AnchorMetaHumanRenderRoots(*mHairSettings,hair_strands);

#ifdef JPH_OBJECT_STREAM
  // Write and read back to test ObjectStream
  RegisterHair();
  std::stringstream object_stream_data;
  if (!ObjectStreamOut::sWriteObject(object_stream_data,
                                     ObjectStream::EStreamType::Binary,
                                     *mHairSettings))
    FatalError("Failed to save settings");
  mHairSettings = nullptr;
  if (!ObjectStreamIn::sReadObject(object_stream_data, mHairSettings))
    FatalError("Failed to load settings");
#endif // JPH_OBJECT_STREAM

  // Initialize the hair settings
  float max_dist_sq = 0.0f;
  mHairSettings->Init(max_dist_sq);
  JPH_ASSERT(max_dist_sq < 1.0e-4f);

  // Write and read back to test SaveBinaryState
  std::stringstream stream_data;
  {
    StreamOutWrapper stream_out(stream_data);
    mHairSettings->SaveBinaryState(stream_out);
  }
  mHairSettings = new HairSettings;
  {
    StreamInWrapper stream_in(stream_data);
    mHairSettings->RestoreBinaryState(stream_in);
  }
  mHairSettings->InitCompute(mComputeSystem);

  mHair = new Hair(mHairSettings, RVec3(neck_transform.GetTranslation()),
                   neck_transform.GetQuaternion(), Layers::MOVING);
  mHair->Init(mComputeSystem);
  mHair->SetBodyPrimitives(mBodyPrimitives,mComputeSystem);
  locomotion.Load(assets,danceClip);
  mWorldPose=locomotion.Pose(mFaceAnimation[0],walkingEnabled||danceClip);
  headWorldPosition=mWorldPose[mHeadJointIdx].GetTranslation();
  headWorldVelocity=headWorldAcceleration=Vec3::sZero();
  headPreviousPosition=headWorldPosition;headPreviousVelocity=Vec3::sZero();
  PublishPose(0,true);

#ifdef JPH_DEBUG_RENDERER
  // Update drawing range
  sDrawSimulationStrandCount = (uint)mHairSettings->mSimStrands.size();
#endif // JPH_DEBUG_RENDERER
}

void JoltScene::Update(float delta) {
  // Select the next animation frame
  if (animate)
    ++mFrame;
  mFrame = mFrame % (uint32)mFaceAnimation.size();
  if((danceClip&&animate)||(!danceClip&&walkingEnabled))locomotion.Advance(moveInput,delta);
  mWorldPose=locomotion.Pose(mFaceAnimation[danceClip?0:mFrame],walkingEnabled||danceClip);
  Vec3 head=mWorldPose[mHeadJointIdx].GetTranslation();
  headPreviousPosition=headWorldPosition;headPreviousVelocity=headWorldVelocity;
  Vec3 velocity=(head-headWorldPosition)/delta;
  headWorldAcceleration=(velocity-headWorldVelocity)/delta;
  headWorldVelocity=velocity;headWorldPosition=head;
  PublishPose(delta,false);
}
void JoltScene::PublishPose(float delta,bool teleport) {
  // Use Jolt's existing skin/global-pose-only path when physics is disabled.
  // Temporary overrides never replace the user's material settings.
  struct RestoreMaterials {
    HairSettings *settings;
    Array<HairSettings::Material> saved;
    ~RestoreMaterials() { if (!saved.empty()) settings->mMaterials = std::move(saved); }
  } restore{mHairSettings, {}};
  if (!hairSimulationEnabled) {
    restore.saved = mHairSettings->mMaterials;
    for (auto &material : mHairSettings->mMaterials) {
      material.mEnableCollision = false;
      material.mGlobalPose = {1.f, 1.f, 0.f, 1.f};
      material.mSkinGlobalPose = {1.f, 1.f, 0.f, 1.f};
    }
  }
  if (hairSimulationEnabled != previousHairSimulationEnabled) {
    teleport = true; // Clear stale velocities when switching either way.
    previousHairSimulationEnabled = hairSimulationEnabled;
  }
  BodyInterface &bi = mPhysicsSystem->GetBodyInterfaceNoLock();
  const auto &joints=mWorldPose;
  using ProfileClock=std::chrono::steady_clock;
  const auto profileHullBegin=profileEnabled?ProfileClock::now():ProfileClock::time_point{};
  profileHullRefitMs=0;profileHullRefits=0;

  // Skin matrices for body primitive endpoints are shared with scalp skinning.
  // Body primitive endpoints are skinned on the GPU. No CPU skin/refit loop.

  // Position the collision hulls
  if(profileEnabled){profileHullTotalMs=std::chrono::duration<double,std::milli>(ProfileClock::now()-profileHullBegin).count();profileHullSkinMs=profileHullTotalMs-profileHullRefitMs;}
  for (const AttachedBody &ab : mAttachedBodies) {
    Mat44 body_transform =
        ab.mJointIdx != 0xffffffff ? joints[ab.mJointIdx] : Mat44::sIdentity();
    if(teleport)bi.SetPositionAndRotation(ab.mBodyID,RVec3(body_transform.GetTranslation()),body_transform.GetQuaternion(),EActivation::DontActivate);
    else bi.MoveKinematic(ab.mBodyID, RVec3(body_transform.GetTranslation()),
                     body_transform.GetQuaternion(), delta);
  }

  // Set the new rotation of the hair
  RVec3 position = RVec3(joints[mHeadJointIdx].GetTranslation());
  Quat rotation = joints[mHeadJointIdx].GetQuaternion();
  mHair->SetPosition(position);
  mHair->SetRotation(rotation);
  if(teleport)mHair->OnTeleported();

  // Update the hair
  const auto profileRecordBegin=profileEnabled?ProfileClock::now():ProfileClock::time_point{};
  mHair->Update(delta, joints[mHeadJointIdx].Inversed(), joints.data(),
                *mPhysicsSystem, mHairShaders, mComputeSystem, mComputeQueue);
  const auto profileWaitBegin=profileEnabled?ProfileClock::now():ProfileClock::time_point{};
  if(profileEnabled)profileHairRecordMs=std::chrono::duration<double,std::milli>(profileWaitBegin-profileRecordBegin).count();
  {
    JPH_PROFILE("Hair Compute");
    mComputeQueue->ExecuteAndWait();
  }
  if(profileEnabled)profileHairWaitMs=std::chrono::duration<double,std::milli>(ProfileClock::now()-profileWaitBegin).count();
}

} // namespace hairlab
