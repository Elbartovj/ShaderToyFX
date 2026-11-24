// Shader Loader - export d'un shader en plugin FFGL autonome.
// Le plugin exporté est une copie de la DLL Shader Loader suivie d'un bloc de données (le shader, ses images,
// son nom et son identifiant). Au chargement, une DLL qui porte ce bloc devient ce plugin-là : nom et type
// propres dans Resolume, sliders du shader, sans bibliothèque ni panneau.
#pragma once
#include "ShaderDoc.h"
#include <string>

namespace stfx
{
namespace standalone
{
struct Payload
{
	bool present = false;
	std::string name;       // nom affiché dans Resolume (16 caractères au plus)
	std::string id;         // identifiant FFGL (4 caractères)
	bool source = false;    // source (sans entrée) ou effet
	std::string description;
	std::string docPath;
};
// Bloc de données de la DLL en cours (lu une fois, au chargement : nom, identifiant, type)
const Payload& Get();
// Écrit le shader et ses images dans le cache média ; retourne le chemin du shader (vide si pas de bloc)
std::string PrepareShaderFile();

struct ExportOptions
{
	std::string name;   // nom du plugin
	bool source = false;
	std::string outPath;// chemin du .dll à écrire
};
// Identifiant FFGL dérivé du nom
std::string MakePluginId( const std::string& name );
// Dossier de la DLL en cours (là où Resolume cherche les plugins)
std::string ModuleDir();
// Écrit le plugin autonome (bloquant). doc : shader avec les valeurs par défaut voulues.
bool WritePlugin( const ShaderDoc& doc, const ExportOptions& opt, std::string& err );
// tests
void SetModulePathForTests( const std::string& path );
bool ReadPayloadForTests( const std::string& path, std::string& payload );
}//namespace standalone
}//namespace stfx
