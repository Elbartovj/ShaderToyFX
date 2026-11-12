#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Renderer.h"
#include <cstdio>
#include <vector>
#include <cstdlib>
using namespace stfx;
int main(int argc,char**argv){ setvbuf(stdout,0,_IONBF,0);
  glfwInit(); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,1); glfwWindowHint(GLFW_VISIBLE,0);
  auto w=glfwCreateWindow(8,8,"",0,0); glfwMakeContextCurrent(w); glewExperimental=1; glewInit(); glGetError();
  Renderer r; r.InitGL(); const int W=300,H=100;
  GLuint tex,fbo,input; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
  glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  // entrée : bande blanche verticale (= relief) dans le tiers DROIT de l'image
  std::vector<unsigned char> px(W*H*4,0); for(int y=0;y<H;y++)for(int x=0;x<W;x++){ unsigned char*p=&px[(y*W+x)*4]; unsigned char v=(x>220&&x<240)?255:0; p[0]=p[1]=p[2]=v; p[3]=255; }
  glGenTextures(1,&input); glBindTexture(GL_TEXTURE_2D,input); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,px.data());
  for(int a=1;a<argc;a++){ ShaderDoc d; std::string e,l; LoadShaderFile(argv[a],d,e); if(!r.Build(d,l)){printf("%s\n",l.c_str());return 1;}
    FrameInputs in; in.hostFbo=fbo; in.width=W; in.height=H; in.inputTex=input; in.inputW=W; in.inputH=H; in.chMode[0]=CH_INPUT; in.aspectFix = getenv("FIX")!=nullptr;
    for(size_t k=0;k<d.params.size();k++) in.user[k]=d.params[k].def;
    in.mouse[0]=W*0.5f; in.mouse[1]=H*0.5f; r.Render(in);
    std::vector<unsigned char> o(W*H*4); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,o.data());
    // où le relief influence-t-il l'image ? on mesure la variation horizontale de luminosité sur la ligne du milieu
    int y=H/2; int best=0,bx=-1; for(int x=1;x<W;x++){ int dd=abs(o[(y*W+x)*4]-o[(y*W+x-1)*4]); if(dd>best){best=dd;bx=x;} }
    printf("%s : plus forte arête à x=%d (bande d'entrée à x=220..240)\n", argv[a], bx);
  }
  r.DeInitGL(); }
