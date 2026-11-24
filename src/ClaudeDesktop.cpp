// Shader Loader - connexion à Claude Desktop (voir ClaudeDesktop.h)
#include "ClaudeDesktop.h"
#include "Util.h"

#include <nlohmann/json.hpp>

#include <filesystem>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

extern const unsigned char kMcpExe[];
extern const size_t kMcpExeSize;

using json   = nlohmann::json;
namespace fs = std::filesystem;

namespace stfx
{
namespace claudedesktop
{
namespace
{
std::string KnownFolder( int csidl )
{
#ifdef _WIN32
	wchar_t buf[ MAX_PATH ] = {};
	if( SUCCEEDED( SHGetFolderPathW( nullptr, csidl, nullptr, 0, buf ) ) )
		return Narrow( buf );
#endif
	return {};
}

// Dossiers de configuration de Claude Desktop : installation classique (%APPDATA%\Claude) et version
// Microsoft Store (%LOCALAPPDATA%\Packages\Claude_*\LocalCache\Roaming\Claude)
std::vector< std::string > ConfigDirs()
{
	std::vector< std::string > dirs;
	std::error_code ec;
	std::string roaming = KnownFolder( CSIDL_APPDATA );
	if( !roaming.empty() )
		dirs.push_back( JoinPath( roaming, "Claude" ) );
	std::string local = KnownFolder( CSIDL_LOCAL_APPDATA );
	if( !local.empty() )
		for( auto& e : fs::directory_iterator( fs::path( Widen( JoinPath( local, "Packages" ) ) ), ec ) )
		{
			std::string n = ToLower( Narrow( e.path().filename().wstring() ) );
			if( n.rfind( "claude", 0 ) == 0 || n.find( "anthropic" ) != std::string::npos )
			{
				std::string d = JoinPath( Narrow( e.path().wstring() ), "LocalCache\\Roaming\\Claude" );
				if( fs::exists( fs::path( Widen( d ) ), ec ) )
					dirs.push_back( d );
			}
		}
	return dirs;
}

bool AddServer( const std::string& dir, const std::string& exe, std::string& err )
{
	std::string path = JoinPath( dir, "claude_desktop_config.json" );
	json cfg         = json::object();
	std::string text;
	if( ReadFileText( path, text ) && !Trim( text ).empty() )
	{
		cfg = json::parse( text, nullptr, false );
		if( !cfg.is_object() )
		{
			err = path + " n'est pas un JSON valide : corrige-le ou supprime-le, puis réessaie";
			return false;
		}
		WriteFileText( path + ".bak", text );
	}
	if( !cfg.contains( "mcpServers" ) || !cfg[ "mcpServers" ].is_object() )
		cfg[ "mcpServers" ] = json::object();
	cfg[ "mcpServers" ][ "shader-loader" ] = { { "command", exe }, { "args", json::array() } };
	MakeDirs( dir );
	if( !WriteFileText( path, cfg.dump( 2 ) ) )
	{
		err = "écriture impossible : " + path;
		return false;
	}
	return true;
}
}//namespace

bool Connect( std::string& message )
{
#ifdef _WIN32
	// 1) le serveur MCP, copié dans %APPDATA%\ShaderToyFX
	std::string base = KnownFolder( CSIDL_APPDATA );
	if( base.empty() )
	{
		message = "dossier AppData introuvable";
		return false;
	}
	std::string dir = JoinPath( base, "ShaderToyFX" );
	MakeDirs( dir );
	std::string exe = JoinPath( dir, "ShaderLoaderMCP.exe" );
	if( !WriteFileBytes( exe, kMcpExe, kMcpExeSize ) )
	{
		// déjà lancé par Claude Desktop (fichier verrouillé) : même contenu si la taille correspond
		std::error_code ec;
		if( !fs::exists( fs::path( Widen( exe ) ), ec ) || fs::file_size( fs::path( Widen( exe ) ), ec ) != kMcpExeSize )
		{
			message = "impossible d'écrire " + exe + " (quitte Claude Desktop puis réessaie)";
			return false;
		}
	}
	// 2) la configuration de Claude Desktop
	auto dirs = ConfigDirs();
	std::string done, err;
	for( auto& d : dirs )
	{
		std::error_code ec;
		bool exists = fs::exists( fs::path( Widen( d ) ), ec );
		// %APPDATA%\Claude est créé s'il manque ; les dossiers Store doivent exister
		if( !exists && d.find( "Packages" ) != std::string::npos )
			continue;
		std::string e;
		if( AddServer( d, exe, e ) )
			done += ( done.empty() ? "" : ", " ) + JoinPath( d, "claude_desktop_config.json" );
		else
			err = e;
	}
	if( done.empty() )
	{
		message = err.empty() ? "configuration de Claude Desktop introuvable (Claude Desktop est-il installé ?)" : err;
		return false;
	}
	message = "Shader Loader ajouté à Claude Desktop (" + done + "). Quitte complètement Claude Desktop (icône près de l'horloge > Quitter) puis rouvre-le.";
	return true;
#else
	message = "Windows seulement";
	return false;
#endif
}
}//namespace claudedesktop
}//namespace stfx
