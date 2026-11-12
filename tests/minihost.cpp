// Mini hôte FFGL pour tester ShaderToyFX.dll sous Wine (WGL + llvmpipe)
#include <GL/glew.h>
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <cstring>

typedef union
{
	uint32_t UIntValue;
	void* PointerValue;
} FFMixed;
typedef FFMixed( __stdcall* PlugMain )( uint32_t, FFMixed, void* );
struct Viewport { GLuint x, y, w, h; };
struct TexStruct { uint32_t Width, Height, HWWidth, HWHeight; GLuint Handle; };
struct ProcessStruct { uint32_t num; TexStruct** inputs; GLuint HostFBO; };
struct StrBuf { char* address; uint32_t maxToWrite; };
struct GetStr { uint32_t param; StrBuf buf; };
static std::string DisplayName( void* inst, uint32_t idx );
struct SetParam { uint32_t index; FFMixed value; };

static PlugMain plug;
static FFMixed Call( uint32_t code, FFMixed in, void* inst = nullptr ) { return plug( code, in, inst ); }
static FFMixed U( uint32_t v ) { FFMixed m; m.PointerValue = nullptr; m.UIntValue = v; return m; }
static FFMixed P( void* p ) { FFMixed m; m.PointerValue = p; return m; }
static void SetF( void* inst, uint32_t idx, float v ) { SetParam s; s.index = idx; s.value.PointerValue = nullptr; memcpy( &s.value.UIntValue, &v, 4 ); Call( 8, P( &s ), inst ); }
static void SetT( void* inst, uint32_t idx, const char* t ) { SetParam s; s.index = idx; s.value.PointerValue = (void*)t; Call( 8, P( &s ), inst ); }

std::string DisplayName( void* inst, uint32_t idx )
{
	static char b[ 256 ];
	b[ 0 ] = 0;
	memset( b, 0, sizeof( b ) ); GetStr g{ idx, { b, 255 } };
	plug( 51, P( &g ), inst );
	return b;
}

int main( int argc, char** argv )
{
	setvbuf( stdout, 0, _IONBF, 0 );
	const char* dll  = argc > 1 ? argv[ 1 ] : "ShaderToyFX.dll";
	const char* shader = argc > 2 ? argv[ 2 ] : "multipass.json";
	int waitPanel = argc > 3 ? atoi( argv[ 3 ] ) : 0;

	HWND hwnd = CreateWindowA( "STATIC", "gl", WS_POPUP, 0, 0, 16, 16, 0, 0, 0, 0 );
	HDC dc    = GetDC( hwnd );
	PIXELFORMATDESCRIPTOR pfd{ sizeof( pfd ), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
	SetPixelFormat( dc, ChoosePixelFormat( dc, &pfd ), &pfd );
	HGLRC rc = wglCreateContext( dc );
	wglMakeCurrent( dc, rc );
	glewInit();
	printf( "GL %s\n", glGetString( GL_VERSION ) );

	HMODULE mod = LoadLibraryA( dll );
	if( !mod ) { printf( "LoadLibrary échoue (%lu)\n", GetLastError() ); return 1; }
	plug = (PlugMain)GetProcAddress( mod, "plugMain" );
	if( !plug ) { printf( "pas de plugMain\n" ); return 1; }

	uint32_t r = Call( 34, U( 0 ) ).UIntValue;
	printf( "INITIALISE_V2 -> %u\n", r );
	struct Info { uint32_t major, minor; char id[ 4 ]; char name[ 16 ]; uint32_t type; };
	Info* info = (Info*)Call( 0, U( 0 ) ).PointerValue;
	printf( "plugin: %.4s '%.16s' type=%u\n", info->id, info->name, info->type );
	uint32_t n = Call( 4, U( 0 ) ).UIntValue;
	printf( "%u paramètres :\n", n );
	for( uint32_t i = 0; i < n; ++i )
		printf( "  %2u %-16.16s type=%u\n", i, (char*)Call( 5, U( i ) ).PointerValue, Call( 15, U( i ) ).UIntValue );

	const int W = 64, H = 36;
	Viewport vp{ 0, 0, W, H };
	void* inst = Call( 18, P( &vp ) ).PointerValue;
	printf( "instance = %p\n", inst );

	GLuint tex, fbo, input;
	glGenTextures( 1, &tex );
	glBindTexture( GL_TEXTURE_2D, tex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0 );
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
	glGenTextures( 1, &input );
	glBindTexture( GL_TEXTURE_2D, input );
	uint32_t red[ 16 ];
	for( auto& p : red ) p = 0xff3264c8;
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, red );
	glBindTexture( GL_TEXTURE_2D, 0 );
	TexStruct ts{ 4, 4, 4, 4, input };
	TexStruct* tsp = &ts;
	ProcessStruct ps{ 1, &tsp, fbo };

	auto frame = [ & ]( int count ) {
		for( int i = 0; i < count; ++i )
		{
			glBindFramebuffer( GL_FRAMEBUFFER, fbo );
			glViewport( 0, 0, W, H );
			Call( 17, P( &ps ), inst );
			Sleep( 5 );
		}
	};
	auto pixel = [ & ]( int x, int y, uint8_t* px ) {
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glReadPixels( x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px );
	};

	frame( 3 );
	uint8_t px[ 4 ];
	pixel( 2, 2, px );
	printf( "passthrough : %d %d %d %d (attendu 200 100 50 255)\n", px[ 0 ], px[ 1 ], px[ 2 ], px[ 3 ] );

	SetT( inst, 0, shader );
	for( int i = 0; i < 200; ++i )// attend le chargement asynchrone
	{
		frame( 1 );
		if( DisplayName( inst, 18 ).find( "(" ) != std::string::npos ) break;
	}
	printf( "param 11 display name = '%s'\n", DisplayName( inst, 26 ).c_str() );
	printf( "param 0 (Shader) = '%s'\n", (const char*)Call( 9, U( 0 ), inst ).PointerValue );
	SetF( inst, 16, 0.5f );// amount = 1.0 (plage 0..2)
	SetF( inst, 6, 0.25f );// Speed 1x
	frame( 10 );
	pixel( 5, 5, px );
	printf( "multipass après ~10 images : %d %d %d %d (R augmente avec le feedback)\n", px[ 0 ], px[ 1 ], px[ 2 ], px[ 3 ] );
	printf( "display speed = %s\n", (const char*)Call( 7, U( 6 ), inst ).PointerValue );

	// Auto Mask : on, luminance, seuil 0.5
	SetF( inst, 13, 1.0f );
	SetF( inst, 15, 0.5f );
	SetF( inst, 16, 0.0f );
	frame( 2 );
	pixel( 5, 5, px );
	printf( "auto mask (luma faible) : alpha = %d (attendu 0)\n", px[ 3 ] );
	SetF( inst, 17, 1.0f );
	frame( 2 );
	pixel( 5, 5, px );
	printf( "auto mask inversé : alpha = %d (attendu 255)\n", px[ 3 ] );

	printf( "sliders :" );
	for( int i = 26; i < 36; ++i )
		printf( " [%s=%s]", DisplayName( inst, i ).c_str(), (const char*)Call( 7, U( i ), inst ).PointerValue );
	printf( "\n" );
	SetF( inst, 13, 0.0f );// mask off
	frame( 2 );
	pixel( 5, 5, px );
	printf( "rouge avant = %d\n", px[ 0 ] );
	SetF( inst, 28, 0.0f );// 3e slider (TINT.r pour params.glsl) à 0
	frame( 2 );
	pixel( 5, 5, px );
	printf( "rouge après TINT.r=0 : %d\n", px[ 0 ] );

	// Inputs : iChannel0 -> entrée Resolume puis image
	printf( "param 18 = '%s'\n", DisplayName( inst, 18 ).c_str() );
	SetF( inst, 18, 1.0f );
	frame( 3 );
	pixel( 5, 5, px );
	printf( "iChannel0=entrée : %d %d %d\n", px[ 0 ], px[ 1 ], px[ 2 ] );
	SetF( inst, 18, 2.0f );
	SetT( inst, 19, "checker.png" );
	for( int i = 0; i < 20; ++i ) frame( 1 );
	pixel( 5, 5, px );
	printf( "iChannel0=image : %d %d %d\n", px[ 0 ], px[ 1 ], px[ 2 ] );
	printf( "param 19 = '%s' -> '%s'\n", DisplayName( inst, 19 ).c_str(), (const char*)Call( 9, U( 19 ), inst ).PointerValue );

	// Random From = dossier Techno (index 4 : Bibliothèque, Ambient, Local, Shadertoy, Techno)
	SetF( inst, 3, 4.0f );
	printf( "Random Folder (texte caché) = '%s'\n", (const char*)Call( 9, U( 4 ), inst ).PointerValue );
	for( int k = 0; k < 4; ++k )
	{
		SetF( inst, 2, 1.0f );
		SetF( inst, 2, 0.0f );
		std::string before = (const char*)Call( 9, U( 0 ), inst ).PointerValue;
		for( int i = 0; i < 200; ++i )
		{
			frame( 1 );
			if( before != (const char*)Call( 9, U( 0 ), inst ).PointerValue ) break;
		}
		printf( "random %d -> %s\n", k, (const char*)Call( 9, U( 0 ), inst ).PointerValue );
	}

	if( waitPanel )
	{
		SetF( inst, 1, 1.0f );// Browse Library
		SetF( inst, 1, 0.0f );
		for( int i = 0; i < waitPanel / 20; ++i ) frame( 1 ), Sleep( 15 );
	}

	Call( 19, U( 0 ), inst );
	Call( 2, U( 0 ) );
	printf( "OK fin\n" );
	return 0;
}
