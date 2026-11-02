#pragma once
#include <Jolt/Physics/Hair/HairSettings.h>
#include <limits>
#include <stdexcept>

namespace hairlab {
// Author Jolt's existing supported influences; no alternate skinning kernel.
// A render follicle must not inherit motion from a free mid-strand vertex.
inline void AnchorMetaHumanRenderRoots(JPH::HairSettings &settings,
    const JPH::Array<JPH::HairSettings::SStrand> &source) {
  using namespace JPH;
  for (uint32 i=0;i<source.size();++i) {
    const auto &strand=source[i];auto &root=settings.mRenderVertices[strand.mStartVtx];
    const auto original=root.mInfluences[0].mVertexIndex;
    if(original==HairSettings::cNoInfluence)throw std::runtime_error("Unbound groom root");
    if((Vec3(root.mPosition)-Vec3(settings.mSimVertices[original].mPosition)).LengthSq()<1.e-16f)
      continue; // Direct simulated strands retain their exact upstream mapping.
    uint32 best=HairSettings::cNoInfluence;float distance=std::numeric_limits<float>::max();
    for(const auto &guide:settings.mSimStrands) {
      if(guide.mMaterialIndex!=strand.mMaterialIndex)continue;
      float d=(Vec3(root.mPosition)-Vec3(settings.mSimVertices[guide.mStartVtx].mPosition)).LengthSq();
      if(d<distance){best=guide.mStartVtx;distance=d;}
    }
    if(best==HairSettings::cNoInfluence)throw std::runtime_error("Missing follicle guide");
    root.mInfluences[0].mVertexIndex=best;
    // The next 15mm grows out of the scalp. A second standard Jolt influence
    // blends its own moving guide with the fixed follicle instead of letting
    // a free parent pull the first edge into the skin.
    float arc=0;
    for(uint32 v=strand.mStartVtx+1;v<strand.mEndVtx;++v) {
      arc+=(Vec3(settings.mRenderVertices[v].mPosition)-Vec3(settings.mRenderVertices[v-1].mPosition)).Length();
      if(arc>.015f)break;
      auto &vertex=settings.mRenderVertices[v];
      if(vertex.mInfluences[0].mVertexIndex!=best)vertex.mInfluences[1].mVertexIndex=best;
    }
  }
}
}
