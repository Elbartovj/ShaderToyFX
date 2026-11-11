// Banc d'essai Linux : contexte GL 4.1 core (GLFW/Xvfb) + Renderer ShaderToyFX
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "Renderer.h"
#include "ShaderDoc.h"
#include "GlslGen.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <cstdio>
#include <vector>
#include <cmath>

using namespace stfx;

static int fails = 0;
#define CHECK( c, ... )                         \
	do                                          \
	{                                           \
		if( !( c ) )                            \
		{                                       \
			printf( "  ECHEC: " __VA_ARGS__ ); \
			printf( "\n" );                     \
			++fails;                            \
		}                                       \
	} while( 0 )

struct HostFbo
{
	GLuint fbo = 0, tex = 0;
	int w, h;
	HostFbo( int W, int H ) : w( W ), h( H )
	{
		glGenTextures( 1, &tex );
		glBindTexture( GL_TEXTURE_2D, tex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
	}
	std::vector< uint8_t > Read()
	{
		std::vector< uint8_t > px( w * h * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data() );
		return px;
	}
};

static GLuint MakeInput( int w, int h )
{
	std::vector< uint8_t > px( w * h * 4 );
	for( int i = 0; i < w * h; ++i )
	{
		px[ i * 4 + 0 ] = 200;
		px[ i * 4 + 1 ] = 100;
		px[ i * 4 + 2 ] = 50;
		px[ i * 4 + 3 ] = 255;
	}
	GLuint t;
	glGenTextures( 1, &t );
	glBindTexture( GL_TEXTURE_2D, t );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return t;
}

static void CheckState( const char* where )
{
	GLint v = 0;
	glGetIntegerv( GL_CURRENT_PROGRAM, &v );
	CHECK( v == 0, "%s: programme encore lié", where );
	glGetIntegerv( GL_ACTIVE_TEXTURE, &v );
	CHECK( v == GL_TEXTURE0, "%s: active texture != 0", where );
	for( int i = 0; i < 4; ++i )
	{
		glActiveTexture( GL_TEXTURE0 + i );
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &v );
		CHECK( v == 0, "%s: texture liée unité %d", where, i );
		glGetIntegerv( GL_SAMPLER_BINDING, &v );
		CHECK( v == 0, "%s: sampler lié unité %d", where, i );
	}
	glActiveTexture( GL_TEXTURE0 );
	CHECK( glGetError() == GL_NO_ERROR, "%s: erreur GL", where );
}

int main()
{
	if( !glfwInit() )
		return 1;
	glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 4 );
	glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 1 );
	glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );
	glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE );
	glfwWindowHint( GLFW_VISIBLE, GL_FALSE );
	GLFWwindow* win = glfwCreateWindow( 64, 64, "t", nullptr, nullptr );
	if( !win )
	{
		printf( "pas de contexte GL 4.1\n" );
		return 1;
	}
	glfwMakeContextCurrent( win );
	glewExperimental = GL_TRUE;
	glewInit();
	glGetError();
	printf( "GL: %s / %s\n", glGetString( GL_VERSION ), glGetString( GL_RENDERER ) );

	Renderer r;
	CHECK( r.InitGL(), "InitGL" );
	const int W = 64, H = 36;
	HostFbo host( W, H );
	GLuint input = MakeInput( 32, 32 );

	// ---------- 1) multipasse JSON
	{
		printf( "[1] multipass.json\n" );
		ShaderDoc doc;
		std::string err;
		bool ok = LoadShaderFile( "multipass.json", doc, err );
		CHECK( ok, "chargement: %s", err.c_str() );
		printf( "  titre=%s passes=%zu buffers=%d params=%zu warnings=%zu\n", doc.title.c_str(), doc.passes.size(), doc.BufferCount(), doc.params.size(), doc.warnings.size() );
		for( auto& w : doc.warnings )
			printf( "  warning: %s\n", w.c_str() );
		CHECK( doc.passes.size() == 3, "3 passes attendues" );
		CHECK( !doc.params.empty() && doc.params[ 0 ].name == "amount" && doc.params[ 0 ].maxV == 2.0f && doc.params[ 0 ].def == 1.0f, "param amount" );
		CHECK( doc.params.size() == 2 && doc.params[ 1 ].name == "GAIN" && doc.params[ 1 ].source == ParamSource::Define, "#define GAIN du Common exposé" );
		const PassDesc* img = doc.ImagePass();
		CHECK( img && img->ch[ 0 ].kind == ChannelKind::Buffer && img->ch[ 0 ].buffer == 1, "Image.iChannel0 = Buffer B" );
		CHECK( img && img->ch[ 1 ].kind == ChannelKind::Texture && img->ch[ 1 ].image, "Image.iChannel1 = texture chargée" );
		CHECK( img && img->ch[ 2 ].kind == ChannelKind::Audio, "Image.iChannel2 = audio" );
		CHECK( img && img->ch[ 3 ].kind == ChannelKind::Input, "Image.iChannel3 libre -> entrée" );
		CHECK( doc.passes[ 1 ].ch[ 1 ].buffer == 0 && doc.passes[ 1 ].ch[ 1 ].filter == Filter::Mipmap, "Buffer B lit A en mipmap" );

		std::string log;
		ok = r.Build( doc, log );
		CHECK( ok, "compilation: %s", log.c_str() );

		float fft[ 512 ];
		for( int i = 0; i < 512; ++i )
			fft[ i ] = 0.5f;
		r.UpdateAudio( fft, 512, 1.0f );

		const int N = 10;
		for( int f = 0; f < N; ++f )
		{
			FrameInputs in;
			in.hostFbo = host.fbo;
			in.width   = W;
			in.height  = H;
			in.inputTex = input;
			in.inputW = in.inputH = 32;
			in.frame              = f;
			in.time               = f / 60.0f;
			for( size_t k = 0; k < doc.params.size(); ++k )
				in.user[ k ] = doc.params[ k ].def;
			glBindFramebuffer( GL_FRAMEBUFFER, host.fbo );
			r.Render( in );
		}
		CheckState( "multipass" );
		auto px = host.Read();
		stbi_write_png( "out_multipass.png", W, H, 4, px.data(), W * 4 );
		int R = px[ ( 5 * W + 5 ) * 4 + 0 ], B = px[ ( 5 * W + 5 ) * 4 + 2 ], A = px[ ( 5 * W + 5 ) * 4 + 3 ];
		printf( "  pixel(5,5) = %d %d %d %d (attendu R≈%d, B≈128, A=255)\n", R, px[ ( 5 * W + 5 ) * 4 + 1 ], B, A, (int)std::lround( 0.02 * N * 255 ) );
		CHECK( std::abs( R - (int)std::lround( 0.02 * N * 255 ) ) <= 2, "feedback buffer A->B->Image" );
		CHECK( std::abs( B - 128 ) <= 2, "texture audio" );
		CHECK( A == 255, "alpha forcé" );
		// damier : uv*2 en repeat nearest -> la colonne x change de valeur
		int g0 = px[ ( 5 * W + 2 ) * 4 + 1 ], g1 = px[ ( 5 * W + 12 ) * 4 + 1 ];
		CHECK( g0 != g1, "texture damier (g0=%d g1=%d)", g0, g1 );
	}

	// ---------- 2) GLSL simple + entrée Resolume + paramètre
	{
		printf( "[2] simple.glsl\n" );
		ShaderDoc doc;
		std::string err, log;
		CHECK( LoadShaderFile( "simple.glsl", doc, err ), "%s", err.c_str() );
		CHECK( doc.params.size() == 1 && doc.params[ 0 ].name == "speed" && doc.params[ 0 ].def == 2.0f, "param speed" );
		CHECK( r.Build( doc, log ), "compilation: %s", log.c_str() );
		FrameInputs in;
		in.hostFbo  = host.fbo;
		in.width    = W;
		in.height   = H;
		in.inputTex = input;
		in.inputW = in.inputH = 32;
		r.Render( in );
		CheckState( "simple" );
		auto px = host.Read();
		stbi_write_png( "out_simple.png", W, H, 4, px.data(), W * 4 );
		printf( "  pixel(0,0) = %d %d %d %d (entrée*0.5 = 100 50 25)\n", px[ 0 ], px[ 1 ], px[ 2 ], px[ 3 ] );
		CHECK( std::abs( px[ 0 ] - 100 ) <= 3 && std::abs( px[ 1 ] - 50 ) <= 3, "entrée Resolume via iChannel0" );
	}

	// ---------- 3) shader invalide : erreur lisible, ancien shader conservé
	{
		printf( "[3] bad.glsl\n" );
		ShaderDoc doc;
		std::string err, log;
		CHECK( LoadShaderFile( "bad.glsl", doc, err ), "%s", err.c_str() );
		bool ok = r.Build( doc, log );
		CHECK( !ok, "la compilation aurait dû échouer" );
		printf( "  log:\n%s", log.c_str() );
		CHECK( log.find( "[Image ligne 2]" ) != std::string::npos, "numéro de ligne relatif au code utilisateur" );
		CHECK( r.HasProgram(), "l'ancien shader reste actif" );
	}

	// ---------- 4) défauts (effet + source)
	{
		printf( "[4] DefaultDoc\n" );
		std::string log;
		{ ShaderDoc dd = DefaultDoc( false ); CHECK( r.Build( dd, log ), "passthrough: %s", log.c_str() );
		ShaderDoc ds = DefaultDoc( true ); CHECK( r.Build( ds, log ), "source: %s", log.c_str() ); }
	}

	// ---------- 5) viewport décalé (offset fragCoord)
	{
		printf( "[5] viewport décalé\n" );
		ShaderDoc doc;
		std::string err, log;
		ParseShaderSource( "void mainImage(out vec4 c, in vec2 f){ c = vec4(f.x < 1.0 ? 1.0 : 0.0, 0, 0, 1); }", "", doc, err );
		CHECK( r.Build( doc, log ), "%s", log.c_str() );
		glBindFramebuffer( GL_FRAMEBUFFER, host.fbo );
		glClearColor( 0, 0, 0, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
		FrameInputs in;
		in.hostFbo = host.fbo;
		in.vpX     = 10;
		in.vpY     = 0;
		in.width   = 20;
		in.height  = 10;
		r.Render( in );
		auto px = host.Read();
		CHECK( px[ ( 0 * W + 10 ) * 4 ] == 255 && px[ ( 0 * W + 11 ) * 4 ] == 0, "fragCoord relatif au viewport" );
	}

	// ---------- 6) Auto Mask
	{
		printf( "[6] auto mask\n" );
		ShaderDoc doc;
		std::string err, log;
		ParseShaderSource( "void mainImage(out vec4 c, in vec2 f){ c = vec4(vec3(f.x / iResolution.x), 1.0); }", "", doc, err );
		CHECK( r.Build( doc, log ), "%s", log.c_str() );
		auto run = [ & ]( int mode, bool inv ) {
			FrameInputs in;
			in.hostFbo = host.fbo;
			in.width = W;
			in.height = H;
			in.maskMode = mode;
			in.maskThreshold = 0.5f;
			in.maskSoftness = 0.01f;
			in.maskInvert = inv;
			r.Render( in );
			return host.Read();
		};
		auto px = run( 1, false );
		printf( "  alpha gauche=%d droite=%d\n", px[ ( 5 * W + 2 ) * 4 + 3 ], px[ ( 5 * W + 60 ) * 4 + 3 ] );
		CHECK( px[ ( 5 * W + 2 ) * 4 + 3 ] == 0 && px[ ( 5 * W + 60 ) * 4 + 3 ] == 255, "luma mask" );
		px = run( 1, true );
		CHECK( px[ ( 5 * W + 2 ) * 4 + 3 ] == 255 && px[ ( 5 * W + 60 ) * 4 + 3 ] == 0, "mask inversé" );
		px = run( 0, false );
		CHECK( px[ ( 5 * W + 2 ) * 4 + 3 ] == 255, "mask désactivé" );
		CheckState( "mask" );
	}

	r.DeInitGL();
	printf( fails ? "\n%d ECHEC(S)\n" : "\nTOUS LES TESTS PASSENT\n", fails );
	glfwTerminate();
	return fails ? 1 : 0;
}
