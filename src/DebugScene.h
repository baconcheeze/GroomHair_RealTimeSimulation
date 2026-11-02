#pragma once
#include <DirectXMath.h>
#include <vector>
struct Vertex { DirectX::XMFLOAT3 position, color; };
struct DebugScene {
    std::vector<Vertex> vertices;
    unsigned gridCount{}, axesStart{}, meshStart{}, meshCount{};
    bool grid = true, axes = true, mesh = true;
    DebugScene() {
        auto line = [&](DirectX::XMFLOAT3 a, DirectX::XMFLOAT3 b, DirectX::XMFLOAT3 c) { vertices.push_back({a,c}); vertices.push_back({b,c}); };
        for (int i=-5;i<=5;++i) {
            float f=static_cast<float>(i);
            line({f,0,-5},{f,0,5},{0.28f,0.31f,0.35f});
            line({-5,0,f},{5,0,f},{0.28f,0.31f,0.35f});
        }
        gridCount=static_cast<unsigned>(vertices.size()); axesStart=gridCount;
        line({0,0.015f,0},{2,0.015f,0},{1,0.15f,0.15f});
        line({0,0.015f,0},{0,2,0},{0.2f,1,0.2f});
        line({0,0.015f,0},{0,0.015f,2},{0.2f,0.4f,1});
        meshStart=static_cast<unsigned>(vertices.size());
        const DirectX::XMFLOAT3 p[]={{-.5f,.02f,-.5f},{-.5f,1.02f,-.5f},{.5f,1.02f,-.5f},{.5f,.02f,-.5f}, {-.5f,.02f,.5f},{-.5f,1.02f,.5f},{.5f,1.02f,.5f},{.5f,.02f,.5f}};
        const unsigned indices[]={0,1,2,0,2,3, 7,6,5,7,5,4, 4,5,1,4,1,0, 3,2,6,3,6,7, 1,5,6,1,6,2, 4,0,3,4,3,7};
        for(unsigned i=0;i<36;++i) { float shade=.55f+.08f*(i/6); vertices.push_back({p[indices[i]],{shade,.48f, .22f}}); }
        meshCount=36;
    }
};
