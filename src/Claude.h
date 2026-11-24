// Shader Loader - « améliorateur » : modifie un shader existant à partir d'une consigne, avec l'API Claude
// (clé API Anthropic de l'utilisateur). Le résultat est vérifié par glslang ; en cas d'erreur de compilation,
// Claude reçoit les erreurs et corrige (2 essais au plus).
#pragma once
#include "ShaderDoc.h"
#include <atomic>
#include <functional>
#include <string>

namespace stfx
{
namespace claude
{
struct Model
{
	const char* id;
	const char* label;
};
const std::vector< Model >& Models();

struct Result
{
	bool ok = false;
	std::string error;  // si !ok
	std::string summary;// ce qui a été changé (une ou deux phrases)
	ShaderDoc doc;      // shader modifié (code des passes et de Common)
	std::string compileLog;// erreurs restantes si la dernière version ne compile pas
	int attempts = 0;
};

// Bloquant (thread de travail). progress : message d'étape ; cancel : mis à true pour abandonner.
Result Improve( const std::string& apiKey, const std::string& model, const std::string& prompt, const ShaderDoc& doc,
				const std::function< void( const std::string& ) >& progress, const std::atomic< bool >& cancel );

// Erreurs de compilation (vide si toutes les passes compilent)
std::string CheckCompiles( ShaderDoc& doc );
}//namespace claude
}//namespace stfx
