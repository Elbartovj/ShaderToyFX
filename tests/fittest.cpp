#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Renderer.h"
#include <cstdio>
#include <vector>
using namespace stfx;
int main(){ setvbuf(stdout,0,_IONBF,0);
  glfwInit(); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,1); glfwWindowHint(GLFW_VISIBLE,0);
  auto w=glfwCreateWindow(8,8,"",0,0); glfwMakeContextCurrent(w); glewExperimental=1; glewInit(); glGetError();
  Renderer r; r.InitGL();
  ShaderDoc d; std::string e,l; ParseShaderSource("void mainImage(out vec4 c,in vec2 f){ c = texture(iChannel0, f/iResolution.xy); }","",d,e); r.Build(d,l);
  const int W=64,H=32;
  GLuint tex,fbo; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
  glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  // entrée : texture matérielle 32x32, zone utile 16x16 rouge, marge verte
  std::vector<unsigned char> px(32*32*4);
  for(int y=0;y<32;y++)for(int x=0;x<32;x++){ unsigned char*p=&px[(y*32+x)*4]; bool in=x<16&&y<16; p[0]=in?255:0; p[1]=in?0:255; p[2]=0; p[3]=255; }
  GLuint input; glGenTextures(1,&input); glBindTexture(GL_TEXTURE_2D,input); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,px.data());
  int bad=0;
  const char* names[]={"étirer","contenir","remplir","1:1"};
  for(int fit=0;fit<4;fit++){
    FrameInputs in; in.hostFbo=fbo; in.width=W; in.height=H; in.inputTex=input; in.inputW=16; in.inputH=16; in.inputHwW=32; in.inputHwH=32; in.inputFit=fit;
    r.Render(in);
    std::vector<unsigned char> o(W*H*4); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,o.data());
    int green=0, black=0, red=0; for(int i=0;i<W*H;i++){ if(o[i*4+1]>128) green++; else if(o[i*4]>128) red++; else black++; }
    printf("%-9s rouge=%4d noir=%4d vert(marge)=%d\n",names[fit],red,black,green);
    if(green) bad++;
    if(fit==0 && red!=W*H) bad++;
    if(fit==1 && !(black>0 && red==H*H)) bad++;   // carré 32x32 au centre
    if(fit==2 && red!=W*H) bad++;
    if(fit==3 && red!=16*16) bad++;
  }
  r.DeInitGL(); printf(bad?"ECHEC\n":"OK\n"); return bad; }
