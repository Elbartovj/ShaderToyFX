// Shader Loader - connexion à Claude Desktop sans extension : installe ShaderLoaderMCP.exe (intégré à la DLL)
// et l'ajoute aux serveurs MCP de claude_desktop_config.json.
#pragma once
#include <string>

namespace stfx
{
namespace claudedesktop
{
// Retourne false avec un message d'erreur ; true avec le résumé (fichiers modifiés)
bool Connect( std::string& message );
}//namespace claudedesktop
}//namespace stfx
