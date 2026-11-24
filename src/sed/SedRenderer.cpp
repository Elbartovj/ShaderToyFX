// Exécution des projets SHADERed. La boucle de rendu, les variables système et les fonctions suivent
// SHADERed (RenderEngine.cpp, SystemVariableManager, FunctionVariableManager, ArcBallCamera,
// licence MIT, (c) dfranx). Les passes Godot suivent PluginGodotShaders (MIT, (c) dfranx).
#include "SedRenderer.h"
#include "SedGeometry.h"
#include "SedGodot.h"
#include "../ShaderDoc.h"
#include "../Util.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <sstream>

#include "stb_image.h"

namespace stfx
{
namespace sed
{
namespace
{
GLuint MakeTex( GLenum target )
{
	GLuint t = 0;
	glGenTextures( 1, &t );
	glBindTexture( target, t );
	return t;
}

GLuint SolidTexture( uint8_t r, uint8_t g, uint8_t b, uint8_t a )
{
	uint8_t px[ 4 ] = { r, g, b, a };
	GLuint t        = MakeTex( GL_TEXTURE_2D );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return t;
}

// Format de données d'un buffer SHADERed ("vec3;vec3;float") -> types (ValueType)
std::vector< int > ParseBufferFormat( const std::string& fmt )
{
	static const std::pair< const char*, int > names[] = {
		{ "bool", 0 }, { "bvec2", 1 }, { "bvec3", 2 }, { "bvec4", 3 }, { "int", 4 }, { "ivec2", 5 }, { "ivec3", 6 }, { "ivec4", 7 },
		{ "float", 8 }, { "vec2", 9 }, { "vec3", 10 }, { "vec4", 11 }, { "mat2", 12 }, { "mat3", 13 }, { "mat4", 14 },
		{ "uint", 4 }, { "uvec2", 5 }, { "uvec3", 6 }, { "uvec4", 7 } };
	std::vector< int > out;
	std::stringstream ss( fmt );
	std::string t;
	while( std::getline( ss, t, ';' ) )
	{
		t = Trim( t );
		for( auto& n : names )
			if( t == n.first )
				out.push_back( n.second );
	}
	return out;
}

GLuint Compile( GLenum type, const std::string& src, const std::string& label, std::string& log )
{
	GLuint s        = glCreateShader( type );
	const char* str = src.c_str();
	glShaderSource( s, 1, &str, nullptr );
	glCompileShader( s );
	GLint ok = 0;
	glGetShaderiv( s, GL_COMPILE_STATUS, &ok );
	if( !ok )
	{
		GLint len = 0;
		glGetShaderiv( s, GL_INFO_LOG_LENGTH, &len );
		std::string l( std::max( len, 1 ), '\0' );
		glGetShaderInfoLog( s, len, nullptr, &l[ 0 ] );
		log += "[" + label + "]\n" + l + "\n";
		glDeleteShader( s );
		return 0;
	}
	return s;
}

GLuint Link( const std::vector< GLuint >& shaders, const std::string& label, std::string& log )
{
	GLuint p = glCreateProgram();
	for( GLuint s : shaders )
		glAttachShader( p, s );
	glLinkProgram( p );
	for( GLuint s : shaders )
		glDeleteShader( s );
	GLint ok = 0;
	glGetProgramiv( p, GL_LINK_STATUS, &ok );
	if( !ok )
	{
		GLint len = 0;
		glGetProgramiv( p, GL_INFO_LOG_LENGTH, &len );
		std::string l( std::max( len, 1 ), '\0' );
		glGetProgramInfoLog( p, len, nullptr, &l[ 0 ] );
		log += "[" + label + " : édition des liens]\n" + l + "\n";
		glDeleteProgram( p );
		return 0;
	}
	return p;
}

// Caméras de SHADERed (ArcBallCamera / FirstPersonCamera)
glm::mat4 ViewMatrix( const Project& p, glm::vec3* posOut, glm::vec3* dirOut )
{
	if( p.fpCamera )
	{
		glm::vec3 pos( p.camPos[ 0 ], p.camPos[ 1 ], p.camPos[ 2 ] );
		glm::mat4 rota   = glm::yawPitchRoll( glm::radians( p.camYaw ), glm::radians( p.camPitch ), 0.0f );
		glm::vec4 target = rota * glm::vec4( 0, 0, 1, 0 );
		target           = glm::vec4( pos, 0 ) - glm::normalize( target );
		glm::mat4 yawM   = glm::rotate( glm::mat4( 1 ), p.camYaw, glm::vec3( 0, 1, 0 ) );
		glm::vec4 up     = yawM * glm::vec4( 0, 1, 0, 0 );
		if( posOut )
			*posOut = pos;
		if( dirOut )
			*dirOut = glm::vec3( target ) - pos;
		return glm::lookAt( pos, glm::vec3( target ), glm::vec3( up ) );
	}
	glm::mat4 rota = glm::yawPitchRoll( glm::radians( p.camYaw ), glm::radians( p.camPitch ), glm::radians( p.camRoll ) );
	glm::vec4 up   = rota * glm::vec4( 0, 1, 0, 0 );
	glm::vec4 pos  = rota * glm::vec4( 0, 0, -p.camDistance, 0 );
	if( posOut )
		*posOut = glm::vec3( pos );
	if( dirOut )
		*dirOut = -glm::vec3( pos );
	return glm::lookAt( glm::vec3( pos ), glm::vec3( 0 ), glm::vec3( up ) );
}

// Modèle .obj minimal (positions, UV, normales ; faces triangulées en éventail)
bool LoadObj( const std::string& path, const std::string& group, bool groupOnly, std::vector< float >& out )
{
	std::string text;
	if( !ReadFileText( path, text ) )
		return false;
	std::vector< glm::vec3 > v, vn;
	std::vector< glm::vec2 > vt;
	std::stringstream ss( text );
	std::string line, curGroup;
	auto vertex = [ & ]( const std::string& tok ) {
		int iv = 0, it = 0, in = 0;
		sscanf( tok.c_str(), "%d/%d/%d", &iv, &it, &in );
		if( tok.find( "//" ) != std::string::npos )
		{
			it = 0;
			sscanf( tok.c_str(), "%d//%d", &iv, &in );
		}
		auto fix = []( int i, size_t n ) { return i < 0 ? (int)n + i : i - 1; };
		iv = fix( iv, v.size() );
		it = fix( it, vt.size() );
		in = fix( in, vn.size() );
		float f[ 18 ] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 };
		if( iv >= 0 && iv < (int)v.size() ) { f[ 0 ] = v[ iv ].x; f[ 1 ] = v[ iv ].y; f[ 2 ] = v[ iv ].z; }
		if( in >= 0 && in < (int)vn.size() ) { f[ 3 ] = vn[ in ].x; f[ 4 ] = vn[ in ].y; f[ 5 ] = vn[ in ].z; }
		if( it >= 0 && it < (int)vt.size() ) { f[ 6 ] = vt[ it ].x; f[ 7 ] = vt[ it ].y; }
		out.insert( out.end(), f, f + 18 );
	};
	while( std::getline( ss, line ) )
	{
		std::stringstream ls( line );
		std::string k;
		ls >> k;
		if( k == "v" ) { glm::vec3 p; ls >> p.x >> p.y >> p.z; v.push_back( p ); }
		else if( k == "vt" ) { glm::vec2 p; ls >> p.x >> p.y; vt.push_back( p ); }
		else if( k == "vn" ) { glm::vec3 p; ls >> p.x >> p.y >> p.z; vn.push_back( p ); }
		else if( k == "g" || k == "o" ) { ls >> curGroup; }
		else if( k == "f" )
		{
			if( groupOnly && curGroup != group )
				continue;
			std::vector< std::string > toks;
			std::string t;
			while( ls >> t )
				toks.push_back( t );
			for( size_t i = 1; i + 1 < toks.size(); ++i )
			{
				vertex( toks[ 0 ] );
				vertex( toks[ i ] );
				vertex( toks[ i + 1 ] );
			}
		}
	}
	return !out.empty();
}

const char* kCanvasVS = R"(#version 330
layout(location = 0) in vec3 pos_attrib;
layout(location = 1) in vec2 uv_attrib;
layout(location = 2) in vec4 color_attrib;
uniform mat4 PROJECTION_MATRIX;
uniform mat4 WORLD_MATRIX;
out vec2 uv_interp;
out vec4 color_interp;
void main()
{
	uv_interp = uv_attrib;
	color_interp = color_attrib;
	gl_Position = PROJECTION_MATRIX * (WORLD_MATRIX * vec4(pos_attrib.xy, 0.0, 1.0));
}
)";
const char* kCanvasPS = R"(#version 330
in vec2 uv_interp;
in vec4 color_interp;
uniform sampler2D TEXTURE;
layout(location = 0) out vec4 frag_color;
void main() { frag_color = color_interp * texture(TEXTURE, uv_interp); }
)";
}//namespace

Renderer::~Renderer()
{
	// les objets GL sont libérés par Release() dans le bon contexte
}

void Renderer::Release()
{
	DestroyAll();
	proj.reset();
}

void Renderer::DestroyAll()
{
	for( auto& gp : passes )
	{
		if( gp.program )
			glDeleteProgram( gp.program );
		if( gp.fbo )
			glDeleteFramebuffers( 1, &gp.fbo );
		for( auto& it : gp.items )
		{
			if( it.vao )
				glDeleteVertexArrays( 1, &it.vao );
			if( it.vbo )
				glDeleteBuffers( 1, &it.vbo );
		}
	}
	passes.clear();
	for( auto& o : objects )
	{
		if( o.tex )
			glDeleteTextures( 1, &o.tex );
		if( o.buffer )
			glDeleteBuffers( 1, &o.buffer );
	}
	objects.clear();
	for( auto& r : rts )
	{
		if( r.color )
			glDeleteTextures( 1, &r.color );
		if( r.depth )
			glDeleteTextures( 1, &r.depth );
	}
	rts.clear();
	GLuint texs[] = { windowColor, windowDepth, white, black, keyboard, screenCopy, flipTex };
	for( GLuint t : texs )
		if( t )
			glDeleteTextures( 1, &t );
	if( windowFbo )
		glDeleteFramebuffers( 1, &windowFbo );
	if( screenCopyFbo )
		glDeleteFramebuffers( 1, &screenCopyFbo );
	if( flipFbo )
		glDeleteFramebuffers( 1, &flipFbo );
	if( flipReadFbo )
		glDeleteFramebuffers( 1, &flipReadFbo );
	flipTex = flipFbo = flipReadFbo = 0;
	flipW = flipH = 0;
	flipFrame = -1;
	windowColor = windowDepth = white = black = keyboard = screenCopy = 0;
	windowFbo = screenCopyFbo = 0;
	winW = winH = screenW = screenH = 0;
	replaceable.clear();
	geoTransforms.clear();
	prevGeoTransforms.clear();
}

void Renderer::ResetTime()
{
	prevGeoTransforms.clear();
}

void Renderer::CreateObjects()
{
	white    = SolidTexture( 255, 255, 255, 255 );
	black    = SolidTexture( 0, 0, 0, 0 );
	keyboard = MakeTex( GL_TEXTURE_2D );
	std::vector< uint8_t > kb( 256 * 3, 0 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, 256, 3, 0, GL_RED, GL_UNSIGNED_BYTE, kb.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

	int replaceCount = 0;
	for( auto& o : proj->objects )
	{
		GpuObject g;
		g.name = o.name;
		g.kind = o.kind;
		switch( o.kind )
		{
		case ObjectKind::Texture:
		{
			int w = 0, h = 0, n = 0;
			std::vector< uint8_t > file;
			stbi_uc* px = nullptr;
			if( ReadFileBytes( o.path, file ) && !file.empty() )
			{
				// SHADERed charge les textures retournées (origine en bas, comme OpenGL)
				stbi_set_flip_vertically_on_load_thread( o.vflip ? 0 : 1 );
				px = stbi_load_from_memory( file.data(), (int)file.size(), &w, &h, &n, 4 );
				stbi_set_flip_vertically_on_load_thread( 0 );
			}
			g.tex = MakeTex( GL_TEXTURE_2D );
			if( px )
			{
				glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px );
				stbi_image_free( px );
			}
			else
			{
				uint8_t magenta[ 4 ] = { 255, 0, 255, 255 };
				glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, magenta );
				proj->warnings.push_back( "Texture introuvable : " + o.name );
				w = h = 1;
			}
			glGenerateMipmap( GL_TEXTURE_2D );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, o.minFilter );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, o.magFilter );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, o.wrapS );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, o.wrapT );
			g.w = w;
			g.h = h;
			if( replaceCount < 4 )
			{
				g.replaceIndex = replaceCount++;
				replaceable.push_back( FileStem( o.name ) );
			}
			break;
		}
		case ObjectKind::Cubemap:
		{
			g.target = GL_TEXTURE_CUBE_MAP;
			g.tex    = MakeTex( GL_TEXTURE_CUBE_MAP );
			// ordre SHADERed : left, top, front, bottom, right, back
			const GLenum faces[ 6 ] = { GL_TEXTURE_CUBE_MAP_NEGATIVE_X, GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
										GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z };
			for( int f = 0; f < 6; ++f )
			{
				int w = 0, h = 0, n = 0;
				std::vector< uint8_t > file;
				stbi_uc* px = nullptr;
				if( ReadFileBytes( o.cube[ f ], file ) && !file.empty() )
					px = stbi_load_from_memory( file.data(), (int)file.size(), &w, &h, &n, 4 );
				uint8_t grey[ 4 ] = { 128, 128, 128, 255 };
				glTexImage2D( faces[ f ], 0, GL_RGBA8, px ? w : 1, px ? h : 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px ? px : grey );
				if( px )
					stbi_image_free( px );
			}
			glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
			glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
			break;
		}
		case ObjectKind::Texture3D:
		{
			g.target = GL_TEXTURE_3D;
			g.tex    = MakeTex( GL_TEXTURE_3D );
			uint8_t grey[ 4 ] = { 128, 128, 128, 255 };
			glTexImage3D( GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, grey );
			proj->warnings.push_back( "Texture 3D « " + o.name + " » remplacée par une texture grise" );
			break;
		}
		case ObjectKind::KeyboardTexture:
			g.tex = 0;// clavier : texture partagée
			break;
		case ObjectKind::AudioTexture:
			g.tex = 0;// audio : spectre de Resolume
			break;
		case ObjectKind::Image:
		case ObjectKind::Image3D:
		{
			bool is3d = o.kind == ObjectKind::Image3D;
			g.target  = is3d ? GL_TEXTURE_3D : GL_TEXTURE_2D;
			g.tex     = MakeTex( g.target );
			g.w       = std::max( 1, o.fixed[ 0 ] );
			g.h       = std::max( 1, o.fixed[ 1 ] );
			g.d       = std::max( 1, o.fixed[ 2 ] );
			g.format  = o.format;
			if( is3d )
				glTexStorage3D( GL_TEXTURE_3D, 1, o.format, g.w, g.h, g.d );
			else
				glTexStorage2D( GL_TEXTURE_2D, 1, o.format, g.w, g.h );
			glTexParameteri( g.target, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glTexParameteri( g.target, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			break;
		}
		case ObjectKind::Buffer:
		{
			glGenBuffers( 1, &g.buffer );
			glBindBuffer( GL_SHADER_STORAGE_BUFFER, g.buffer );
			glBufferData( GL_SHADER_STORAGE_BUFFER, std::max( 4, o.bufferSize ), o.bufferData.empty() ? nullptr : o.bufferData.data(), GL_DYNAMIC_DRAW );
			glBindBuffer( GL_SHADER_STORAGE_BUFFER, 0 );
			g.bufferFormat = ParseBufferFormat( o.bufferFormat );
			g.bufferSize   = o.bufferSize;
			break;
		}
		default:
			break;
		}
		objects.push_back( g );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );

	// render textures (taille définie au premier rendu)
	for( auto& o : proj->objects )
		if( o.kind == ObjectKind::RenderTexture )
		{
			RT r;
			r.name = o.name;
			r.obj  = &o;
			rts.push_back( r );
		}
}

void Renderer::ResizeTargets( int w, int h )
{
	if( w == winW && h == winH && windowFbo )
		return;
	winW = w;
	winH = h;
	auto alloc = [ & ]( GLuint& color, GLuint& depth, GLenum fmt, int rw, int rh ) {
		if( !color )
			color = MakeTex( GL_TEXTURE_2D );
		glBindTexture( GL_TEXTURE_2D, color );
		GLenum ext = GL_RGBA, type = GL_UNSIGNED_BYTE;
		// formats entiers : données au format entier
		if( fmt >= GL_RGBA32UI && fmt <= GL_RGB8I )
		{
			ext  = GL_RGBA_INTEGER;
			type = GL_INT;
		}
		glTexImage2D( GL_TEXTURE_2D, 0, fmt, rw, rh, 0, ext, type, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		if( !depth )
			depth = MakeTex( GL_TEXTURE_2D );
		glBindTexture( GL_TEXTURE_2D, depth );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, rw, rh, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	};
	alloc( windowColor, windowDepth, GL_RGBA16F, w, h );
	if( !windowFbo )
		glGenFramebuffers( 1, &windowFbo );
	glBindFramebuffer( GL_FRAMEBUFFER, windowFbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, windowColor, 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, windowDepth, 0 );
	for( auto& r : rts )
	{
		int rw = r.obj->fixed[ 0 ] > 0 ? r.obj->fixed[ 0 ] : std::max( 1, (int)( r.obj->ratio[ 0 ] * w ) );
		int rh = r.obj->fixed[ 1 ] > 0 ? r.obj->fixed[ 1 ] : std::max( 1, (int)( r.obj->ratio[ 1 ] * h ) );
		if( r.color && rw == r.w && rh == r.h )
			continue;
		GLenum fmt = r.obj->format == GL_RGBA || r.obj->format == 0xFFFFFFFFu ? GL_RGBA8 : r.obj->format;
		alloc( r.color, r.depth, fmt, rw, rh );
		r.w = rw;
		r.h = rh;
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	for( auto& gp : passes )
		UpdateFbo( gp );
}

void Renderer::UpdateFbo( GpuPass& gp )
{
	if( gp.pass->kind != PassKind::Shader && gp.pass->kind != PassKind::Godot )
		return;
	if( !gp.fbo )
		glGenFramebuffers( 1, &gp.fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, gp.fbo );
	GLenum bufs[ 16 ];
	int n         = 0;
	GLuint depth  = windowDepth;
	gp.rtW        = winW;
	gp.rtH        = winH;
	for( int idx : gp.rts )
	{
		GLuint tex = idx < 0 ? windowColor : rts[ idx ].color;
		depth      = idx < 0 ? windowDepth : rts[ idx ].depth;
		if( idx >= 0 )
		{
			gp.rtW = rts[ idx ].w;
			gp.rtH = rts[ idx ].h;
		}
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + n, GL_TEXTURE_2D, tex, 0 );
		bufs[ n ] = GL_COLOR_ATTACHMENT0 + n;
		++n;
		if( n >= 8 )
			break;
	}
	// la profondeur est celle de la dernière cible (comme SHADERed)
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth, 0 );
	gp.depth = depth;
	glDrawBuffers( n, bufs );
}

bool Renderer::BuildPass( const Pass& p, GpuPass& gp, std::string& log )
{
	gp.pass = &p;
	std::vector< GLuint > shaders;
	auto stage = [ & ]( const std::string& path, Stage st, const std::string& entry, GLenum type, const char* label ) -> bool {
		StageResult r = CompileStage( path, st, entry, p.macros, proj->includePaths, p.gsUsed, p.tsUsed );
		if( !r.ok )
		{
			log += "[" + p.name + " " + label + " (" + LangName( r.lang ) + ")]\n" + r.log + "\n";
			return false;
		}
		if( st == Stage::Pixel || st == Stage::Compute )
		{
			gp.glslSamplers = r.lang == Lang::GLSL || r.lang == Lang::GLSLES;
			gp.samplers     = SamplerOrder( r.glsl );
		}
		GLuint s = Compile( type, r.glsl, p.name + " " + label, log );
		if( !s )
			return false;
		shaders.push_back( s );
		return true;
	};
	bool ok = true;
	if( p.kind == PassKind::Compute )
		ok = stage( p.csPath, Stage::Compute, p.csEntry, GL_COMPUTE_SHADER, "compute" );
	else
	{
		ok = stage( p.vsPath, Stage::Vertex, p.vsEntry, GL_VERTEX_SHADER, "vertex" ) && ok;
		ok = stage( p.psPath, Stage::Pixel, p.psEntry, GL_FRAGMENT_SHADER, "pixel" ) && ok;
		if( p.gsUsed && !p.gsPath.empty() )
			ok = stage( p.gsPath, Stage::Geometry, p.gsEntry, GL_GEOMETRY_SHADER, "geometry" ) && ok;
		if( p.tsUsed && !p.tcsPath.empty() && !p.tesPath.empty() )
		{
			ok = stage( p.tcsPath, Stage::TessControl, p.tcsEntry, GL_TESS_CONTROL_SHADER, "tessellation control" ) && ok;
			ok = stage( p.tesPath, Stage::TessEval, p.tesEntry, GL_TESS_EVALUATION_SHADER, "tessellation evaluation" ) && ok;
		}
	}
	if( !ok )
	{
		for( GLuint s : shaders )
			glDeleteShader( s );
		return false;
	}
	gp.program = Link( shaders, p.name, log );
	if( !gp.program )
		return false;

	for( auto& v : p.variables )
	{
		GpuVar gv;
		gv.var = &v;
		gv.loc = glGetUniformLocation( gp.program, v.name.c_str() );
		memcpy( gv.data, v.value, sizeof( gv.data ) );
		gp.vars.push_back( gv );
	}

	// cibles
	for( auto& name : p.renderTextures )
	{
		int idx = -1;
		for( size_t i = 0; i < rts.size(); ++i )
			if( rts[ i ].name == name )
				idx = (int)i;
		gp.rts.push_back( idx );
	}
	if( gp.rts.empty() )
		gp.rts.push_back( -1 );

	// géométries
	for( auto& it : p.items )
	{
		GpuItem gi;
		gi.item = &it;
		if( it.kind == ItemKind::Geometry )
		{
			gi.vao         = CreateGeometry( gi.vbo, it.geo, it.size, p.inputLayout );
			gi.vertexCount = GeoVertexCount[ (int)it.geo ];
		}
		else if( it.kind == ItemKind::Model )
		{
			std::vector< float > verts;
			if( LoadObj( it.modelPath, it.modelGroup, it.modelGroupOnly, verts ) )
			{
				glGenBuffers( 1, &gi.vbo );
				glBindBuffer( GL_ARRAY_BUFFER, gi.vbo );
				glBufferData( GL_ARRAY_BUFFER, verts.size() * sizeof( float ), verts.data(), GL_STATIC_DRAW );
				CreateVAO( gi.vao, gi.vbo, p.inputLayout );
				gi.vertexCount = (int)( verts.size() / 18 );
			}
			else
				proj->warnings.push_back( "Modèle introuvable ou non .obj : " + it.modelPath );
		}
		else if( it.kind == ItemKind::VertexBuffer )
		{
			for( auto& o : objects )
				if( o.name == it.vertexBuffer && o.buffer )
				{
					int stride = 0;
					for( int t : o.bufferFormat )
						stride += ValueTypeBytes( t );
					if( stride <= 0 )
						break;
					glGenVertexArrays( 1, &gi.vao );
					glBindVertexArray( gi.vao );
					glBindBuffer( GL_ARRAY_BUFFER, o.buffer );
					int off = 0;
					GLuint index = 0;
					for( int t : o.bufferFormat )
					{
						int comps = t >= 12 ? ( t - 10 ) : ( t % 4 ) + 1;
						glVertexAttribPointer( index, comps, GL_FLOAT, GL_FALSE, stride, (void*)(intptr_t)off );
						glEnableVertexAttribArray( index++ );
						off += ValueTypeBytes( t );
					}
					glBindVertexArray( 0 );
					gi.vertexCount = o.bufferSize / stride;
				}
		}
		// buffer d'instances
		if( !it.instanceBuffer.empty() && gi.vao )
			for( auto& o : objects )
				if( o.name == it.instanceBuffer && o.buffer )
				{
					glBindVertexArray( gi.vao );
					AddInstanceAttributes( (unsigned)p.inputLayout.size(), o.buffer, o.bufferFormat );
					glBindVertexArray( 0 );
				}
		gp.items.push_back( gi );
	}
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	return true;
}

bool Renderer::BuildGodotPass( const Pass& p, GpuPass& gp, std::string& log )
{
	gp.pass = &p;
	std::string vs = kCanvasVS, ps = kCanvasPS;
	GodotShader gd;
	if( !p.godotPath.empty() )
	{
		std::string src, err;
		if( !ReadFileText( p.godotPath, src ) )
		{
			log += "[" + p.name + "] shader Godot introuvable : " + p.godotPath + "\n";
			return false;
		}
		if( !TranscompileGodot( src, gd, err ) )
		{
			log += "[" + p.name + " (Godot)]\n" + err + "\n";
			return false;
		}
		vs = gd.vertex;
		ps = gd.fragment;
	}
	GLuint v = Compile( GL_VERTEX_SHADER, vs, p.name + " vertex (Godot)", log );
	GLuint f = Compile( GL_FRAGMENT_SHADER, ps, p.name + " fragment (Godot)", log );
	if( !v || !f )
	{
		if( v ) glDeleteShader( v );
		if( f ) glDeleteShader( f );
		return false;
	}
	gp.program = Link( { v, f }, p.name, log );
	if( !gp.program )
		return false;
	gp.gdProj          = glGetUniformLocation( gp.program, "PROJECTION_MATRIX" );
	gp.gdWorld         = glGetUniformLocation( gp.program, "WORLD_MATRIX" );
	gp.gdTime          = glGetUniformLocation( gp.program, "TIME" );
	gp.gdPixel         = glGetUniformLocation( gp.program, "SCREEN_PIXEL_SIZE" );
	gp.gdTexture       = glGetUniformLocation( gp.program, "TEXTURE" );
	gp.gdScreen        = glGetUniformLocation( gp.program, "SCREEN_TEXTURE" );
	gp.gdTexPixel      = glGetUniformLocation( gp.program, "color_texpixel_size" );
	gp.gdNormalTex     = glGetUniformLocation( gp.program, "normal_texture" );
	gp.gdBlend         = gd.blendMode;
	gp.gdScreenTexture = gd.screenTexture;

	int unit = 2;
	for( auto& u : gd.uniforms )
	{
		GpuPass::GdUniform gu;
		gu.name  = u.name;
		gu.loc   = glGetUniformLocation( gp.program, u.name.c_str() );
		gu.type  = u.isSampler ? 10 : ( u.isInt ? 1 : 0 );
		gu.count = u.components;
		gu.value = u.defaults;
		// valeurs enregistrées dans le projet
		for( auto& pu : p.godotUniforms )
			if( pu.name == u.name )
			{
				if( u.isSampler )
				{
					for( auto& o : objects )
						if( o.name == pu.texture )
							gu.tex = o.tex;
				}
				else if( !pu.value.empty() )
					gu.value = pu.value;
				gu.userParam = pu.userParam;
			}
		if( u.isSampler )
		{
			gu.unit = unit++;
			if( !gu.tex )
				gu.tex = u.hintBlack ? black : white;
		}
		gu.value.resize( std::max( 1, gu.count ), 0.0f );
		gp.gdUniforms.push_back( gu );
	}

	for( auto& name : p.renderTextures )
	{
		int idx = -1;
		for( size_t i = 0; i < rts.size(); ++i )
			if( rts[ i ].name == name )
				idx = (int)i;
		gp.rts.push_back( idx );
	}
	if( gp.rts.empty() )
		gp.rts.push_back( -1 );

	// sprites : 6 sommets (position, UV, couleur)
	for( auto& it : p.items )
	{
		if( it.kind != ItemKind::GodotSprite )
			continue;
		GpuItem gi;
		gi.item = &it;
		float w = it.size[ 0 ], h = it.size[ 1 ];
		float c[ 4 ] = { it.spriteColor[ 0 ], it.spriteColor[ 1 ], it.spriteColor[ 2 ], it.spriteColor[ 3 ] };
		float uv[ 6 ][ 2 ] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };
		float ps2[ 6 ][ 2 ] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
		std::vector< float > data;
		for( int i = 0; i < 6; ++i )
		{
			float u = uv[ i ][ 0 ], vv = uv[ i ][ 1 ];
			if( it.flipH ) u = 1.0f - u;
			if( it.flipV ) vv = 1.0f - vv;
			float vert[ 9 ] = { ps2[ i ][ 0 ] * ( w > 0 ? w : 1 ), ps2[ i ][ 1 ] * ( h > 0 ? h : 1 ), 0, u, vv, c[ 0 ], c[ 1 ], c[ 2 ], c[ 3 ] };
			data.insert( data.end(), vert, vert + 9 );
		}
		glGenVertexArrays( 1, &gi.vao );
		glBindVertexArray( gi.vao );
		glGenBuffers( 1, &gi.vbo );
		glBindBuffer( GL_ARRAY_BUFFER, gi.vbo );
		glBufferData( GL_ARRAY_BUFFER, data.size() * sizeof( float ), data.data(), GL_STATIC_DRAW );
		glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 9 * sizeof( float ), (void*)0 );
		glEnableVertexAttribArray( 0 );
		glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, 9 * sizeof( float ), (void*)( 3 * sizeof( float ) ) );
		glEnableVertexAttribArray( 1 );
		glVertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof( float ), (void*)( 5 * sizeof( float ) ) );
		glEnableVertexAttribArray( 2 );
		glBindVertexArray( 0 );
		gi.vertexCount = 6;
		for( auto& o : objects )
			if( !it.spriteTexture.empty() && o.name == it.spriteTexture )
				gi.spriteTex = o.tex;
		gp.items.push_back( gi );
	}
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	return true;
}

bool Renderer::Build( const Project& project, std::string& log )
{
	// appelé sur une instance neuve : le propriétaire garde l'ancienne tant que la nouvelle ne compile pas
	DestroyAll();
	proj = std::make_shared< Project >( project );
	CreateObjects();
	bool ok = true;
	for( auto& p : proj->passes )
	{
		if( !p.active )
			continue;
		GpuPass gp;
		bool built = false;
		if( p.kind == PassKind::Shader || p.kind == PassKind::Compute )
			built = BuildPass( p, gp, log );
		else if( p.kind == PassKind::Godot )
			built = BuildGodotPass( p, gp, log );
		else
			continue;// audio / greffons : ignorés (avertissement déjà émis)
		ok = ok && built;
		passes.push_back( std::move( gp ) );
	}
	if( !ok )
	{
		DestroyAll();
		proj.reset();
		return false;
	}
	winW = winH = 0;// cibles créées au premier rendu
	return true;
}

GLuint Renderer::ObjectTexture( const std::string& name, GLenum& target, const Frame& f )
{
	target = GL_TEXTURE_2D;
	for( auto& r : rts )
		if( r.name == name )
			return r.color;
	for( auto& o : objects )
		if( o.name == name )
		{
			target = o.target;
			if( o.kind == ObjectKind::KeyboardTexture )
				return keyboard;
			if( o.kind == ObjectKind::AudioTexture )
				return f.audioTex ? f.audioTex : black;
			if( o.replaceIndex >= 0 )
			{
				switch( f.texMode[ o.replaceIndex ] )
				{
				case 1: return f.inputTex ? f.inputTex : black;
				case 2: return f.texImage[ o.replaceIndex ] ? f.texImage[ o.replaceIndex ] : o.tex;
				case 3: return f.audioTex ? f.audioTex : black;
				case 4: return black;
				default: break;
				}
			}
			return o.tex;
		}
	return black;
}

void Renderer::BindTextures( GpuPass& gp, const Frame& f )
{
	auto& list = gp.pass->textures;
	for( size_t j = 0; j < list.size() && j < 32; ++j )
	{
		GLenum target = GL_TEXTURE_2D;
		GLuint tex    = list[ j ].empty() ? black : ObjectTexture( list[ j ], target, f );
		glActiveTexture( GL_TEXTURE0 + (GLenum)j );
		glBindTexture( target, tex );
		glBindSampler( (GLuint)j, 0 );
		if( gp.glslSamplers && j < gp.samplers.size() )
			glUniform1i( glGetUniformLocation( gp.program, gp.samplers[ j ].c_str() ), (GLint)j );
	}
	// buffers (SSBO) et images (passes compute)
	auto& bl = gp.pass->buffers;
	for( size_t j = 0; j < bl.size() && j < 16; ++j )
		for( auto& o : objects )
			if( o.name == bl[ j ] )
			{
				if( o.kind == ObjectKind::Buffer )
					glBindBufferBase( GL_SHADER_STORAGE_BUFFER, (GLuint)j, o.buffer );
				else if( o.kind == ObjectKind::Image || o.kind == ObjectKind::Image3D )
					glBindImageTexture( (GLuint)j, o.tex, 0, o.kind == ObjectKind::Image3D, 0, GL_READ_WRITE, o.format );
			}
	glActiveTexture( GL_TEXTURE0 );
}

void Renderer::ComputeVar( GpuPass& gp, GpuVar& gv, const GpuItem* item, const Frame& f )
{
	const Variable& v = *gv.var;
	float* d          = gv.data;
	glm::vec2 vp( (float)gp.rtW, (float)gp.rtH );
	if( v.lastFrame )
		vp = glm::vec2( prevViewport[ 0 ], prevViewport[ 1 ] );
	glm::vec3 camPos, camDir;
	glm::mat4 view = ViewMatrix( *proj, &camPos, &camDir );
	glm::mat4 persp = glm::perspective( glm::radians( 45.0f ), vp.x / std::max( vp.y, 1.0f ), 0.1f, 1000.0f );
	glm::mat4 ortho = glm::ortho( 0.0f, vp.x, vp.y, 0.0f, 0.1f, 1000.0f );
	auto putMat     = [ & ]( const glm::mat4& m ) { memcpy( d, glm::value_ptr( m ), 16 * sizeof( float ) ); };
	// souris SHADERed : position normalisée (0..1) dans la fenêtre d'aperçu
	float mx = f.width > 0 ? f.mouse[ 0 ] / (float)f.width : 0.0f;
	float my = f.height > 0 ? f.mouse[ 1 ] / (float)f.height : 0.0f;
	bool down = f.mouse[ 2 ] > 0.0f;

	switch( v.system )
	{
	case SystemVar::Time: d[ 0 ] = f.time; break;
	case SystemVar::TimeDelta: d[ 0 ] = f.timeDelta; break;
	case SystemVar::FrameIndex: { int fi = v.lastFrame ? std::max( 0, f.frame - 1 ) : f.frame; memcpy( d, &fi, 4 ); } break;
	case SystemVar::ViewportSize: d[ 0 ] = vp.x; d[ 1 ] = vp.y; break;
	case SystemVar::MousePosition: d[ 0 ] = mx; d[ 1 ] = my; break;
	case SystemVar::Mouse: d[ 0 ] = mx; d[ 1 ] = my; d[ 2 ] = down ? 1.0f : 0.0f; d[ 3 ] = 0; break;
	case SystemVar::MouseButton: d[ 0 ] = mx; d[ 1 ] = my; d[ 2 ] = down ? 1.0f : 0.0f; d[ 3 ] = 0; break;
	case SystemVar::View: putMat( view ); break;
	case SystemVar::Projection: putMat( persp ); break;
	case SystemVar::ViewProjection: putMat( persp * view ); break;
	case SystemVar::Orthographic: putMat( ortho ); break;
	case SystemVar::ViewOrthographic: putMat( ortho * view ); break;
	case SystemVar::GeometryTransform:
	{
		auto& store = v.lastFrame ? prevGeoTransforms : geoTransforms;
		std::string key = gp.pass->name + "/" + ( item && item->item ? item->item->name : "" );
		auto it = store.find( key );
		if( it != store.end() && it->second.size() == 16 )
			memcpy( d, it->second.data(), 64 );
		else
			putMat( glm::mat4( 1.0f ) );
		break;
	}
	case SystemVar::IsPicked: case SystemVar::IsSavingToFile: d[ 0 ] = 0; break;
	case SystemVar::CameraPosition: d[ 0 ] = camPos.x; d[ 1 ] = camPos.y; d[ 2 ] = camPos.z; d[ 3 ] = 1; break;
	case SystemVar::CameraPosition3: d[ 0 ] = camPos.x; d[ 1 ] = camPos.y; d[ 2 ] = camPos.z; break;
	case SystemVar::CameraDirection3: d[ 0 ] = camDir.x; d[ 1 ] = camDir.y; d[ 2 ] = camDir.z; break;
	case SystemVar::KeysWASD: memset( d, 0, 16 ); break;
	case SystemVar::PickPosition: d[ 0 ] = d[ 1 ] = d[ 2 ] = 0; break;
	case SystemVar::None:
	{
		memcpy( d, v.value, sizeof( v.value ) );
		// valeur propre à l'item (itemvalues)
		if( item && item->item )
			for( auto& iv : gp.pass->itemValues )
				if( iv.variable == v.name && iv.item == item->item->name )
					memcpy( d, iv.value, sizeof( iv.value ) );
		// slider Resolume
		if( v.userParam >= 0 && f.user )
		{
			int n = std::min( ValueCount( v.type ), 4 );
			for( int c = 0; c < n; ++c )
				if( v.userParam + c < f.userCount )
					d[ c ] = f.user[ v.userParam + c ];
		}
		const float* a = v.value;
		switch( v.func )
		{
		case FunctionVar::Pointer:
			for( auto& o : gp.vars )
				if( o.var->name == v.args[ 0 ] && &o != &gv )
					memcpy( d, o.data, 64 );
			break;
		case FunctionVar::CameraSnapshot:
		{
			auto it = proj->cameraSnapshots.find( v.args[ 0 ] );
			if( it != proj->cameraSnapshots.end() )
				memcpy( d, it->second.data(), 64 );
			break;
		}
		case FunctionVar::ObjectProperty:
		{
			glm::vec4 val( 0.0f );
			for( auto& it : gp.pass->items )
				if( it.name == v.args[ 0 ] )
				{
					if( v.args[ 1 ] == "Position" ) val = glm::vec4( it.position[ 0 ], it.position[ 1 ], it.position[ 2 ], 1 );
					else if( v.args[ 1 ] == "Scale" ) val = glm::vec4( it.scale[ 0 ], it.scale[ 1 ], it.scale[ 2 ], 0 );
					else if( v.args[ 1 ] == "Rotation" ) val = glm::vec4( it.rotation[ 0 ], it.rotation[ 1 ], it.rotation[ 2 ], 0 );
				}
			memcpy( d, glm::value_ptr( val ), 16 );
			break;
		}
		case FunctionVar::MatrixIdentity: putMat( glm::mat4( 1.0f ) ); break;
		case FunctionVar::MatrixLookAtLH: putMat( glm::lookAt( glm::make_vec3( a ), glm::make_vec3( a + 3 ), glm::make_vec3( a + 6 ) ) ); break;
		case FunctionVar::MatrixLookToLH: putMat( glm::lookAt( glm::make_vec3( a ), glm::make_vec3( a ) + glm::make_vec3( a + 3 ), glm::make_vec3( a + 6 ) ) ); break;
		case FunctionVar::MatrixOrthographicLH: putMat( glm::ortho( 0.0f, a[ 0 ], 0.0f, a[ 1 ], a[ 2 ], a[ 3 ] ) ); break;
		case FunctionVar::MatrixPerspectiveFovLH: putMat( glm::perspective( a[ 0 ], a[ 1 ], a[ 2 ], a[ 3 ] ) ); break;
		case FunctionVar::MatrixPerspectiveLH: putMat( glm::perspectiveFov( 45.0f, std::max( a[ 0 ], 1.0f ), std::max( a[ 1 ], 1.0f ), a[ 2 ], a[ 3 ] ) ); break;
		case FunctionVar::MatrixRotationAxis:
		case FunctionVar::MatrixRotationNormal: putMat( glm::rotate( glm::mat4( 1 ), glm::radians( a[ 3 ] ), glm::make_vec3( a ) ) ); break;
		case FunctionVar::MatrixRotationRollPitchYaw: putMat( glm::yawPitchRoll( glm::radians( a[ 2 ] ), glm::radians( a[ 1 ] ), glm::radians( a[ 0 ] ) ) ); break;
		case FunctionVar::MatrixRotationX: putMat( glm::rotate( glm::mat4( 1 ), glm::radians( a[ 0 ] ), glm::vec3( 1, 0, 0 ) ) ); break;
		case FunctionVar::MatrixRotationY: putMat( glm::rotate( glm::mat4( 1 ), glm::radians( a[ 0 ] ), glm::vec3( 0, 1, 0 ) ) ); break;
		case FunctionVar::MatrixRotationZ: putMat( glm::rotate( glm::mat4( 1 ), glm::radians( a[ 0 ] ), glm::vec3( 0, 0, 1 ) ) ); break;
		case FunctionVar::MatrixScaling: putMat( glm::scale( glm::mat4( 1 ), glm::make_vec3( a ) ) ); break;
		case FunctionVar::MatrixTranslation: putMat( glm::translate( glm::mat4( 1 ), glm::make_vec3( a ) ) ); break;
		case FunctionVar::ScalarCos: d[ 0 ] = std::cos( glm::radians( a[ 0 ] ) ); break;
		case FunctionVar::ScalarSin: d[ 0 ] = std::sin( glm::radians( a[ 0 ] ) ); break;
		case FunctionVar::VectorNormalize:
		{
			int n = ValueCount( v.type );
			float len = 0;
			for( int i = 0; i < n && i < 4; ++i ) len += a[ i ] * a[ i ];
			len = std::sqrt( len );
			for( int i = 0; i < n && i < 4; ++i ) d[ i ] = len > 0 ? a[ i ] / len : 0;
			break;
		}
		default: break;
		}
		break;
	}
	default: break;
	}

	if( v.invert )
	{
		if( v.type == ValueType::Float4x4 ) putMat( glm::inverse( glm::make_mat4( d ) ) );
		else if( v.type == ValueType::Float3x3 ) { glm::mat3 m = glm::inverse( glm::make_mat3( d ) ); memcpy( d, glm::value_ptr( m ), 36 ); }
		else if( v.type == ValueType::Float2x2 ) { glm::mat2 m = glm::inverse( glm::make_mat2( d ) ); memcpy( d, glm::value_ptr( m ), 16 ); }
	}
}

void Renderer::UploadVar( const GpuVar& gv )
{
	if( gv.loc < 0 )
		return;
	const float* d = gv.data;
	int iv[ 4 ];
	switch( gv.var->type )
	{
	case ValueType::Bool1: case ValueType::Bool2: case ValueType::Bool3: case ValueType::Bool4:
	case ValueType::Int1: case ValueType::Int2: case ValueType::Int3: case ValueType::Int4:
	{
		int n = ValueCount( gv.var->type );
		for( int i = 0; i < n; ++i )
			iv[ i ] = gv.var->system == SystemVar::FrameIndex ? *(const int*)d : (int)std::lround( d[ i ] );
		if( gv.var->system == SystemVar::FrameIndex )
		{
			// FrameIndex est un uint dans SHADERed : selon le shader, uniform int ou uint
			glUniform1i( gv.loc, iv[ 0 ] );
			if( glGetError() != GL_NO_ERROR )
				glUniform1ui( gv.loc, (GLuint)iv[ 0 ] );
			break;
		}
		if( n == 1 ) glUniform1i( gv.loc, iv[ 0 ] );
		else if( n == 2 ) glUniform2iv( gv.loc, 1, iv );
		else if( n == 3 ) glUniform3iv( gv.loc, 1, iv );
		else glUniform4iv( gv.loc, 1, iv );
		break;
	}
	case ValueType::Float1: glUniform1f( gv.loc, d[ 0 ] ); break;
	case ValueType::Float2: glUniform2fv( gv.loc, 1, d ); break;
	case ValueType::Float3: glUniform3fv( gv.loc, 1, d ); break;
	case ValueType::Float4: glUniform4fv( gv.loc, 1, d ); break;
	case ValueType::Float2x2: glUniformMatrix2fv( gv.loc, 1, GL_FALSE, d ); break;
	case ValueType::Float3x3: glUniformMatrix3fv( gv.loc, 1, GL_FALSE, d ); break;
	case ValueType::Float4x4: glUniformMatrix4fv( gv.loc, 1, GL_FALSE, d ); break;
	default: break;
	}
}

static void DefaultState()
{
	glDisable( GL_DEPTH_CLAMP );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glEnable( GL_CULL_FACE );
	glCullFace( GL_BACK );
	glFrontFace( GL_CCW );
	glDisable( GL_BLEND );
	glEnable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDepthFunc( GL_LESS );
	glDisable( GL_STENCIL_TEST );
}

static void ApplyState( const RenderState& s )
{
	if( s.depthClamp ) glEnable( GL_DEPTH_CLAMP ); else glDisable( GL_DEPTH_CLAMP );
	glPolygonMode( GL_FRONT_AND_BACK, s.wireframe ? GL_LINE : GL_FILL );
	if( s.cull ) glEnable( GL_CULL_FACE ); else glDisable( GL_CULL_FACE );
	glCullFace( s.cullFront ? GL_FRONT : GL_BACK );
	glFrontFace( s.ccw ? GL_CCW : GL_CW );
	if( s.blend )
	{
		glEnable( GL_BLEND );
		glBlendEquationSeparate( s.opRGB, s.opA );
		glBlendFuncSeparate( s.srcRGB, s.dstRGB, s.srcA, s.dstA );
		glBlendColor( s.blendFactor[ 0 ], s.blendFactor[ 1 ], s.blendFactor[ 2 ], s.blendFactor[ 3 ] );
	}
	else
		glDisable( GL_BLEND );
	if( s.depthTest ) glEnable( GL_DEPTH_TEST ); else glDisable( GL_DEPTH_TEST );
	glDepthMask( s.depthMask ? GL_TRUE : GL_FALSE );
	glDepthFunc( s.depthFunc );
	glPolygonOffset( 0.0f, s.depthBias );
	if( s.stencilTest )
	{
		glEnable( GL_STENCIL_TEST );
		glStencilFuncSeparate( GL_FRONT, s.stFrontFunc, 1, s.stencilRef );
		glStencilFuncSeparate( GL_BACK, s.stBackFunc, 1, s.stencilRef );
		glStencilMask( s.stencilMask );
		glStencilOpSeparate( GL_FRONT, s.stFrontFail, s.stFrontDepthFail, s.stFrontPass );
		glStencilOpSeparate( GL_BACK, s.stBackFail, s.stBackDepthFail, s.stBackPass );
	}
	else
		glDisable( GL_STENCIL_TEST );
}

GLuint Renderer::FlippedInput( const Frame& f )
{
	if( !f.inputTex )
		return white;
	int w = std::max( 1, f.inputW ), h = std::max( 1, f.inputH );
	if( flipFrame == f.frame && flipTex && flipW == w && flipH == h )
		return flipTex;
	GLint prevFbo = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &prevFbo );
	if( !flipTex || flipW != w || flipH != h )
	{
		if( !flipTex )
			flipTex = MakeTex( GL_TEXTURE_2D );
		glBindTexture( GL_TEXTURE_2D, flipTex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glBindTexture( GL_TEXTURE_2D, 0 );
		if( !flipFbo )
			glGenFramebuffers( 1, &flipFbo );
		glBindFramebuffer( GL_FRAMEBUFFER, flipFbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, flipTex, 0 );
		flipW = w;
		flipH = h;
	}
	if( !flipReadFbo )
		glGenFramebuffers( 1, &flipReadFbo );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, flipReadFbo );
	glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f.inputTex, 0 );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, flipFbo );
	glBlitFramebuffer( 0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR );
	glBindFramebuffer( GL_FRAMEBUFFER, (GLuint)prevFbo );
	flipFrame = f.frame;
	return flipTex;
}

void Renderer::DrawGodot( GpuPass& gp, const Frame& f )
{
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glUseProgram( gp.program );
	float w = (float)gp.rtW, h = (float)gp.rtH;
	glm::mat4 projM = glm::ortho( 0.0f, w, h, 0.0f, 0.1f, 1000.0f );
	glUniformMatrix4fv( gp.gdProj, 1, GL_FALSE, glm::value_ptr( projM ) );
	if( gp.gdTime >= 0 ) glUniform1f( gp.gdTime, f.time );
	if( gp.gdPixel >= 0 ) glUniform2f( gp.gdPixel, 1.0f / std::max( w, 1.0f ), 1.0f / std::max( h, 1.0f ) );
	if( gp.gdTexture >= 0 ) glUniform1i( gp.gdTexture, 0 );
	if( gp.gdScreenTexture && gp.gdScreen >= 0 )
	{
		// SCREEN_TEXTURE : copie de ce qui est déjà dessiné
		if( !screenCopy || screenW != gp.rtW || screenH != gp.rtH )
		{
			if( !screenCopy )
				screenCopy = MakeTex( GL_TEXTURE_2D );
			glBindTexture( GL_TEXTURE_2D, screenCopy );
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, gp.rtW, gp.rtH, 0, GL_RGBA, GL_FLOAT, nullptr );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			if( !screenCopyFbo )
				glGenFramebuffers( 1, &screenCopyFbo );
			glBindFramebuffer( GL_FRAMEBUFFER, screenCopyFbo );
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenCopy, 0 );
			screenW = gp.rtW;
			screenH = gp.rtH;
		}
		glBindFramebuffer( GL_READ_FRAMEBUFFER, gp.fbo );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, screenCopyFbo );
		glBlitFramebuffer( 0, 0, gp.rtW, gp.rtH, 0, 0, gp.rtW, gp.rtH, GL_COLOR_BUFFER_BIT, GL_NEAREST );
		glBindFramebuffer( GL_FRAMEBUFFER, gp.fbo );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, screenCopy );
		glUniform1i( gp.gdScreen, 1 );
	}
	for( auto& u : gp.gdUniforms )
	{
		if( u.loc < 0 )
			continue;
		if( u.userParam >= 0 && f.user )
			for( int c = 0; c < u.count && c < 4; ++c )
				if( u.userParam + c < f.userCount )
					u.value[ c ] = f.user[ u.userParam + c ];
		const float* v = u.value.data();
		if( u.type == 10 )
		{
			glActiveTexture( GL_TEXTURE0 + u.unit );
			glBindTexture( GL_TEXTURE_2D, u.tex );
			glUniform1i( u.loc, u.unit );
		}
		else if( u.type == 1 )
		{
			int iv[ 4 ] = { (int)v[ 0 ], u.count > 1 ? (int)v[ 1 ] : 0, u.count > 2 ? (int)v[ 2 ] : 0, u.count > 3 ? (int)v[ 3 ] : 0 };
			if( u.count == 1 ) glUniform1i( u.loc, iv[ 0 ] );
			else if( u.count == 2 ) glUniform2iv( u.loc, 1, iv );
			else if( u.count == 3 ) glUniform3iv( u.loc, 1, iv );
			else glUniform4iv( u.loc, 1, iv );
		}
		else if( u.count == 1 ) glUniform1f( u.loc, v[ 0 ] );
		else if( u.count == 2 ) glUniform2fv( u.loc, 1, v );
		else if( u.count == 3 ) glUniform3fv( u.loc, 1, v );
		else if( u.count == 4 ) glUniform4fv( u.loc, 1, v );
		else if( u.count == 9 ) glUniformMatrix3fv( u.loc, 1, GL_FALSE, v );
		else if( u.count == 16 ) glUniformMatrix4fv( u.loc, 1, GL_FALSE, v );
	}
	// modes de fusion canvas_item
	switch( gp.gdBlend )
	{
	case 5: glDisable( GL_BLEND ); break;// disabled
	case 1: glEnable( GL_BLEND ); glBlendEquation( GL_FUNC_ADD ); glBlendFunc( GL_ONE, GL_ONE ); break;
	case 2: glEnable( GL_BLEND ); glBlendEquation( GL_FUNC_REVERSE_SUBTRACT ); glBlendFunc( GL_SRC_ALPHA, GL_ONE ); break;
	case 3: glEnable( GL_BLEND ); glBlendEquation( GL_FUNC_ADD ); glBlendFuncSeparate( GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE ); break;
	case 4: glEnable( GL_BLEND ); glBlendEquation( GL_FUNC_ADD ); glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA ); break;
	default: glEnable( GL_BLEND ); glBlendEquation( GL_FUNC_ADD ); glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA ); break;
	}
	for( auto& gi : gp.items )
	{
		const Item& it = *gi.item;
		if( !it.visible || !gi.vao )
			continue;
		// sprite plein écran (effet Godot autonome) : taille <= 0
		float sw = it.size[ 0 ] > 0 ? it.size[ 0 ] : w;
		float sh = it.size[ 1 ] > 0 ? it.size[ 1 ] : h;
		glm::mat4 world = glm::translate( glm::mat4( 1 ), glm::vec3( it.position[ 0 ] + sw / 2, it.position[ 1 ] + sh / 2, -1000.0f + 0.5f ) ) *
						  glm::rotate( glm::mat4( 1.0f ), glm::radians( it.spriteRotationDeg ), glm::vec3( 0, 0, 1 ) );
		if( it.size[ 0 ] <= 0 || it.size[ 1 ] <= 0 )
			world = world * glm::scale( glm::mat4( 1 ), glm::vec3( sw, sh, 1 ) );
		glUniformMatrix4fv( gp.gdWorld, 1, GL_FALSE, glm::value_ptr( world ) );
		GLuint tex = gi.spriteTex;
		if( !tex )
			tex = f.inputTex ? FlippedInput( f ) : white;
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, tex );
		if( gp.gdTexPixel >= 0 )
		{
			GLint tw = 1, th = 1;
			glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw );
			glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th );
			glUniform2f( gp.gdTexPixel, 1.0f / std::max( tw, 1 ), 1.0f / std::max( th, 1 ) );
		}
		if( gp.gdNormalTex >= 0 )
		{
			glActiveTexture( GL_TEXTURE0 + 31 );
			glBindTexture( GL_TEXTURE_2D, black );
			glUniform1i( gp.gdNormalTex, 31 );
			glActiveTexture( GL_TEXTURE0 );
		}
		glBindVertexArray( gi.vao );
		glDrawArrays( GL_TRIANGLES, 0, gi.vertexCount );
	}
	glBindVertexArray( 0 );
	glDisable( GL_BLEND );
}

void Renderer::Render( const Frame& f )
{
	if( !proj )
		return;
	ResizeTargets( std::max( 1, f.width ), std::max( 1, f.height ) );

	GLuint prevTex[ 8 ] = {};
	GLuint prevDepth    = 0;
	bool clearedWindow  = false;
	std::map< std::string, std::vector< float > > newTransforms;

	for( auto& gp : passes )
	{
		if( !gp.program )
			continue;
		const Pass& p = *gp.pass;

		if( p.kind == PassKind::Compute )
		{
			glUseProgram( gp.program );
			BindTextures( gp, f );
			for( auto& v : gp.vars )
			{
				ComputeVar( gp, v, nullptr, f );
				UploadVar( v );
			}
			glDispatchCompute( p.groupSize[ 0 ], p.groupSize[ 1 ], p.groupSize[ 2 ] );
			glMemoryBarrier( GL_ALL_BARRIER_BITS );
			continue;
		}

		if( p.kind == PassKind::Shader && p.items.empty() )
			continue;

		glBindFramebuffer( GL_FRAMEBUFFER, gp.fbo );
		// profondeur : effacée une fois par cible
		if( gp.depth != prevDepth )
		{
			if( gp.depth != windowDepth || !clearedWindow )
			{
				glStencilMask( 0xFFFFFFFF );
				glDepthMask( GL_TRUE );
				glClearBufferfi( GL_DEPTH_STENCIL, 0, 1.0f, 0 );
			}
			prevDepth = gp.depth;
		}
		for( size_t i = 0; i < gp.rts.size() && i < 8; ++i )
		{
			int idx    = gp.rts[ i ];
			GLuint tex = idx < 0 ? windowColor : rts[ idx ].color;
			if( idx >= 0 )
			{
				bool usedPrev = false;
				for( GLuint t : prevTex )
					usedPrev = usedPrev || t == tex;
				if( !usedPrev && rts[ idx ].obj->clear )
					glClearBufferfv( GL_COLOR, (GLint)i, rts[ idx ].obj->clearColor );
			}
			else if( !clearedWindow )
			{
				glClearBufferfv( GL_COLOR, (GLint)i, proj->clearColor );
				clearedWindow = true;
			}
		}
		memset( prevTex, 0, sizeof( prevTex ) );
		for( size_t i = 0; i < gp.rts.size() && i < 8; ++i )
			prevTex[ i ] = gp.rts[ i ] < 0 ? windowColor : rts[ gp.rts[ i ] ].color;

		glViewport( 0, 0, gp.rtW, gp.rtH );

		if( p.kind == PassKind::Godot )
		{
			DrawGodot( gp, f );
			continue;
		}

		glUseProgram( gp.program );
		BindTextures( gp, f );
		DefaultState();
		if( p.tsUsed )
			glPatchParameteri( GL_PATCH_VERTICES, p.patchVerts );

		for( auto& gi : gp.items )
		{
			const Item& it = *gi.item;
			if( it.kind == ItemKind::RenderState )
			{
				ApplyState( it.state );
				continue;
			}
			if( !gi.vao || gi.vertexCount <= 0 )
				continue;
			glm::mat4 xf;
			glm::vec3 rot( it.rotation[ 0 ], it.rotation[ 1 ], it.rotation[ 2 ] );
			if( it.kind == ItemKind::Geometry && it.geo == GeoType::Rectangle )
			{
				glm::vec3 scaleRect( it.scale[ 0 ] * gp.rtW, it.scale[ 1 ] * gp.rtH, 1.0f );
				glm::vec3 posRect( ( it.position[ 0 ] + 0.5f ) * gp.rtW, ( it.position[ 1 ] + 0.5f ) * gp.rtH, -1000.0f );
				xf = glm::translate( glm::mat4( 1 ), posRect ) * glm::yawPitchRoll( rot.y, rot.x, rot.z ) * glm::scale( glm::mat4( 1 ), scaleRect );
			}
			else
				xf = glm::translate( glm::mat4( 1 ), glm::make_vec3( it.position ) ) * glm::yawPitchRoll( rot.y, rot.x, rot.z ) *
					 glm::scale( glm::mat4( 1 ), glm::make_vec3( it.scale ) );
			std::string key = p.name + "/" + it.name;
			geoTransforms[ key ].assign( glm::value_ptr( xf ), glm::value_ptr( xf ) + 16 );
			newTransforms[ key ] = geoTransforms[ key ];

			for( auto& v : gp.vars )
			{
				ComputeVar( gp, v, &gi, f );
				UploadVar( v );
			}
			glBindVertexArray( gi.vao );
			GLenum topo = p.tsUsed ? GL_PATCHES : it.topology;
			if( it.instanced && it.instanceCount > 0 )
				glDrawArraysInstanced( topo, 0, gi.vertexCount, it.instanceCount );
			else
				glDrawArrays( topo, 0, gi.vertexCount );
		}
		glBindVertexArray( 0 );
	}

	prevGeoTransforms = newTransforms;
	prevViewport[ 0 ] = (float)winW;
	prevViewport[ 1 ] = (float)winH;

	// état par défaut pour l'hôte
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_DEPTH_CLAMP );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glDepthMask( GL_TRUE );
	glBlendEquation( GL_FUNC_ADD );
	glBlendFunc( GL_ONE, GL_ZERO );
	glUseProgram( 0 );
	for( int i = 7; i >= 0; --i )
	{
		glActiveTexture( GL_TEXTURE0 + i );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glBindTexture( GL_TEXTURE_CUBE_MAP, 0 );
		glBindTexture( GL_TEXTURE_3D, 0 );
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}
}//namespace sed
}//namespace stfx
