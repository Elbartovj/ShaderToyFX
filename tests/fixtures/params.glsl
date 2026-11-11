// Shader de test façon Shadertoy
#define SPEED 1.5
#define STEPS 64
#define AA 2
#define NUM 4
#define USE_GLOW 1
#define PI 3.14159265
#define SCALE (0.5) // 0 2
const float BRIGHT = 0.8;
const vec3 TINT = vec3(1.0, 0.5, 0.25);
const vec2 OFFSET = vec2(0.1);
const float DERIVED = BRIGHT * 2.0;
const int OCT = 3;
float gArr[NUM];
float gInit = SPEED * 0.0 + 0.0;
#define UNUSED 7.0

#if AA > 1
float aaFactor() { return 1.0; }
#else
float aaFactor() { return 0.5; }
#endif

float fbm(vec2 p){ float s = 0.0; for (int i = 0; i < OCT; i++) { s += sin(p.x * float(i + 1)); } return s; }

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord / iResolution.xy * SCALE + OFFSET;
    float acc = 0.0;
    for (int i = 0; i < STEPS; i++) acc += 1.0 / float(STEPS);
    gArr[0] = acc;
    vec3 col = TINT * BRIGHT * acc * aaFactor() + 0.0 * fbm(uv) + 0.0 * SPEED * iTime + 0.0 * DERIVED + 0.0 * gInit;
#if USE_GLOW
    col += 0.0;
#endif
    fragColor = vec4(col, 1.0);
}
