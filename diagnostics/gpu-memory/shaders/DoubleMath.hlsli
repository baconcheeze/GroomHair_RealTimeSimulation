// FP64 helpers: no sqrt/isfinite overload which might narrow to binary32.
bool DFinite(double x) { uint lo,hi;asuint(x,lo,hi);return (hi&0x7ff00000u)!=0x7ff00000u; }
double DSqrt(double input) {
    if(input==0.0)return input;
    uint lo,hi;asuint(input,lo,hi);
    if(!DFinite(input)){if(input>0.0)return input;return asdouble(0u,0x7ff80000u);}
    if(input<0.0)return asdouble(0u,0x7ff80000u);
    precise double x=input;
    precise double correction=1.0;
    if((hi&0x7ff00000u)==0u){x=x*asdouble(0u,2045u<<20);correction=asdouble(0u,512u<<20);asuint(x,lo,hi);}
    int exponent=int((hi>>20)&2047u)-1023;
    int halfExponent=exponent>>1;
    precise double scale=asdouble(0u,uint(halfExponent+1023)<<20);
    precise double m=(x/scale)/scale; // [1,4), every intermediate binary64 and finite.
    precise double y=1.5;
    [unroll]for(uint k=0;k<7;k++) y=0.5*(y+m/y);
    return (scale*y)*correction;
}
