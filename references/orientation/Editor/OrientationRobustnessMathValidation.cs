using System;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;
using D=HairRest.Editor.OrientationLocalReference.D;
using Ref=HairRest.Editor.OrientationLocalReference;
namespace HairRest.Editor
{
    public static partial class OrientationRobustnessValidation
    {
        static D RandomQ(System.Random r)=>new D(r.NextDouble()*2-1,r.NextDouble()*2-1,r.NextDouble()*2-1,r.NextDouble()*2-1).Unit;
        public static void ValidateMathBatch()
        {try{Directory.CreateDirectory(Output);File.WriteAllText(Output+"/cpu-math.txt",ValidateMath());EditorApplication.Exit(0);}catch(Exception e){Debug.LogException(e);EditorApplication.Exit(1);}}
        static string ValidateMath()
        {
            var random=new System.Random(3901);double fdMax=0,identityMax=0,referenceMax=0,spectralMax=0,projectorMax=0,energyMax=0;
            int checkedDirections=0;
            for(int n=0;n<128;n++)
            {
                D q=RandomQ(random),other=RandomQ(random),left=RandomQ(random),right=RandomQ(random),rl=RandomQ(random),rr=RandomQ(random);
                D edge=new D(.003,.004,.01,0);double l=edge.Norm,kss=.01,kl=.004,kr=.007;
                D v=(-2*kss/l)*edge;
                D b=kl*((left.Conj*q).Dot(rl)>=0?1:-1)*(left*rl)+kr*((q.Conj*right).Dot(rr)>=0?1:-1)*(right*rr.Conj);
                double alpha=v.Norm;
                identityMax=Math.Max(identityMax,Math.Abs(q.Dot(Ref.Apply(v,other))-Ref.Apply(v,q).Dot(other)));
                identityMax=Math.Max(identityMax,(Ref.Apply(v,Ref.Apply(v,q))-alpha*alpha*q).Norm);
                D plus=.5*(q+Ref.Apply(v,q)/alpha),minus=.5*(q-Ref.Apply(v,q)/alpha);
                projectorMax=Math.Max(projectorMax,Math.Abs(plus.Dot(minus)));
                projectorMax=Math.Max(projectorMax,(Ref.Apply(v,plus)-alpha*plus).Norm);
                projectorMax=Math.Max(projectorMax,(Ref.Apply(v,minus)+alpha*minus).Norm);
                D candidate=Ref.Solve(v,b,q);double res=Ref.Residual(v,b,candidate,out double violation);
                referenceMax=Math.Max(referenceMax,res/(alpha+b.Norm));spectralMax=Math.Max(spectralMax,violation/(alpha+b.Norm));
                Require(Ref.Objective(v,b,candidate)<=Ref.Objective(v,b,q)+1e-12,"Reference minimum energy");
                // Freeze poles by staying away from their boundaries; compare actual energy slopes.
                if(Math.Abs((left.Conj*q).Dot(rl))<.05||Math.Abs((q.Conj*right).Dot(rr))<.05)continue;
                D dir=(other-q.Dot(other)*q).Unit;
                D s=Ref.Apply(v,q)+b,residual=s-q.Dot(s)*q;
                foreach(double h in new[]{1e-3,1e-4,1e-5,1e-6})
                {
                    D qp=(q+h*dir).Unit,qm=(q-h*dir).Unit;
                    double ep=Ref.PhysicalEnergy(qp,edge,l,kss,left,right,rl,rr,kl,kr),em=Ref.PhysicalEnergy(qm,edge,l,kss,left,right,rl,rr,kl,kr);
                    double err=Math.Abs((ep-em)/(2*h)+residual.Dot(dir))/(alpha+b.Norm);
                    fdMax=Math.Max(fdMax,err);Require(err<3e-6,"Directional derivative sign/factor");checkedDirections++;
                    energyMax=Math.Max(energyMax,Math.Abs((ep-em)-(Ref.Objective(v,b,qp)-Ref.Objective(v,b,qm))));
                }
            }
            D hardV=new D(0,0,-.02,0),hardB=new D(.004,0,0,0),old=new D(1,0,0,0),hard=Ref.Solve(hardV,hardB,old);
            double hardEnergy=Ref.PhysicalEnergy(hard,new D(0,0,.01,0),.01,.01,new D(),old,new D(),new D(0,0,0,1),0,.004);
            Require(Math.Abs(hardEnergy-.0038)<1e-12,"Independent dense reference hard minimum");
            Ref.Residual(hardV,hardB,old,out double oldViolation);Require(oldViolation>.035,"Stationary maximum must fail spectral certificate");
            Require(identityMax<1e-12&&projectorMax<1e-12&&referenceMax<1e-10&&spectralMax<1e-10,"CPU operator/reference identities");
            return FormattableString.Invariant($"PASS CPU-only double: 128 problems, derivative samples={checkedDirections}, max normalized FD error={fdMax:R}, objective/constraint energy difference={energyMax:R}, symmetry/A^2 error={identityMax:R}, projector error={projectorMax:R}, reference residual={referenceMax:R}, spectral violation={spectralMax:R}, hard energy={hardEnergy:R}, initial hard spectral violation={oldViolation:R}\n");
        }
    }
}
