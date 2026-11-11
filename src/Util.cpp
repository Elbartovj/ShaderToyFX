#include "Util.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <algorithm>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace stfx
{
std::wstring Widen( const std::string& s )
{
#ifdef _WIN32
	if( s.empty() )
		return {};
	int n = MultiByteToWideChar( CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0 );
	std::wstring w( n, L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, s.data(), (int)s.size(), &w[ 0 ], n );
	return w;
#else
	return std::wstring( s.begin(), s.end() );
#endif
}

std::string Narrow( const std::wstring& w )
{
#ifdef _WIN32
	if( w.empty() )
		return {};
	int n = WideCharToMultiByte( CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr );
	std::string s( n, '\0' );
	WideCharToMultiByte( CP_UTF8, 0, w.data(), (int)w.size(), &s[ 0 ], n, nullptr, nullptr );
	return s;
#else
	return std::string( w.begin(), w.end() );
#endif
}

static fs::path P( const std::string& utf8 )
{
	return fs::path( Widen( utf8 ) );
}

bool ReadFileBytes( const std::string& path, std::vector< uint8_t >& out )
{
	std::ifstream f( P( path ), std::ios::binary );
	if( !f )
		return false;
	f.seekg( 0, std::ios::end );
	std::streamoff size = f.tellg();
	f.seekg( 0, std::ios::beg );
	if( size < 0 )
		return false;
	out.resize( (size_t)size );
	if( size > 0 )
		f.read( (char*)out.data(), size );
	return (bool)f;
}

bool ReadFileText( const std::string& path, std::string& out )
{
	std::vector< uint8_t > b;
	if( !ReadFileBytes( path, b ) )
		return false;
	out.assign( b.begin(), b.end() );
	// retire un éventuel BOM UTF-8
	if( out.size() >= 3 && (uint8_t)out[ 0 ] == 0xEF && (uint8_t)out[ 1 ] == 0xBB && (uint8_t)out[ 2 ] == 0xBF )
		out.erase( 0, 3 );
	return true;
}

bool WriteFileBytes( const std::string& path, const void* data, size_t size )
{
	std::error_code ec;
	fs::create_directories( P( path ).parent_path(), ec );
	std::ofstream f( P( path ), std::ios::binary | std::ios::trunc );
	if( !f )
		return false;
	f.write( (const char*)data, (std::streamsize)size );
	return (bool)f;
}

bool WriteFileText( const std::string& path, const std::string& text )
{
	return WriteFileBytes( path, text.data(), text.size() );
}

bool FileExists( const std::string& path )
{
	std::error_code ec;
	return !path.empty() && fs::is_regular_file( P( path ), ec );
}

void MakeDirs( const std::string& path )
{
	std::error_code ec;
	fs::create_directories( P( path ), ec );
}

std::string ToLower( std::string s )
{
	std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
	return s;
}

std::string Trim( const std::string& s )
{
	size_t a = s.find_first_not_of( " \t\r\n\"'" );
	if( a == std::string::npos )
		return {};
	size_t b = s.find_last_not_of( " \t\r\n\"'" );
	return s.substr( a, b - a + 1 );
}

std::string FileExtLower( const std::string& path )
{
	return ToLower( Narrow( P( path ).extension().wstring() ) );
}

std::string FileStem( const std::string& path )
{
	return Narrow( P( path ).stem().wstring() );
}

std::string ParentDir( const std::string& path )
{
	return Narrow( P( path ).parent_path().wstring() );
}

std::string JoinPath( const std::string& a, const std::string& b )
{
	return Narrow( ( P( a ) / P( b ) ).wstring() );
}

std::string SanitizeFileName( const std::string& name )
{
	std::string r;
	for( char c : name )
	{
		if( c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || (unsigned char)c < 32 )
			r += '_';
		else
			r += c;
	}
	r = Trim( r );
	while( !r.empty() && ( r.back() == '.' || r.back() == ' ' ) )
		r.pop_back();
	if( r.size() > 80 )
		r.resize( 80 );
	return r.empty() ? "shader" : r;
}

std::string RootDir()
{
	static std::string root;
	static std::once_flag once;
	std::call_once( once, [] {
		std::string base;
#ifdef _WIN32
		PWSTR docs = nullptr;
		if( SUCCEEDED( SHGetKnownFolderPath( FOLDERID_Documents, 0, nullptr, &docs ) ) )
		{
			base = Narrow( docs );
			CoTaskMemFree( docs );
		}
#endif
		if( base.empty() )
			base = ".";
		root = JoinPath( base, "ShaderToyFX" );
		MakeDirs( JoinPath( root, "Shadertoy" ) );
		MakeDirs( JoinPath( root, "Local" ) );
		MakeDirs( JoinPath( root, "media" ) );
	} );
	return root;
}
std::string ShadertoyCacheDir() { return JoinPath( RootDir(), "Shadertoy" ); }
std::string LocalDir() { return JoinPath( RootDir(), "Local" ); }
std::string MediaDir() { return JoinPath( RootDir(), "media" ); }

std::string DownloadsDir()
{
	static std::string dir;
	static std::once_flag once;
	std::call_once( once, [] {
#ifdef _WIN32
		PWSTR p = nullptr;
		if( SUCCEEDED( SHGetKnownFolderPath( FOLDERID_Downloads, 0, nullptr, &p ) ) )
		{
			dir = Narrow( p );
			CoTaskMemFree( p );
		}
#endif
	} );
	return dir;
}

std::vector< LocalEntry > ListShaderFiles( const std::string& dir )
{
	std::vector< LocalEntry > out;
	std::error_code ec;
	fs::path root = P( dir );
	if( !fs::is_directory( root, ec ) )
		return out;
	for( auto it = fs::recursive_directory_iterator( root, fs::directory_options::skip_permission_denied, ec ); it != fs::recursive_directory_iterator(); it.increment( ec ) )
	{
		if( ec )
			break;
		if( !it->is_regular_file( ec ) )
			continue;
		std::string ext = ToLower( Narrow( it->path().extension().wstring() ) );
		if( ext != ".json" && ext != ".glsl" && ext != ".frag" && ext != ".txt" && ext != ".fs" )
			continue;
		// ignore le dossier media
		std::string rel = Narrow( fs::relative( it->path(), root, ec ).wstring() );
		if( rel.rfind( "media", 0 ) == 0 )
			continue;
		LocalEntry e;
		e.path   = Narrow( it->path().wstring() );
		e.name   = Narrow( it->path().stem().wstring() );
		e.folder = Narrow( it->path().parent_path().filename().wstring() );
		{
			std::error_code ec2;
			auto relDir = fs::relative( it->path().parent_path(), root, ec2 );
			e.relFolder = ( ec2 || relDir == "." ) ? std::string() : Narrow( relDir.wstring() );
		}
		auto t   = fs::last_write_time( it->path(), ec );
		e.modified = (int64_t)t.time_since_epoch().count();
		out.push_back( std::move( e ) );
	}
	std::sort( out.begin(), out.end(), []( const LocalEntry& a, const LocalEntry& b ) { return ToLower( a.name ) < ToLower( b.name ); } );
	return out;
}

std::vector< std::string > ListFolders()
{
	std::vector< std::string > out;
	std::error_code ec;
	fs::path root = P( RootDir() );
	for( auto it = fs::recursive_directory_iterator( root, fs::directory_options::skip_permission_denied, ec ); it != fs::recursive_directory_iterator(); it.increment( ec ) )
	{
		if( ec )
			break;
		if( !it->is_directory( ec ) )
			continue;
		std::string rel = Narrow( fs::relative( it->path(), root, ec ).wstring() );
		if( rel == "media" || rel.rfind( "media\\", 0 ) == 0 || rel.rfind( "media/", 0 ) == 0 )
		{
			it.disable_recursion_pending();
			continue;
		}
		out.push_back( rel );
	}
	std::sort( out.begin(), out.end(), []( const std::string& a, const std::string& b ) { return ToLower( a ) < ToLower( b ); } );
	return out;
}

std::string MoveToFolder( const std::string& file, const std::string& relFolder )
{
	std::error_code ec;
	fs::path src = P( file );
	fs::path dir = relFolder.empty() ? P( RootDir() ) : P( RootDir() ) / P( relFolder );
	fs::create_directories( dir, ec );
	if( src.parent_path() == dir )
		return file;
	fs::path dst = dir / src.filename();
	for( int n = 2; fs::exists( dst, ec ); ++n )
		dst = dir / ( src.stem().wstring() + L" (" + std::to_wstring( n ) + L")" + src.extension().wstring() );
	fs::rename( src, dst, ec );
	if( ec )
	{
		ec.clear();
		fs::copy_file( src, dst, ec );
		if( ec )
			return {};
		fs::remove( src, ec );
	}
	return Narrow( dst.wstring() );
}

bool RecycleFile( const std::string& file )
{
#ifdef _WIN32
	std::wstring from = Widen( file );
	from.push_back( L'\0' );// double terminaison requise
	SHFILEOPSTRUCTW op{};
	op.wFunc  = FO_DELETE;
	op.pFrom  = from.c_str();
	op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
	return SHFileOperationW( &op ) == 0;
#else
	std::error_code ec;
	return fs::remove( P( file ), ec );
#endif
}

std::string FindByFileName( const std::string& missingPath )
{
	std::string name = ToLower( Narrow( P( missingPath ).filename().wstring() ) );
	if( name.empty() )
		return {};
	std::error_code ec;
	fs::path root = P( RootDir() );
	for( auto it = fs::recursive_directory_iterator( root, fs::directory_options::skip_permission_denied, ec ); it != fs::recursive_directory_iterator(); it.increment( ec ) )
	{
		if( ec )
			break;
		if( it->is_regular_file( ec ) && ToLower( Narrow( it->path().filename().wstring() ) ) == name )
			return Narrow( it->path().wstring() );
	}
	return {};
}

void OpenInExplorer( const std::string& path )
{
#ifdef _WIN32
	ShellExecuteW( nullptr, L"open", Widen( path ).c_str(), nullptr, nullptr, SW_SHOWNORMAL );
#endif
}

void OpenUrl( const std::string& url )
{
	OpenInExplorer( url );
}

//------------------------------------------------------------------ settings
static std::mutex s_settingsMutex;
static bool s_settingsLoaded = false;
static Settings s_settings;

static std::string SettingsPath()
{
	std::string base;
#ifdef _WIN32
	PWSTR appdata = nullptr;
	if( SUCCEEDED( SHGetKnownFolderPath( FOLDERID_RoamingAppData, 0, nullptr, &appdata ) ) )
	{
		base = Narrow( appdata );
		CoTaskMemFree( appdata );
	}
#endif
	if( base.empty() )
		base = RootDir();
	return JoinPath( JoinPath( base, "ShaderToyFX" ), "settings.json" );
}

Settings GetSettings()
{
	std::lock_guard< std::mutex > lock( s_settingsMutex );
	if( !s_settingsLoaded )
	{
		s_settingsLoaded = true;
		std::string text;
		if( ReadFileText( SettingsPath(), text ) )
		{
			auto j = nlohmann::json::parse( text, nullptr, false );
			if( j.is_object() )
			{
				s_settings.apiKey          = j.value( "apiKey", std::string() );
				s_settings.perPage         = std::clamp( j.value( "perPage", 25 ), 5, 100 );
				s_settings.sort            = j.value( "sort", std::string( "popular" ) );
				s_settings.skipUnsupported = j.value( "skipUnsupported", true );
				s_settings.shaderAlpha     = j.value( "shaderAlpha", false );
				s_settings.watchDownloads  = j.value( "watchDownloads", true );
				s_settings.autoLoadDownloads = j.value( "autoLoadDownloads", true );
			}
		}
	}
	return s_settings;
}

void SaveSettings( const Settings& s )
{
	std::lock_guard< std::mutex > lock( s_settingsMutex );
	s_settings       = s;
	s_settingsLoaded = true;
	nlohmann::json j;
	j[ "apiKey" ]          = s.apiKey;
	j[ "perPage" ]         = s.perPage;
	j[ "sort" ]            = s.sort;
	j[ "skipUnsupported" ] = s.skipUnsupported;
	j[ "shaderAlpha" ]     = s.shaderAlpha;
	j[ "watchDownloads" ]  = s.watchDownloads;
	j[ "autoLoadDownloads" ] = s.autoLoadDownloads;
	WriteFileText( SettingsPath(), j.dump( 2 ) );
}

uint32_t RandomU32()
{
	static std::mutex m;
	static std::mt19937 rng( std::random_device{}() );
	std::lock_guard< std::mutex > lock( m );
	return rng();
}
}//namespace stfx
