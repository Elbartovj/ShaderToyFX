#include "SedGodot.h"
#include "../Util.h"

#include <ShaderTranscompiler.h>

#include <algorithm>
#include <regex>

namespace stfx
{
namespace sed
{
namespace
{
// Syntaxes Godot 4 courantes -> Godot 3 (ce que comprend le transcompileur)
std::string Godot4To3( std::string s, bool& usesScreenTexture, std::vector< std::string >& colorNames )
{
	// couleurs Godot 4 (source_color, accepté aussi sur vec3) : mémorisées pour les sliders 0..1
	static const std::regex reColor( R"(uniform\s+(?:lowp\s+|mediump\s+|highp\s+)?vec[34]\s+([A-Za-z_]\w*)\s*:\s*[^;=]*\b(source_color|hint_color)\b)" );
	for( std::sregex_iterator it( s.begin(), s.end(), reColor ), end; it != end; ++it )
		colorNames.push_back( ( *it )[ 1 ].str() );
	// vec3 : l'indice de couleur Godot 3 n'existe que pour vec4 -> retiré
	s = std::regex_replace( s, std::regex( R"((uniform\s+(?:lowp\s+|mediump\s+|highp\s+)?vec3\s+[A-Za-z_]\w*\s*):\s*source_color\s*(,\s*)?)" ), "$1:" );
	// uniform sampler2D screen_tex : hint_screen_texture, filter_linear;  ->  SCREEN_TEXTURE
	static const std::regex reScreen( R"(uniform\s+sampler2D\s+([A-Za-z_]\w*)\s*:\s*[^;]*hint_screen_texture[^;]*;)" );
	std::smatch m;
	std::vector< std::string > screenNames;
	std::string tmp = s;
	while( std::regex_search( tmp, m, reScreen ) )
	{
		screenNames.push_back( m[ 1 ].str() );
		tmp = m.suffix();
	}
	s = std::regex_replace( s, reScreen, "" );
	for( auto& n : screenNames )
	{
		s = std::regex_replace( s, std::regex( "\\b" + n + "\\b" ), "SCREEN_TEXTURE" );
		usesScreenTexture = true;
	}
	// indices de texture Godot 4 sans équivalent : retirés
	static const std::regex reFilter( R"(,\s*(filter_\w+|repeat_enable|repeat_disable)|(filter_\w+|repeat_enable|repeat_disable)\s*,?)" );
	s = std::regex_replace( s, reFilter, "" );
	s = std::regex_replace( s, std::regex( R"(\bsource_color\b)" ), "hint_color" );
	s = std::regex_replace( s, std::regex( R"(\bhint_default_white\b)" ), "hint_white" );
	s = std::regex_replace( s, std::regex( R"(\bhint_default_black\b)" ), "hint_black" );
	s = std::regex_replace( s, std::regex( R"(\b(instance|global)\s+uniform\b)" ), "uniform" );
	// « : » laissé seul après suppression d'un indice
	s = std::regex_replace( s, std::regex( R"(:\s*(=|;))" ), "$1" );
	return s;
}
}//namespace

bool TranscompileGodot( const std::string& sourceIn, GodotShader& out, std::string& err )
{
	bool screen     = false;
	std::vector< std::string > colorNames;
	std::string src = Godot4To3( sourceIn, screen, colorNames );
	if( src.find( "shader_type" ) == std::string::npos )
		src = "shader_type canvas_item;\n" + src;
	if( gd::ShaderTranscompiler::GetShaderType( src ) != VisualServer::SHADER_CANVAS_ITEM )
	{
		err = "Seuls les shaders Godot « canvas_item » (2D) sont pris en charge (pas spatial / particles).";
		return false;
	}
	gd::GLSLOutput g;
	gd::ShaderTranscompiler::Transcompile( src, g );
	if( g.Error )
	{
		err = "Ligne " + std::to_string( g.ErrorLine ) + " : " + g.ErrorMessage;
		return false;
	}
	// identifiants internes de Godot non déclarés par le gabarit du transcompileur
	auto declare = []( std::string& code ) {
		std::string decl;
		if( code.find( "color_texpixel_size" ) != std::string::npos )
			decl += "uniform vec2 color_texpixel_size;\n";
		if( code.find( "normal_texture" ) != std::string::npos )
			decl += "uniform sampler2D normal_texture;\n";
		if( code.find( "at_light_pass" ) != std::string::npos )
			decl += "const bool at_light_pass = false;\n";
		if( code.find( "extra_matrix" ) != std::string::npos )
			decl += "const mat4 extra_matrix = mat4(1.0);\n";
		if( code.find( "instance_custom" ) != std::string::npos )
			decl += "const vec4 instance_custom = vec4(0.0);\n";
		size_t eol = code.find( '\n' );
		code.insert( eol == std::string::npos ? 0 : eol + 1, decl );
	};
	out.vertex   = g.Vertex;
	out.fragment = g.Fragment;
	declare( out.vertex );
	declare( out.fragment );
	out.blendMode     = g.BlendMode;
	out.screenTexture = g.SCREEN_TEXTURE || screen;
	out.usesTime      = g.TIME;
	out.uniforms.clear();

	// ordre de déclaration
	std::vector< std::pair< int, std::string > > order;
	for( auto& u : g.Uniforms )
		order.push_back( { u.second.order, u.first } );
	std::sort( order.begin(), order.end() );
	for( auto& o : order )
	{
		auto& u = g.Uniforms[ o.second ];
		GodotUniform gu;
		gu.name      = o.second;
		gu.isSampler = ShaderLanguage::is_sampler_type( u.type );
		auto scalar  = ShaderLanguage::get_scalar_type( u.type );
		gu.isInt     = scalar == ShaderLanguage::TYPE_INT || scalar == ShaderLanguage::TYPE_UINT || scalar == ShaderLanguage::TYPE_BOOL;
		gu.isBool    = scalar == ShaderLanguage::TYPE_BOOL;
		gu.components = gu.isSampler ? 1 : ShaderLanguage::get_cardinality( u.type );
		gu.isColor   = u.hint == ShaderLanguage::ShaderNode::Uniform::HINT_COLOR ||
					 std::find( colorNames.begin(), colorNames.end(), gu.name ) != colorNames.end();
		gu.hintBlack = u.hint == ShaderLanguage::ShaderNode::Uniform::HINT_BLACK || u.hint == ShaderLanguage::ShaderNode::Uniform::HINT_BLACK_ALBEDO;
		if( u.hint == ShaderLanguage::ShaderNode::Uniform::HINT_RANGE )
		{
			gu.hasRange = true;
			gu.rangeMin = u.hint_range[ 0 ];
			gu.rangeMax = u.hint_range[ 1 ];
		}
		for( size_t i = 0; i < u.default_value.size(); ++i )
		{
			const auto& v = u.default_value[ i ];
			if( scalar == ShaderLanguage::TYPE_BOOL ) gu.defaults.push_back( v.boolean ? 1.0f : 0.0f );
			else if( scalar == ShaderLanguage::TYPE_INT ) gu.defaults.push_back( (float)v.sint );
			else if( scalar == ShaderLanguage::TYPE_UINT ) gu.defaults.push_back( (float)v.uint );
			else gu.defaults.push_back( v.real );
		}
		if( !gu.isSampler )
			gu.defaults.resize( gu.components, gu.isColor && gu.defaults.empty() ? 1.0f : 0.0f );
		out.uniforms.push_back( gu );
	}
	return true;
}

bool MakeGodotProject( const std::string& path, Project& proj, std::string& err )
{
	std::string src;
	if( !ReadFileText( path, src ) )
	{
		err = "Impossible de lire " + path;
		return false;
	}
	GodotShader test;
	if( !TranscompileGodot( src, test, err ) )
		return false;
	proj      = Project();
	proj.path = path;
	proj.dir  = ParentDir( path );
	Pass p;
	p.name      = FileStem( path );
	p.kind      = PassKind::Godot;
	p.godotPath = path;
	Item sprite;
	sprite.name     = "Sprite";
	sprite.kind     = ItemKind::GodotSprite;
	sprite.size[ 0 ] = sprite.size[ 1 ] = 0;// plein écran
	p.items.push_back( sprite );
	proj.passes.push_back( p );
	proj.clearColor[ 3 ] = 0.0f;
	proj.useAlpha        = true;
	return true;
}
}//namespace sed
}//namespace stfx
