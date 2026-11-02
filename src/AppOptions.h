#pragma once
#include <filesystem>
#include <string>
#include <array>
struct AppOptions {
    bool hair=true,warp=false,validate=false,originalHead=false;
    unsigned frames=0,width=1280,height=800;
    std::string view="initial",draw="render";
    std::filesystem::path log="artifacts/session.log",capture,report;
    std::string test,orientationPerturb;
    unsigned orientationSweeps=0,participating=1,frameStride=4;
    bool pinRoot=true,materialFrames=false,resetAfter=false,autoOrientation=false;
    std::string solverMode="static",windowTarget,windowPerturb;
    unsigned windowSweeps=0;float windowH=1.f/120;
    bool windowPinX=true,windowPinQ=true,windowReset=false,windowAuto=false;
    unsigned simInitializer=0;
    unsigned simSteps=0,sweepsPerStep=8;float simH=1.f/120;bool simSpecified=false,simReset=false,simDiagnostics=false;
    std::array<float,3> gravity{0,-9.81f,0},simVelocity{0,0,0};
    std::string referenceBackend="cpu";
    unsigned referenceRefinements=1;
    unsigned referenceGuides=3,referenceSteps=0,referenceEvery=4;
    bool referenceRun=false,referenceTip=false,referenceResetCheck=false;
    std::array<float,3> referenceKick{0,0,0};std::filesystem::path referenceCaptureDir;
    static AppOptions parse();
};
