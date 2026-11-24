// Régression : variables locales déclarées sans valeur (« float i, s; »), courantes dans les shaders « golf ».
// WebGL les met à zéro ; Shader Loader ajoute « = type(0) » pour que les pilotes OpenGL donnent le même rendu.
void mainImage(out vec4 o,in vec2 f){vec2 uv=f/iResolution.xy;vec3 c;for(float i,s;i++<8.;)s+=.1,c+=s*.02*vec3(uv,1);o=vec4(c,1.);}
