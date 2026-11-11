uniform float speed; // 0 4 2
void mainImage(out vec4 c, in vec2 f){ vec2 uv=f/iResolution.xy; c = texture(iChannel0, uv) * 0.5 + vec4(uv, fract(iTime*speed), 1.0)*0.5 + vec4(iMouse.xy/iResolution.xy,0,0)*0.0; }
