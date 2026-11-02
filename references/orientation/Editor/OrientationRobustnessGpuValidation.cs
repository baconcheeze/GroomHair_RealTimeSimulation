using System;
using System.IO;
using System.Collections.Generic;
using System.Text;
using UnityEditor;
using UnityEngine;
using D=HairRest.Editor.OrientationLocalReference.D;
using Ref=HairRest.Editor.OrientationLocalReference;
namespace HairRest.Editor
{
    public static partial class OrientationRobustnessValidation
    {
        sealed class Problem
        {
            public string name;public D q,v,b,edge,left,right,rl,rr;public double l,kss,kl,kr;public bool physical;
            public double Energy(D value)=>Ref.PhysicalEnergy(value,edge,l,kss,left,right,rl,rr,kl,kr);
        }
        static Problem Physical(string name,D q,D edge,double ss,D left,D right,D rl,D rr,double kl,double kr)
        {
            // Match float GPU coefficients exactly for branch-reference comparisons.
            q=new D(q.Float).Unit;
            D b=kl*((left.Conj*q).Dot(rl)>=0?1:-1)*(left*rl)+kr*((q.Conj*right).Dot(rr)>=0?1:-1)*(right*rr.Conj);
            return new Problem{name=name,q=new D(q.Float),v=new D(((-2*ss)*edge).Float),b=new D(b.Float),edge=edge,l=edge.Norm,kss=ss*edge.Norm,left=left,right=right,rl=rl,rr=rr,kl=kl,kr=kr,physical=true};
        }
        public static void ValidateLocalBatch()
        {try{Directory.CreateDirectory(Output);File.WriteAllText(Output+"/cpu-math.txt",ValidateMath());File.WriteAllText(Output+"/local-gpu.txt",ValidateLocal());EditorApplication.Exit(0);}catch(Exception e){Debug.LogException(e);File.WriteAllText(Output+"/local-failure.txt",e.ToString());EditorApplication.Exit(1);}}
        static string ValidateLocal()
        {
            var cases=new List<Problem>();D identity=new D(0,0,0,1),flip=new D(1,0,0,0),edge=new D(0,0,.01,0);
            cases.Add(Physical("exact180",flip,edge,1,new D(),flip,identity,identity,0,.004));
            for(int mask=0;mask<16;mask++)
            {
                D q=new D(.3,.2,.1,.9).Unit,neighbor=new D(-.1,.4,.2,.8).Unit,rest=new D(.1,-.2,.3,.9).Unit;
                cases.Add(Physical("sign_combination_"+mask,((mask&1)==0?1:-1)*q,edge,1,
                    ((mask&2)==0?1:-1)*neighbor,((mask&4)==0?1:-1)*neighbor,
                    ((mask&8)==0?1:-1)*rest,rest,.004,.003));
            }
            foreach(double scale in new[]{1e-6,1d,1e6})
            foreach(double offset in new[]{-1e-2,-1e-4,-1e-6,-1e-8,0d,1e-8,1e-6,1e-4,1e-2})
            foreach(int sign in new[]{-1,1})
            {
                D perturbed=new D(1,offset*.23,offset*.31,offset).Unit;
                // Negated rest representation and current representations: same physical constraints.
                cases.Add(Physical($"near_{offset:R}_scale{scale:R}_sign{sign}",sign*perturbed,edge,scale,new D(),sign*perturbed,identity,sign*identity,0,.004*scale));
            }
            var random=new System.Random(71831);
            for(int i=0;i<300;i++)
            {
                double ss=i%3==0?100:i%3==1?.0001:1;
                D axis=RandomQ(random);D e=new D(axis.x,axis.y,axis.z,0);e=.01*e.Unit;
                cases.Add(Physical("random_"+i,RandomQ(random),e,ss,RandomQ(random),RandomQ(random),RandomQ(random),RandomQ(random),.004,.003));
            }
            cases.Add(new Problem{name="b_zero",q=flip,v=new D(0,0,-.02,0)});
            cases.Add(new Problem{name="alpha_zero",q=identity,b=new D(.1,.2,.3,.4)});
            cases.Add(new Problem{name="both_zero",q=flip});
            cases.Add(new Problem{name="minus_above_hard",q=flip,v=new D(0,0,-.02,0),b=new D(.06,0,0,0)});
            var inputs=new List<Vector4>();foreach(var c in cases){inputs.Add(c.q.Float);inputs.Add(c.v.Float);inputs.Add(c.b.Float);}
            Vector4[] gpu=Dispatch("RobustLocal",inputs.ToArray());
            double maxObjective=0,maxResidual=0,maxSpectral=0,maxNorm=0,maxPhysicalIncrease=0;int ordinary=0,fallback=0,hard=0;
            double signEnergy=double.NaN,maxSignDifference=0;var scaledEnergy=new Dictionary<string,double>();double maxScaleDifference=0;
            var csv=new StringBuilder("case,kind,energy_before,energy_after,reference_energy,normalized_objective_error,normalized_residual,normalized_spectral_violation,norm_error,coefficient_scale,raw_stationarity_residual,raw_spectral_violation\n");
            for(int i=0;i<cases.Count;i++)
            {
                var c=cases[i];D q=new D(gpu[i*4]),refQ=Ref.Solve(c.v,c.b,c.q);
                Require(gpu[i*4+1].x==1,"GPU local unresolved: "+c.name);
                int kind=(int)gpu[i*4+1].y;if(kind==0)ordinary++;else fallback++;if(kind==2)hard++;
                double scale=c.v.Norm+c.b.Norm;if(scale==0)scale=1;
                double err=Math.Abs(Ref.Objective(c.v,c.b,q.Unit)-Ref.Objective(c.v,c.b,refQ))/scale;
                double residual=Ref.Residual(c.v,c.b,q,out double spectral)/scale;spectral/=scale;
                double norm=Math.Abs(q.Norm-1);
                Require(err<4e-6&&residual<5e-6&&spectral<5e-6&&norm<5e-7,$"Local reference mismatch {c.name}: obj={err:R} residual={residual:R} spectral={spectral:R} norm={norm:R}");
                maxObjective=Math.Max(maxObjective,err);maxResidual=Math.Max(maxResidual,residual);maxSpectral=Math.Max(maxSpectral,spectral);maxNorm=Math.Max(maxNorm,norm);
                double before=c.physical?c.Energy(c.q.Unit):Ref.Objective(c.v,c.b,c.q.Unit),after=c.physical?c.Energy(q.Unit):Ref.Objective(c.v,c.b,q.Unit),reference=c.physical?c.Energy(refQ):Ref.Objective(c.v,c.b,refQ);
                maxPhysicalIncrease=Math.Max(maxPhysicalIncrease,(after-before)/scale);Require(after<=before+4e-6*scale,"Actual closest-pole energy increase "+c.name);
                if(c.name.StartsWith("sign_combination_"))
                {if(double.IsNaN(signEnergy))signEnergy=after;maxSignDifference=Math.Max(maxSignDifference,Math.Abs(after-signEnergy));Require(Math.Abs(after-signEnergy)<1e-8,"Physical sign equivalence");}
                if(c.name.StartsWith("near_"))
                {
                    string key=c.name.Substring(0,c.name.IndexOf("_scale"));double normalized=after/(c.kss/.01);
                    if(scaledEnergy.TryGetValue(key,out double value)){maxScaleDifference=Math.Max(maxScaleDifference,Math.Abs(normalized-value));Require(Math.Abs(normalized-value)<1e-8,"Common coefficient scaling preserves energy-normalized minimum");}
                    else scaledEnergy.Add(key,normalized);
                }
                csv.AppendLine(FormattableString.Invariant($"{c.name},{kind},{before:R},{after:R},{reference:R},{err:R},{residual:R},{spectral:R},{norm:R},{scale:R},{residual*scale:R},{spectral*scale:R}"));
                if(i==0)Require(Math.Abs(after-.0038)<1e-8&&after<.2*before,"Hard case must make genuine minimum-energy progress");
            }
            File.WriteAllText(Output+"/local-cases.csv",csv.ToString());
            // Actual production kernel, isolated imported +Z geometry; first local result is q[0].
            var data=Fixture();using var runtime=new GuideHairRuntime(data.Guide);
            using var solver=new GuideHairOrientationSolver(runtime,new GuideOrientationSettings{pinRootOrientation=false},Resources.Load<ComputeShader>("StableCosseratOrientation"));
            StateBuffer(runtime).SetData(new[]{Q(flip.Float),Q(flip.Float)});var initial=solver.Measure();
            Require(initial.MaxNormalizedStationarityResidual<1e-6&&initial.MaxNormalizedSpectralViolation>.1,"Zero residual alone must not certify 180 state");
            var beforeQ=solver.ReadOrientations();solver.Run(1);var diagnostics=solver.Measure(beforeQ);var actual=solver.ReadOrientations();
            double firstEnergy=ExactFixtureEnergy(actual[0]);Require(Math.Abs(firstEnergy-.0038)<1e-8,"Production first local hard minimum");
            Require(Math.Abs(diagnostics.MaxBatchUpdate-diagnostics.MaxLastSweepUpdate)<2e-6,"Run(1) update diagnostics agree: "+diagnostics);
            var log=new StringBuilder();log.AppendLine(FormattableString.Invariant($"PASS GPU local cases={cases.Count}; ordinary={ordinary}, fallback={fallback}, hard={hard}, unresolved=0; max normalized objective error={maxObjective:R}; residual={maxResidual:R}; spectral={maxSpectral:R}; norm={maxNorm:R}; physical energy increase={maxPhysicalIncrease:R}"));
            log.AppendLine($"Production isolated first local energy: {firstEnergy:R}; independent minimum=0.0038; initial diagnostics: {initial}; after sweep: {diagnostics}");
            // N-sweep metric verified with a reset replay split before its last sweep, no extra sweep.
            solver.Reset();StateBuffer(runtime).SetData(new[]{Q(flip.Float),Q(flip.Float)});var start=solver.ReadOrientations();solver.Run(128);var batch=solver.Measure(start);var finish=solver.ReadOrientations();
            solver.Reset();StateBuffer(runtime).SetData(new[]{Q(flip.Float),Q(flip.Float)});solver.Run(127);var preLast=solver.ReadOrientations();solver.Run(1);var last=solver.Measure(preLast);var replay=solver.ReadOrientations();
            for(int i=0;i<finish.Length;i++)Require(GuideHairFrames.AngleRadians(finish[i],replay[i])<1e-6,"Split replay same final state");
            Require(Math.Abs(batch.MaxLastSweepUpdate-last.MaxBatchUpdate)<2e-6,"Run(N) final-sweep metric matches independent capture");
            Require(batch.RegularUpdates+batch.FallbackUpdates==256,"Counter accumulation across dispatch chunks");
            log.AppendLine("128 batch: "+batch);log.AppendLine("Independent captured final sweep: "+last);
            solver.Reset();StateBuffer(runtime).SetData(new[]{Q(flip.Float),Q(flip.Float)});start=solver.ReadOrientations();solver.Run(2);var two=solver.Measure(start);finish=solver.ReadOrientations();
            solver.Reset();StateBuffer(runtime).SetData(new[]{Q(flip.Float),Q(flip.Float)});solver.Run(1);preLast=solver.ReadOrientations();solver.Run(1);last=solver.Measure(preLast);replay=solver.ReadOrientations();
            Require(two.MaxLastSweepUpdate>1e-4&&Math.Abs(two.MaxLastSweepUpdate-last.MaxBatchUpdate)<2e-6,"Nonzero final-sweep metric independently captured");
            for(int i=0;i<finish.Length;i++)Require(GuideHairFrames.AngleRadians(finish[i],replay[i])<1e-6,"Two-sweep replay exact state");
            log.AppendLine("Nonconverged Run(2): "+two);log.AppendLine("Independent sweep 2 capture: "+last);
            // Near-tie reporting uses a nonsmooth physical energy; do not call its residual unique.
            solver.Reset();StateBuffer(runtime).SetData(new[]{Quaternion.identity,Q(flip.Float)});
            Require(solver.Measure().NearPoleTies==2,"Both free incident near-tie terms counted");
            // Explicit failure visibility: overflowed coefficients are not replaced with an identity.
            solver.Settings.temporaryBendTwistStiffness=float.MaxValue;solver.Run(128);
            bool rejected=false;try{solver.Measure();}catch(InvalidOperationException e){rejected=e.Message.Contains("code=6");log.AppendLine("Expected isolated coefficient failure: "+e.Message);}
            Require(rejected,"Nonfinite coefficients surface unresolved failure");
            solver.Settings.temporaryBendTwistStiffness=1e-5f;solver.Run(1);
            rejected=false;try{solver.Measure();}catch(InvalidOperationException e){rejected=e.Message.Contains("code=6");}
            Require(rejected,"Later successful settings cannot clear earlier failure");
            solver.Reset();solver.Run(1);solver.Measure();
            log.AppendLine(FormattableString.Invariant($"PASS sign combinations energy difference={maxSignDifference:R}; common-scale energy difference={maxScaleDifference:R}; pole-tie count; failure latch/reset."));
            Debug.Log(log.ToString());return log.ToString();
        }
    }
}
