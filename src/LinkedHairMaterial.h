#pragma once

namespace hairlab {
inline int ContactMaterialPartner(int region) {
  switch (region) {
  case 0: return 5;
  case 5: return 0;
  case 2: return 6;
  case 6: return 2;
  default: return -1;
  }
}

// Propagate UI edits only. Preserve contact-specific radius and guide selection.
template<class Material>
void ApplyLinkedMaterialEdits(const Material &before, const Material &after,
                             Material &target) {
  auto scalar = [](const auto &a, const auto &b, auto &out) {
    if (a != b) out = b;
  };
  auto gradient = [&](const auto &a, const auto &b, auto &out) {
    scalar(a.mMin, b.mMin, out.mMin);
    scalar(a.mMax, b.mMax, out.mMax);
    scalar(a.mMinFraction, b.mMinFraction, out.mMinFraction);
    scalar(a.mMaxFraction, b.mMaxFraction, out.mMaxFraction);
  };
  gradient(before.mGlobalPose, after.mGlobalPose, target.mGlobalPose);
  gradient(before.mSkinGlobalPose, after.mSkinGlobalPose, target.mSkinGlobalPose);
  gradient(before.mGridVelocityFactor, after.mGridVelocityFactor, target.mGridVelocityFactor);
  scalar(before.mEnableCollision, after.mEnableCollision, target.mEnableCollision);
  scalar(before.mEnableLRA, after.mEnableLRA, target.mEnableLRA);
  scalar(before.mLinearDamping, after.mLinearDamping, target.mLinearDamping);
  scalar(before.mAngularDamping, after.mAngularDamping, target.mAngularDamping);
  scalar(before.mFriction, after.mFriction, target.mFriction);
  scalar(before.mBendCompliance, after.mBendCompliance, target.mBendCompliance);
  scalar(before.mAirResistance, after.mAirResistance, target.mAirResistance);
}
}
