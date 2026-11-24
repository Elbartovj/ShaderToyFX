// Shader Loader - export d'un shader en plugin FFGL autonome (voir Export.h)
#include "Export.h"
#include "Util.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

namespace stfx
{
namespace standalone
{
namespace
{
const char kMagic[ 8 ] = { 'S', 'T', 'F', 'X', 'P', 'L', 'A', 'Y' };

std::string g_modulePathOverride;// tests

std::string OwnModulePath()
{
	if( !g_modulePathOverride.empty() )
		return g_modulePathOverride;
#ifdef _WIN32
	HMODULE h = nullptr;
	GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&OwnModulePath, &h );
	wchar_t buf[ 4096 ] = {};
	if( h && GetModuleFileNameW( h, buf, 4096 ) )
		return Narrow( buf );
#endif
	return {};
}

// Lit le bloc en fin de fichier (taille sur 8 octets + signature). Sans std::filesystem : appelé au chargement de la DLL.
bool ReadTrailer( const std::string& path, std::string& payload, uint64_t& codeSize )
{
#ifdef _WIN32
	HANDLE f = CreateFileW( Widen( path ).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr );
	if( f == INVALID_HANDLE_VALUE )
		return false;
	LARGE_INTEGER size;
	bool ok = false;
	if( GetFileSizeEx( f, &size ) && size.QuadPart > 16 )
	{
		char tail[ 16 ];
		LARGE_INTEGER pos;
		pos.QuadPart = size.QuadPart - 16;
		DWORD n      = 0;
		if( SetFilePointerEx( f, pos, nullptr, FILE_BEGIN ) && ReadFile( f, tail, 16, &n, nullptr ) && n == 16 && memcmp( tail + 8, kMagic, 8 ) == 0 )
		{
			uint64_t len = 0;
			memcpy( &len, tail, 8 );
			if( len > 0 && len < (uint64_t)size.QuadPart - 16 && len < ( 512ull << 20 ) )
			{
				payload.resize( (size_t)len );
				pos.QuadPart = size.QuadPart - 16 - (LONGLONG)len;
				if( SetFilePointerEx( f, pos, nullptr, FILE_BEGIN ) && ReadFile( f, &payload[ 0 ], (DWORD)len, &n, nullptr ) && n == len )
				{
					codeSize = (uint64_t)pos.QuadPart;
					ok       = true;
				}
			}
		}
		else
			codeSize = (uint64_t)size.QuadPart;
	}
	CloseHandle( f );
	return ok;
#else
	FILE* f = fopen( path.c_str(), "rb" );
	if( !f )
		return false;
	fseek( f, 0, SEEK_END );
	long size = ftell( f );
	bool ok   = false;
	codeSize  = (uint64_t)size;
	char tail[ 16 ];
	if( size > 16 && fseek( f, size - 16, SEEK_SET ) == 0 && fread( tail, 1, 16, f ) == 16 && memcmp( tail + 8, kMagic, 8 ) == 0 )
	{
		uint64_t len = 0;
		memcpy( &len, tail, 8 );
		if( len > 0 && len < (uint64_t)size - 16 )
		{
			payload.resize( (size_t)len );
			fseek( f, size - 16 - (long)len, SEEK_SET );
			ok       = fread( &payload[ 0 ], 1, (size_t)len, f ) == len;
			codeSize = (uint64_t)( size - 16 - (long)len );
		}
	}
	fclose( f );
	return ok;
#endif
}

Payload Load()
{
	Payload p;
	std::string text;
	uint64_t codeSize = 0;
	if( !ReadTrailer( OwnModulePath(), text, codeSize ) )
		return p;
	json j = json::parse( text, nullptr, false );
	if( !j.is_object() || !j.contains( "doc" ) )
		return p;
	p.present     = true;
	p.name        = j.value( "name", std::string( "Shader" ) ).substr( 0, 16 );
	p.id          = j.value( "id", std::string( "SLXX" ) ).substr( 0, 4 );
	p.source      = j.value( "type", std::string( "effect" ) ) == "source";
	p.description = j.value( "description", std::string() );
	return p;
}

// Images et code du shader écrits dans le cache média (au premier InitGL, pas au chargement de la DLL)
bool WriteFiles( Payload& p )
{
	std::string text;
	uint64_t codeSize = 0;
	if( !ReadTrailer( OwnModulePath(), text, codeSize ) )
		return false;
	json j = json::parse( text, nullptr, false );
	if( !j.is_object() )
		return false;
	try
	{
		MakeDirs( MediaDir() );
		for( auto& kv : j.value( "media", json::object() ).items() )
		{
			std::string dst = JoinPath( MediaDir(), SanitizeFileName( kv.key() ) );
			std::vector< uint8_t > existing;
			if( ReadFileBytes( dst, existing ) && !existing.empty() )
				continue;
			std::string b64 = kv.value().get< std::string >();
			static const std::string t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::vector< uint8_t > bytes;
			int val = 0, bits = -8;
			for( char c : b64 )
			{
				size_t k = t.find( c );
				if( k == std::string::npos )
					continue;
				val = ( ( val << 6 ) + (int)k ) & 0xFFFFFF;
				bits += 6;
				if( bits >= 0 )
				{
					bytes.push_back( uint8_t( ( val >> bits ) & 0xFF ) );
					bits -= 8;
				}
			}
			WriteFileBytes( dst, bytes.data(), bytes.size() );
		}
		std::string dir = JoinPath( MediaDir(), "plugins" );
		MakeDirs( dir );
		p.docPath = JoinPath( dir, SanitizeFileName( p.id + " " + p.name ) + j.value( "ext", std::string( ".json" ) ) );
		return WriteFileText( p.docPath, j[ "doc" ].get< std::string >() );
	}
	catch( ... )
	{
		return false;
	}
}

std::string Base64( const std::vector< uint8_t >& in )
{
	static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	size_t i = 0;
	for( ; i + 2 < in.size(); i += 3 )
	{
		uint32_t v = ( in[ i ] << 16 ) | ( in[ i + 1 ] << 8 ) | in[ i + 2 ];
		out += t[ v >> 18 ];
		out += t[ ( v >> 12 ) & 63 ];
		out += t[ ( v >> 6 ) & 63 ];
		out += t[ v & 63 ];
	}
	if( i < in.size() )
	{
		uint32_t v = in[ i ] << 16;
		if( i + 1 < in.size() )
			v |= in[ i + 1 ] << 8;
		out += t[ v >> 18 ];
		out += t[ ( v >> 12 ) & 63 ];
		out += i + 1 < in.size() ? t[ ( v >> 6 ) & 63 ] : '=';
		out += '=';
	}
	return out;
}

std::string MediaName( const std::string& src )
{
	std::string name = src;
	size_t slash     = name.find_last_of( "/\\" );
	if( slash != std::string::npos )
		name = name.substr( slash + 1 );
	size_t q = name.find( '?' );
	if( q != std::string::npos )
		name = name.substr( 0, q );
	return SanitizeFileName( name );
}
}//namespace

const Payload& Get()
{
	static const Payload p = Load();
	return p;
}

std::string PrepareShaderFile()
{
	static std::once_flag once;
	static std::string path;
	std::call_once( once, [] {
		Payload p = Get();
		if( p.present && WriteFiles( p ) )
			path = p.docPath;
	} );
	return path;
}

std::string MakePluginId( const std::string& name )
{
	uint32_t h = 2166136261u;
	for( unsigned char c : name )
		h = ( h ^ c ) * 16777619u;
	static const char* a = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	std::string id = "L";
	for( int i = 0; i < 3; ++i )
	{
		id += a[ h % 32 ];
		h /= 32;
	}
	return id;
}

std::string ModuleDir() { return ParentDir( OwnModulePath() ); }

void SetModulePathForTests( const std::string& path ) { g_modulePathOverride = path; }

bool ReadPayloadForTests( const std::string& path, std::string& payload )
{
	uint64_t cs = 0;
	return ReadTrailer( path, payload, cs );
}

bool WritePlugin( const ShaderDoc& docIn, const ExportOptions& opt, std::string& err )
{
	if( docIn.IsShadered() )
	{
		err = "les projets SHADERed / Godot ne peuvent pas être exportés en plugin";
		return false;
	}
	std::string self = OwnModulePath();
	std::vector< uint8_t > bin;
	if( self.empty() || !ReadFileBytes( self, bin ) || bin.size() < 1024 )
	{
		err = "lecture de la DLL Shader Loader impossible";
		return false;
	}
	// une DLL déjà exportée porte un bloc : on repart du code seul
	std::string old;
	uint64_t codeSize = bin.size();
	ReadTrailer( self, old, codeSize );
	bin.resize( (size_t)codeSize );

	ShaderDoc doc = docIn;
	std::string ext;
	std::string text = SerializeDoc( doc, ext );
	json media       = json::object();
	for( auto& p : doc.passes )
		for( auto& c : p.ch )
		{
			if( c.src.empty() || c.src.rfind( "/media/previz/", 0 ) == 0 )
				continue;
			std::vector< uint8_t > bytes;
			std::string name = MediaName( c.src );
			if( ( FileExists( c.src ) && ReadFileBytes( c.src, bytes ) ) || ReadFileBytes( JoinPath( MediaDir(), name ), bytes ) )
				media[ name ] = Base64( bytes );
		}
	std::string name = Trim( opt.name ).substr( 0, 16 );
	if( name.empty() )
		name = "Shader";
	json j = { { "name", name },
			   { "id", MakePluginId( name ) },
			   { "type", opt.source ? "source" : "effect" },
			   { "description", "Shader Loader : " + doc.title + ( doc.author.empty() ? "" : " par " + doc.author ) },
			   { "ext", ext },
			   { "doc", text },
			   { "media", media } };
	std::string payload = j.dump( -1, ' ', false, json::error_handler_t::replace );
	uint64_t len        = payload.size();
	bin.insert( bin.end(), payload.begin(), payload.end() );
	uint8_t tail[ 16 ];
	memcpy( tail, &len, 8 );
	memcpy( tail + 8, kMagic, 8 );
	bin.insert( bin.end(), tail, tail + 16 );
	if( !WriteFileBytes( opt.outPath, bin.data(), bin.size() ) )
	{
		err = "écriture impossible : " + opt.outPath + " (dossier protégé ? plugin déjà chargé par Resolume ?)";
		return false;
	}
	return true;
}
}//namespace standalone
}//namespace stfx
