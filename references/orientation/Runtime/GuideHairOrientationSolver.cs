using System;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;

namespace HairRest
{
    public enum GuideOrientationCount { One=1, Ten=10, Hundred=100, All=0 }
    public enum GuideOrientationPerturbation { Bend, Twist, Smooth, Random }
    [Serializable]
    public sealed class GuideOrientationSettings
    {
        public GuideOrientationCount participatingStrandCount=GuideOrientationCount.One;
        public bool pinRootOrientation=true;
        [Range(1,4096)] public int solverIterations=32;
        public bool autoSolveOrientations=false;
        public bool perIterationDiagnostics=false;
        [Tooltip("TEMPORARY base tilde k_ss; segment energy coefficient = base * L0.")]
        public float temporaryShearStretchStiffness=1;
        [Tooltip("TEMPORARY base tilde k_bt; bend energy coefficient = 4 * base / left L0.")]
        public float temporaryBendTwistStiffness=1e-5f;
        [Range(0.1f,30)] public float randomDegrees=5;
        public int randomSeed=12345;
        public bool drawGuideMaterialFrames=false;
        [Range(1,10)] public int frameStrandLimit=1;
        [Range(1,32)] public int frameStride=4;
        [Range(.0005f,.02f)] public float axisLength=.004f;
    }
    public sealed class GuideOrientationDiagnostics
    {
        public double ShearStretchEnergy,BendTwistEnergy;
        public double TotalEnergy=>ShearStretchEnergy+BendTwistEnergy;
        public float MaxBatchUpdate=float.NaN,MaxLastSweepUpdate=float.NaN,MaxNorm,MaxRootError,MaxPosition,MaxVelocity;
        public double MaxOrientationStationarityResidual,MaxNormalizedStationarityResidual,MaxLocalSpectralViolation,MaxNormalizedSpectralViolation;
        public ulong RegularUpdates,FallbackUpdates,HardCases,RejectedCandidates,UnresolvedFailures;
        public int NearPoleTies;
        public int InvalidQuaternions;
        static string Available(float x)=>float.IsNaN(x)?"unavailable":x.ToString("R",System.Globalization.CultureInfo.InvariantCulture);
        public override string ToString()=>FormattableString.Invariant($"E_ss={ShearStretchEnergy:R}, E_bt={BendTwistEnergy:R}, E_total={TotalEnergy:R}; MaxBatchUpdate={Available(MaxBatchUpdate)} rad; MaxLastSweepUpdate={Available(MaxLastSweepUpdate)} rad; stationarity={MaxOrientationStationarityResidual:R} (normalized={MaxNormalizedStationarityResidual:R}); spectral violation={MaxLocalSpectralViolation:R} (normalized={MaxNormalizedSpectralViolation:R}); near pole ties={NearPoleTies}; regular/fallback/hard/rejected/failed={RegularUpdates}/{FallbackUpdates}/{HardCases}/{RejectedCandidates}/{UnresolvedFailures}; norm={MaxNorm:R}; root={MaxRootError:R} rad; position={MaxPosition:R} m; velocity={MaxVelocity:R} m/s; invalid={InvalidQuaternions}");
    }
    // Borrows physical resources. Owns only a shader instance, error status and frame material.
    public sealed class GuideHairOrientationSolver : IDisposable
    {
        readonly GuideHairRuntime guide;
        readonly ComputeShader shader;
        readonly int kernel;
        readonly GraphicsBuffer errors;
        [StructLayout(LayoutKind.Sequential)]
        struct SolveStats { public uint regular,fallback,hard,rejected,failures; public float lastSweep; }
        readonly GraphicsBuffer stats;
        bool hasRun;
        Material frames;
        readonly MaterialPropertyBlock frameProperties=new MaterialPropertyBlock();
        public GuideOrientationSettings Settings { get; }
        public GuideOrientationDiagnostics LastDiagnostics { get; private set; }
        public int Participating=>Settings.participatingStrandCount==GuideOrientationCount.All ? guide.Topology.StrandCount : Mathf.Min((int)Settings.participatingStrandCount,guide.Topology.StrandCount);
        public int DispatchGroups=>(Participating+63)/64;
        public GuideHairOrientationSolver(GuideHairRuntime runtime,GuideOrientationSettings settings,ComputeShader compute)
        {
            guide=runtime; Settings=settings;
            if(compute==null) throw new InvalidOperationException("StableCosseratOrientation compute asset is missing.");
            try
            {
                shader=UnityEngine.Object.Instantiate(compute); kernel=shader.FindKernel("SolveOrientation");
                errors=new GraphicsBuffer(GraphicsBuffer.Target.Structured,guide.Topology.StrandCount,4);
                errors.SetData(new uint[guide.Topology.StrandCount]);
                stats=new GraphicsBuffer(GraphicsBuffer.Target.Structured,guide.Topology.StrandCount,24);
                stats.SetData(new SolveStats[guide.Topology.StrandCount]);
            }
            catch { Dispose(); throw; }
        }
        void CheckSettings()
        {
            if(Participating<1 || !(Settings.temporaryShearStretchStiffness>0) || !(Settings.temporaryBendTwistStiffness>0) ||
                !Finite(Settings.temporaryShearStretchStiffness) || !Finite(Settings.temporaryBendTwistStiffness))
                throw new InvalidOperationException("Orientation stiffnesses must be finite and positive; select a nonempty strand prefix.");
        }
        public Quaternion[] ReadOrientations()=>GuideBuffer.Read<Quaternion>(guide.State.Orientation,guide.Topology.SegmentCount);
        public void Reset()
        {
            guide.State.Orientation.SetData(guide.Rest.Orientations);
            errors.SetData(new uint[guide.Topology.StrandCount]); LastDiagnostics=null;
            stats.SetData(new SolveStats[guide.Topology.StrandCount]);hasRun=false;
        }
        public void Perturb(GuideOrientationPerturbation mode)
        {
            CheckSettings(); Reset();
            var q=(Quaternion[])guide.Rest.Orientations.Clone(); var random=new System.Random(Settings.randomSeed);
            for(int strand=0;strand<Participating;strand++)
            {
                var r=guide.Topology.Ranges[strand]; int middle=(int)r.segmentCount/2;
                for(int j=0;j<r.segmentCount;j++)
                {
                    if(j==0 && Settings.pinRootOrientation) continue;
                    Vector3 axis=mode==GuideOrientationPerturbation.Twist?Vector3.forward:Vector3.right;
                    float angle=j==middle?20:0;
                    if(mode==GuideOrientationPerturbation.Smooth) angle=20*Mathf.Max(0,1-Mathf.Abs(j-middle)/4f);
                    if(mode==GuideOrientationPerturbation.Random)
                    {
                        axis=new Vector3((float)random.NextDouble()*2-1,(float)random.NextDouble()*2-1,(float)random.NextDouble()*2-1);
                        if(axis.sqrMagnitude<1e-12f) axis=Vector3.right;
                        angle=(float)(random.NextDouble()*2-1)*Settings.randomDegrees;
                    }
                    int s=(int)r.firstSegment+j; q[s]=(q[s]*Quaternion.AngleAxis(angle,axis.normalized)).normalized;
                }
            }
            guide.State.Orientation.SetData(q);
        }
        // GPU-resident path: no GetData, no position/velocity writes, no per-frame CPU solver.
        public void Run(int iterations)
        {
            CheckSettings(); if(iterations<1 || iterations>4096) throw new ArgumentOutOfRangeException(nameof(iterations));
            var t=guide.Topology;
            shader.SetInt("_Strands",t.StrandCount); shader.SetInt("_Vertices",t.VertexCount); shader.SetInt("_Segments",t.SegmentCount); shader.SetInt("_Bends",t.BendCount);
            shader.SetInt("_Participating",Participating); shader.SetInt("_PinRoot",Settings.pinRootOrientation?1:0);
            shader.SetFloat("_BaseSS",Settings.temporaryShearStretchStiffness); shader.SetFloat("_BaseBT",Settings.temporaryBendTwistStiffness);
            shader.SetBuffer(kernel,"_Ranges",t.Buffer); shader.SetBuffer(kernel,"_Position",guide.State.Position);
            shader.SetBuffer(kernel,"_RestLength",guide.Rest.Length); shader.SetBuffer(kernel,"_RestOrientation",guide.Rest.Orientation);
            // Zero-bend grooms never access this binding; reuse a valid float4 resource.
            shader.SetBuffer(kernel,"_RestBendTwist",guide.Rest.BendTwist??guide.Rest.Orientation);
            shader.SetBuffer(kernel,"_Orientation",guide.State.Orientation); shader.SetBuffer(kernel,"_Error",errors);
            shader.SetBuffer(kernel,"_Stats",stats);
            for(int remaining=iterations;remaining>0;remaining-=64)
            { shader.SetInt("_FirstChunk",remaining==iterations?1:0);shader.SetInt("_Iterations",Math.Min(remaining,64)); shader.Dispatch(kernel,DispatchGroups,1,1); }
            hasRun=true;
        }
        public void RunWithDiagnostics(int iterations)
        {
            if(Settings.perIterationDiagnostics && Participating!=1) throw new InvalidOperationException("Per-iteration diagnostics require one participating strand.");
            var before=ReadOrientations(); var initial=Measure(before); Debug.Log("Orientation before: "+initial);
            int steps=Settings.perIterationDiagnostics?iterations:1;
            for(int i=0;i<steps;i++)
            {
                Run(Settings.perIterationDiagnostics?1:iterations);
                var after=Measure(before); FlagEnergy(initial,after);
                Debug.Log($"Orientation after { (Settings.perIterationDiagnostics?i+1:iterations) } iterations: "+after);
                before=ReadOrientations(); initial=after;
            }
        }
        public static void FlagEnergy(GuideOrientationDiagnostics before,GuideOrientationDiagnostics after)
        {
            double increase=after.TotalEnergy-before.TotalEnergy;
            if(increase>Math.Max(1e-12,before.TotalEnergy*1e-4)) Debug.LogError($"ORIENTATION ENERGY INCREASE: {increase:R}");
            else if(increase>0) Debug.Log($"Orientation energy roundoff increase (unclamped): {increase:R}");
        }
        static bool Finite(float x)=>!float.IsNaN(x)&&!float.IsInfinity(x);
        static float Angle(Quaternion a,Quaternion b)=>a.Equals(b)||a.Equals(GuideHairFrames.Negate(b))?0:GuideHairFrames.AngleRadians(a,b);
        public GuideOrientationDiagnostics Measure(Quaternion[] before=null)
        {
            CheckSettings();
            var status=GuideBuffer.Read<uint>(errors,guide.Topology.StrandCount);
            for(int s=0;s<status.Length;s++) if(status[s]!=0)
            {
                var failed=GuideBuffer.Read<SolveStats>(stats,guide.Topology.StrandCount)[s];
                throw new InvalidOperationException($"Orientation GPU safety failure strand={s}, code={status[s]} (1 topology, 2 quaternion, 3 length/position, 4 bend quaternion, 5 update, 6 unresolved local solve). Run counters regular/fallback/hard/rejected/failed={failed.regular}/{failed.fallback}/{failed.hard}/{failed.rejected}/{failed.failures}. Failure is latched until Reset.");
            }
            var q=ReadOrientations(); var x=GuideBuffer.Read<Vector3>(guide.State.Position,guide.Topology.VertexCount);
            var velocity=GuideBuffer.Read<Vector3>(guide.State.Velocity,guide.Topology.VertexCount);
            var d=new GuideOrientationDiagnostics();
            if(before!=null)d.MaxBatchUpdate=0;
            if(hasRun)
            {
                d.MaxLastSweepUpdate=0;var counters=GuideBuffer.Read<SolveStats>(stats,guide.Topology.StrandCount);
                for(int i=0;i<Participating;i++)
                {var c=counters[i];d.RegularUpdates+=c.regular;d.FallbackUpdates+=c.fallback;d.HardCases+=c.hard;d.RejectedCandidates+=c.rejected;d.UnresolvedFailures+=c.failures;d.MaxLastSweepUpdate=Mathf.Max(d.MaxLastSweepUpdate,(float)(4*Math.Asin(Math.Min(1,c.lastSweep*.5))));}
            }
            for(int v=0;v<x.Length;v++)
            {
                if(!x[v].Equals(guide.Rest.Positions[v]) || !velocity[v].Equals(Vector3.zero)) throw new InvalidOperationException("Fixed position/zero velocity invariant violated.");
                d.MaxPosition=Mathf.Max(d.MaxPosition,Vector3.Distance(x[v],guide.Rest.Positions[v])); d.MaxVelocity=Mathf.Max(d.MaxVelocity,velocity[v].magnitude);
            }
            for(int strand=0;strand<guide.Topology.StrandCount;strand++)
            {
                var r=guide.Topology.Ranges[strand];
                for(int j=0;j<r.segmentCount;j++)
                {
                    int s=(int)r.firstSegment+j,v=(int)r.firstVertex+j;
                    float norm=Quaternion.Dot(q[s],q[s]);
                    if(!Finite(norm)||norm<1e-20f) { ++d.InvalidQuaternions; continue; }
                    d.MaxNorm=Mathf.Max(d.MaxNorm,Mathf.Abs(Mathf.Sqrt(norm)-1));
                    if(strand>=Participating) { if(!q[s].Equals(guide.Rest.Orientations[s])) throw new InvalidOperationException("Nonparticipating orientation changed."); continue; }
                    if(before!=null) d.MaxBatchUpdate=Mathf.Max(d.MaxBatchUpdate,Angle(before[s],q[s]));
                    if(j==0 && Settings.pinRootOrientation) d.MaxRootError=Mathf.Max(d.MaxRootError,Angle(guide.Rest.Orientations[s],q[s]));
                    float l=guide.Rest.Lengths[s]; Vector3 css=(x[v+1]-x[v])/l-q[s]*Vector3.forward;
                    d.ShearStretchEnergy+=0.5*Settings.temporaryShearStretchStiffness*l*css.sqrMagnitude;
                    if(!(j==0&&Settings.pinRootOrientation)) MeasureStationarity(d,q,x,r,j);
                    if(j+1<r.segmentCount)
                    {
                        Quaternion rel=Quaternion.Inverse(q[s])*q[s+1],rest=guide.Rest.Bends[(int)r.firstBend+j];
                        float phi=Quaternion.Dot(rel,rest)>=0?1:-1;
                        double a=rel.x-phi*rest.x,b=rel.y-phi*rest.y,c=rel.z-phi*rest.z,e=rel.w-phi*rest.w;
                        d.BendTwistEnergy+=0.5*(4*Settings.temporaryBendTwistStiffness/l)*(a*a+b*b+c*c+e*e);
                    }
                }
            }
            if(d.InvalidQuaternions>0||d.MaxNorm>2e-5f||d.MaxRootError>1e-6f) throw new InvalidOperationException("Orientation safety validation failed: "+d);
            LastDiagnostics=d; return d;
        }
        // Explicit diagnostics only. Rebuild neighbors/poles from the FINAL snapshot, never GS cached b.
        // Four-dimensional constrained quaternion gradient, NOT a derived 3D physical torque.
        void MeasureStationarity(GuideOrientationDiagnostics d,Quaternion[] q,Vector3[] x,GuideStrandRange r,int j)
        {
            int s=(int)r.firstSegment+j,v=(int)r.firstVertex+j;
            Vector3 coeff=-2*Settings.temporaryShearStretchStiffness*(x[v+1]-x[v]);
            Quaternion current=q[s].normalized;Vector4 b=Vector4.zero;
            if(j>0)AddNeighbor(q[s-1],guide.Rest.Bends[(int)r.firstBend+j-1],4*Settings.temporaryBendTwistStiffness/guide.Rest.Lengths[s-1],false);
            if(j+1<r.segmentCount)AddNeighbor(q[s+1],guide.Rest.Bends[(int)r.firstBend+j],4*Settings.temporaryBendTwistStiffness/guide.Rest.Lengths[s],true);
            Quaternion aq=new Quaternion(coeff.x,coeff.y,coeff.z,0)*current*new Quaternion(0,0,1,0);
            Vector4 c=ToVector(current),sum=ToVector(aq)+b;double lambda=Vector4.Dot(c,sum),scale=coeff.magnitude+b.magnitude;
            double residual=(sum-(float)lambda*c).magnitude,violation=Math.Max(0,coeff.magnitude-lambda);
            d.MaxOrientationStationarityResidual=Math.Max(d.MaxOrientationStationarityResidual,residual);
            d.MaxLocalSpectralViolation=Math.Max(d.MaxLocalSpectralViolation,violation);
            d.MaxNormalizedStationarityResidual=Math.Max(d.MaxNormalizedStationarityResidual,scale==0?0:residual/scale);
            d.MaxNormalizedSpectralViolation=Math.Max(d.MaxNormalizedSpectralViolation,scale==0?0:violation/scale);
            void AddNeighbor(Quaternion neighbor,Quaternion rest,float weight,bool right)
            {
                Quaternion rel=right?Quaternion.Inverse(current)*neighbor:Quaternion.Inverse(neighbor)*current;
                float dot=Quaternion.Dot(rel,rest);if(Mathf.Abs(dot)<=1e-6f) d.NearPoleTies++;
                Quaternion target=right?neighbor*Quaternion.Inverse(rest):neighbor*rest;
                b+=weight*(dot>=0?1:-1)*ToVector(target);
            }
        }
        static Vector4 ToVector(Quaternion q)=>new Vector4(q.x,q.y,q.z,q.w);
        public void DrawFrames(Matrix4x4 localToWorld,Shader frameShader)
        {
            if(!Settings.drawGuideMaterialFrames || frameShader==null) return;
            if(frames==null) frames=new Material(frameShader){hideFlags=HideFlags.HideAndDontSave};
            int strands=Math.Min(Participating,Math.Max(1,Settings.frameStrandLimit));
            var last=guide.Topology.Ranges[strands-1]; int count=(int)(last.firstSegment+last.segmentCount),stride=Math.Max(1,Settings.frameStride);
            frameProperties.SetBuffer("_Position",guide.State.Position); frameProperties.SetBuffer("_Orientation",guide.State.Orientation);
            frameProperties.SetBuffer("_SegmentStarts",guide.Topology.DebugSegmentStarts); frameProperties.SetInt("_FrameStride",stride);
            frameProperties.SetFloat("_AxisLength",Settings.axisLength); frameProperties.SetMatrix("_HairLocalToWorld",localToWorld);
            var bounds=new Bounds(localToWorld.MultiplyPoint3x4(guide.Rest.Positions[0]),Vector3.one*10);
            Graphics.DrawProcedural(frames,bounds,MeshTopology.Lines,((count+stride-1)/stride)*6,1,null,frameProperties,ShadowCastingMode.Off,false);
        }
        public void Dispose()
        {
            errors?.Dispose();
            stats?.Dispose();
            if(shader!=null) { if(Application.isPlaying) UnityEngine.Object.Destroy(shader); else UnityEngine.Object.DestroyImmediate(shader); }
            if(frames!=null) { if(Application.isPlaying) UnityEngine.Object.Destroy(frames); else UnityEngine.Object.DestroyImmediate(frames); }
        }
    }
}
