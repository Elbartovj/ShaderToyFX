shader_type canvas_item;
// Godot 4 : source_color, hint_screen_texture, filtres
uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap;
uniform vec3 glow_color : source_color = vec3(0.2, 0.6, 1.0);
uniform float strength : hint_range(0.0, 2.0, 0.01) = 0.8;
uniform int bands = 8;
void fragment() {
	vec4 c = texture(TEXTURE, UV);
	float b = floor(UV.y * float(bands)) / float(bands);
	vec2 off = vec2(sin(b * 12.0 + TIME) * 0.01 * strength, 0.0);
	vec4 g = texture(TEXTURE, UV + off);
	COLOR = vec4(mix(c.rgb, g.rgb + glow_color * strength * 0.3, 0.5), c.a);
	COLOR.rgb += TEXTURE_PIXEL_SIZE.x * 0.0;
}
