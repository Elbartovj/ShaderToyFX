#include "ShaderDoc.h"
#include <cstdio>
#include <cmath>
using namespace stfx;
int main(int argc,char**argv){ int bad=0;
  for(int a=1;a<argc;a++){ ShaderDoc d; std::string e; if(!LoadShaderFile(argv[a],d,e)){printf("%s\n",e.c_str());return 1;}
    printf("== %s\n",argv[a]);
    for(auto&p:d.params){ float v0=UserParamValue(p,0), vm=UserParamValue(p,kUserParamDefaultNorm), v1=UserParamValue(p,1);
      bool ok=std::fabs(vm-p.def)<1e-5f && std::fabs(v0-p.minV)<1e-5f && std::fabs(v1-p.maxV)<1e-5f;
      bad+=!ok; printf("  %s %-16s slider 0 -> %g | défaut Resolume (0.5) -> %g (shader: %g) | 1 -> %g\n", ok?"OK ":"BAD", p.name.c_str(), v0, vm, p.def, v1); } }
  printf(bad?"%d ECHEC(S)\n":"TOUT OK\n",bad); return bad; }
