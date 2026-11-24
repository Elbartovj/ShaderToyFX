// ShaderToyFX - compilation des shaders SHADERed vers du GLSL exécutable par OpenGL.
// GLSL : utilisé tel quel (includes + macros) ; GLSL ES : version desktop équivalente ;
// HLSL et GLSL Vulkan : glslang -> SPIR-V -> SPIRV-Cross -> GLSL (même chaîne que SHADERed).
#pragma once
#include "SedProject.h"

#include <string>
#include <vector>

namespace stfx
{
namespace sed
{
enum class Stage
{
	Vertex, Pixel, Geometry, TessControl, TessEval, Compute
};

struct StageResult
{
	bool ok = false;
	std::string glsl;// code GLSL prêt pour glShaderSource
	std::string log; // erreurs / avertissements
	Lang lang = Lang::GLSL;
};

// Traduit un fichier source d'une étape. gsUsed/tsUsed : nom des entrées/sorties (SPIRV-Cross).
StageResult CompileStage( const std::string& path, Stage stage, const std::string& entry, const std::vector< Macro >& macros,
						  const std::vector< std::string >& includeDirs, bool gsUsed, bool tsUsed );
// Même chose depuis une chaîne (sans fichier)
StageResult CompileStageSource( const std::string& source, Lang lang, const std::string& pathHint, Stage stage, const std::string& entry,
								const std::vector< Macro >& macros, const std::vector< std::string >& includeDirs, bool gsUsed, bool tsUsed );

// Noms des samplers déclarés dans l'ordre (règle de SHADERed pour les unités de texture en GLSL)
std::vector< std::string > SamplerOrder( const std::string& glsl );

// Explication pour les langages qui demandent un compilateur externe (Rust, C++, Slang)
std::string ExternalCompilerMessage( Lang lang );
}//namespace sed
}//namespace stfx
