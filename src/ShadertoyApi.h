// ShaderToyFX - client de l'API REST Shadertoy (https://www.shadertoy.com/howto)
// Seuls les shaders publiés en "Public + API" sont accessibles par l'API.
#pragma once
#include <string>
#include <vector>

namespace stfx
{
struct ShaderSummary
{
	std::string id;
	std::string name;
	std::string author;
	std::string description;
	std::string tags;
	int views  = 0;
	int likes  = 0;
	int passes = 0;
	bool hasBuffers   = false;
	bool hasSoundPass = false;
	bool hasCubePass  = false;
	std::string json;// JSON normalisé
	std::string imageCode;
};

// Extrait un id à partir d'un lien (…/view/XXXXXX, …/embed/XXXXXX) ou d'un id brut
std::string ExtractShaderId( const std::string& linkOrId );

bool ApiGetShader( const std::string& id, const std::string& key, ShaderSummary& out, std::string& err );

// sort : name | love | popular | newest | hot
bool ApiQuery( const std::string& term, const std::string& sort, int from, int num, const std::string& key,
			   std::vector< std::string >& ids, int& total, std::string& err );

// Liste complète des ids publics (mise en cache mémoire + disque 24 h)
bool ApiListAll( const std::string& key, std::vector< std::string >& ids, std::string& err );

bool SummaryFromJson( const std::string& normalizedJson, ShaderSummary& out );

// Enregistre le JSON dans Documents\ShaderToyFX\Shadertoy\<titre> [id].json et renvoie le chemin
std::string SaveToCache( const ShaderSummary& s );
std::string FindCachedPath( const std::string& id );

// Récupère (cache ou réseau) un shader et renvoie le chemin du fichier JSON en cache
bool FetchShaderToCache( const std::string& id, std::string& path, std::string& title, std::string& err );

// Mode sans clé : importe un export du bookmarklet ("Titre [id].json") dans le cache
// (le fichier d'origine est supprimé). Renvoie false si le fichier n'est pas (encore) lisible.
bool ImportShaderJsonFile( const std::string& file, std::string& cachedPath, std::string& title, std::string& err );

// Fichiers "… [xxxxxx].json" présents à la racine des Téléchargements
std::vector< std::string > FindDownloadedExports();

// Code JavaScript du bookmarklet (à mettre comme URL d'un favori)
const char* BookmarkletCode();

// Choisit un shader aléatoire (en ligne si clé API, sinon parmi les fichiers locaux)
bool PickRandomShader( std::string& path, std::string& title, std::string& err );
}//namespace stfx
