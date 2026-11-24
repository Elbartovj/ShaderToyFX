shader_type canvas_item;
render_mode blend_mix;
uniform float radius : hint_range(0.0, 1.0) = 0.6;
uniform float softness = 0.3;
void fragment() {
	vec4 c = texture(TEXTURE, UV);
	float d = distance(UV, vec2(0.5));
	c.rgb *= smoothstep(radius, radius - softness, d);
	COLOR = c;
}
