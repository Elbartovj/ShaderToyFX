#define A 2
#define B (A*2)
#define GAIN 0.7
float arr[B];
void mainImage( out vec4 c, in vec2 f ){ arr[0] = GAIN; c = vec4(arr[0], 0.0, 0.0, 1.0); }
