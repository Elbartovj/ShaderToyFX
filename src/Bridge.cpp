// Shader Loader - pont local pour Claude Desktop.
// Chaque DLL chargée par Resolume ouvre un canal nommé local \\.\pipe\ShaderLoaderBridge-<pid>-<module>.
// Le serveur MCP (ShaderLoaderMCP.exe, lancé par Claude Desktop) s'y connecte pour lire, modifier,
// compiler, enregistrer et capturer les shaders des instances. Une requête JSON par connexion, une réponse JSON.
#include "Bridge.h"
#include "Claude.h"
#include "Export.h"
#include "Instance.h"
#include "Util.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <thread>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

namespace stfx
{
namespace bridge
{
namespace
{
const char* ChannelLabel( const ChannelDesc& c )
{
	static const char* letters[] = { "Buffer A", "Buffer B", "Buffer C", "Buffer D" };
	switch( c.kind )
	{
	case ChannelKind::Input: return "input (video from Resolume)";
	case ChannelKind::Audio: return "audio (Resolume FFT, 512x2: y=0.25 spectrum, y=0.75 waveform)";
	case ChannelKind::Keyboard: return "keyboard (always empty)";
	case ChannelKind::Buffer: return c.buffer >= 0 && c.buffer < 4 ? letters[ c.buffer ] : "buffer";
	case ChannelKind::Texture: return "image texture";
	case ChannelKind::Cubemap: return "cubemap texture (samplerCube)";
	case ChannelKind::Volume: return "3D texture (sampler3D)";
	default: return "nothing (black)";
	}
}

json Describe( const SharedPtr& s, bool withCode )
{
	InstanceSnapshot snap = Snapshot( s );
	json j                = { { "id", snap.id }, { "status", snap.status }, { "compiled", snap.lastBuildOk } };
	if( !snap.log.empty() )
		j[ "log" ] = snap.log.substr( 0, 6000 );
	{
		std::lock_guard< std::mutex > lock( s->m );
		j[ "module" ] = s->moduleKind;
	}
	if( FocusedInstance() == s )
		j[ "focused_in_panel" ] = true;
	const ShaderDoc* d = snap.current.get();
	if( !d )
	{
		j[ "shader" ] = nullptr;
		return j;
	}
	j[ "title" ]  = d->title;
	j[ "author" ] = d->author;
	j[ "file" ]   = d->sourcePath;
	j[ "editable" ] = !d->IsShadered();
	if( !withCode )
		return j;
	if( d->IsShadered() )
	{
		j[ "note" ] = "SHADERed project / Godot shader: not editable through this bridge.";
		return j;
	}
	j[ "common" ] = d->common;
	json passes   = json::array();
	for( auto& p : d->passes )
	{
		json ch = json::object();
		for( int i = 0; i < 4; ++i )
			if( p.ch[ i ].kind != ChannelKind::None )
				ch[ "iChannel" + std::to_string( i ) ] = ChannelLabel( p.ch[ i ] );
		passes.push_back( { { "name", p.name }, { "channels", ch }, { "code", p.code } } );
	}
	j[ "passes" ] = passes;
	json params   = json::array();
	for( auto& p : d->params )
		params.push_back( { { "name", p.name }, { "min", p.minV }, { "max", p.maxV }, { "default", p.def } } );
	j[ "sliders" ] = params;
	return j;
}

SharedPtr FindInstance( const json& req, std::string& err )
{
	auto all = ListInstances();
	if( req.contains( "id" ) && req[ "id" ].is_number_integer() )
	{
		int id = req[ "id" ].get< int >();
		for( auto& s : all )
			if( Snapshot( s ).id == id )
				return s;
		err = "instance " + std::to_string( id ) + " introuvable";
		return nullptr;
	}
	if( auto f = FocusedInstance() )
		for( auto& s : all )
			if( s == f )
				return s;
	if( all.size() == 1 )
		return all[ 0 ];
	err = all.empty() ? "aucune instance Shader Loader dans Resolume" : "plusieurs instances : précise laquelle";
	return nullptr;
}

// attend la fin de la compilation demandée (le rendu doit tourner dans Resolume)
bool WaitBuild( const SharedPtr& s, uint64_t serialBefore, int timeoutMs )
{
	auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds( timeoutMs );
	while( std::chrono::steady_clock::now() < end )
	{
		{
			std::lock_guard< std::mutex > lock( s->m );
			if( s->buildSerial != serialBefore && !s->pendingDoc )
				return true;
		}
		std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
	}
	return false;
}

json Handle( const json& req )
{
	std::string cmd = req.value( "cmd", std::string() );
	if( cmd == "list" )
	{
		json arr = json::array();
		for( auto& s : ListInstances() )
			arr.push_back( Describe( s, false ) );
		return { { "ok", true }, { "instances", arr } };
	}
	std::string err;
	SharedPtr s = FindInstance( req, err );
	if( !s )
		return { { "ok", false }, { "error", err } };
	InstanceSnapshot snap = Snapshot( s );
	if( cmd == "get" )
		return { { "ok", true }, { "instance", Describe( s, true ) } };

	if( cmd == "update" )
	{
		if( !snap.current || snap.current->IsShadered() )
			return { { "ok", false }, { "error", "aucun shader modifiable dans cette instance" } };
		ShaderDoc doc = *snap.current;
		std::string common;
		bool hasCommon = req.contains( "common" ) && req[ "common" ].is_string();
		if( hasCommon )
			common = req[ "common" ].get< std::string >();
		std::vector< PassEdit > edits;
		for( auto& p : req.value( "passes", json::array() ) )
		{
			PassEdit e;
			e.name = p.value( "name", std::string() );
			if( p.contains( "code" ) && p[ "code" ].is_string() )
			{
				e.hasCode = true;
				e.code    = p[ "code" ].get< std::string >();
			}
			if( p.contains( "channels" ) && p[ "channels" ].is_object() )
				for( auto& kv : p[ "channels" ].items() )
				{
					std::string k = ToLower( kv.key() );
					int ch        = k.empty() ? -1 : k.back() - '0';
					if( kv.value().is_string() )
						e.channels.push_back( { ch, kv.value().get< std::string >() } );
				}
			edits.push_back( e );
		}
		std::string editErr;
		if( !ApplyDocEdits( doc, hasCommon ? &common : nullptr, edits, editErr ) )
			return { { "ok", false }, { "error", editErr }, { "applied", false } };
		doc.warnings.clear();
		std::string log = claude::CheckCompiles( doc );
		if( !log.empty() )
			return { { "ok", false }, { "compiled", false }, { "errors", log.substr( 0, 8000 ) }, { "applied", false } };
		uint64_t serial;
		{
			std::lock_guard< std::mutex > lock( s->m );
			serial = s->buildSerial;
		}
		RequestApplyDoc( s, doc, false );
		if( !WaitBuild( s, serial, 8000 ) )
			return { { "ok", true }, { "applied", true }, { "note", "envoyé ; la compilation aura lieu quand le clip sera joué dans Resolume" } };
		InstanceSnapshot after = Snapshot( s );
		if( !after.lastBuildOk )
			return { { "ok", false }, { "compiled", false }, { "errors", after.log.substr( 0, 8000 ) }, { "applied", false } };
		json r = { { "ok", true }, { "compiled", true }, { "applied", true }, { "saved", false } };
		if( !after.log.empty() )
			r[ "warnings" ] = after.log.substr( 0, 4000 );
		if( after.current )
		{
			json params = json::array();
			for( auto& p : after.current->params )
				params.push_back( p.name );
			r[ "sliders" ] = params;
		}
		return r;
	}

	if( cmd == "save" )
	{
		if( !snap.current || snap.current->IsShadered() )
			return { { "ok", false }, { "error", "rien à enregistrer" } };
		ShaderDoc doc = *snap.current;
		std::string ext;
		std::string text = SerializeDoc( doc, ext );
		std::string path = doc.sourcePath;
		bool inRoot      = !path.empty() && ToLower( path ).rfind( ToLower( RootDir() ), 0 ) == 0 && FileExtLower( path ) == ext;
		std::string newName = Trim( req.value( "new_name", std::string() ) );
		if( !inRoot || !newName.empty() )
		{
			MakeDirs( LocalDir() );
			path = JoinPath( LocalDir(), SanitizeFileName( newName.empty() ? ( doc.title.empty() ? std::string( "shader" ) : doc.title ) : newName ) + ext );
		}
		if( !WriteFileText( path, text ) )
			return { { "ok", false }, { "error", "écriture impossible : " + path } };
		doc.sourcePath = path;
		RequestApplyDoc( s, doc, true );
		return { { "ok", true }, { "file", path } };
	}

	if( cmd == "revert" )
	{
		if( !snap.current || snap.current->sourcePath.empty() )
			return { { "ok", false }, { "error", "pas de fichier d'origine" } };
		uint64_t serial;
		{
			std::lock_guard< std::mutex > lock( s->m );
			serial = s->buildSerial;
		}
		RequestLoadFile( s, snap.current->sourcePath, false );
		WaitBuild( s, serial, 8000 );
		return { { "ok", true }, { "file", snap.current->sourcePath } };
	}

	if( cmd == "export" )
	{
		if( !snap.current || snap.current->IsShadered() )
			return { { "ok", false }, { "error", "rien à exporter (projet SHADERed / Godot ou aucun shader)" } };
		std::string name = Trim( req.value( "name", snap.current->title ) ).substr( 0, 16 );
		if( name.empty() )
			name = "Shader";
		bool source;
		{
			std::lock_guard< std::mutex > lock( s->m );
			source = req.value( "type", s->moduleKind ) == "source";
		}
		std::string file = JoinPath( standalone::ModuleDir(), SanitizeFileName( "Shader Loader - " + name ) + ".dll" );
		uint64_t serial;
		{
			std::lock_guard< std::mutex > lock( s->m );
			serial = s->exportSerial;
		}
		RequestExportPlugin( s, name, file, source, req.value( "use_current_values", true ) );
		auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds( 30000 );
		while( std::chrono::steady_clock::now() < end )
		{
			{
				std::lock_guard< std::mutex > lock( s->m );
				if( s->exportSerial != serial )
				{
					if( !s->exportOk )
						return { { "ok", false }, { "error", s->exportResult } };
					return { { "ok", true }, { "file", file }, { "plugin_name", name }, { "type", source ? "source" : "effect" },
							 { "note", "Redémarre Resolume pour voir le nouveau plugin dans la liste des " + std::string( source ? "sources" : "effets" ) + "." } };
				}
			}
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
		}
		return { { "ok", false }, { "error", "l'export n'a pas démarré : le clip de cette instance doit jouer dans Resolume" } };
	}

	if( cmd == "capture" )
	{
		uint64_t serial;
		{
			std::lock_guard< std::mutex > lock( s->m );
			serial              = s->captureSerial;
			s->captureRequested = true;
		}
		auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds( 4000 );
		while( std::chrono::steady_clock::now() < end )
		{
			std::vector< uint8_t > rgba;
			int w = 0, h = 0;
			{
				std::lock_guard< std::mutex > lock( s->m );
				if( s->captureSerial != serial )
				{
					rgba = s->captureRgba;
					w    = s->captureW;
					h    = s->captureH;
				}
			}
			if( w > 0 )
			{
				int len             = 0;
				unsigned char* png  = stbi_write_png_to_mem( rgba.data(), w * 4, w, h, 4, &len );
				std::string bytes( (const char*)png, (size_t)len );
				STBIW_FREE( png );
				static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				std::string b64;
				size_t i = 0;
				for( ; i + 2 < bytes.size(); i += 3 )
				{
					uint32_t v = ( (uint8_t)bytes[ i ] << 16 ) | ( (uint8_t)bytes[ i + 1 ] << 8 ) | (uint8_t)bytes[ i + 2 ];
					b64 += t[ v >> 18 ];
					b64 += t[ ( v >> 12 ) & 63 ];
					b64 += t[ ( v >> 6 ) & 63 ];
					b64 += t[ v & 63 ];
				}
				if( i < bytes.size() )
				{
					uint32_t v = (uint8_t)bytes[ i ] << 16;
					if( i + 1 < bytes.size() )
						v |= (uint8_t)bytes[ i + 1 ] << 8;
					b64 += t[ v >> 18 ];
					b64 += t[ ( v >> 12 ) & 63 ];
					b64 += i + 1 < bytes.size() ? t[ ( v >> 6 ) & 63 ] : '=';
					b64 += '=';
				}
				return { { "ok", true }, { "png_base64", b64 }, { "width", w }, { "height", h } };
			}
			std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
		}
		{
			std::lock_guard< std::mutex > lock( s->m );
			s->captureRequested = false;
		}
		return { { "ok", false }, { "error", "pas d'image : le clip ou la couche de cette instance ne joue pas dans Resolume" } };
	}
	return { { "ok", false }, { "error", "commande inconnue : " + cmd } };
}

#ifdef _WIN32
void ServeLoop( std::wstring name )
{
	for( ;; )
	{
		HANDLE pipe = CreateNamedPipeW( name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
										PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 0, nullptr );
		if( pipe == INVALID_HANDLE_VALUE )
		{
			std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
			continue;
		}
		BOOL connected = ConnectNamedPipe( pipe, nullptr ) ? TRUE : ( GetLastError() == ERROR_PIPE_CONNECTED );
		if( connected )
		{
			std::thread( [ pipe ] {
				std::string in;
				char buf[ 8192 ];
				DWORD n = 0;
				while( in.find( '\n' ) == std::string::npos && ReadFile( pipe, buf, sizeof( buf ), &n, nullptr ) && n > 0 )
					in.append( buf, n );
				json res;
				try
				{
					json req = json::parse( in, nullptr, false );
					res      = req.is_object() ? Handle( req ) : json{ { "ok", false }, { "error", "requête illisible" } };
				}
				catch( const std::exception& e )
				{
					res = { { "ok", false }, { "error", std::string( "erreur : " ) + e.what() } };
				}
				catch( ... )
				{
					res = { { "ok", false }, { "error", "erreur inattendue" } };
				}
				std::string out = res.dump( -1, ' ', false, json::error_handler_t::replace ) + "\n";
				DWORD w         = 0;
				WriteFile( pipe, out.data(), (DWORD)out.size(), &w, nullptr );
				FlushFileBuffers( pipe );
				DisconnectNamedPipe( pipe );
				CloseHandle( pipe );
			} ).detach();
		}
		else
			CloseHandle( pipe );
	}
}
#endif
}//namespace

std::string HandleText( const std::string& request )
{
	json req = json::parse( request, nullptr, false );
	try
	{
		return ( req.is_object() ? Handle( req ) : json{ { "ok", false }, { "error", "requête illisible" } } ).dump();
	}
	catch( const std::exception& e )
	{
		return json{ { "ok", false }, { "error", e.what() } }.dump();
	}
}

void Start()
{
#ifdef _WIN32
	static std::once_flag once;
	std::call_once( once, [] {
		PinModuleForThreads();
#ifdef STFX_SOURCE
		const wchar_t* kind = L"source";
#else
		const wchar_t* kind = L"effect";
#endif
		wchar_t mod[ 32 ];
		swprintf( mod, 32, L"%llx", (unsigned long long)(uintptr_t)&Start );
		std::wstring name = L"\\\\.\\pipe\\ShaderLoaderBridge-" + std::to_wstring( GetCurrentProcessId() ) + L"-" + kind + L"-" + mod;
		std::thread( ServeLoop, name ).detach();
	} );
#endif
}
}//namespace bridge
}//namespace stfx
