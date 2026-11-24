// Chaîne de compilation reprise de SHADERed (src/SHADERed/Objects/ShaderCompiler.cpp et RenderEngine.cpp,
// licence MIT, (c) dfranx) : mêmes options glslang, même conversion SPIRV-Cross et mêmes retouches des UBO,
// pour que les variables du projet (.sprj) se lient de la même façon.
#include "SedCompiler.h"
#include "../Util.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_cross_util.hpp>
#include <spirv_glsl.hpp>

#include <algorithm>
#include <mutex>
#include <regex>
#include <sstream>

namespace stfx
{
namespace sed
{
namespace
{
std::mutex g_glslangMutex;
bool g_glslangInit = false;

void EnsureGlslang()
{
	if( !g_glslangInit )
	{
		glslang::InitializeProcess();
		g_glslangInit = true;
	}
}

bool IsAbsolute( const std::string& p )
{
	return p.size() > 1 && ( p[ 0 ] == '/' || p[ 0 ] == '\\' || p[ 1 ] == ':' );
}

// #include "fichier" : dossier du fichier, puis dossiers d'include du projet (comme SHADERed)
class Includer : public glslang::TShader::Includer
{
public:
	std::vector< std::string > dirs;
	IncludeResult* includeLocal( const char* headerName, const char* includerName, size_t depth ) override
	{
		return Find( headerName );
	}
	IncludeResult* includeSystem( const char* headerName, const char* includerName, size_t depth ) override
	{
		return Find( headerName );
	}
	void releaseInclude( IncludeResult* r ) override
	{
		if( r )
		{
			delete (std::string*)r->userData;
			delete r;
		}
	}

private:
	IncludeResult* Find( const char* name )
	{
		std::vector< std::string > candidates;
		if( IsAbsolute( name ) )
			candidates.push_back( name );
		for( auto& d : dirs )
			candidates.push_back( JoinPath( d, name ) );
		for( auto& c : candidates )
		{
			std::string* text = new std::string();
			if( ReadFileText( c, *text ) )
				return new IncludeResult( c, text->c_str(), text->size(), text );
			delete text;
		}
		return nullptr;
	}
};

EShLanguage ToEsh( Stage s )
{
	switch( s )
	{
	case Stage::Pixel: return EShLangFragment;
	case Stage::Geometry: return EShLangGeometry;
	case Stage::TessControl: return EShLangTessControl;
	case Stage::TessEval: return EShLangTessEvaluation;
	case Stage::Compute: return EShLangCompute;
	default: return EShLangVertex;
	}
}

std::string MacroPreamble( const std::vector< Macro >& macros )
{
	std::string s = "#define SHADERED_DESKTOP\n#define SHADERED_VERSION 1005005\n";
	for( auto& m : macros )
		if( m.active )
			s += "#define " + m.name + " " + m.value + "\n";
	return s;
}

bool CompileToSpirv( const std::string& source, Lang lang, const std::string& path, Stage stage, const std::string& entry,
					 const std::vector< Macro >& macros, const std::vector< std::string >& includeDirs, std::vector< unsigned int >& spv,
					 std::string& log )
{
	std::lock_guard< std::mutex > lock( g_glslangMutex );
	EnsureGlslang();
	EShLanguage st = ToEsh( stage );
	glslang::TShader shader( st );
	const char* src = source.c_str();
	shader.setStrings( &src, 1 );
	// HLSL : le point d'entrée doit être nommé explicitement (sinon glslang produit un main() vide)
	std::string ep = entry.empty() ? std::string( "main" ) : entry;
	if( lang == Lang::HLSL || ep != "main" )
	{
		shader.setEntryPoint( ep.c_str() );
		shader.setSourceEntryPoint( ep.c_str() );
	}
	std::string preamble = ( lang == Lang::HLSL ? "#extension GL_GOOGLE_include_directive : enable\n" : "" ) + MacroPreamble( macros );
	shader.setPreamble( preamble.c_str() );

	int version = stage == Stage::Compute ? 430 : 330;
	shader.setEnvInput( lang == Lang::HLSL ? glslang::EShSourceHlsl : glslang::EShSourceGlsl, st, glslang::EShClientOpenGL, version );
	shader.setEnvClient( glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450 );
	shader.setEnvTarget( glslang::EShTargetSpv, glslang::EShTargetSpv_1_5 );
	shader.setAutoMapLocations( true );
	EShMessages messages = (EShMessages)( EShMsgSpvRules | EShMsgKeepUncalled );

	Includer includer;
	includer.dirs.push_back( ParentDir( path ) );
	for( auto& d : includeDirs )
		includer.dirs.push_back( d );

	std::string processed;
	if( !shader.preprocess( GetDefaultResources(), version, ENoProfile, false, false, messages, &processed, includer ) )
	{
		log = shader.getInfoLog();
		return false;
	}
	const char* pstr = processed.c_str();
	shader.setStrings( &pstr, 1 );
	if( !shader.parse( GetDefaultResources(), 100, false, messages ) )
	{
		log = shader.getInfoLog();
		return false;
	}
	glslang::TProgram prog;
	prog.addShader( &shader );
	if( !prog.link( messages ) )
	{
		log = prog.getInfoLog();
		return false;
	}
	glslang::SpvOptions opts;
	opts.disableOptimizer  = true;
	opts.generateDebugInfo = true;// garde les noms des variables (utilisés pour lier les uniforms)
	opts.validate          = false;
	spv::SpvBuildLogger logger;
	glslang::GlslangToSpv( *prog.getIntermediate( st ), spv, &logger, &opts );
	log = logger.getAllMessages();
	return !spv.empty();
}

// SHADERed : ShaderCompiler::ConvertToGLSL
std::string ConvertToGLSL( const std::vector< unsigned int >& spvIn, Lang inLang, Stage sType, bool tsUsed, bool gsUsed, std::string& log )
{
	if( spvIn.empty() )
		return "";
	try
	{
		spirv_cross::CompilerGLSL glsl( spvIn );
		spirv_cross::CompilerGLSL::Options options;
		options.version = sType == Stage::Compute ? 430 : 430;
		glsl.set_common_options( options );

		spv::ExecutionModel model = spv::ExecutionModelMax;
		switch( sType )
		{
		case Stage::Vertex: model = spv::ExecutionModelVertex; break;
		case Stage::Pixel: model = spv::ExecutionModelFragment; break;
		case Stage::Geometry: model = spv::ExecutionModelGeometry; break;
		case Stage::Compute: model = spv::ExecutionModelGLCompute; break;
		case Stage::TessControl: model = spv::ExecutionModelTessellationControl; break;
		case Stage::TessEval: model = spv::ExecutionModelTessellationEvaluation; break;
		}
		for( auto& e : glsl.get_entry_points_and_stages() )
			if( e.execution_model == model )
			{
				glsl.set_entry_point( e.name, model );
				break;
			}

		// noms des entrées / sorties alignés entre étapes (outputVS0 -> entrée du PS…)
		spirv_cross::ShaderResources resources = glsl.get_shader_resources();
		std::string outputName = "outputVS";
		if( sType == Stage::TessControl ) outputName = "outputTCS";
		else if( sType == Stage::TessEval ) outputName = "outputTES";
		else if( sType == Stage::Geometry ) outputName = "outputGS";
		else if( sType == Stage::Pixel ) outputName = "outputPS";
		for( auto& r : resources.stage_outputs )
			glsl.set_name( r.id, outputName + std::to_string( glsl.get_decoration( r.id, spv::DecorationLocation ) ) );
		std::string inputName = "inputVS";
		if( sType == Stage::Pixel ) inputName = gsUsed ? "outputGS" : ( tsUsed ? "outputTES" : "outputVS" );
		else if( sType == Stage::Geometry ) inputName = tsUsed ? "outputTES" : "outputVS";
		else if( sType == Stage::TessControl ) inputName = "outputVS";
		else if( sType == Stage::TessEval ) inputName = "outputTCS";
		for( auto& r : resources.stage_inputs )
			glsl.set_name( r.id, inputName + std::to_string( glsl.get_decoration( r.id, spv::DecorationLocation ) ) );

		glsl.build_dummy_sampler_for_combined_images();
		glsl.build_combined_image_samplers();
		spirv_cross_util::inherit_combined_sampler_bindings( glsl );
		std::string source = glsl.compile();

		// SHADERed : les UBO deviennent des uniforms séparés (les variables du projet portent leurs noms)
		std::stringstream ss( source );
		std::string line;
		std::string out;
		bool inUBO = false;
		std::vector< std::string > uboNames;
		if( inLang == Lang::HLSL )
		{
			while( std::getline( ss, line ) )
			{
				if( line.find( "layout(binding" ) != std::string::npos && line.find( "uniform" ) != std::string::npos &&
					line.find( "sampler" ) == std::string::npos && line.find( "image" ) == std::string::npos && line.find( " buffer " ) == std::string::npos )
				{
					inUBO = true;
					continue;
				}
				else if( inUBO )
				{
					if( line == "{" )
						continue;
					else if( !line.empty() && line[ 0 ] == '}' )
					{
						inUBO = false;
						uboNames.push_back( line.substr( 2, line.size() - 3 ) );
						continue;
					}
					else
					{
						size_t playout = line.find( ")" );
						if( playout != std::string::npos )
							line.erase( 0, playout + 2 );
						line = "uniform " + line;
					}
				}
				else
				{
					for( auto& n : uboNames )
					{
						std::string what = n + ".";
						for( size_t j = line.find( what ); j != std::string::npos; j = line.find( what ) )
							line.erase( j, what.size() );
					}
				}
				out += line + "\n";
			}
			source = out;
		}
		else if( inLang == Lang::VulkanGLSL )
		{
			size_t deleteUboPos = 0, deleteUboLength = 0;
			while( std::getline( ss, line ) )
			{
				if( line.find( "layout(binding" ) != std::string::npos && line.find( "uniform" ) != std::string::npos &&
					line.find( "sampler" ) == std::string::npos && line.find( "image" ) == std::string::npos && line.find( " buffer " ) == std::string::npos )
				{
					inUBO           = true;
					deleteUboPos    = out.size();
					size_t last     = line.find_last_of( ' ' );
					std::string nm  = line.substr( last + 1 );
					std::string nl  = "uniform struct " + nm + " {\n";
					deleteUboLength = nl.size();
					out += nl;
					continue;
				}
				else if( inUBO )
				{
					if( line == "{" )
						continue;
					else if( !line.empty() && line[ 0 ] == '}' )
					{
						inUBO              = false;
						std::string uboName = line.substr( 2, line.size() - 3 );
						bool generated     = !uboName.empty() && uboName[ 0 ] == '_';
						for( size_t i = 1; i < uboName.size() && generated; ++i )
							generated = isdigit( (unsigned char)uboName[ i ] ) != 0;
						if( generated )
						{
							uboNames.push_back( uboName );
							out.erase( deleteUboPos, deleteUboLength );
							for( size_t loc = deleteUboPos; loc < out.size(); loc++ )
							{
								out.insert( loc, "uniform " );
								loc = out.find_first_of( ';', loc ) + 1;
							}
						}
						else
							out += "} " + uboName + ";\n";
						continue;
					}
					else
					{
						// les qualificatifs layout(…) ne sont pas permis dans un struct
						size_t pl = line.find( "layout(" );
						if( pl != std::string::npos )
						{
							size_t pe = line.find( ')', pl );
							if( pe != std::string::npos )
								line.erase( pl, pe - pl + 2 );
						}
					}
				}
				else
				{
					for( auto& n : uboNames )
					{
						std::string what = n + ".";
						for( size_t j = line.find( what ); j != std::string::npos; j = line.find( what ) )
							line.erase( j, what.size() );
					}
				}
				out += line + "\n";
			}
			source = out;
		}
		return source;
	}
	catch( const std::exception& e )
	{
		log += std::string( "SPIRV-Cross : " ) + e.what() + "\n";
		return "";
	}
}

// GLSL : #include (dossier du fichier, puis dossiers d'include), comme SHADERed
void ExpandIncludes( std::string& src, const std::vector< std::string >& dirs, std::vector< std::string > stack, int depth )
{
	if( depth > 16 )
		return;
	size_t incLoc = src.find( "#include" );
	while( incLoc != std::string::npos )
	{
		if( incLoc != 0 && src[ incLoc - 1 ] != '\n' )
		{
			incLoc = src.find( "#include", incLoc + 1 );
			continue;
		}
		size_t q0 = src.find_first_of( "\"<", incLoc );
		size_t q1 = q0 == std::string::npos ? std::string::npos : src.find_first_of( "\">", q0 + 1 );
		size_t eol = src.find( '\n', incLoc );
		if( q0 == std::string::npos || q1 == std::string::npos )
			break;
		std::string fileName = src.substr( q0 + 1, q1 - q0 - 1 );
		src.erase( incLoc, ( eol == std::string::npos ? src.size() : eol ) - incLoc );
		for( auto& d : dirs )
		{
			std::string p = IsAbsolute( fileName ) ? fileName : JoinPath( d, fileName );
			if( std::count( stack.begin(), stack.end(), p ) )
				break;
			std::string inc;
			if( ReadFileText( p, inc ) )
			{
				auto st = stack;
				st.push_back( p );
				ExpandIncludes( inc, dirs, st, depth + 1 );
				src.insert( incLoc, inc );
				break;
			}
		}
		incLoc = src.find( "#include", incLoc + 1 );
	}
}

// Insère les macros après la ligne #version (ou en tête)
void ApplyMacros( std::string& src, const std::vector< Macro >& macros )
{
	std::string m = MacroPreamble( macros );
	size_t v      = src.find( "#version" );
	if( v == std::string::npos )
	{
		src = m + src;
		return;
	}
	size_t eol = src.find( '\n', v );
	src.insert( eol == std::string::npos ? src.size() : eol + 1, m );
}

// GLSL ES (WebGL, version web de SHADERed) -> GLSL desktop équivalent
void DesktopVersion( std::string& src, Stage stage )
{
	static const std::regex reVer( R"(#version\s+(\d+)(\s+es)?[^\n]*)" );
	std::smatch m;
	if( !std::regex_search( src, m, reVer ) )
	{
		src = std::string( stage == Stage::Compute ? "#version 430\n" : "#version 330\n" ) + src;
		return;
	}
	int ver = atoi( m[ 1 ].str().c_str() );
	bool es = m[ 2 ].matched;
	if( !es )
		return;
	std::string repl;
	if( ver <= 100 )
		repl = "#version 120";
	else if( ver < 310 )
		repl = "#version 330";
	else if( ver < 320 )
		repl = "#version 430";
	else
		repl = "#version 450";
	src.replace( m.position( 0 ), m.length( 0 ), repl );
}
}//namespace

std::vector< std::string > SamplerOrder( const std::string& glsl )
{
	std::vector< std::string > out;
	static const std::regex re( R"([a-z]?sampler[A-Za-z0-9]* +([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*;)" );
	for( std::sregex_iterator it( glsl.begin(), glsl.end(), re ), end; it != end; ++it )
		out.push_back( ( *it )[ 1 ].str() );
	return out;
}

std::string ExternalCompilerMessage( Lang lang )
{
	switch( lang )
	{
	case Lang::Rust:
		return "Shader Rust (rust-gpu) : il faut le compilateur Rust GPU installé sur la machine, qui ne peut pas être "
			   "intégré au plugin. Exporte-le en GLSL ou HLSL depuis SHADERed (Fichier > Exporter) puis réimporte-le.";
	case Lang::Cpp:
		return "Shader C++ (compilateur Circle) : ce compilateur n'existe que sous Linux et ne peut pas tourner dans "
			   "Resolume sous Windows. Exporte-le en GLSL ou HLSL depuis SHADERed puis réimporte-le.";
	case Lang::Slang:
		return "Shader Slang : nécessite le compilateur Slang, non intégré. Exporte-le en GLSL ou HLSL depuis SHADERed.";
	default:
		return "Langage non pris en charge.";
	}
}

StageResult CompileStageSource( const std::string& sourceIn, Lang lang, const std::string& path, Stage stage, const std::string& entry,
								const std::vector< Macro >& macros, const std::vector< std::string >& includeDirs, bool gsUsed, bool tsUsed )
{
	StageResult r;
	r.lang = lang;
	if( lang == Lang::Rust || lang == Lang::Cpp || lang == Lang::Slang )
	{
		r.log = ExternalCompilerMessage( lang );
		return r;
	}
	std::vector< std::string > dirs = { ParentDir( path ) };
	dirs.insert( dirs.end(), includeDirs.begin(), includeDirs.end() );

	if( lang == Lang::GLSL || lang == Lang::GLSLES )
	{
		std::string src = sourceIn;
		ExpandIncludes( src, dirs, {}, 0 );
		DesktopVersion( src, stage );
		ApplyMacros( src, macros );
		// point d'entrée différent de main() : renommé (SHADERed n'accepte que main en GLSL)
		if( !entry.empty() && entry != "main" )
		{
			static const std::regex reMain( R"(\bmain\s*\()" );
			src = std::regex_replace( src, reMain, "stfx_original_main(" );
			src = std::regex_replace( src, std::regex( "\\b" + entry + "\\s*\\(" ), "main(" );
		}
		r.glsl = src;
		r.ok   = true;
		return r;
	}

	std::vector< unsigned int > spv;
	if( !CompileToSpirv( sourceIn, lang, path, stage, entry, macros, includeDirs, spv, r.log ) )
		return r;
	r.glsl = ConvertToGLSL( spv, lang, stage, tsUsed, gsUsed, r.log );
	r.ok   = !r.glsl.empty();
	return r;
}

StageResult CompileStage( const std::string& path, Stage stage, const std::string& entry, const std::vector< Macro >& macros,
						  const std::vector< std::string >& includeDirs, bool gsUsed, bool tsUsed )
{
	std::string src;
	if( !ReadFileText( path, src ) )
	{
		StageResult r;
		r.log = "Fichier introuvable : " + path;
		return r;
	}
	return CompileStageSource( src, LangFromPath( path, src ), path, stage, entry, macros, includeDirs, gsUsed, tsUsed );
}
}//namespace sed
}//namespace stfx
