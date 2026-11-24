shader_type canvas_item;
// Godot 3 : ondulation + teinte
uniform float amplitude : hint_range(0.0, 0.1) = 0.02;
uniform float frequency = 20.0;
uniform vec4 tint : hint_color = vec4(1.0, 0.8, 0.6, 1.0);
void fragment() {
	vec2 uv = UV;
	uv.x += sin(uv.y * frequency + TIME * 3.0) * amplitude;
	COLOR = texture(TEXTURE, uv) * tint;
}
