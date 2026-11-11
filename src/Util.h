// ShaderToyFX - utilitaires communs (chemins, UTF-8, fichiers, réglages)
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace stfx
{
std::wstring Widen( const std::string& utf8 );
std::string Narrow( const std::wstring& wide );

bool ReadFileBytes( const std::string& utf8Path, std::vector< uint8_t >& out );
bool ReadFileText( const std::string& utf8Path, std::string& out );
bool WriteFileBytes( const std::string& utf8Path, const void* data, size_t size );
bool WriteFileText( const std::string& utf8Path, const std::string& text );
bool FileExists( const std::string& utf8Path );
void MakeDirs( const std::string& utf8Path );
std::string FileExtLower( const std::string& utf8Path );
std::string FileStem( const std::string& utf8Path );
std::string ParentDir( const std::string& utf8Path );
std::string JoinPath( const std::string& a, const std::string& b );
std::string SanitizeFileName( const std::string& name );
std::string ToLower( std::string s );
std::string Trim( const std::string& s );

// Dossiers : Documents\ShaderToyFX\{Shadertoy, Local, media}
std::string RootDir();
std::string ShadertoyCacheDir();
std::string LocalDir();
std::string MediaDir();
std::string DownloadsDir();

// Liste récursive des shaders (.json/.glsl/.frag/.txt) dans un dossier
struct LocalEntry
{
	std::string path;
	std::string name;
	std::string folder;   // nom du dossier parent
	std::string relFolder;// chemin du dossier relatif à la racine ("" = racine)
	int64_t modified = 0;
};
std::vector< LocalEntry > ListShaderFiles( const std::string& dir );

// Dossiers de rangement (relatifs à RootDir, hors "media"), triés
std::vector< std::string > ListFolders();
// Déplace un fichier dans un dossier (relatif à RootDir) ; renvoie le nouveau chemin ou "" si échec
std::string MoveToFolder( const std::string& file, const std::string& relFolder );
// Envoie un fichier à la corbeille Windows
bool RecycleFile( const std::string& file );
// Retrouve un shader déplacé : même nom de fichier ailleurs dans RootDir
std::string FindByFileName( const std::string& missingPath );

void OpenInExplorer( const std::string& utf8Path );
void OpenUrl( const std::string& url );

// Réglages persistants (%APPDATA%\ShaderToyFX\settings.json)
struct Settings
{
	std::string apiKey;
	int perPage           = 25;
	std::string sort      = "popular";
	bool skipUnsupported  = true;// le bouton Random évite les shaders "sound"/"cubemap pass"
	bool shaderAlpha      = false;
	bool watchDownloads   = true;// importe les "Titre [id].json" exportés par le bookmarklet
	bool autoLoadDownloads = true;// et les charge directement dans l'instance ciblée
};
Settings GetSettings();
void SaveSettings( const Settings& s );

// petit générateur aléatoire thread-safe
uint32_t RandomU32();
}//namespace stfx
