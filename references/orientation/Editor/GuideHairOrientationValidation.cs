using System;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace HairRest.Editor
{
    public static class GuideHairOrientationValidation
    {
        const string Output="Artifacts/OrientationRobustness/Regression";
        static HairRestPreview preview;
        static GuideHairOrientationSolver solver;
        static StreamWriter trace;
        static readonly StringBuilder summary=new StringBuilder();
        static int phase,startFrame;
        static byte[] originalImage;
        static Quaternion[] originalQ;
        public static void ValidateBatch()
        {
            try
            {
                Directory.CreateDirectory(Output);
                EditorSceneManager.OpenScene(HairRestEditor.ScenePath);
                preview=UnityEngine.Object.FindFirstObjectByType<HairRestPreview>(); preview.Initialize();
                Require(!preview.OrientationSettings.autoSolveOrientations,"Auto Solve defaults off");
                preview.SetMaterialFrameShader(AssetDatabase.LoadAssetAtPath<Shader>("Assets/HairRest/Shaders/GuideMaterialFrames.shader"));
                solver=preview.OrientationSolver; originalQ=solver.ReadOrientations();
                trace=new StreamWriter(Output+"/iterations.csv"); trace.WriteLine("test,strands,iteration,E_ss,E_bt,E_total,max_batch_update_rad,max_last_sweep_update_rad,stationarity,normalized_stationarity,spectral_violation,normalized_spectral_violation,near_pole_ties,regular,fallback,hard,rejected,failed,max_norm,max_root_rad,max_position,max_velocity,invalid");
                phase=0; summary.Clear();
                EditorSettings.enterPlayModeOptionsEnabled=true;
                EditorSettings.enterPlayModeOptions=EnterPlayModeOptions.DisableDomainReload|EnterPlayModeOptions.DisableSceneReload;
                EditorApplication.isPlaying=true; EditorApplication.update+=Tick;
            }
            catch(Exception e) { Fail(e); }
        }
        static void Tick()
        {
            try
            {
                if(!Application.isPlaying || Time.frameCount<10) return;
                if(phase==0) { solver=preview.OrientationSolver; originalQ=solver.ReadOrientations(); originalImage=Capture("before");startFrame=Time.frameCount;phase=1;return; }
                if(phase==1)
                {
                    if(Time.frameCount<startFrame+130) return;
                    var still=solver.ReadOrientations();
                    for(int i=0;i<still.Length;i++) Require(still[i].Equals(originalQ[i]),"No automatic orientation writes on Play entry");
                    Require(Equal(originalImage,Capture("after-130-idle-frames")),"Idle scene pixel invariance");
                    RestNoOp();
                    foreach(var mode in new[]{GuideOrientationPerturbation.Bend,GuideOrientationPerturbation.Twist,GuideOrientationPerturbation.Smooth,GuideOrientationPerturbation.Random}) Single(mode);
                    foreach(var count in new[]{GuideOrientationCount.Ten,GuideOrientationCount.Hundred,GuideOrientationCount.All}) Scale(count);
                    SafetyFixtures();
                    solver.Reset(); preview.ValidateGpuPositionsEqualRest();
                    solver.Settings.participatingStrandCount=GuideOrientationCount.One;
                    solver.Perturb(GuideOrientationPerturbation.Bend);
                    phase=2; startFrame=Time.frameCount;return;
                }
                if(Time.frameCount<startFrame+5) return;
                if(phase==2)
                {
                    Require(Equal(originalImage,Capture("perturbed-lines-unchanged")),"Perturbation leaves lines/head pixel-identical");
                    solver.Settings.drawGuideMaterialFrames=true; solver.Settings.frameStride=1;
                    phase=3;startFrame=Time.frameCount;return;
                }
                if(phase==3)
                {
                    var frames=Capture("frames-perturbed"); Require(!Equal(originalImage,frames),"Material axes visible");
                    solver.Run(512); phase=4;startFrame=Time.frameCount;return;
                }
                if(phase==4)
                {
                    Capture("frames-relaxed"); solver.Settings.drawGuideMaterialFrames=false; solver.Reset();
                    phase=5;startFrame=Time.frameCount;return;
                }
                Require(Equal(originalImage,Capture("after-tests")),"Final scene pixel invariance");
                preview.ValidateGpuPositionsEqualRest();
                summary.AppendLine("PASS: 130+ idle frames, no automatic dispatch, all fixed positions/zero velocities, exact inactive orientations, immutable rest buffers, pixel-identical line/head output; material-frame overlay captured.");
                File.WriteAllText(Output+"/summary.txt",summary.ToString());
                Debug.Log(summary.ToString()); Cleanup();EditorApplication.Exit(0);
            }
            catch(Exception e){Fail(e);}
        }
        static void RestNoOp()
        {
            solver.Reset(); var before=solver.ReadOrientations();
            solver.Run(1); var one=solver.Measure(before); Record("rest",1,one);
            Require(one.MaxBatchUpdate<1e-5f,"Rest no-op 1 iteration");
            var beforeTen=solver.ReadOrientations(); solver.Run(10); var ten=solver.Measure(beforeTen); Record("rest",11,ten);
            Require(ten.MaxBatchUpdate<1e-5f,"Rest no-op 10 further iterations");
            summary.AppendLine("Rest no-op 1: "+one);summary.AppendLine("Rest no-op +10: "+ten);
        }
        static void Single(GuideOrientationPerturbation mode)
        {
            solver.Perturb(mode);
            var initial=solver.Measure(); Record(mode.ToString(),0,initial); var previous=initial;
            var cpu=solver.ReadOrientations(); CpuReferenceSweep(cpu);
            float maxCpuError=0;
            int last=0; GuideOrientationDiagnostics d=initial;
            for(int iteration=1;iteration<=2048;iteration++)
            {
                var before=solver.ReadOrientations(); solver.Run(1); d=solver.Measure(before);
                if(iteration==1)
                {
                    var actual=solver.ReadOrientations(); int n=preview.Data.Guide.GetStrandVertexCount(0)-1;
                    for(int s=0;s<n;s++) maxCpuError=Mathf.Max(maxCpuError,GuideHairFrames.AngleRadians(cpu[s],actual[s]));
                    Require(maxCpuError<2e-5f,"GPU/reference local update mapping");
                }
                double rise=d.TotalEnergy-previous.TotalEnergy;
                Require(rise<=Math.Max(1e-12,previous.TotalEnergy*1e-4),$"ENERGY INCREASE {mode} iteration {iteration}: {rise:R}");
                if(rise>0) Debug.Log($"Measured roundoff energy rise {mode} iteration {iteration}: {rise:R}");
                Record(mode.ToString(),iteration,d);
                if(iteration==1||iteration==2||iteration==4||iteration==8||iteration==16||iteration==32)
                    summary.AppendLine($"{mode} {iteration}: "+d);
                previous=d;last=iteration;
                if(iteration>=32 && d.MaxLastSweepUpdate<2e-6f && d.MaxNormalizedStationarityResidual<1e-5 && d.MaxNormalizedSpectralViolation<1e-5 && d.TotalEnergy<initial.TotalEnergy*.001) break;
            }
            Require(d.TotalEnergy<initial.TotalEnergy*.01,$"{mode} energy substantially decreased");
            Require(d.MaxLastSweepUpdate<1e-5f && d.MaxNormalizedStationarityResidual<1e-5 && d.MaxNormalizedSpectralViolation<1e-5,$"{mode} update converged");
            summary.AppendLine($"{mode} final iteration={last}, initial energy={initial.TotalEnergy:R}, CPU/GPU angular error={maxCpuError:R}: "+d);
            if(mode==GuideOrientationPerturbation.Random)
            {
                solver.Perturb(mode);var a=solver.ReadOrientations(); solver.Perturb(mode);var b=solver.ReadOrientations();
                for(int i=0;i<a.Length;i++) Require(a[i].Equals(b[i]),"Deterministic random perturbation");
            }
        }
        static void Scale(GuideOrientationCount count)
        {
            solver.Settings.participatingStrandCount=count; solver.Perturb(GuideOrientationPerturbation.Random);
            var initial=solver.Measure(); var before=solver.ReadOrientations();Record("scale",0,initial);
            solver.Run(128);var final=solver.Measure(before);Record("scale",128,final);
            Require(final.TotalEnergy<initial.TotalEnergy*.05,"Scale energy decreased");
            summary.AppendLine($"Scale {solver.Participating}, groups={solver.DispatchGroups}, 128 iterations, initial={initial.TotalEnergy:R}: "+final);
            preview.ValidateGpuPositionsEqualRest();
        }
        static void Record(string test,int iteration,GuideOrientationDiagnostics d)
        {
            trace.WriteLine(FormattableString.Invariant($"{test},{solver.Participating},{iteration},{d.ShearStretchEnergy:R},{d.BendTwistEnergy:R},{d.TotalEnergy:R},{d.MaxBatchUpdate:R},{d.MaxLastSweepUpdate:R},{d.MaxOrientationStationarityResidual:R},{d.MaxNormalizedStationarityResidual:R},{d.MaxLocalSpectralViolation:R},{d.MaxNormalizedSpectralViolation:R},{d.NearPoleTies},{d.RegularUpdates},{d.FallbackUpdates},{d.HardCases},{d.RejectedCandidates},{d.UnresolvedFailures},{d.MaxNorm:R},{d.MaxRootError:R},{d.MaxPosition:R},{d.MaxVelocity:R},{d.InvalidQuaternions}"));trace.Flush();
        }
        static void SafetyFixtures()
        {
            using var stream=new MemoryStream(); using var writer=new BinaryWriter(stream);
            foreach(uint n in new uint[]{0x5452484a,1,1,3,1,0,3})writer.Write(n);
            foreach(var p in new[]{Vector3.zero,Vector3.forward*.01f,Vector3.forward*.02f}){writer.Write(p.x);writer.Write(p.y);writer.Write(p.z);}writer.Write(0);
            var data=HairRestData.Load(stream.ToArray());
            using var runtime=new GuideHairRuntime(data.Guide);
            using var test=new GuideHairOrientationSolver(runtime,new GuideOrientationSettings(),Resources.Load<ComputeShader>("StableCosseratOrientation"));
            var flags=System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic;
            var orientation=(GraphicsBuffer)typeof(GuideHairDynamicState).GetField("Orientation",flags).GetValue(runtime.State);
            foreach(var invalid in new[]{new Quaternion(0,0,0,0),new Quaternion(float.NaN,0,0,1),new Quaternion(float.PositiveInfinity,0,0,1)})
            {
                test.Reset();orientation.SetData(new[]{invalid},0,1,1);test.Run(1);
                bool rejected=false;try{test.Measure();}catch(InvalidOperationException e){rejected=e.Message.Contains("code=2");}
                Require(rejected,"GPU rejects zero/NaN/Inf quaternion");
            }
            // Corrupt only an isolated fixture's topology, never the scene/rest resources.
            var ranges=(GraphicsBuffer)typeof(GuideHairTopology).GetProperty("Buffer",flags).GetValue(runtime.Topology);
            var original=new GuideStrandRange[1];ranges.GetData(original);
            for(int kind=0;kind<3;kind++)
            {
                test.Reset();var bad=original[0];
                if(kind==0)bad.firstVertex=uint.MaxValue;else if(kind==1)bad.firstSegment=uint.MaxValue;else bad.firstBend=uint.MaxValue;
                ranges.SetData(new[]{bad});test.Run(1);bool rejected=false;
                try{test.Measure();}catch(InvalidOperationException e){rejected=e.Message.Contains("code=1");}
                Require(rejected,"GPU rejects vertex/segment/bend range overflow");ranges.SetData(original);
            }
            test.Reset();test.Settings.pinRootOrientation=false;test.Perturb(GuideOrientationPerturbation.Random);test.Run(64);test.Measure();
            summary.AppendLine("PASS isolated GPU safety fixtures: zero/NaN/Inf quaternions, vertex/segment/bend range overflow; unpinned finite solve.");
            using var twoStream=new MemoryStream();using var twoWriter=new BinaryWriter(twoStream);
            foreach(uint n in new uint[]{0x5452484a,1,1,2,1,0,2})twoWriter.Write(n);
            foreach(var p in new[]{Vector3.zero,Vector3.forward*.01f}){twoWriter.Write(p.x);twoWriter.Write(p.y);twoWriter.Write(p.z);}twoWriter.Write(0);
            using var twoRuntime=new GuideHairRuntime(HairRestData.Load(twoStream.ToArray()).Guide);
            using var twoSolver=new GuideHairOrientationSolver(twoRuntime,new GuideOrientationSettings{pinRootOrientation=false},Resources.Load<ComputeShader>("StableCosseratOrientation"));
            twoSolver.Perturb(GuideOrientationPerturbation.Bend);twoSolver.Run(1);
            Require(twoSolver.Measure().TotalEnergy<1e-12,"Two-vertex SS-only solve without bend buffer");
            summary.AppendLine("PASS two-vertex unpinned strand: exact SS-only minimum, zero bend buffer.");
        }
        // Double precision transcription of official CPU lambda.cpp exact local update.
        // Separate from HLSL; compares first GS sweep, neighbor ordering, poles, xyzw and +Z.
        struct Q
        {
            public double x,y,z,w;
            public Q(double x,double y,double z,double w){this.x=x;this.y=y;this.z=z;this.w=w;}
            public Q(Quaternion q):this(q.x,q.y,q.z,q.w){}
            public double Norm=>Math.Sqrt(x*x+y*y+z*z+w*w);
            public Q Conj=>new Q(-x,-y,-z,w);
            public static Q operator+(Q a,Q b)=>new Q(a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w);
            public static Q operator*(double s,Q q)=>new Q(s*q.x,s*q.y,s*q.z,s*q.w);
            public static Q operator*(Q a,Q b)=>new Q(a.w*b.x+b.w*a.x+a.y*b.z-a.z*b.y,a.w*b.y+b.w*a.y+a.z*b.x-a.x*b.z,a.w*b.z+b.w*a.z+a.x*b.y-a.y*b.x,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z);
            public double Dot(Q b)=>x*b.x+y*b.y+z*b.z+w*b.w;
            public Quaternion Unity=>new Quaternion((float)x,(float)y,(float)z,(float)w);
        }
        static void CpuReferenceSweep(Quaternion[] orientations)
        {
            var data=preview.Data.Guide; int first=data.GetStrandStart(0),segments=data.GetStrandVertexCount(0)-1;
            var q=new Q[segments]; var rest=new Quaternion[segments]; var length=new double[segments];
            for(int j=0;j<segments;j++)
            {
                q[j]=new Q(orientations[j]); Vector3 edge=data.GetRestPosition(first+j+1)-data.GetRestPosition(first+j);
                length[j]=edge.magnitude; rest[j]=j==0?GuideHairFrames.First(edge.normalized):GuideHairFrames.Transport(rest[j-1],edge.normalized);
            }
            for(int j=1;j<segments;j++)
            {
                Vector3 edge=data.GetRestPosition(first+j+1)-data.GetRestPosition(first+j);
                Q v=new Q(-2*solver.Settings.temporaryShearStretchStiffness*edge.x,-2*solver.Settings.temporaryShearStretchStiffness*edge.y,-2*solver.Settings.temporaryShearStretchStiffness*edge.z,0),b=new Q();
                Q target=new Q(GuideHairFrames.BendTwist(rest[j-1],rest[j]));
                double phi=(q[j-1].Conj*q[j]).Dot(target)>=0?1:-1;
                b=b+(4*solver.Settings.temporaryBendTwistStiffness/length[j-1]*phi)*(q[j-1]*target);
                if(j+1<segments)
                {
                    target=new Q(GuideHairFrames.BendTwist(rest[j],rest[j+1]));phi=(q[j].Conj*q[j+1]).Dot(target)>=0?1:-1;
                    b=b+(4*solver.Settings.temporaryBendTwistStiffness/length[j]*phi)*(q[j+1]*target.Conj);
                }
                Q a=v*b*new Q(0,0,1,0);double lambda=v.Norm+b.Norm;
                for(int i=0;i<64;i++)lambda=Math.Sqrt((a+lambda*b).Norm+v.Norm*v.Norm);
                Q next=a+lambda*b;q[j]=(1/next.Norm)*next; orientations[j]=q[j].Unity;
            }
        }
        static byte[] Capture(string name)
        {
            var rt=new RenderTexture(1024,1024,24);var image=new Texture2D(1024,1024,TextureFormat.RGB24,false);var old=RenderTexture.active;
            try { rt.Create();RenderPipeline.SubmitRenderRequest(Camera.main,new UniversalRenderPipeline.SingleCameraRequest{destination=rt});RenderTexture.active=rt;image.ReadPixels(new Rect(0,0,1024,1024),0,0);image.Apply();var bytes=image.EncodeToPNG();File.WriteAllBytes(Output+"/"+name+".png",bytes);return bytes; }
            finally{RenderTexture.active=old;rt.Release();UnityEngine.Object.DestroyImmediate(rt);UnityEngine.Object.DestroyImmediate(image);}
        }
        static bool Equal(byte[] a,byte[] b){if(a.Length!=b.Length)return false;for(int i=0;i<a.Length;i++)if(a[i]!=b[i])return false;return true;}
        static void Require(bool pass,string message){if(!pass)throw new InvalidOperationException(message);}
        static void Cleanup(){EditorApplication.update-=Tick;trace?.Dispose();trace=null;}
        static void Fail(Exception e){Debug.LogException(e);Directory.CreateDirectory(Output);File.WriteAllText(Output+"/summary.txt",summary+"\nFAILED: "+e);Cleanup();EditorApplication.Exit(1);}
    }
}
