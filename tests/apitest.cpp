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
  ShaderDoc doc; std::string err, log;
  if(!LoadShaderFile("api.json",doc,err)){ printf("load fail %s\n",err.c_str()); return 1;}
  for(auto&x:doc.warnings) printf("warn %s\n",x.c_str());
  auto*img=doc.ImagePass(); printf("kinds %d %d %d img0=%p faces=%d vol d=%d\n",(int)img->ch[0].kind,(int)img->ch[1].kind,(int)img->ch[2].kind,(void*)img->ch[0].image.get(), img->ch[0].image?img->ch[0].image->faces:0, img->ch[1].image?img->ch[1].image->depth:0);
  if(!r.Build(doc,log)){ printf("build fail\n%s\n",log.c_str()); return 1;}
  GLuint tex,fbo; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
  glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
  FrameInputs in; in.hostFbo=fbo; in.width=16; in.height=16; r.Render(in);
  unsigned char px[4]; glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
  printf("pixel = %d %d %d %d  (attendu 255 77 4 255)  glerr=%x\n",px[0],px[1],px[2],px[3],glGetError());
  return !(px[0]==255 && abs(px[1]-77)<=1 && abs(px[2]-4)<=1);
}
