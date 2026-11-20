#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>// avant FFGL.h (qui inclut une version allégée de windows.h)

#include "Preview.h"
#include "Renderer.h"
#include "ShaderDoc.h"
#include "Util.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <mutex>
#include <thread>

namespace stfx
{
namespace Preview
{
namespace
{
using Clock = std::chrono::steady_clock;

std::mutex g_m;
std::condition_variable g_cv;
std::thread g_thread;
bool g_quit = false;

// demande du panneau
std::string g_reqPath;
int g_reqW = 256, g_reqH = 144;
Clock::time_point g_lastRequest;
bool g_restart = false;

// résultat
Frame g_frame;
Status g_status;

//------------------------------------------------------------------ contexte OpenGL caché
struct GLContext
{
	HWND hwnd = nullptr;
	HDC dc    = nullptr;
	HGLRC rc  = nullptr;

	bool Create()
	{
		HINSTANCE inst = nullptr;
		GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Request, &inst );
		WNDCLASSW wc{};
		wc.style         = CS_OWNDC;
		wc.lpfnWndProc   = DefWindowProcW;
		wc.hInstance     = inst;
		wc.lpszClassName = L"ShaderToyFX_PreviewGL";
		RegisterClassW( &wc );// déjà enregistrée : sans effet
		hwnd = CreateWindowExW( 0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, inst, nullptr );
		if( !hwnd )
			return false;
		dc = GetDC( hwnd );
		PIXELFORMATDESCRIPTOR pfd{};
		pfd.nSize      = sizeof( pfd );
		pfd.nVersion   = 1;
		pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 32;
		pfd.cAlphaBits = 8;
		pfd.iLayerType = PFD_MAIN_PLANE;
		int pf         = ChoosePixelFormat( dc, &pfd );
		if( !pf || !SetPixelFormat( dc, pf, &pfd ) )
			return false;
		// contexte de compatibilité : le driver donne sa version la plus récente (4.6 en général),
		// comme le contexte de Resolume ; les pointeurs GLEW déjà chargés par le plugin restent valides
		rc = wglCreateContext( dc );
		if( !rc || !wglMakeCurrent( dc, rc ) )
			return false;
		if( !glCreateShader )
		{
			glewExperimental = GL_TRUE;
			if( glewInit() != GLEW_OK )
				return false;
		}
		return true;
	}
	void Destroy()
	{
		wglMakeCurrent( nullptr, nullptr );
		if( rc )
			wglDeleteContext( rc );
		if( hwnd && dc )
			ReleaseDC( hwnd, dc );
		if( hwnd )
			DestroyWindow( hwnd );
		rc   = nullptr;
		dc   = nullptr;
		hwnd = nullptr;
	}
};

// Image de test pour les effets (shaders qui lisent l'entrée Resolume) : dégradé + damier + cercles
GLuint MakeTestInput( int w, int h )
{
	std::vector< uint8_t > px( (size_t)w * h * 4 );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			float u = ( x + 0.5f ) / w, v = ( y + 0.5f ) / h;
			float r = 0.5f + 0.5f * std::cos( 6.2832f * ( u + 0.00f ) );
			float g = 0.5f + 0.5f * std::cos( 6.2832f * ( u + 0.33f + v * 0.3f ) );
			float b = 0.5f + 0.5f * std::cos( 6.2832f * ( u + 0.67f - v * 0.3f ) );
			bool check = ( ( x / 24 ) + ( y / 24 ) ) % 2 == 0;
			float k    = check ? 1.0f : 0.75f;
			float dx = ( u - 0.5f ) * ( (float)w / h ), dy = v - 0.5f;
			float d = std::sqrt( dx * dx + dy * dy );
			if( std::fabs( d - 0.3f ) < 0.02f )
				r = g = b = k = 1.0f;
			uint8_t* p = &px[ ( (size_t)y * w + x ) * 4 ];
			p[ 0 ]     = (uint8_t)std::clamp( r * k * 255.0f, 0.0f, 255.0f );
			p[ 1 ]     = (uint8_t)std::clamp( g * k * 255.0f, 0.0f, 255.0f );
			p[ 2 ]     = (uint8_t)std::clamp( b * k * 255.0f, 0.0f, 255.0f );
			p[ 3 ]     = 255;
		}
	GLuint t = 0;
	glGenTextures( 1, &t );
	glBindTexture( GL_TEXTURE_2D, t );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return t;
}

struct Target
{
	GLuint fbo = 0, tex = 0;
	int w = 0, h = 0;
	void Ensure( int nw, int nh )
	{
		if( fbo && nw == w && nh == h )
			return;
		Release();
		w = nw;
		h = nh;
		glGenTextures( 1, &tex );
		glBindTexture( GL_TEXTURE_2D, tex );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}
	void Release()
	{
		if( fbo )
			glDeleteFramebuffers( 1, &fbo );
		if( tex )
			glDeleteTextures( 1, &tex );
		fbo = tex = 0;
	}
};

void SetStatus( const std::string& path, bool compiling, const std::string& error )
{
	std::lock_guard< std::mutex > lock( g_m );
	g_status.path      = path;
	g_status.compiling = compiling;
	g_status.error     = error;
	if( compiling || !error.empty() )
	{
		g_status.fps  = 0;
		g_status.time = 0;
	}
}

std::string FirstErrorLines( const std::string& log )
{
	std::string out;
	int lines = 0;
	size_t a  = 0;
	while( a < log.size() && lines < 4 )
	{
		size_t b = log.find( '\n', a );
		if( b == std::string::npos )
			b = log.size();
		std::string l = Trim( log.substr( a, b - a ) );
		if( !l.empty() )
		{
			out += ( out.empty() ? "" : "\n" ) + l;
			++lines;
		}
		a = b + 1;
	}
	return out.empty() ? std::string( "Compilation impossible." ) : out;
}

void ThreadMain()
{
	GLContext gl;
	if( !gl.Create() )
	{
		SetStatus( "", false, "OpenGL indisponible pour l'aperçu." );
		gl.Destroy();
		std::unique_lock< std::mutex > lock( g_m );
		g_cv.wait( lock, [] { return g_quit; } );
		return;
	}

	Renderer renderer;
	bool rendererOk = renderer.InitGL();
	GLuint testInput = MakeTestInput( 320, 180 );
	Target target;

	std::string loadedPath;
	bool docOk = false;
	std::vector< float > userDefaults( kMaxUserParams, 0.5f );
	Clock::time_point start = Clock::now(), last = start;
	int frame = 0;
	float fpsAvg = 0;
	std::vector< uint8_t > pixels;

	for( ;; )
	{
		std::string path;
		int w, h;
		bool restart;
		{
			std::unique_lock< std::mutex > lock( g_m );
			// en veille si le panneau ne demande plus d'aperçu
			g_cv.wait( lock, [] { return g_quit || ( !g_reqPath.empty() && Clock::now() - g_lastRequest < std::chrono::milliseconds( 500 ) ); } );
			if( g_quit )
				break;
			path      = g_reqPath;
			w         = g_reqW;
			h         = g_reqH;
			restart   = g_restart;
			g_restart = false;
		}
		if( !rendererOk )
		{
			SetStatus( path, false, "Initialisation OpenGL de l'aperçu impossible." );
			std::this_thread::sleep_for( std::chrono::milliseconds( 200 ) );
			continue;
		}

		// nouveau shader : chargement + compilation (avec les valeurs d'origine des paramètres)
		if( path != loadedPath )
		{
			loadedPath = path;
			docOk      = false;
			SetStatus( path, true, "" );
			ShaderDoc doc;
			std::string err;
			if( !LoadShaderFile( path, doc, err ) )
				SetStatus( path, false, err.empty() ? "Lecture impossible." : err );
			else
			{
				std::string log;
				if( renderer.Build( doc, log ) )
				{
					docOk = true;
					std::fill( userDefaults.begin(), userDefaults.end(), 0.5f );
					for( size_t i = 0; i < doc.params.size() && i < userDefaults.size(); ++i )
						userDefaults[ i ] = UserParamValue( doc.params[ i ], 0.5f );
					SetStatus( path, false, "" );
				}
				else
					SetStatus( path, false, FirstErrorLines( log ) );
			}
			restart = true;
		}
		if( !docOk )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
			continue;
		}
		if( restart )
		{
			renderer.ResetBuffers();
			start = last = Clock::now();
			frame        = 0;
		}

		target.Ensure( w, h );
		auto now = Clock::now();
		float t  = std::chrono::duration< float >( now - start ).count();
		float dt = frame == 0 ? 1.0f / 30.0f : std::chrono::duration< float >( now - last ).count();
		last     = now;

		FrameInputs in;
		in.hostFbo   = target.fbo;
		in.width     = w;
		in.height    = h;
		in.time      = t;
		in.timeDelta = dt;
		in.frameRate = dt > 0 ? 1.0f / dt : 30.0f;
		in.frame     = frame++;
		in.inputTex  = testInput;
		in.inputW = in.inputHwW = 320;
		in.inputH = in.inputHwH = 180;
		in.inputFit             = 2;// remplir
		std::time_t tt          = std::time( nullptr );
		std::tm lt{};
		localtime_s( &lt, &tt );
		in.date[ 0 ] = (float)( lt.tm_year + 1900 );
		in.date[ 1 ] = (float)lt.tm_mon;
		in.date[ 2 ] = (float)lt.tm_mday;
		in.date[ 3 ] = (float)( lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec );
		for( int i = 0; i < kMaxUserParams; ++i )
			in.user[ i ] = userDefaults[ i ];

		renderer.Render( in );

		// lecture (synchrone : limite naturellement la cadence si le shader est lourd)
		pixels.resize( (size_t)w * h * 4 );
		glBindFramebuffer( GL_READ_FRAMEBUFFER, target.fbo );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
		float cost = std::chrono::duration< float >( Clock::now() - now ).count();
		{
			std::lock_guard< std::mutex > lock( g_m );
			g_frame.w = w;
			g_frame.path = path;
			g_frame.h = h;
			g_frame.rgba.resize( pixels.size() );
			const size_t row = (size_t)w * 4;
			for( int y = 0; y < h; ++y )// OpenGL : bas en haut -> haut en bas
			{
				std::copy( pixels.begin() + ( h - 1 - y ) * row, pixels.begin() + ( h - y ) * row, g_frame.rgba.begin() + y * row );
				for( size_t x = 3; x < row; x += 4 )
					g_frame.rgba[ y * row + x ] = 255;// l'aperçu ignore l'alpha
			}
			g_frame.serial++;
			fpsAvg        = fpsAvg <= 0 ? in.frameRate : fpsAvg * 0.9f + in.frameRate * 0.1f;
			g_status.fps  = fpsAvg;
			g_status.time = t;
		}

		// ~30 i/s max ; un shader lourd laisse le GPU respirer pour Resolume
		float wait = std::max( 1.0f / 30.0f - cost, cost > 0.05f ? cost : 0.0f );
		std::this_thread::sleep_for( std::chrono::duration< float >( wait ) );
	}

	target.Release();
	if( testInput )
		glDeleteTextures( 1, &testInput );
	renderer.DeInitGL();
	gl.Destroy();
}
}//namespace

void Request( const std::string& path, int width, int height )
{
	std::lock_guard< std::mutex > lock( g_m );
	if( !g_thread.joinable() )
	{
		g_quit   = false;
		g_thread = std::thread( ThreadMain );
	}
	bool wake     = g_reqPath != path || Clock::now() - g_lastRequest > std::chrono::milliseconds( 400 );
	g_reqPath     = path;
	g_reqW        = std::clamp( width, 16, 1024 );
	g_reqH        = std::clamp( height, 16, 1024 );
	g_lastRequest = Clock::now();
	if( wake )
		g_cv.notify_all();
}

void Restart()
{
	std::lock_guard< std::mutex > lock( g_m );
	g_restart = true;
}

bool TakeFrame( uint64_t lastSerial, Frame& out )
{
	std::lock_guard< std::mutex > lock( g_m );
	if( g_frame.serial == lastSerial || g_frame.rgba.empty() )
		return false;
	out = g_frame;
	return true;
}

Status GetStatus()
{
	std::lock_guard< std::mutex > lock( g_m );
	return g_status;
}

void Shutdown()
{
	{
		std::lock_guard< std::mutex > lock( g_m );
		g_quit = true;
		g_cv.notify_all();
	}
	if( g_thread.joinable() )
		g_thread.join();
	std::lock_guard< std::mutex > lock( g_m );
	g_frame   = Frame();
	g_status  = Status();
	g_reqPath.clear();
}
}//namespace Preview
}//namespace stfx
