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
  for(int i=1;i<argc;i++){ ShaderDoc d; std::string e,l; bool ok=LoadShaderFile(argv[i],d,e)&&r.Build(d,l);
    printf("%s : %s  params=", argv[i], ok?"OK":"ECHEC"); for(auto&p:d.params) printf("%s[%g..%g=%g] ",p.name.c_str(),p.minV,p.maxV,p.def);
    printf("\n"); if(!ok){printf("%s%s\n",e.c_str(),l.c_str()); bad++;} }
  r.DeInitGL(); return bad; }
