#include "GlslGen.h"
#include <sstream>
#include <regex>

namespace stfx
{
const char* kVertexSource = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
void main()
{
	gl_Position = vPosition;
}
)";

static const char* SamplerType( ChannelKind k )
{
	switch( k )
	{
	case ChannelKind::Cubemap:
		return "samplerCube";
	case ChannelKind::Volume:
		return "sampler3D";
	default:
		return "sampler2D";
	}
}

std::string BuildFragmentSource( const ShaderDoc& doc, const PassDesc& pass, int glslVersion )
{
	std::ostringstream s;
	s << "#version " << glslVersion << " core\n";
	if( glslVersion < 420 )
		s << "#extension GL_ARB_shading_language_packing : enable\n";
	s << ""
		 "#define HW_PERFORMANCE 1\n"
		 "#define SHADERTOYFX 1\n"
		 "uniform vec3  iResolution;\n"
		 "uniform float iTime;\n"
		 "uniform float iTimeDelta;\n"
		 "uniform float iFrameRate;\n"
		 "uniform int   iFrame;\n"
		 "uniform float iChannelTime[4];\n"
		 "uniform vec3  iChannelResolution[4];\n"
		 "uniform vec4  iMouse;\n"
		 "uniform vec4  iDate;\n"
		 "uniform float iSampleRate;\n";
	for( int i = 0; i < 4; ++i )
		s << "uniform " << SamplerType( pass.ch[ i ].kind ) << " iChannel" << i << ";\n";
	s << "uniform vec2 stfx_Offset;\n"
		 "uniform int  stfx_ForceAlpha;\n"
		 "uniform vec4 stfx_Mask; // mode, seuil, douceur, inversion\n"
		 "out vec4 stfx_FragColor;\n"
		 // compatibilité GLSL ES / WebGL1
		 "#define texture2D texture\n"
		 "#define textureCube texture\n"
		 "#define texture2DLod textureLod\n"
		 "#define texture2DLodEXT textureLod\n"
		 "#define textureCubeLod textureLod\n"
		 "#define textureCubeLodEXT textureLod\n"
		 "#define texture2DGradEXT textureGrad\n"
		 "#line 1 1\n"
	  << ApplyParamRewrites( doc.common, doc.params ) << "\n"
	  << "#line 1 2\n"
	  << ApplyParamRewrites( pass.code, doc.params ) << "\n"
	  << "#line 1 3\n"
		 "void main()\n"
		 "{\n"
		 "\tvec4 stfx_c = vec4( 0.0, 0.0, 0.0, 1.0 );\n"
		 "\tmainImage( stfx_c, gl_FragCoord.xy - stfx_Offset );\n"
		 "\tvec4 stfx_o = ( stfx_ForceAlpha != 0 ) ? vec4( stfx_c.rgb, 1.0 ) : stfx_c;\n"
		 "\tif( stfx_Mask.x > 0.5 )\n"
		 "\t{\n"
		 "\t\tvec3 stfx_rgb = clamp( stfx_c.rgb, 0.0, 1.0 );\n"
		 "\t\tfloat stfx_l = dot( stfx_rgb, vec3( 0.2126, 0.7152, 0.0722 ) );\n"
		 "\t\tif( stfx_Mask.x > 1.5 && stfx_Mask.x < 2.5 ) stfx_l = max( stfx_rgb.r, max( stfx_rgb.g, stfx_rgb.b ) );\n"
		 "\t\tif( stfx_Mask.w > 0.5 ) stfx_l = 1.0 - stfx_l;\n"
		 "\t\tfloat stfx_m = smoothstep( stfx_Mask.y, stfx_Mask.y + max( stfx_Mask.z, 0.0001 ), stfx_l );\n"
		 "\t\tif( stfx_Mask.x > 2.5 ) stfx_m *= clamp( stfx_c.a, 0.0, 1.0 );\n"
		 "\t\tstfx_o.a = stfx_m;\n"
		 "\t}\n"
		 "\tstfx_FragColor = stfx_o;\n"
		 "}\n";
	return s.str();
}

std::string PrettifyLog( const std::string& log, const std::string& passName )
{
	// Mesa : "2:14(7): error"   AMD/Intel : "ERROR: 2:14: ..."   NVIDIA : "2(14) : error C0000: ..."
	static const std::regex reMesa( R"(\b([123]):(\d+)\(\d+\))" );
	static const std::regex reAmd( R"(\b([123]):(\d+):)" );
	static const std::regex reNv( R"(\b([123])\((\d+)\))" );
	auto repl = [ & ]( const std::smatch& m ) {
		int src           = std::stoi( m[ 1 ] );
		std::string where = src == 1 ? "Common" : src == 2 ? passName : "main";
		return "[" + where + " ligne " + m[ 2 ].str() + "]";
	};
	std::string out;
	std::istringstream in( log );
	std::string line;
	while( std::getline( in, line ) )
	{
		std::smatch m;
		if( std::regex_search( line, m, reMesa ) || std::regex_search( line, m, reAmd ) || std::regex_search( line, m, reNv ) )
			line = m.prefix().str() + repl( m ) + m.suffix().str();
		if( !line.empty() )
			out += line + "\n";
	}
	return out;
}
}//namespace stfx
