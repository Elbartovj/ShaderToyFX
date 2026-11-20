struct light { vec3 position, color; float size; };
float sample(float char) { float buffer; buffer += char; return buffer; }
void mainImage(out vec4 c, in vec2 f){ light l = light(vec3(0), vec3(1), 2.0); float shared = sample(0.5); c = vec4(shared, l.size*0.25, 0, 1); }
