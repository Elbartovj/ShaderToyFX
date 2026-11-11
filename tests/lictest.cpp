#include "License.h"
#include <cstdio>
using namespace stfx;
int main(){
  struct T{const char* txt; bool st; LicenseKind want;} t[]={
   {"// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.\nvoid mainImage(){}",true,LicenseKind::NonCommercial},
   {"// CC0 - public domain\n",true,LicenseKind::Commercial},
   {"// The MIT License\n// Copyright © 2013 Inigo Quilez\n",true,LicenseKind::Commercial},
   {"// license: CC BY 4.0\n",true,LicenseKind::Attribution},
   {"void mainImage(out vec4 c, in vec2 f){c=vec4(1);}",true,LicenseKind::NonCommercial},
   {"void mainImage(out vec4 c, in vec2 f){c=vec4(1);}",false,LicenseKind::Unknown},
   {"// All rights reserved. Contact me for licensing.\n",true,LicenseKind::Restricted},
   {"// Feel free to use this in your projects!\n",true,LicenseKind::Commercial},
   {"// do not use this function, slow\nfloat f(){return 1.;}",true,LicenseKind::NonCommercial},
   {"// for commercial use please contact me\n",true,LicenseKind::Unknown},
   {"// Unlicense\n",true,LicenseKind::Commercial},
   {"// cc by-sa 3.0\n",true,LicenseKind::Attribution},
   {"vec3 submit(){} // permit\n",true,LicenseKind::NonCommercial},
  };
  int bad=0;
  for(auto&x:t){ auto li=DetectLicense(x.txt,x.st); bool ok=li.kind==x.want; bad+=!ok;
    printf("%s %-22s | %s | %s\n", ok?"OK ":"BAD", LicenseShortName(li.kind), li.label.c_str(), li.evidence.c_str()); }
  printf("%d échec(s)\n",bad); return bad; }
