// Lecture des projets SHADERed (.sprj v1/v2). Logique et tables reprises de SHADERed
// (src/SHADERed/Objects/ProjectParser.cpp, licence MIT, (c) dfranx), réduites à ce qu'il faut pour le rendu.
#include "SedProject.h"
#include "../Util.h"

#include <GL/glew.h>
#include <pugixml.hpp>

#include <algorithm>
#include <cstring>

namespace stfx
{
namespace sed
{
#include "SedNames.inc"

#define ARRSIZE( a ) ( sizeof( a ) / sizeof( a[ 0 ] ) )

const char* LangName( Lang l )
{
	switch( l )
	{
	case Lang::GLSL: return "GLSL";
	case Lang::GLSLES: return "GLSL ES";
	case Lang::VulkanGLSL: return "GLSL Vulkan";
	case Lang::HLSL: return "HLSL";
	case Lang::Godot: return "Godot";
	case Lang::Rust: return "Rust";
	case Lang::Cpp: return "C++";
	case Lang::Slang: return "Slang";
	default: return "?";
	}
}

Lang LangFromPath( const std::string& path, const std::string& source )
{
	std::string ext = FileExtLower( path );
	if( !ext.empty() && ext[ 0 ] == '.' )
		ext = ext.substr( 1 );
	if( ext == "hlsl" || ext == "fx" || ext == "hlsli" )
		return Lang::HLSL;
	if( ext == "vk" || ( ( ext == "vert" || ext == "frag" ) && source.find( "binding" ) != std::string::npos ) )
		return Lang::VulkanGLSL;
	if( ext == "rs" )
		return Lang::Rust;
	if( ext == "cpp" || ext == "cxx" || ext == "cc" )
		return Lang::Cpp;
	if( ext == "slang" )
		return Lang::Slang;
	if( ext == "gdshader" || ( ext == "shader" && source.find( "shader_type" ) != std::string::npos ) )
		return Lang::Godot;
	if( source.find( "#version" ) != std::string::npos && source.find( " es" ) != std::string::npos )
	{
		size_t v = source.find( "#version" );
		size_t e = source.find( '\n', v );
		if( source.substr( v, e - v ).find( "es" ) != std::string::npos )
			return Lang::GLSLES;
	}
	return Lang::GLSL;
}

int ValueCount( ValueType t )
{
	switch( t )
	{
	case ValueType::Bool1: case ValueType::Int1: case ValueType::Float1: return 1;
	case ValueType::Bool2: case ValueType::Int2: case ValueType::Float2: return 2;
	case ValueType::Bool3: case ValueType::Int3: case ValueType::Float3: return 3;
	case ValueType::Bool4: case ValueType::Int4: case ValueType::Float4: case ValueType::Float2x2: return 4;
	case ValueType::Float3x3: return 9;
	case ValueType::Float4x4: return 16;
	default: return 1;
	}
}
int ValueColumns( ValueType t )
{
	switch( t )
	{
	case ValueType::Float2x2: return 2;
	case ValueType::Float3x3: return 3;
	case ValueType::Float4x4: return 4;
	default: return ValueCount( t );
	}
}

ValueType ValueTypeFromName( const std::string& name )
{
	for( size_t i = 0; i < ARRSIZE( VARIABLE_TYPE_NAMES ); ++i )
		if( name == VARIABLE_TYPE_NAMES[ i ] )
			return (ValueType)i;
	return ValueType::Float1;
}
SystemVar SystemVarFromName( const std::string& name )
{
	for( size_t i = 0; i < ARRSIZE( SYSTEM_VARIABLE_NAMES ); ++i )
		if( name == SYSTEM_VARIABLE_NAMES[ i ] )
			return (SystemVar)i;
	return SystemVar::None;
}
static FunctionVar FunctionFromName( const std::string& name )
{
	for( size_t i = 0; i < ARRSIZE( FUNCTION_NAMES ); ++i )
		if( name == FUNCTION_NAMES[ i ] )
			return (FunctionVar)i;
	return FunctionVar::None;
}
static ValueType SystemType( SystemVar s )
{
	switch( s )
	{
	case SystemVar::MousePosition: case SystemVar::ViewportSize: return ValueType::Float2;
	case SystemVar::Mouse: case SystemVar::MouseButton: case SystemVar::CameraPosition: return ValueType::Float4;
	case SystemVar::Projection: case SystemVar::View: case SystemVar::ViewProjection: case SystemVar::Orthographic:
	case SystemVar::ViewOrthographic: case SystemVar::GeometryTransform: return ValueType::Float4x4;
	case SystemVar::FrameIndex: return ValueType::Int1;
	case SystemVar::IsPicked: case SystemVar::IsSavingToFile: return ValueType::Bool1;
	case SystemVar::CameraPosition3: case SystemVar::CameraDirection3: case SystemVar::PickPosition: return ValueType::Float3;
	case SystemVar::KeysWASD: return ValueType::Int4;
	default: return ValueType::Float1;
	}
}

template< size_t N, size_t M >
static uint32_t Lookup( const char* text, const char* const ( &names )[ N ], const unsigned int ( &values )[ M ], uint32_t def )
{
	for( size_t k = 0; k < N && k < M; ++k )
		if( strcmp( text, names[ k ] ) == 0 )
			return values[ k ] == 0xFFFFFFFFu ? def : values[ k ];
	return def;
}

static std::string GenericPath( std::string p )
{
	std::replace( p.begin(), p.end(), '\\', '/' );
	return p;
}

static std::string ProjectPath( const Project& proj, const std::string& rel )
{
	if( rel.empty() )
		return rel;
	std::string p = GenericPath( rel );
	if( p.size() > 1 && ( p[ 0 ] == '/' || p[ 1 ] == ':' ) )
		return p;
	return GenericPath( JoinPath( proj.dir, p ) );
}

// <row><value>…</value></row> -> valeurs (et arguments pour les fonctions)
static void ParseValue( const pugi::xml_node& node, Variable& v )
{
	int idx  = 0;
	int cols = ValueColumns( v.type );
	int row  = 0;
	for( pugi::xml_node rowNode : node.children( "row" ) )
	{
		int col = 0;
		for( pugi::xml_node val : rowNode.children( "value" ) )
		{
			if( v.func == FunctionVar::Pointer || v.func == FunctionVar::CameraSnapshot )
				v.args[ 0 ] = val.text().as_string();
			else if( v.func == FunctionVar::ObjectProperty )
				v.args[ col < 2 ? col : 1 ] = val.text().as_string();
			else if( v.func != FunctionVar::None && v.func != FunctionVar::PluginFunction )
			{
				if( idx < 16 )
					v.value[ idx ] = val.text().as_float();
			}
			else if( v.func == FunctionVar::None )
			{
				// comme SHADERed : donnée[ ligne * colonnes + colonne ], envoyée telle quelle à glUniformMatrix
				int slot = row * cols + col;
				if( slot >= 0 && slot < 16 )
				{
					if( v.type <= ValueType::Bool4 )
						v.value[ slot ] = val.text().as_bool() ? 1.0f : 0.0f;
					else if( v.type <= ValueType::Int4 )
						v.value[ slot ] = (float)val.text().as_int();
					else
						v.value[ slot ] = val.text().as_float();
				}
			}
			++col;
			++idx;
		}
		++row;
	}
}

static void ParseVariables( const pugi::xml_node& node, std::vector< Variable >& out )
{
	for( pugi::xml_node vn : node.children( "variable" ) )
	{
		Variable v;
		v.name      = vn.attribute( "name" ).as_string();
		v.type      = ValueTypeFromName( vn.attribute( "type" ).as_string() );
		v.invert    = vn.attribute( "invert" ).as_bool();
		v.lastFrame = vn.attribute( "lastframe" ).as_bool();
		if( !vn.attribute( "system" ).empty() )
		{
			v.system = SystemVarFromName( vn.attribute( "system" ).as_string() );
			if( v.system != SystemVar::PluginVariable && SystemType( v.system ) != v.type )
				v.system = SystemVar::None;
		}
		if( !vn.attribute( "function" ).empty() && v.system == SystemVar::None )
			v.func = FunctionFromName( vn.attribute( "function" ).as_string() );
		// matrices identité par défaut (SHADERed initialise à 0 sauf fonction)
		if( v.system == SystemVar::None )
			ParseValue( vn, v );
		out.push_back( v );
	}
}

static void ParseRenderState( const pugi::xml_node& node, RenderState& s )
{
	for( pugi::xml_node a : node.children() )
	{
		const char* n = a.name();
		const char* t = a.text().as_string();
		if( !strcmp( n, "wireframe" ) ) s.wireframe = a.text().as_bool();
		else if( !strcmp( n, "cull" ) ) s.cull = a.text().as_bool();
		else if( !strcmp( n, "cullfront" ) ) s.cullFront = a.text().as_bool();
		else if( !strcmp( n, "ccw" ) ) s.ccw = a.text().as_bool();
		else if( !strcmp( n, "blend" ) ) s.blend = a.text().as_bool();
		else if( !strcmp( n, "colorsrcfactor" ) ) s.srcRGB = Lookup( t, BLEND_NAMES, BLEND_VALUES, GL_CONSTANT_COLOR );
		else if( !strcmp( n, "colorfunc" ) ) s.opRGB = Lookup( t, BLEND_OPERATOR_NAMES, BLEND_OPERATOR_VALUES, GL_FUNC_ADD );
		else if( !strcmp( n, "colordstfactor" ) ) s.dstRGB = Lookup( t, BLEND_NAMES, BLEND_VALUES, GL_CONSTANT_COLOR );
		else if( !strcmp( n, "alphasrcfactor" ) ) s.srcA = Lookup( t, BLEND_NAMES, BLEND_VALUES, GL_CONSTANT_COLOR );
		else if( !strcmp( n, "alphafunc" ) ) s.opA = Lookup( t, BLEND_OPERATOR_NAMES, BLEND_OPERATOR_VALUES, GL_FUNC_ADD );
		else if( !strcmp( n, "alphadstfactor" ) ) s.dstA = Lookup( t, BLEND_NAMES, BLEND_VALUES, GL_CONSTANT_COLOR );
		else if( !strcmp( n, "alpha2coverage" ) ) s.alphaToCoverage = a.text().as_bool();
		else if( !strcmp( n, "blendfactor_r" ) ) s.blendFactor[ 0 ] = (float)a.text().as_uint();
		else if( !strcmp( n, "blendfactor_g" ) ) s.blendFactor[ 1 ] = (float)a.text().as_uint();
		else if( !strcmp( n, "blendfactor_b" ) ) s.blendFactor[ 2 ] = (float)a.text().as_uint();
		else if( !strcmp( n, "blendfactor_a" ) ) s.blendFactor[ 3 ] = (float)a.text().as_uint();
		else if( !strcmp( n, "depthtest" ) ) s.depthTest = a.text().as_bool();
		else if( !strcmp( n, "depthfunc" ) ) s.depthFunc = Lookup( t, COMPARISON_FUNCTION_NAMES, COMPARISON_FUNCTION_VALUES, GL_LESS );
		else if( !strcmp( n, "depthbias" ) ) s.depthBias = a.text().as_float();
		else if( !strcmp( n, "depthclamp" ) ) s.depthClamp = a.text().as_bool();
		else if( !strcmp( n, "depthmask" ) ) s.depthMask = a.text().as_bool();
		else if( !strcmp( n, "stenciltest" ) ) s.stencilTest = a.text().as_bool();
		else if( !strcmp( n, "stencilmask" ) ) s.stencilMask = a.text().as_uint();
		else if( !strcmp( n, "stencilref" ) ) s.stencilRef = a.text().as_uint();
		else if( !strcmp( n, "stencilfrontfunc" ) ) s.stFrontFunc = Lookup( t, COMPARISON_FUNCTION_NAMES, COMPARISON_FUNCTION_VALUES, GL_ALWAYS );
		else if( !strcmp( n, "stencilfrontpass" ) ) s.stFrontPass = Lookup( t, STENCIL_OPERATION_NAMES, STENCIL_OPERATION_VALUES, GL_KEEP );
		else if( !strcmp( n, "stencilfrontfail" ) ) s.stFrontFail = Lookup( t, STENCIL_OPERATION_NAMES, STENCIL_OPERATION_VALUES, GL_KEEP );
		else if( !strcmp( n, "stencilbackfunc" ) ) s.stBackFunc = Lookup( t, COMPARISON_FUNCTION_NAMES, COMPARISON_FUNCTION_VALUES, GL_ALWAYS );
		else if( !strcmp( n, "stencilbackpass" ) ) s.stBackPass = Lookup( t, STENCIL_OPERATION_NAMES, STENCIL_OPERATION_VALUES, GL_KEEP );
		else if( !strcmp( n, "stencilbackfail" ) ) s.stBackFail = Lookup( t, STENCIL_OPERATION_NAMES, STENCIL_OPERATION_VALUES, GL_KEEP );
		else if( !strcmp( n, "depthfrontfail" ) ) s.stFrontDepthFail = Lookup( t, STENCIL_OPERATION_NAMES, STENCIL_OPERATION_VALUES, GL_KEEP );
		else if( !strcmp( n, "depthbackfail" ) ) s.stBackDepthFail = Lookup( t, STENCIL_OPERATION_NAMES, STENCIL_OPERATION_VALUES, GL_KEEP );
	}
}

static void ParseTransform( const pugi::xml_node& a, Item& it )
{
	const char* n = a.name();
	float f       = a.text().as_float();
	if( !strcmp( n, "width" ) ) it.size[ 0 ] = f;
	else if( !strcmp( n, "height" ) ) it.size[ 1 ] = f;
	else if( !strcmp( n, "depth" ) ) it.size[ 2 ] = f;
	else if( !strcmp( n, "scaleX" ) ) it.scale[ 0 ] = f;
	else if( !strcmp( n, "scaleY" ) ) it.scale[ 1 ] = f;
	else if( !strcmp( n, "scaleZ" ) ) it.scale[ 2 ] = f;
	else if( !strcmp( n, "roll" ) ) it.rotation[ 2 ] = f;
	else if( !strcmp( n, "yaw" ) ) it.rotation[ 1 ] = f;
	else if( !strcmp( n, "pitch" ) ) it.rotation[ 0 ] = f;
	else if( !strcmp( n, "x" ) ) it.position[ 0 ] = f;
	else if( !strcmp( n, "y" ) ) it.position[ 1 ] = f;
	else if( !strcmp( n, "z" ) ) it.position[ 2 ] = f;
	else if( !strcmp( n, "instanced" ) ) it.instanced = a.text().as_bool();
	else if( !strcmp( n, "instancecount" ) ) it.instanceCount = a.text().as_int();
	else if( !strcmp( n, "instancebuffer" ) ) it.instanceBuffer = a.text().as_string();
	else if( !strcmp( n, "topology" ) ) it.topology = Lookup( a.text().as_string(), TOPOLOGY_ITEM_NAMES, TOPOLOGY_ITEM_VALUES, GL_TRIANGLES );
}

static void ParseItems( Project& proj, Pass& pass, const pugi::xml_node& node )
{
	for( pugi::xml_node in : node.children() )
	{
		Item it;
		it.name          = in.attribute( "name" ).as_string();
		std::string type = in.attribute( "type" ).as_string();
		if( type == "geometry" )
		{
			it.kind = ItemKind::Geometry;
			for( pugi::xml_node a : in.children() )
			{
				if( !strcmp( a.name(), "type" ) )
				{
					std::string g = a.text().as_string();
					for( size_t k = 0; k < ARRSIZE( GEOMETRY_NAMES ); ++k )
						if( g == GEOMETRY_NAMES[ k ] )
							it.geo = (GeoType)k;
				}
				else
					ParseTransform( a, it );
			}
		}
		else if( type == "renderstate" )
		{
			it.kind = ItemKind::RenderState;
			ParseRenderState( in, it.state );
		}
		else if( type == "model" )
		{
			it.kind = ItemKind::Model;
			for( pugi::xml_node a : in.children() )
			{
				if( !strcmp( a.name(), "filepath" ) )
					it.modelPath = ProjectPath( proj, a.text().as_string() );
				else if( !strcmp( a.name(), "group" ) )
					it.modelGroup = a.text().as_string();
				else if( !strcmp( a.name(), "grouponly" ) )
					it.modelGroupOnly = a.text().as_bool();
				else
					ParseTransform( a, it );
			}
		}
		else if( type == "vertexbuffer" )
		{
			it.kind = ItemKind::VertexBuffer;
			for( pugi::xml_node a : in.children() )
			{
				if( !strcmp( a.name(), "buffer" ) )
					it.vertexBuffer = a.text().as_string();
				else
					ParseTransform( a, it );
			}
		}
		else if( type == "plugin" && std::string( in.attribute( "itemtype" ).as_string() ) == "GSprite" )
		{
			it.kind              = ItemKind::GodotSprite;
			pugi::xml_node d     = in.child( "data" );
			it.size[ 0 ]         = d.child( "width" ).text().as_float( 128 );
			it.size[ 1 ]         = d.child( "height" ).text().as_float( 128 );
			it.position[ 0 ]     = d.child( "x" ).text().as_float();
			it.position[ 1 ]     = d.child( "y" ).text().as_float();
			it.spriteRotationDeg = d.child( "rotation" ).text().as_float();
			it.flipH             = d.child( "fliph" ).text().as_bool();
			it.flipV             = d.child( "flipv" ).text().as_bool();
			it.visible           = d.child( "visible" ).empty() ? true : d.child( "visible" ).text().as_bool();
			it.spriteColor[ 0 ]  = d.child( "color_r" ).text().as_float( 1 );
			it.spriteColor[ 1 ]  = d.child( "color_g" ).text().as_float( 1 );
			it.spriteColor[ 2 ]  = d.child( "color_b" ).text().as_float( 1 );
			it.spriteColor[ 3 ]  = d.child( "color_a" ).text().as_float( 1 );
			it.spriteTexture     = GenericPath( d.child( "texture" ).text().as_string() );
		}
		else
		{
			it.kind = ItemKind::Unsupported;
			proj.warnings.push_back( "Élément « " + it.name + " » (" + type + ") non pris en charge" );
		}
		pass.items.push_back( it );
	}
}

static void BindObject( Project& proj, const pugi::xml_node& objNode, const std::string& name, bool uav )
{
	for( pugi::xml_node b : objNode.children( "bind" ) )
	{
		Pass* p  = proj.FindPass( b.attribute( "name" ).as_string() );
		int slot = b.attribute( "slot" ).as_int();
		if( !p || slot < 0 || slot > 31 )
			continue;
		int uavAttr = b.attribute( "uav" ).empty() ? -1 : b.attribute( "uav" ).as_int();
		bool asUav  = uav;
		if( uavAttr == 0 )
			asUav = false;
		else if( uavAttr == 1 )
			asUav = true;
		else if( uavAttr == -1 && !uav )
			asUav = false;
		auto& list = asUav ? p->buffers : p->textures;
		if( (int)list.size() <= slot )
			list.resize( slot + 1 );
		list[ slot ] = name;
	}
}

const Object* Project::FindObject( const std::string& name ) const
{
	for( auto& o : objects )
		if( o.name == name )
			return &o;
	return nullptr;
}
Pass* Project::FindPass( const std::string& name )
{
	for( auto& p : passes )
		if( p.name == name )
			return &p;
	return nullptr;
}

static void ParseShaderNodes( Project& proj, Pass& data, const pugi::xml_node& passNode )
{
	for( pugi::xml_node sn : passNode.children( "shader" ) )
	{
		std::string t     = sn.attribute( "type" ).as_string();
		std::string path  = ProjectPath( proj, sn.attribute( "path" ).as_string() );
		std::string entry = sn.attribute( "entry" ).as_string();
		if( entry.empty() )
			entry = "main";
		bool used = sn.attribute( "used" ).as_bool();
		if( t == "vs" ) { data.vsPath = path; data.vsEntry = entry; }
		else if( t == "ps" ) { data.psPath = path; data.psEntry = entry; }
		else if( t == "gs" ) { data.gsPath = path; data.gsEntry = entry; data.gsUsed = used; }
		else if( t == "tcs" ) { data.tcsPath = path; data.tcsEntry = entry; data.tsUsed = used; }
		else if( t == "tes" ) { data.tesPath = path; data.tesEntry = entry; data.tsUsed = data.tsUsed || used; }
		else if( t == "cs" || data.kind == PassKind::Compute || data.kind == PassKind::Audio ) { data.csPath = path; data.csEntry = entry; }
	}
}

static void ParseMacros( Pass& data, const pugi::xml_node& node )
{
	for( pugi::xml_node m : node.children( "define" ) )
	{
		Macro mc;
		mc.name   = m.attribute( "name" ).as_string();
		mc.active = m.attribute( "active" ).empty() ? true : m.attribute( "active" ).as_bool();
		mc.value  = m.text().get();
		data.macros.push_back( mc );
	}
}

bool LoadProject( const std::string& sprjPath, Project& proj, std::string& err )
{
	std::string text;
	if( !ReadFileText( sprjPath, text ) )
	{
		err = "Impossible de lire " + sprjPath;
		return false;
	}
	pugi::xml_document doc;
	pugi::xml_parse_result res = doc.load_string( text.c_str() );
	if( !res )
	{
		err = std::string( "Projet SHADERed invalide : " ) + res.description();
		return false;
	}
	pugi::xml_node projectNode = doc.child( "project" );
	if( !projectNode )
	{
		err = "Ce fichier n'est pas un projet SHADERed (.sprj).";
		return false;
	}
	proj      = Project();
	proj.path = GenericPath( sprjPath );
	proj.dir  = GenericPath( ParentDir( sprjPath ) );
	int version = projectNode.attribute( "version" ).as_int( 1 );

	for( pugi::xml_node pn : projectNode.child( "plugins" ).children( "entry" ) )
		proj.plugins.push_back( pn.attribute( "name" ).as_string() );

	// passes
	for( pugi::xml_node passNode : projectNode.child( "pipeline" ).children( "pass" ) )
	{
		Pass data;
		data.name        = passNode.attribute( "name" ).as_string();
		std::string type = passNode.attribute( "type" ).as_string();
		data.active      = passNode.attribute( "active" ).empty() ? true : passNode.attribute( "active" ).as_bool();
		if( type == "compute" )
			data.kind = PassKind::Compute;
		else if( type == "audio" )
			data.kind = PassKind::Audio;
		else if( type == "plugin" )
		{
			std::string plugin = passNode.attribute( "plugin" ).as_string();
			std::string itype  = passNode.attribute( "itemtype" ).as_string();
			if( plugin == "GodotShaders" && itype == "GCanvasMaterial" )
			{
				data.kind            = PassKind::Godot;
				pugi::xml_node d     = passNode.child( "data" );
				data.godotPath       = ProjectPath( proj, d.child( "path" ).text().as_string() );
				for( pugi::xml_node un : d.child( "uniforms" ).children( "uniform" ) )
				{
					Pass::GodotUniform u;
					u.name = un.attribute( "name" ).as_string();
					u.type = un.attribute( "type" ).as_string();
					if( u.type.find( "sampler" ) != std::string::npos )
						u.texture = GenericPath( un.child( "value" ).text().as_string() );
					else
						for( pugi::xml_node vn : un.children( "value" ) )
							u.value.push_back( vn.text().as_float() );
					data.godotUniforms.push_back( u );
				}
				if( d.child( "render_texture" ) )
				{
					std::string rt = d.child( "render_texture" ).text().get();
					if( !rt.empty() )
						data.renderTextures.push_back( rt );
				}
			}
			else
			{
				data.kind = PassKind::Unsupported;
				proj.warnings.push_back( "Passe « " + data.name + " » : greffon SHADERed « " + plugin + " » non pris en charge" );
			}
		}
		else if( version < 2 && type.empty() )
			data.kind = PassKind::Shader;

		if( data.kind == PassKind::Shader )
		{
			data.patchVerts = std::max( 1, passNode.attribute( "patchverts" ).as_int( 1 ) );
			for( pugi::xml_node rt : passNode.children( "rendertexture" ) )
				data.renderTextures.push_back( rt.attribute( "name" ).as_string() );
		}
		ParseShaderNodes( proj, data, passNode );
		ParseVariables( passNode.child( "variables" ), data.variables );
		ParseMacros( data, passNode.child( "macros" ) );
		for( pugi::xml_node li : passNode.child( "inputlayout" ).children( "item" ) )
		{
			InputLayoutItem item;
			std::string v = li.attribute( "value" ).as_string();
			for( size_t k = 0; k < ARRSIZE( ATTRIBUTE_VALUE_NAMES ); ++k )
				if( v == ATTRIBUTE_VALUE_NAMES[ k ] )
					item.value = (int)k;
			item.semantic = li.attribute( "semantic" ).as_string();
			data.inputLayout.push_back( item );
		}
		if( data.inputLayout.empty() )
			data.inputLayout = { { 0, "POSITION" }, { 1, "NORMAL" }, { 2, "TEXCOORD0" } };
		pugi::xml_node gs = passNode.child( "groupsize" );
		data.groupSize[ 0 ] = std::max( 1, gs.attribute( "x" ).as_int( 1 ) );
		data.groupSize[ 1 ] = std::max( 1, gs.attribute( "y" ).as_int( 1 ) );
		data.groupSize[ 2 ] = std::max( 1, gs.attribute( "z" ).as_int( 1 ) );
		proj.passes.push_back( data );
		// les items sont lus après l'ajout (ParseItems a besoin du projet pour les chemins)
		ParseItems( proj, proj.passes.back(), passNode.child( "items" ) );
		for( pugi::xml_node iv : passNode.child( "itemvalues" ).children( "value" ) )
		{
			ItemValue val;
			val.variable = iv.attribute( "variable" ).as_string();
			val.item     = iv.attribute( "for" ).as_string();
			Variable tmp;
			for( auto& v : proj.passes.back().variables )
				if( v.name == val.variable )
					tmp.type = v.type;
			ParseValue( iv, tmp );
			memcpy( val.value, tmp.value, sizeof( val.value ) );
			proj.passes.back().itemValues.push_back( val );
		}
	}

	// instantanés de caméra
	for( pugi::xml_node cam : projectNode.child( "cameras" ).children( "camera" ) )
	{
		std::vector< float > m( 16, 0.0f );
		int r = 0;
		for( pugi::xml_node row : cam.children( "row" ) )
		{
			int c = 0;
			for( pugi::xml_node v : row.children( "value" ) )
			{
				if( r < 4 && c < 4 )
					m[ c * 4 + r ] = v.text().as_float();
				c = ( c + 1 ) % 4;
			}
			++r;
		}
		proj.cameraSnapshots[ cam.attribute( "name" ).as_string() ] = m;
	}

	// objets
	for( pugi::xml_node on : projectNode.child( "objects" ).children( "object" ) )
	{
		Object o;
		std::string type = on.attribute( "type" ).as_string();
		if( type == "texture" )
		{
			bool cube = on.attribute( "cube" ).as_bool();
			bool kb   = on.attribute( "keyboard_texture" ).as_bool();
			bool is3d = on.attribute( "is_3d" ).as_bool();
			if( cube || kb )
				o.name = on.attribute( "name" ).as_string();
			else
				o.name = GenericPath( on.attribute( "path" ).as_string() );
			o.kind = cube ? ObjectKind::Cubemap : kb ? ObjectKind::KeyboardTexture : is3d ? ObjectKind::Texture3D : ObjectKind::Texture;
			o.path = ProjectPath( proj, o.name );
			if( cube )
			{
				const char* faces[ 6 ] = { "left", "top", "front", "bottom", "right", "back" };
				for( int f = 0; f < 6; ++f )
					o.cube[ f ] = ProjectPath( proj, on.attribute( faces[ f ] ).as_string() );
			}
			o.vflip = on.attribute( "vflip" ).as_bool();
			if( !on.attribute( "min_filter" ).empty() )
				o.minFilter = Lookup( on.attribute( "min_filter" ).as_string(), TEXTURE_MIN_FILTER_NAMES, TEXTURE_MIN_FILTER_VALUES, GL_LINEAR_MIPMAP_LINEAR );
			if( !on.attribute( "mag_filter" ).empty() )
				o.magFilter = Lookup( on.attribute( "mag_filter" ).as_string(), TEXTURE_MAG_FILTER_NAMES, TEXTURE_MAG_FILTER_VALUES, GL_LINEAR );
			if( !on.attribute( "wrap_s" ).empty() )
				o.wrapS = Lookup( on.attribute( "wrap_s" ).as_string(), TEXTURE_WRAP_NAMES, TEXTURE_WRAP_VALUES, GL_REPEAT );
			if( !on.attribute( "wrap_t" ).empty() )
				o.wrapT = Lookup( on.attribute( "wrap_t" ).as_string(), TEXTURE_WRAP_NAMES, TEXTURE_WRAP_VALUES, GL_REPEAT );
			if( !on.attribute( "wrap_r" ).empty() )
				o.wrapR = Lookup( on.attribute( "wrap_r" ).as_string(), TEXTURE_WRAP_NAMES, TEXTURE_WRAP_VALUES, GL_REPEAT );
			proj.objects.push_back( o );
			BindObject( proj, on, o.name, false );
		}
		else if( type == "rendertexture" )
		{
			o.kind = ObjectKind::RenderTexture;
			o.name = on.attribute( "name" ).as_string();
			if( !on.attribute( "format" ).empty() )
				o.format = Lookup( on.attribute( "format" ).as_string(), FORMAT_NAMES, FORMAT_VALUES, GL_RGBA );
			else
				o.format = GL_RGBA;
			if( on.attribute( "fsize" ).empty() )
			{
				std::string r = on.attribute( "rsize" ).as_string( "1,1" );
				o.ratio[ 0 ]  = (float)atof( r.substr( 0, r.find( ',' ) ).c_str() );
				o.ratio[ 1 ]  = (float)atof( r.substr( r.find( ',' ) + 1 ).c_str() );
			}
			else
			{
				std::string f = on.attribute( "fsize" ).as_string();
				o.fixed[ 0 ]  = atoi( f.substr( 0, f.find( ',' ) ).c_str() );
				o.fixed[ 1 ]  = atoi( f.substr( f.find( ',' ) + 1 ).c_str() );
			}
			o.clear = on.attribute( "clear" ).empty() ? true : on.attribute( "clear" ).as_bool();
			o.clearColor[ 0 ] = on.attribute( "r" ).as_float();
			o.clearColor[ 1 ] = on.attribute( "g" ).as_float();
			o.clearColor[ 2 ] = on.attribute( "b" ).as_float();
			o.clearColor[ 3 ] = on.attribute( "a" ).as_float();
			proj.objects.push_back( o );
			BindObject( proj, on, o.name, false );
		}
		else if( type == "audio" )
		{
			o.kind = ObjectKind::AudioTexture;
			o.name = on.attribute( "name" ).as_string();
			o.path = ProjectPath( proj, o.name );
			proj.objects.push_back( o );
			BindObject( proj, on, o.name, false );
		}
		else if( type == "image" || type == "image3d" )
		{
			o.kind = type == "image" ? ObjectKind::Image : ObjectKind::Image3D;
			o.name = on.attribute( "name" ).as_string();
			if( !on.attribute( "format" ).empty() )
				o.format = Lookup( on.attribute( "format" ).as_string(), FORMAT_NAMES, FORMAT_VALUES, GL_RGBA8 );
			o.fixed[ 0 ] = on.attribute( "width" ).as_int( 1 );
			o.fixed[ 1 ] = on.attribute( "height" ).as_int( 1 );
			o.fixed[ 2 ] = on.attribute( "depth" ).as_int( 1 );
			if( !on.attribute( "data" ).empty() )
				o.path = ProjectPath( proj, on.attribute( "data" ).as_string() );
			proj.objects.push_back( o );
			// une image va en sampler pour une passe de rendu, en image2D pour une passe compute
			for( pugi::xml_node b : on.children( "bind" ) )
			{
				Pass* p = proj.FindPass( b.attribute( "name" ).as_string() );
				if( !p )
					continue;
				int uav  = b.attribute( "uav" ).empty() ? -1 : b.attribute( "uav" ).as_int();
				bool asU = uav == 1 || ( uav == -1 && p->kind == PassKind::Compute );
				int slot = b.attribute( "slot" ).as_int();
				if( slot < 0 || slot > 31 )
					continue;
				auto& list = asU ? p->buffers : p->textures;
				if( (int)list.size() <= slot )
					list.resize( slot + 1 );
				list[ slot ] = o.name;
			}
		}
		else if( type == "buffer" )
		{
			o.kind         = ObjectKind::Buffer;
			o.name         = on.attribute( "name" ).as_string();
			o.bufferSize   = std::max( 0, on.attribute( "size" ).as_int() );
			o.bufferFormat = on.attribute( "format" ).as_string();
			o.bufferData.assign( o.bufferSize, 0 );
			std::vector< uint8_t > raw;
			if( o.bufferSize > 0 && ReadFileBytes( ProjectPath( proj, "buffers/" + o.name + ".buf" ), raw ) )
				memcpy( o.bufferData.data(), raw.data(), std::min( raw.size(), o.bufferData.size() ) );
			proj.objects.push_back( o );
			BindObject( proj, on, o.name, true );
		}
		else
			proj.warnings.push_back( "Objet « " + std::string( on.attribute( "name" ).as_string() ) + " » (" + type + ") non pris en charge" );
	}

	// réglages
	for( pugi::xml_node e : projectNode.child( "settings" ).children( "entry" ) )
	{
		std::string type = e.attribute( "type" ).as_string();
		if( type == "camera" )
		{
			proj.fpCamera = e.attribute( "fp" ).as_bool();
			auto f        = [ & ]( const char* a, const char* b, float def ) {
                pugi::xml_node n = e.child( a );
                if( !n && b )
                    n = e.child( b );
                return n ? n.text().as_float( def ) : def;
			};
			if( proj.fpCamera )
			{
				proj.camPos[ 0 ] = f( "positionX", nullptr, 0 );
				proj.camPos[ 1 ] = f( "positionY", nullptr, 0 );
				proj.camPos[ 2 ] = f( "positionZ", nullptr, 7 );
				proj.camYaw      = f( "yaw", nullptr, 0 );
				proj.camPitch    = f( "pitch", nullptr, 0 );
			}
			else
			{
				proj.camDistance = std::clamp( f( "distance", nullptr, 7 ), 2.0f, 50.0f );
				proj.camYaw      = f( "rotationX", "yaw", 0 );
				proj.camPitch    = f( "rotationY", "pitch", 0 );
				proj.camRoll     = f( "rotationZ", "roll", 0 );
			}
		}
		else if( type == "clearcolor" )
		{
			proj.clearColor[ 0 ] = e.attribute( "r" ).as_uint() / 255.0f;
			proj.clearColor[ 1 ] = e.attribute( "g" ).as_uint() / 255.0f;
			proj.clearColor[ 2 ] = e.attribute( "b" ).as_uint() / 255.0f;
			proj.clearColor[ 3 ] = e.attribute( "a" ).as_uint() / 255.0f;
		}
		else if( type == "usealpha" )
			proj.useAlpha = e.attribute( "val" ).as_bool();
		else if( type == "ipaths" )
			for( pugi::xml_node p : e.children( "path" ) )
				proj.includePaths.push_back( ProjectPath( proj, p.text().as_string() ) );
	}

	if( proj.passes.empty() )
	{
		err = "Le projet ne contient aucune passe.";
		return false;
	}
	return true;
}
}//namespace sed
}//namespace stfx
