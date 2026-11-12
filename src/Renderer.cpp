#include "Renderer.h"
#include "GlslGen.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <regex>

namespace stfx
{
static const int kAudioSize = 512;

GLuint Renderer::Compile( GLenum type, const std::string& src, std::string& log )
{
	GLuint sh       = glCreateShader( type );
	const char* p   = src.c_str();
	glShaderSource( sh, 1, &p, nullptr );
	glCompileShader( sh );
	GLint ok = 0;
	glGetShaderiv( sh, GL_COMPILE_STATUS, &ok );
	if( !ok )
	{
		GLint len = 0;
		glGetShaderiv( sh, GL_INFO_LOG_LENGTH, &len );
		std::string l( std::max( len, 1 ), '\0' );
		glGetShaderInfoLog( sh, len, nullptr, &l[ 0 ] );
		log += l.c_str();
		glDeleteShader( sh );
		return 0;
	}
	return sh;
}

static GLuint MakeTex( GLenum target )
{
	GLuint t = 0;
	glGenTextures( 1, &t );
	glBindTexture( target, t );
	glTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
	return t;
}

bool Renderer::InitGL()
{
	if( !quad.Initialise() )
		return false;
	std::string log;
	vertexShader = Compile( GL_VERTEX_SHADER, kVertexSource, log );

	// version GLSL disponible : "4.60 NVIDIA" -> 460
	glslVersion = 410;
	if( const char* v = (const char*)glGetString( GL_SHADING_LANGUAGE_VERSION ) )
	{
		int major = 0, minor = 0;
		if( sscanf( v, "%d.%d", &major, &minor ) == 2 && major * 100 + minor >= 420 )
			glslVersion = 420;
	}
	if( !vertexShader )
		return false;

	GLint prevUnpack = 4;
	glGetIntegerv( GL_UNPACK_ALIGNMENT, &prevUnpack );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	const uint8_t zero[ 4 ] = { 0, 0, 0, 0 };

	black2D = MakeTex( GL_TEXTURE_2D );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero );

	blackCube = MakeTex( GL_TEXTURE_CUBE_MAP );
	for( int f = 0; f < 6; ++f )
		glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero );

	black3D = MakeTex( GL_TEXTURE_3D );
	glTexImage3D( GL_TEXTURE_3D, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero );

	// texture audio Shadertoy : 512x2, ligne 0 = FFT, ligne 1 = forme d'onde
	audioRows.assign( kAudioSize * 2, 0.0f );
	std::fill( audioRows.begin() + kAudioSize, audioRows.end(), 0.5f );
	audioTex = MakeTex( GL_TEXTURE_2D );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R32F, kAudioSize, 2, 0, GL_RED, GL_FLOAT, audioRows.data() );

	std::vector< uint8_t > kb( 256 * 3, 0 );
	keyboardTex = MakeTex( GL_TEXTURE_2D );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, 256, 3, 0, GL_RED, GL_UNSIGNED_BYTE, kb.data() );

	glBindTexture( GL_TEXTURE_2D, 0 );
	glBindTexture( GL_TEXTURE_CUBE_MAP, 0 );
	glBindTexture( GL_TEXTURE_3D, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, prevUnpack );

	// samplers des canaux remplacés (entrée/audio : linéaire ; image : mipmaps, répétition)
	glGenSamplers( 1, &linearSampler );
	glSamplerParameteri( linearSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glSamplerParameteri( linearSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glSamplerParameteri( linearSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glSamplerParameteri( linearSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	// programme de copie de l'entrée
	{
		static const char* fsCopy = R"(#version 410 core
uniform sampler2D uTex;
uniform vec2 uMaxUV;  // Width/HardwareWidth, Height/HardwareHeight
uniform vec4 uFit;    // échelle xy (autour du centre)
uniform vec2 uSize;   // taille de sortie
uniform vec2 uHalf;   // demi-texel : évite de filtrer la marge matérielle au bord
out vec4 o;
void main()
{
	vec2 uv = gl_FragCoord.xy / uSize;
	vec2 s  = ( uv - 0.5 ) * uFit.xy + 0.5;
	if( any( lessThan( s, vec2( 0.0 ) ) ) || any( greaterThan( s, vec2( 1.0 ) ) ) )
		o = vec4( 0.0 );
	else
		o = texture( uTex, clamp( s * uMaxUV, uHalf, uMaxUV - uHalf ) );
}
)";
		std::string clog;
		GLuint fs = Compile( GL_FRAGMENT_SHADER, fsCopy, clog );
		if( fs )
		{
			copyProgram = glCreateProgram();
			glAttachShader( copyProgram, vertexShader );
			glAttachShader( copyProgram, fs );
			glLinkProgram( copyProgram );
			glDetachShader( copyProgram, vertexShader );
			glDetachShader( copyProgram, fs );
			glDeleteShader( fs );
			uCopyTex   = glGetUniformLocation( copyProgram, "uTex" );
			uCopyMaxUV = glGetUniformLocation( copyProgram, "uMaxUV" );
			uCopyFit   = glGetUniformLocation( copyProgram, "uFit" );
			uCopySize  = glGetUniformLocation( copyProgram, "uSize" );
			uCopyHalf  = glGetUniformLocation( copyProgram, "uHalf" );
		}
	}

	glGenSamplers( 1, &mipSampler );
	glSamplerParameteri( mipSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
	glSamplerParameteri( mipSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glSamplerParameteri( mipSampler, GL_TEXTURE_WRAP_S, GL_REPEAT );
	glSamplerParameteri( mipSampler, GL_TEXTURE_WRAP_T, GL_REPEAT );
	return true;
}

void Renderer::SetChannelImage( int ch, const ImageData* img )
{
	if( ch < 0 || ch > 3 )
		return;
	if( chImageTex[ ch ] )
		glDeleteTextures( 1, &chImageTex[ ch ] );
	chImageTex[ ch ] = 0;
	chImageW[ ch ] = chImageH[ ch ] = 0;
	if( !img || img->faces != 1 || img->depth != 1 )
		return;
	GLenum target;
	chImageTex[ ch ] = UploadImage( *img, ChannelKind::Texture, Filter::Mipmap, target );
	chImageW[ ch ]   = img->width;
	chImageH[ ch ]   = img->height;
}

void Renderer::DeInitGL()
{
	DestroyPasses( passes, ownedTextures );
	DestroyBuffers();
	GLuint t[] = { audioTex, keyboardTex, black2D, blackCube, black3D };
	glDeleteTextures( 5, t );
	for( int i = 0; i < 4; ++i )
		SetChannelImage( i, nullptr );
	if( copyProgram )
		glDeleteProgram( copyProgram );
	if( copyFbo )
		glDeleteFramebuffers( 1, &copyFbo );
	if( copyTex )
		glDeleteTextures( 1, &copyTex );
	copyProgram = copyFbo = copyTex = 0;
	copyW = copyH = 0;
	if( linearSampler )
		glDeleteSamplers( 1, &linearSampler );
	if( mipSampler )
		glDeleteSamplers( 1, &mipSampler );
	linearSampler = mipSampler = 0;
	audioTex = keyboardTex = black2D = blackCube = black3D = 0;
	if( vertexShader )
		glDeleteShader( vertexShader );
	vertexShader = 0;
	quad.Release();
}

void Renderer::DestroyPasses( std::vector< Pass >& list, std::vector< GLuint >& textures )
{
	for( auto& p : list )
	{
		if( p.program )
			glDeleteProgram( p.program );
		glDeleteSamplers( 4, p.samplers );
	}
	list.clear();
	if( !textures.empty() )
		glDeleteTextures( (GLsizei)textures.size(), textures.data() );
	textures.clear();
}

void Renderer::DestroyBuffers()
{
	for( auto& b : buffers )
	{
		if( b.fbo[ 0 ] )
			glDeleteFramebuffers( 2, b.fbo );
		if( b.tex[ 0 ] )
			glDeleteTextures( 2, b.tex );
		b = Buffer();
	}
}

void Renderer::ResetBuffers()
{
	bool mips[ 4 ];
	for( int i = 0; i < 4; ++i )
		mips[ i ] = buffers[ i ].wantMips;
	DestroyBuffers();
	for( int i = 0; i < 4; ++i )
		buffers[ i ].wantMips = mips[ i ];
}

void Renderer::EnsureBuffer( int index, int w, int h )
{
	Buffer& b = buffers[ index ];
	if( b.fbo[ 0 ] && b.w == w && b.h == h )
		return;
	bool mips = b.wantMips;
	if( b.fbo[ 0 ] )
	{
		glDeleteFramebuffers( 2, b.fbo );
		glDeleteTextures( 2, b.tex );
	}
	b          = Buffer();
	b.wantMips = mips;
	b.w        = w;
	b.h        = h;
	glGenFramebuffers( 2, b.fbo );
	for( int i = 0; i < 2; ++i )
	{
		b.tex[ i ] = MakeTex( GL_TEXTURE_2D );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr );
		glBindFramebuffer( GL_FRAMEBUFFER, b.fbo[ i ] );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, b.tex[ i ], 0 );
		glClearColor( 0, 0, 0, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
}

GLuint Renderer::UploadImage( const ImageData& img, ChannelKind kind, Filter filter, GLenum& target )
{
	GLint prevUnpack = 4;
	glGetIntegerv( GL_UNPACK_ALIGNMENT, &prevUnpack );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	GLuint tex = 0;
	GLenum type     = img.isFloat ? GL_FLOAT : GL_UNSIGNED_BYTE;
	GLenum format   = img.comps == 1 ? GL_RED : GL_RGBA;
	GLenum internal = img.comps == 1 ? ( img.isFloat ? GL_R32F : GL_R8 ) : ( img.isFloat ? GL_RGBA32F : GL_RGBA8 );
	if( kind == ChannelKind::Cubemap && img.faces == 6 )
	{
		target          = GL_TEXTURE_CUBE_MAP;
		tex             = MakeTex( target );
		size_t faceSize = (size_t)img.width * img.height * 4;
		for( int f = 0; f < 6; ++f )
			glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA8, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data.data() + f * faceSize );
	}
	else if( kind == ChannelKind::Volume )
	{
		target = GL_TEXTURE_3D;
		tex    = MakeTex( target );
		glTexImage3D( GL_TEXTURE_3D, 0, internal, img.width, img.height, img.depth, 0, format, type, img.data.data() );
	}
	else
	{
		target = GL_TEXTURE_2D;
		tex    = MakeTex( target );
		glTexImage2D( GL_TEXTURE_2D, 0, internal, img.width, img.height, 0, format, type, img.data.data() );
	}
	if( filter == Filter::Mipmap )
		glGenerateMipmap( target );
	glBindTexture( target, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, prevUnpack );
	return tex;
}

static void ConfigureSampler( GLuint s, ChannelKind kind, Filter filter, bool repeat )
{
	GLint minF = GL_LINEAR, magF = GL_LINEAR;
	if( filter == Filter::Nearest || kind == ChannelKind::Keyboard )
		minF = magF = GL_NEAREST;
	else if( filter == Filter::Mipmap && kind != ChannelKind::Input && kind != ChannelKind::Audio )
		minF = GL_LINEAR_MIPMAP_LINEAR;
	GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
	glSamplerParameteri( s, GL_TEXTURE_MIN_FILTER, minF );
	glSamplerParameteri( s, GL_TEXTURE_MAG_FILTER, magF );
	glSamplerParameteri( s, GL_TEXTURE_WRAP_S, wrap );
	glSamplerParameteri( s, GL_TEXTURE_WRAP_T, wrap );
	glSamplerParameteri( s, GL_TEXTURE_WRAP_R, wrap );
}

bool Renderer::Build( ShaderDoc& doc, std::string& log )
{
	if( BuildOnce( doc, log ) )
		return true;
	auto isAuto = []( const UserParam& p ) { return p.source != ParamSource::Uniform; };
	if( std::none_of( doc.params.begin(), doc.params.end(), isAuto ) )
		return false;
	// 1) retire les paramètres automatiques cités dans les erreurs
	std::vector< UserParam > kept;
	bool removed = false;
	for( auto& p : doc.params )
	{
		bool cited = isAuto( p ) && std::regex_search( log, std::regex( "\\b" + p.uniform + "\\b" ) );
		if( cited )
			removed = true;
		else
			kept.push_back( p );
	}
	if( removed )
	{
		ShaderDoc trial = doc;
		trial.params    = kept;
		std::string log2;
		if( BuildOnce( trial, log2 ) )
		{
			doc.params = kept;
			doc.warnings.push_back( "Certains réglages (#define/const) doivent rester constants : ils ne sont pas exposés dans Resolume." );
			log.clear();
			return true;
		}
	}
	// 2) en dernier recours, aucun paramètre automatique
	kept.clear();
	for( auto& p : doc.params )
		if( !isAuto( p ) )
			kept.push_back( p );
	ShaderDoc trial = doc;
	trial.params    = kept;
	std::string log3;
	if( BuildOnce( trial, log3 ) )
	{
		doc.params = kept;
		doc.warnings.push_back( "Les #define/const de ce shader n'ont pas pu être exposés comme paramètres (compilation impossible)." );
		log.clear();
		return true;
	}
	return false;// on garde le log de la première tentative (code d'origine)
}

bool Renderer::BuildOnce( const ShaderDoc& doc, std::string& log )
{
	log.clear();
	if( !vertexShader )
	{
		log = "Contexte OpenGL non initialisé";
		return false;
	}
	std::vector< Pass > newPasses;
	std::vector< GLuint > newTextures;
	// regroupe les paramètres par uniform (un vec3 = 3 sliders)
	std::vector< UserUniform > newUniforms;
	for( size_t i = 0; i < doc.params.size() && i < (size_t)kMaxUserParams; ++i )
	{
		const UserParam& up = doc.params[ i ];
		auto it = std::find_if( newUniforms.begin(), newUniforms.end(), [ & ]( const UserUniform& u ) { return u.name == up.uniform; } );
		if( it == newUniforms.end() )
		{
			UserUniform u;
			u.name    = up.uniform;
			u.vecSize = up.vecSize;
			u.isInt   = up.isInt;
			newUniforms.push_back( u );
			it = newUniforms.end() - 1;
		}
		it->param[ std::max( 0, up.comp ) ] = (int)i;
	}
	bool wantMips[ 4 ] = {};
	bool ok            = true;

	for( const PassDesc& pd : doc.passes )
	{
		Pass p;
		p.isImage     = pd.isImage;
		p.bufferIndex = pd.bufferIndex;

		std::string passLog;
		GLuint fs = Compile( GL_FRAGMENT_SHADER, BuildFragmentSource( doc, pd, glslVersion ), passLog );
		if( !fs && glslVersion > 410 )
		{
			// repli : certains shaders utilisent des noms devenus mots-clés en 4.20
			std::string log410;
			fs = Compile( GL_FRAGMENT_SHADER, BuildFragmentSource( doc, pd, 410 ), log410 );
			if( fs )
				passLog.clear();
		}
		if( !fs )
		{
			log += "--- " + pd.name + " ---\n" + PrettifyLog( passLog, pd.name );
			ok = false;
			continue;
		}
		p.program = glCreateProgram();
		glAttachShader( p.program, vertexShader );
		glAttachShader( p.program, fs );
		glLinkProgram( p.program );
		glDetachShader( p.program, vertexShader );
		glDetachShader( p.program, fs );
		glDeleteShader( fs );
		GLint linked = 0;
		glGetProgramiv( p.program, GL_LINK_STATUS, &linked );
		if( !linked )
		{
			GLint len = 0;
			glGetProgramiv( p.program, GL_INFO_LOG_LENGTH, &len );
			std::string l( std::max( len, 1 ), '\0' );
			glGetProgramInfoLog( p.program, len, nullptr, &l[ 0 ] );
			log += "--- " + pd.name + " (link) ---\n" + PrettifyLog( l.c_str(), pd.name );
			ok = false;
			newPasses.push_back( p );
			continue;
		}

		GLuint prog   = p.program;
		p.uRes        = glGetUniformLocation( prog, "iResolution" );
		p.uTime       = glGetUniformLocation( prog, "iTime" );
		p.uTimeDelta  = glGetUniformLocation( prog, "iTimeDelta" );
		p.uFrameRate  = glGetUniformLocation( prog, "iFrameRate" );
		p.uFrame      = glGetUniformLocation( prog, "iFrame" );
		p.uChannelTime = glGetUniformLocation( prog, "iChannelTime" );
		p.uChannelRes = glGetUniformLocation( prog, "iChannelResolution" );
		p.uMouse      = glGetUniformLocation( prog, "iMouse" );
		p.uDate       = glGetUniformLocation( prog, "iDate" );
		p.uSampleRate = glGetUniformLocation( prog, "iSampleRate" );
		p.uOffset     = glGetUniformLocation( prog, "stfx_Offset" );
		p.uForceAlpha = glGetUniformLocation( prog, "stfx_ForceAlpha" );
		p.uMask       = glGetUniformLocation( prog, "stfx_Mask" );
		for( int i = 0; i < 4; ++i )
			p.uChannel[ i ] = glGetUniformLocation( prog, ( "iChannel" + std::to_string( i ) ).c_str() );
		p.uUser.clear();
		for( auto& uu : newUniforms )
			p.uUser.push_back( glGetUniformLocation( prog, uu.name.c_str() ) );

		glGenSamplers( 4, p.samplers );
		for( int i = 0; i < 4; ++i )
		{
			const ChannelDesc& cd = pd.ch[ i ];
			Channel& c            = p.ch[ i ];
			c.kind                = cd.kind;
			c.buffer              = cd.buffer;
			c.filter              = cd.filter;
			if( cd.image && ( cd.kind == ChannelKind::Texture || cd.kind == ChannelKind::Cubemap || cd.kind == ChannelKind::Volume ) )
			{
				c.tex = UploadImage( *cd.image, cd.kind, cd.filter, c.target );
				c.w   = cd.image->width;
				c.h   = cd.image->height;
				c.d   = cd.image->depth;
				newTextures.push_back( c.tex );
			}
			else if( cd.kind == ChannelKind::Cubemap )
				c.target = GL_TEXTURE_CUBE_MAP;
			else if( cd.kind == ChannelKind::Volume )
				c.target = GL_TEXTURE_3D;
			if( cd.kind == ChannelKind::Buffer && cd.buffer >= 0 && cd.filter == Filter::Mipmap )
				wantMips[ cd.buffer ] = true;
			ConfigureSampler( p.samplers[ i ], cd.kind, cd.filter, cd.repeat );
		}
		newPasses.push_back( p );
	}

	if( !ok || newPasses.empty() )
	{
		DestroyPasses( newPasses, newTextures );
		if( log.empty() )
			log = "Aucune passe à compiler";
		return false;
	}

	DestroyPasses( passes, ownedTextures );
	passes        = std::move( newPasses );
	ownedTextures = std::move( newTextures );
	userUniforms  = std::move( newUniforms );
	DestroyBuffers();
	for( int i = 0; i < 4; ++i )
		buffers[ i ].wantMips = wantMips[ i ];
	return true;
}

void Renderer::UpdateAudio( const float* fft, int count, float gain )
{
	if( !audioTex || audioRows.empty() )
		return;
	for( int i = 0; i < kAudioSize; ++i )
	{
		float v = 0.0f;
		if( fft && count > 0 )
		{
			float pos = (float)i * (float)count / (float)kAudioSize;
			int i0    = std::min( (int)pos, count - 1 );
			int i1    = std::min( i0 + 1, count - 1 );
			float t   = pos - (float)i0;
			v         = fft[ i0 ] * ( 1.0f - t ) + fft[ i1 ] * t;
		}
		audioRows[ i ] = std::min( std::max( v * gain, 0.0f ), 1.0f );
		// pas de forme d'onde côté FFGL : on synthétise une onde centrée modulée par le spectre
		audioRows[ kAudioSize + i ] = 0.5f + 0.5f * audioRows[ i ] * ( ( i & 1 ) ? 1.0f : -1.0f );
	}
	glBindTexture( GL_TEXTURE_2D, audioTex );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, kAudioSize, 2, GL_RED, GL_FLOAT, audioRows.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

void Renderer::PrepareInput( const FrameInputs& in )
{
	if( !in.inputTex || !copyProgram || in.inputW <= 0 || in.inputH <= 0 )
		return;
	int w = in.width, h = in.height;
	if( !copyFbo || copyW != w || copyH != h )
	{
		if( !copyFbo )
			glGenFramebuffers( 1, &copyFbo );
		if( copyTex )
			glDeleteTextures( 1, &copyTex );
		copyTex = MakeTex( GL_TEXTURE_2D );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glBindFramebuffer( GL_FRAMEBUFFER, copyFbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copyTex, 0 );
		copyW = w;
		copyH = h;
	}
	float ao = (float)w / (float)h, ai = (float)in.inputW / (float)in.inputH;
	float sx = 1.0f, sy = 1.0f;
	switch( in.inputFit )
	{
	case 1:// contenir : image entière, bandes noires
		if( ai > ao )
			sy = ai / ao;
		else
			sx = ao / ai;
		break;
	case 2:// remplir : couvre tout, rogne
		if( ai > ao )
			sx = ao / ai;
		else
			sy = ai / ao;
		break;
	case 3:// pixel pour pixel, centré
		sx = (float)w / (float)in.inputW;
		sy = (float)h / (float)in.inputH;
		break;
	default:// étirer
		break;
	}
	float mu = in.inputHwW > 0 ? (float)in.inputW / (float)in.inputHwW : 1.0f;
	float mv = in.inputHwH > 0 ? (float)in.inputH / (float)in.inputHwH : 1.0f;

	glBindFramebuffer( GL_FRAMEBUFFER, copyFbo );
	glViewport( 0, 0, w, h );
	glUseProgram( copyProgram );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, in.inputTex );
	glBindSampler( 0, linearSampler );
	glUniform1i( uCopyTex, 0 );
	glUniform2f( uCopyMaxUV, mu, mv );
	glUniform4f( uCopyFit, sx, sy, 0.0f, 0.0f );
	glUniform2f( uCopySize, (float)w, (float)h );
	float hw = (float)( in.inputHwW > 0 ? in.inputHwW : in.inputW ), hh = (float)( in.inputHwH > 0 ? in.inputHwH : in.inputH );
	glUniform2f( uCopyHalf, 0.5f / hw, 0.5f / hh );
	quad.Draw();
	glBindSampler( 0, 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

void Renderer::Render( const FrameInputs& in )
{
	if( passes.empty() )
		return;

	// entrée Resolume recadrée : même taille que la sortie, marge matérielle retirée
	PrepareInput( in );
	GLuint inputTex = ( in.inputTex && copyTex ) ? copyTex : in.inputTex;
	float inputW    = ( in.inputTex && copyTex ) ? (float)copyW : (float)in.inputW;
	float inputH    = ( in.inputTex && copyTex ) ? (float)copyH : (float)in.inputH;

	bool mipsDone[ 4 ] = {};
	float chRes[ 12 ];
	float chTime[ 4 ] = { in.time, in.time, in.time, in.time };

	// crée/redimensionne d'abord tous les buffers utilisés (EnsureBuffer modifie le FBO lié)
	for( const Pass& p : passes )
	{
		if( !p.isImage && p.bufferIndex >= 0 && p.bufferIndex < 4 )
			EnsureBuffer( p.bufferIndex, in.width, in.height );
		for( const Channel& c : p.ch )
			if( c.kind == ChannelKind::Buffer && c.buffer >= 0 && c.buffer < 4 )
				EnsureBuffer( c.buffer, in.width, in.height );
	}

	for( Pass& p : passes )
	{
		int w = in.width, h = in.height;
		if( p.isImage )
		{
			glBindFramebuffer( GL_FRAMEBUFFER, in.hostFbo );
			glViewport( in.vpX, in.vpY, in.width, in.height );
		}
		else
		{
			if( p.bufferIndex < 0 || p.bufferIndex > 3 )
				continue;
			Buffer& b = buffers[ p.bufferIndex ];
			glBindFramebuffer( GL_FRAMEBUFFER, b.fbo[ 1 - b.cur ] );
			glViewport( 0, 0, w, h );
		}

		glUseProgram( p.program );

		for( int i = 0; i < 4; ++i )
		{
			const Channel& c = p.ch[ i ];
			GLuint tex       = 0;
			GLenum target    = c.target;
			float rw = 0, rh = 0, rd = 0;
			GLuint sampler   = p.samplers[ i ];
			// remplacement choisi dans Resolume : jamais sur un buffer (feedback) ni sur un sampler cube/3D
			int mode = in.chMode[ i ];
			if( mode != CH_SHADER && c.kind != ChannelKind::Buffer && target == GL_TEXTURE_2D )
			{
				switch( mode )
				{
				case CH_INPUT:
					tex     = inputTex;
					rw      = inputW;
					rh      = inputH;
					sampler = linearSampler;
					break;
				case CH_IMAGE:
					tex     = chImageTex[ i ];
					rw      = (float)chImageW[ i ];
					rh      = (float)chImageH[ i ];
					sampler = mipSampler;
					break;
				case CH_AUDIO:
					tex     = audioTex;
					rw      = (float)kAudioSize;
					rh      = 2;
					sampler = linearSampler;
					break;
				default:
					break;
				}
				if( !tex )
					tex = black2D;
			}
			else
			switch( c.kind )
			{
			case ChannelKind::Input:
				tex = inputTex;
				rw  = inputW;
				rh  = inputH;
				break;
			case ChannelKind::Audio:
				tex = audioTex;
				rw  = (float)kAudioSize;
				rh  = 2;
				break;
			case ChannelKind::Keyboard:
				tex = keyboardTex;
				rw  = 256;
				rh  = 3;
				break;
			case ChannelKind::Buffer:
				if( c.buffer >= 0 && c.buffer < 4 )
				{
					Buffer& b = buffers[ c.buffer ];
					// lecture de soi-même = image précédente ; d'un buffer déjà rendu = image courante
					tex = b.tex[ b.cur ];
					rw  = (float)b.w;
					rh  = (float)b.h;
					if( b.wantMips && !mipsDone[ c.buffer ] )
					{
						glBindTexture( GL_TEXTURE_2D, tex );
						glGenerateMipmap( GL_TEXTURE_2D );
						mipsDone[ c.buffer ] = true;
					}
				}
				break;
			case ChannelKind::Texture:
			case ChannelKind::Cubemap:
			case ChannelKind::Volume:
				tex = c.tex;
				rw  = (float)c.w;
				rh  = (float)c.h;
				rd  = (float)c.d;
				break;
			default:
				break;
			}
			if( !tex )
				tex = target == GL_TEXTURE_CUBE_MAP ? blackCube : target == GL_TEXTURE_3D ? black3D : black2D;
			glActiveTexture( GL_TEXTURE0 + i );
			glBindTexture( target, tex );
			glBindSampler( i, sampler );
			if( p.uChannel[ i ] >= 0 )
				glUniform1i( p.uChannel[ i ], i );
			chRes[ i * 3 + 0 ] = rw;
			chRes[ i * 3 + 1 ] = rh;
			chRes[ i * 3 + 2 ] = rd;
		}

		if( p.uRes >= 0 )
			glUniform3f( p.uRes, (float)w, (float)h, 1.0f );
		if( p.uTime >= 0 )
			glUniform1f( p.uTime, in.time );
		if( p.uTimeDelta >= 0 )
			glUniform1f( p.uTimeDelta, in.timeDelta );
		if( p.uFrameRate >= 0 )
			glUniform1f( p.uFrameRate, in.frameRate );
		if( p.uFrame >= 0 )
			glUniform1i( p.uFrame, in.frame );
		if( p.uChannelTime >= 0 )
			glUniform1fv( p.uChannelTime, 4, chTime );
		if( p.uChannelRes >= 0 )
			glUniform3fv( p.uChannelRes, 4, chRes );
		if( p.uMouse >= 0 )
			glUniform4fv( p.uMouse, 1, in.mouse );
		if( p.uDate >= 0 )
			glUniform4fv( p.uDate, 1, in.date );
		if( p.uSampleRate >= 0 )
			glUniform1f( p.uSampleRate, in.sampleRate );
		if( p.uOffset >= 0 )
			glUniform2f( p.uOffset, p.isImage ? (float)in.vpX : 0.0f, p.isImage ? (float)in.vpY : 0.0f );
		if( p.uForceAlpha >= 0 )
			glUniform1i( p.uForceAlpha, ( p.isImage && !in.shaderAlpha ) ? 1 : 0 );
		if( p.uMask >= 0 )
			glUniform4f( p.uMask, p.isImage ? (float)in.maskMode : 0.0f, in.maskThreshold, in.maskSoftness, in.maskInvert ? 1.0f : 0.0f );
		for( size_t u = 0; u < userUniforms.size() && u < p.uUser.size(); ++u )
		{
			GLint loc = p.uUser[ u ];
			if( loc < 0 )
				continue;
			const UserUniform& uu = userUniforms[ u ];
			float v[ 4 ]          = {};
			for( int k = 0; k < uu.vecSize; ++k )
				v[ k ] = uu.param[ k ] >= 0 ? in.user[ uu.param[ k ] ] : 0.0f;
			if( uu.isInt )
				glUniform1i( loc, (int)std::lround( v[ 0 ] ) );
			else if( uu.vecSize == 2 )
				glUniform2fv( loc, 1, v );
			else if( uu.vecSize == 3 )
				glUniform3fv( loc, 1, v );
			else if( uu.vecSize == 4 )
				glUniform4fv( loc, 1, v );
			else
				glUniform1f( loc, v[ 0 ] );
		}

		quad.Draw();

		if( !p.isImage )
			buffers[ p.bufferIndex ].cur = 1 - buffers[ p.bufferIndex ].cur;
	}

	// on rend au host un contexte propre
	for( int i = 0; i < 4; ++i )
	{
		glActiveTexture( GL_TEXTURE0 + i );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glBindTexture( GL_TEXTURE_CUBE_MAP, 0 );
		glBindTexture( GL_TEXTURE_3D, 0 );
		glBindSampler( i, 0 );
	}
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, in.hostFbo );
	glViewport( in.vpX, in.vpY, in.width, in.height );
}
}//namespace stfx
