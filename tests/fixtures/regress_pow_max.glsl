// régressions : pow() dans une initialisation constante, max() redéfini en macro
const float K = 0.5;
const float T[3] = float[]( 1.0, pow( K, 2.0 ), pow( K, 3.0 ) );
#define max(a,b) ( (a).y > (b).y ? a : b )
void mainImage( out vec4 c, in vec2 f ){ vec2 m = max( vec2(0.0, T[1]), vec2(0.0, 0.1) ); c = vec4( m.y, pow( -2.0, 2.0 ) * 0.1, 0.0, 1.0 ); }
