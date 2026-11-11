#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Renderer.h"
#include <cstdio>
using namespace stfx;
int main(int argc,char**argv){ setvbuf(stdout,0,_IONBF,0);
  glfwInit(); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,1); glfwWindowHint(GLFW_VISIBLE,0);
  auto w=glfwCreateWindow(8,8,"",0,0); glfwMakeContextCurrent(w); glewExperimental=1; glewInit(); glGetError();
  Renderer r; r.InitGL(); int bad=0;
  for(int a=1;a<argc;a++){
    ShaderDoc d; std::string e,l; if(!LoadShaderFile(argv[a],d,e)){printf("load %s\n",e.c_str()); return 1;}
    printf("== %s : %zu params détectés\n", argv[a], d.params.size());
    for(auto&p:d.params) printf("   %-10s src=%d int=%d vec=%d comp=%d [%g..%g] def=%g\n",p.name.c_str(),(int)p.source,p.isInt,p.vecSize,p.comp,p.minV,p.maxV,p.def);
    bool ok=r.Build(d,l); printf("   build=%s params après=%zu\n",ok?"OK":"ECHEC",d.params.size()); for(auto&x:d.warnings) printf("   warn: %s\n",x.c_str());
    if(!ok){printf("%s\n",l.c_str()); bad++; continue;}
    GLuint tex,fbo; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    auto render=[&](float bright){ FrameInputs in; in.hostFbo=fbo; in.width=16; in.height=16;
      for(size_t i=0;i<d.params.size();i++){ in.user[i]=d.params[i].def; if(d.params[i].name=="TINT.r") in.user[i]=bright; }
      r.Render(in); unsigned char px[4]; glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px); return (int)px[0]; };
    int r1=render(0.8f), r2=render(0.4f);
    printf("   rouge BRIGHT=0.8 -> %d, BRIGHT=0.4 -> %d\n", r1, r2);
  }
  r.DeInitGL(); return bad; }
