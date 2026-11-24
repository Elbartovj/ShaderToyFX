// ShaderToyFX - import depuis d'autres sites de shaders (ShaderKit, FragCoord, Shaderoo, NEORT…)
// et conversion des shaders GLSL « main() » (GLSLSandbox, The Book of Shaders, NEORT…) au format Shadertoy.
// Le favori universel exporte depuis le navigateur un fichier « Titre [site-id].json/.glsl » dans les
// Téléchargements ; ShaderToyFX le convertit en JSON Shadertoy rangé dans Documents\ShaderToyFX\<Site>.
#pragma once
#include <functional>
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
// Enregistre un shader converti dans Documents\ShaderToyFX\<Site>\Titre [site-id].ext
bool SaveImported( const std::string& site, const std::string& id, const std::string& title, const std::string& siteDir,
				   const std::string& content, const std::string& ext, std::string& savedPath, std::string& err );

//------------------------------------------------------------------ accès direct (sans navigateur)
// Sites parcourables depuis le plugin : shaderkit, fragcoord, shaderoo, neort, shadered.
struct WebSite
{
	const char* key;
	const char* name;
	const char* home;
	std::vector< const char* > sortLabels;
	const char* searchHint;
};
const std::vector< WebSite >& WebSites();

// Lien ou texte -> site (shaderkit, fragcoord, shaderoo, neort) et identifiant. SHADERed et Shadertoy : voir leurs modules.
bool ParseSiteLink( const std::string& text, std::string& site, std::string& id );
std::string SiteShaderUrl( const std::string& site, const std::string& id );

// Télécharge le shader (API publique ou page du site), le convertit et l'enregistre. Bloquant : thread de travail.
bool FetchAndImport( const std::string& site, const std::string& id, std::string& savedPath, std::string& title, std::string& err );

struct WebEntry
{
	std::string id, title, author, thumb, info;// thumb : URL https ou data:image/…
};
// Une page de résultats. cursor : "" pour la première page ; next : curseur de la page suivante ("" = fin).
bool Browse( const std::string& site, const std::string& query, int sortIdx, const std::string& cursor, std::vector< WebEntry >& out,
			 std::string& next, std::string& err );

// Tests : remplace les accès réseau (méthode, URL, corps) -> réponse.
struct FetchResult
{
	int status = 0;
	std::string body, error;
	bool Ok() const { return error.empty() && status >= 200 && status < 300; }
};
using FetchHook = std::function< FetchResult( const std::string& method, const std::string& url, const std::string& body ) >;
void SetFetchHook( FetchHook hook );
}//namespace sites
}//namespace stfx
