#version 300 es
precision mediump float;
in vec2 vUV;
out vec4 fragColor;
uniform float uTime;
uniform vec2 uRes;
uniform float speed;
uniform vec3 tintColor;
void main() {
	vec2 p = vUV * 2.0 - 1.0;
	float r = length(p * vec2(uRes.x / uRes.y, 1.0));
	fragColor = vec4(tintColor * (0.5 + 0.5 * cos(r * 12.0 - uTime * speed)), 1.0);
}
