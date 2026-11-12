#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Renderer.h"
#include <cstdio>
using namespace stfx;
int main(int argc,char**argv){ setvbuf(stdout,0,_IONBF,0);
  glfwInit(); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,1);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,1); glfwWindowHint(GLFW_VISIBLE,0);
  auto w=glfwCreateWindow(8,8,"",0,0); glfwMakeContextCurrent(w); glewExperimental=1; glewInit(); glGetError();
  Renderer r; r.InitGL();
  // shader qui affiche iChannel0 (déclaré texture dans le shader)
  ShaderDoc d; std::string e,l;
  ParseShaderSource("void mainImage(out vec4 c,in vec2 f){ c = texture(iChannel0, f/iResolution.xy); }","",d,e);
  d.passes[0].ch[0].kind = ChannelKind::Texture; // texture absente -> noir par défaut
  r.Build(d,l);
  GLuint tex,fbo,input; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
  glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  unsigned char red[4*4]={}; for(int i=0;i<4;i++){red[i*4]=255;red[i*4+3]=255;}
  glGenTextures(1,&input); glBindTexture(GL_TEXTURE_2D,input); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,red);
  auto run=[&](int mode){ FrameInputs in; in.hostFbo=fbo; in.width=16; in.height=16; in.inputTex=input; in.inputW=in.inputH=2; in.chMode[0]=mode; r.Render(in);
    unsigned char px[4]; glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px); printf("mode %d -> %d %d %d  glerr=%x\n",mode,px[0],px[1],px[2],glGetError()); return px[0]+256*px[1]; };
  int bad=0;
  bad += run(CH_SHADER)!=0;
  bad += run(CH_INPUT)!=255;
  ImageData img; img.width=img.height=2; img.data.assign(16,0); for(int i=0;i<4;i++){img.data[i*4+1]=255; img.data[i*4+3]=255;}
  r.SetChannelImage(0,&img);
  bad += run(CH_IMAGE)!=255*256;
  bad += run(CH_BLACK)!=0;
  // vrais shaders
  for(int a=1;a<argc;a++){ ShaderDoc s; std::string e2,l2; bool ok=LoadShaderFile(argv[a],s,e2)&&r.Build(s,l2); printf("%s : %s %s\n",argv[a],ok?"OK":"ECHEC",ok?"":(e2+l2).c_str()); bad+=!ok;
    if(ok){ FrameInputs in; in.hostFbo=fbo; in.width=16; in.height=16; in.inputTex=input; in.inputW=in.inputH=2; in.chMode[0]=CH_INPUT; for(int k=0;k<3;k++){in.frame=k; r.Render(in);} printf("   rendu avec iChannel0=entrée : glerr=%x\n",glGetError()); } }
  r.DeInitGL(); printf(bad?"%d ECHEC(S)\n":"OK\n",bad); return bad; }
