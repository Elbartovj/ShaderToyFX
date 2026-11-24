// Shader Loader - pont local (canal nommé) entre les instances du plugin et le serveur MCP de Claude Desktop
#pragma once
#include <string>

namespace stfx
{
namespace bridge
{
// Démarre le serveur du pont (une fois par DLL)
void Start();
// Traite une requête JSON (tests)
std::string HandleText( const std::string& request );
}//namespace bridge
}//namespace stfx
