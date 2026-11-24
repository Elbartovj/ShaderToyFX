// ShaderToyFX - import depuis d'autres sites de shaders (ShaderKit, FragCoord, Shaderoo, NEORT…)
// et conversion des shaders GLSL « main() » (GLSLSandbox, The Book of Shaders, NEORT…) au format Shadertoy.
// Le favori universel exporte depuis le navigateur un fichier « Titre [site-id].json/.glsl » dans les
// Téléchargements ; ShaderToyFX le convertit en JSON Shadertoy rangé dans Documents\ShaderToyFX\<Site>.
#pragma once
#include <string>
#include <vector>

namespace stfx
{
namespace sites
{
// Code JavaScript du favori universel (Shadertoy, SHADERed, ShaderKit, FragCoord, Shaderoo, NEORT)
const char* UniversalBookmarkletCode();

// Fichiers « … [shaderkit-…].json », « … [fragcoord-…].glsl »… à la racine des Téléchargements
std::vector< std::string > FindDownloadedExports();

// Convertit et range l'export ; supprime le fichier téléchargé. false si illisible (ou pas encore écrit).
bool ImportDownloadedExport( const std::string& file, std::string& savedPath, std::string& title, std::string& err );

// Paquet exporté par le favori -> JSON Shadertoy (renderpass…). Les images embarquées sont
// enregistrées dans le cache média.
bool ConvertBundle( const std::string& bundleJson, std::string& shadertoyJson, std::string& title, std::string& siteDir,
					std::string& err );

// GLSL avec void main() / gl_FragColor (uniforms time, resolution, mouse, backbuffer… ou u_time, u_resolution…)
// -> JSON Shadertoy (passe Image, + Buffer A si le shader lit l'image précédente). false si ce n'est pas ce format.
bool ConvertMainStyleGlsl( const std::string& code, const std::string& title, const std::string& author, std::string& shadertoyJson,
						   std::string& err );
}//namespace sites
}//namespace stfx
