// Vérifie : 1) sliders au milieu = rendu identique au shader d'origine ; 2) chaque slider a un effet visible
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Renderer.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <algorithm>
using namespace stfx;
static const int W=96,H=54,FR=12;
GLuint fbo,tex,input;
std::vector<unsigned char> run(Renderer& r, const ShaderDoc& d, const float* user){
  r.ResetBuffers();
  for(int f=0;f<FR;f++){ FrameInputs in; in.hostFbo=fbo; in.width=W; in.height=H; in.inputTex=input; in.inputW=in.inputH=8;
    in.frame=f; in.time=f/60.f; in.timeDelta=1/60.f; in.mouse[0]=W*0.3f; in.mouse[1]=H*0.6f;
    for(size_t k=0;k<d.params.size();k++) in.user[k]=user[k]; r.Render(in); }
  std::vector<unsigned char> o(W*H*4); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,o.data()); return o; }
double diff(const std::vector<unsigned char>&a,const std::vector<unsigned char>&b){ double s=0; for(size_t i=0;i<a.size();i++) s+=std::abs(a[i]-b[i]); return s/a.size(); }
int main(int argc,char**argv){ setvbuf(stdout,0,_IONBF,0);
  glfwInit(); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,1); glfwWindowHint(GLFW_VISIBLE,0);
  auto w=glfwCreateWindow(8,8,"",0,0); glfwMakeContextCurrent(w); glewExperimental=1; glewInit(); glGetError();
  glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
  glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  std::vector<unsigned char> ip(8*8*4); for(size_t i=0;i<ip.size();i++) ip[i]=(i*37)%255; glGenTextures(1,&input); glBindTexture(GL_TEXTURE_2D,input); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,ip.data());
  Renderer r; r.InitGL(); int bad=0;
  for(int a=1;a<argc;a++){
    ShaderDoc d; std::string e,l; if(!LoadShaderFile(argv[a],d,e)){printf("%s\n",e.c_str());return 1;}
    ShaderDoc orig=d; orig.params.erase(std::remove_if(orig.params.begin(),orig.params.end(),[](const UserParam&p){return p.source!=ParamSource::Uniform;}),orig.params.end());
    float u0[kMaxUserParams]={}; for(size_t k=0;k<orig.params.size();k++) u0[k]=orig.params[k].def;
    if(!r.Build(orig,l)){printf("orig build fail %s\n",l.c_str()); bad++; continue;}
    auto ref=run(r,orig,u0);
    if(!r.Build(d,l)){printf("build fail %s\n",l.c_str()); bad++; continue;}
    // valeur réelle quand le slider Resolume est à 0.5 (sa valeur par défaut)
    float um[kMaxUserParams]={}; for(size_t k=0;k<d.params.size();k++){ auto&p=d.params[k]; float v=UserParamValue(p,0.5f); um[k]=p.isInt?std::round(v):v; }
    auto mid=run(r,d,um);
    double dd=diff(ref,mid); bool same=dd<0.01;
    printf("== %s : %zu sliders, slider au milieu = shader d'origine ? %s (écart %.3f)\n",argv[a],d.params.size(),same?"OUI":"NON",dd); bad+=!same;
    for(size_t k=0;k<d.params.size();k++){ auto&p=d.params[k];
      float uu[kMaxUserParams]; for(int j=0;j<kMaxUserParams;j++) uu[j]=um[j];
      float lo=UserParamValue(p,0.2f), hi=UserParamValue(p,0.8f); if(p.isInt){lo=std::round(lo);hi=std::round(hi);}
      uu[k]=lo; auto A=run(r,d,uu); uu[k]=hi; auto B=run(r,d,uu);
      double eff=std::max(diff(A,mid),diff(B,mid));
      printf("   %-18s [%g .. %g] milieu=%g (shader %g)  effet=%s %.2f\n",p.name.c_str(),p.minV,p.maxV,um[k],p.def,eff>0.05?"oui":"AUCUN",eff);
      if(std::fabs(um[k]-p.def)>1e-4) { printf("      ^ milieu != valeur du shader\n"); bad++; }
    }
  }
  r.DeInitGL(); printf(bad?"%d PROBLEME(S)\n":"TOUT OK\n",bad); return bad; }
