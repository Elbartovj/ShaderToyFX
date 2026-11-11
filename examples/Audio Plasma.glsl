// ShaderToyFX - exemple : plasma réactif au son, avec paramètres exposés dans Resolume.
// Les lignes "uniform float nom; // min max défaut" deviennent des sliders (groupe "Shader Params").
// iChannel0 = entrée Resolume (clip sous l'effet), iChannel1 = FFT audio (512x2, ligne y=0.25)
// @iChannel0 input
// @iChannel1 audio

uniform float zoom;      // 0.5 8 3
uniform float mixInput;  // 0 1 0.35
uniform float bassBoost; // 0 4 1.5

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord / iResolution.xy;
    vec2 p  = ( fragCoord - 0.5 * iResolution.xy ) / iResolution.y * zoom;

    float bass = texture( iChannel1, vec2( 0.02, 0.25 ) ).x * bassBoost;
    float mids = texture( iChannel1, vec2( 0.25, 0.25 ) ).x;

    float t = iTime * 0.6;
    float v = sin( p.x + t ) + sin( p.y * 1.3 - t ) + sin( length( p ) * ( 2.0 + bass * 3.0 ) - t * 2.0 );
    vec3 col = 0.5 + 0.5 * cos( v + vec3( 0.0, 2.1, 4.2 ) + mids * 3.0 );
    col *= 0.8 + bass;

    vec3 src = texture( iChannel0, uv ).rgb;
    fragColor = vec4( mix( col, src * col * 2.0, mixInput ), 1.0 );
}
