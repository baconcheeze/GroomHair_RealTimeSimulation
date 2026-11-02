#include "AppOptions.h"
#include <windows.h>
#include <shellapi.h>
#include <stdexcept>
#include <cmath>
#include <sstream>
AppOptions AppOptions::parse() {
#ifdef _DEBUG
    const std::string dynamicsOutput="artifacts/dynamics/Debug/";
#else
    const std::string dynamicsOutput="artifacts/dynamics/Release/";
#endif
    AppOptions o; bool explicitMode=false,windowOptions=false,orientationOptions=false,dynamicsOptions=false,referenceOptions=false;
    int argc; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc); if(!argv) throw std::runtime_error("Command line unavailable");
    struct Guard { LPWSTR* p; ~Guard(){LocalFree(p);} } guard{argv};
    auto text=[](const std::wstring& v) { std::string result; for(auto c:v) result.push_back(c<=127?static_cast<char>(c):'?'); return result; };
    for(int i=1;i<argc;++i) {
        std::wstring key=argv[i]; auto value=[&]() { if(i+1>=argc) throw std::runtime_error("Missing command line value"); return std::wstring(argv[++i]); };
        if(key.starts_with(L"--sim-")||key==L"--gravity"||key==L"--sweeps-per-step")dynamicsOptions=true;
        if(key.starts_with(L"--reference-"))referenceOptions=true;
        if(key==L"--solver-mode")explicitMode=true;
        if(key.starts_with(L"--window-"))windowOptions=true;
        if(key.starts_with(L"--orientation-")||key==L"--unpin-root")orientationOptions=true;
        auto number=[&](unsigned minimum,unsigned maximum) { auto s=value(); if(s.empty() || s.find_first_not_of(L"0123456789")!=s.npos) throw std::runtime_error("Invalid numeric option"); auto n=std::stoul(s); if(n<minimum||n>maximum) throw std::runtime_error("Numeric option out of range"); return static_cast<unsigned>(n); };
        if(key==L"--warp") o.warp=true;
        else if(key==L"--frames") o.frames=number(1,1000000);
        else if(key==L"--width") o.width=number(64,8192);
        else if(key==L"--height") o.height=number(64,8192);
        else if(key==L"--log") o.log=value();
        else if(key==L"--capture") o.capture=value();
        else if(key==L"--report") o.report=value();
        else if(key==L"--validate-rest") o.validate=true;
        else if(key==L"--test") {o.test=text(value());if(o.test!="guide-state"&&o.test!="orientation-local"&&o.test!="orientation-groom"&&o.test!="position-local"&&o.test!="window-solver"&&o.test!="window-groom"&&o.test!="dynamics-local"&&o.test!="dynamics-groom")throw std::runtime_error("Unknown test");}
        else if(key==L"--solver-mode"){o.solverMode=text(value());if(o.solverMode!="static"&&o.solverMode!="orientation"&&o.solverMode!="window"&&o.solverMode!="dynamics"&&o.solverMode!="reference")throw std::runtime_error("Unknown execution mode");}
        else if(key==L"--reference-backend"){auto b=value();if(b!=L"cpu"&&b!=L"gpu")throw std::runtime_error("Reference backend must be cpu|gpu");o.referenceBackend=b==L"gpu"?"gpu":"cpu";}
        else if(key==L"--reference-refinements"){o.referenceRefinements=number(1,5);if(o.referenceRefinements!=1&&o.referenceRefinements!=5)throw std::runtime_error("Reference refinements must be 1|5");}
        else if(key==L"--reference-guides"){o.referenceGuides=number(3,128);if(o.referenceGuides!=3&&o.referenceGuides!=32&&o.referenceGuides!=128)throw std::runtime_error("Reference guides must be 3/32/128");}
        else if(key==L"--reference-steps")o.referenceSteps=number(1,1200);
        else if(key==L"--reference-every")o.referenceEvery=number(1,120);
        else if(key==L"--reference-capture-dir")o.referenceCaptureDir=value();
        else if(key==L"--reference-run")o.referenceRun=true;
        else if(key==L"--reference-tip")o.referenceTip=true;
        else if(key==L"--reference-reset-check")o.referenceResetCheck=true;
        else if(key==L"--reference-kick"){auto t=value();for(auto&c:t)if(c==L',')c=L' ';std::wistringstream in(t);auto&v=o.referenceKick;if(!(in>>v[0]>>v[1]>>v[2])||!std::isfinite(v[0])||!std::isfinite(v[1])||!std::isfinite(v[2]))throw std::runtime_error("Invalid reference kick m/s");in>>std::ws;if(!in.eof())throw std::runtime_error("Reference kick suffix");}
        else if(key==L"--sim-steps"||key==L"--sim-capture-step"){o.simSteps=number(0,10000);o.simSpecified=true;}
        else if(key==L"--sim-initializer"){auto mode=value();if(mode!=L"full"&&mode!=L"guarded")throw std::runtime_error("Initializer must be full or guarded");o.simInitializer=mode==L"guarded"?3u:0u;}
        else if(key==L"--sweeps-per-step")o.sweepsPerStep=number(1,4096);
        else if(key==L"--sim-h"){auto s=value();size_t end{};o.simH=std::stof(s,&end);if(end!=s.size()||!std::isfinite(o.simH)||o.simH<=0)throw std::runtime_error("Invalid simulation h");}
        else if(key==L"--sim-unpin-position")o.windowPinX=false;
        else if(key==L"--sim-unpin-orientation")o.windowPinQ=false;
        else if(key==L"--sim-diagnostics")o.simDiagnostics=true;
        else if(key==L"--sim-reset-after")o.simReset=true;
        else if(key==L"--gravity"||key==L"--sim-velocity"){auto vectorText=value();for(auto& c:vectorText)if(c==L',')c=L' ';std::wistringstream in(vectorText);auto& v=key==L"--gravity"?o.gravity:o.simVelocity;if(!(in>>v[0]>>v[1]>>v[2])||!std::isfinite(v[0])||!std::isfinite(v[1])||!std::isfinite(v[2]))throw std::runtime_error("Expected finite x,y,z vector");in>>std::ws;if(!in.eof())throw std::runtime_error("Unexpected vector suffix");}
        else if(key==L"--window-target"){o.windowTarget=text(value());if(o.windowTarget!="rest"&&o.windowTarget!="displaced")throw std::runtime_error("Unknown target");}
        else if(key==L"--window-perturb"){o.windowPerturb=text(value());if(o.windowPerturb!="position"&&o.windowPerturb!="orientation"&&o.windowPerturb!="joint")throw std::runtime_error("Unknown window perturbation");}
        else if(key==L"--window-sweeps")o.windowSweeps=number(0,65536);
        else if(key==L"--window-h"){auto s=value();size_t end{};o.windowH=std::stof(s,&end);if(end!=s.size()||!std::isfinite(o.windowH)||o.windowH<=0)throw std::runtime_error("Invalid h");}
        else if(key==L"--window-unpin-position")o.windowPinX=false;
        else if(key==L"--window-unpin-orientation")o.windowPinQ=false;
        else if(key==L"--window-reset-after")o.windowReset=true;
        else if(key==L"--window-auto")o.windowAuto=true;
        else if(key==L"--orientation-perturb") {o.orientationPerturb=text(value());if(o.orientationPerturb!="rest"&&o.orientationPerturb!="bend"&&o.orientationPerturb!="twist"&&o.orientationPerturb!="smooth"&&o.orientationPerturb!="random")throw std::runtime_error("Unknown perturbation");}
        else if(key==L"--orientation-sweeps") o.orientationSweeps=number(0,4096);
        else if(key==L"--participating") {auto v=value();if(v!=L"1"&&v!=L"10"&&v!=L"100"&&v!=L"all")throw std::runtime_error("Participating must be 1/10/100/all");o.participating=v==L"all"?0:unsigned(std::stoul(v));}
        else if(key==L"--frame-stride") o.frameStride=number(1,32);
        else if(key==L"--unpin-root") o.pinRoot=false;
        else if(key==L"--material-frames") o.materialFrames=true;
        else if(key==L"--orientation-reset-after") o.resetAfter=true;
        else if(key==L"--orientation-auto") o.autoOrientation=true;
        else if(key==L"--scene") { auto v=value(); if(v!=L"hair"&&v!=L"bootstrap") throw std::runtime_error("Scene must be hair or bootstrap"); o.hair=v==L"hair"; }
        else if(key==L"--view") { o.view=text(value()); if(o.view!="initial"&&o.view!="front"&&o.view!="side"&&o.view!="back") throw std::runtime_error("View must be initial/front/side/back"); }
        else if(key==L"--draw") { o.draw=text(value()); if(o.draw!="guides"&&o.draw!="render"&&o.draw!="both"&&o.draw!="head"&&o.draw!="hulls") throw std::runtime_error("Draw must be guides/render/both/head/hulls"); }
        else if(key==L"--head") { auto v=value(); if(v!=L"selected"&&v!=L"original") throw std::runtime_error("Head must be selected or original"); o.originalHead=v==L"original"; }
        else throw std::runtime_error("Unknown option: "+text(key));
    }
    if(o.validate && !o.hair) throw std::runtime_error("--validate-rest requires --scene hair");
    if((windowOptions?1:0)+(orientationOptions?1:0)+(dynamicsOptions?1:0)+(referenceOptions?1:0)>1)throw std::runtime_error("Orientation and window execution are mutually exclusive");
    const std::string impliedMode=referenceOptions?"reference":dynamicsOptions?"dynamics":windowOptions?"window":orientationOptions?"orientation":"static";
    if(explicitMode&&impliedMode!="static"&&o.solverMode!=impliedMode)throw std::runtime_error("Solver options conflict with explicit --solver-mode");
    if(!explicitMode&&impliedMode!="static")o.solverMode=impliedMode;
    if(!o.hair&&(o.solverMode!="static"||o.materialFrames))throw std::runtime_error("Solver and material-frame options require --scene hair");
    if(!o.test.empty()&&(windowOptions||orientationOptions||dynamicsOptions||o.solverMode!="static"))throw std::runtime_error("--test owns solver setup; do not combine it with solver execution options");
    if((o.windowSweeps||!o.windowPerturb.empty()||o.windowAuto)&&o.windowTarget.empty())throw std::runtime_error("Explicit --window-target is required before relaxation");
    if(!o.report.empty() && !o.validate && o.test.empty()&&o.solverMode!="window"&&o.solverMode!="dynamics"&&o.solverMode!="reference") throw std::runtime_error("--report requires validation or window mode");
    if(!o.test.empty()) {if(!o.hair)throw std::runtime_error("Orientation tests require hair scene");if(!o.frames)o.frames=130;if(o.frames<120)throw std::runtime_error("Orientation tests require >=120 idle frames");if(o.report.empty())o.report=o.test.starts_with("dynamics-")?dynamicsOutput+o.test+".json":"artifacts/"+o.test+".json";}
    if((o.validate||!o.capture.empty()) && !o.frames) o.frames=120;
    if(o.validate && o.frames<120) throw std::runtime_error("Rest validation requires at least 120 frames");
    if(o.solverMode=="dynamics"&&o.report.empty()&&(o.simSpecified||o.validate))o.report=dynamicsOutput+"run.json";
    if(o.validate && o.report.empty()) o.report="artifacts/hair-rest.json";
    if(o.autoOrientation && !o.frames)throw std::runtime_error("CLI --orientation-auto requires bounded --frames");
    if(o.windowAuto&&!o.frames)throw std::runtime_error("CLI Auto Relax requires bounded --frames");
    if(o.simSpecified&&!o.frames)o.frames=1;
    if(o.solverMode=="dynamics"&&!o.report.empty()&&!o.frames)o.frames=1;
    if(o.solverMode=="reference"){
      if(o.referenceSteps&&o.referenceCaptureDir.empty())throw std::runtime_error("--reference-steps requires --reference-capture-dir");
      if(!o.referenceCaptureDir.empty()&&(!o.referenceSteps||o.referenceRun||o.frames||!o.capture.empty()))throw std::runtime_error("Reference sequence owns steps/completion; do not combine --frames/--capture/--reference-run");
      if(o.referenceSteps&&o.report.empty())o.report=o.referenceCaptureDir/"summary.json";
    }
    return o;
}
