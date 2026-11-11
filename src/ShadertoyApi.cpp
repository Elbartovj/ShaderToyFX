#include "ShadertoyApi.h"
#include "ShaderDoc.h"
#include "Util.h"
#include "Net.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <mutex>
#include <ctime>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using nlohmann::json;

namespace stfx
{
static const char* kApi = "https://www.shadertoy.com/api/v1/";

std::string ExtractShaderId( const std::string& input )
{
	std::string s = Trim( input );
	static const std::regex reLink( R"((?:view|embed)/([A-Za-z0-9]{6}))" );
	std::smatch m;
	if( std::regex_search( s, m, reLink ) )
		return m[ 1 ];
	static const std::regex reId( R"(^[A-Za-z0-9]{6}$)" );
	if( std::regex_match( s, reId ) )
		return s;
	return {};
}

static bool CheckKey( const std::string& key, std::string& err )
{
	if( Trim( key ).empty() )
	{
		err = "Clé API Shadertoy manquante : onglet Préférences (gratuite sur shadertoy.com/myapps).";
		return false;
	}
	return true;
}

static bool GetJson( const std::string& url, json& out, std::string& err )
{
	HttpResult r = HttpGet( url );
	if( !r.Ok() )
	{
		err = r.error.empty() ? "HTTP " + std::to_string( r.status ) : r.error;
		return false;
	}
	out = json::parse( r.Text(), nullptr, false );
	if( out.is_discarded() )
	{
		err = "Réponse illisible de Shadertoy";
		return false;
	}
	if( out.is_object() && out.contains( "Error" ) )
	{
		err = "Shadertoy : " + ( out[ "Error" ].is_string() ? out[ "Error" ].get< std::string >() : out[ "Error" ].dump() );
		return false;
	}
	return true;
}

static int JInt( const json& j, const char* key )
{
	auto it = j.find( key );
	if( it == j.end() )
		return 0;
	if( it->is_number() )
		return it->get< int >();
	if( it->is_string() )
	{
		try
		{
			return std::stoi( it->get< std::string >() );
		}
		catch( ... )
		{
		}
	}
	return 0;
}

bool SummaryFromJson( const std::string& normalizedJson, ShaderSummary& s )
{
	json j = json::parse( normalizedJson, nullptr, false );
	if( j.is_discarded() || !j.is_object() )
		return false;
	s.json = normalizedJson;
	if( j.contains( "info" ) )
	{
		const json& info = j[ "info" ];
		s.id             = info.value( "id", std::string() );
		s.name           = info.value( "name", std::string() );
		s.author         = info.value( "username", std::string() );
		s.description    = info.value( "description", std::string() );
		s.views          = JInt( info, "viewed" );
		s.likes          = JInt( info, "likes" );
		if( info.contains( "tags" ) && info[ "tags" ].is_array() )
			for( auto& t : info[ "tags" ] )
				if( t.is_string() )
					s.tags += ( s.tags.empty() ? "" : ", " ) + t.get< std::string >();
	}
	if( j.contains( "renderpass" ) )
		for( auto& p : j[ "renderpass" ] )
		{
			std::string type = ToLower( p.value( "type", std::string() ) );
			if( type == "common" )
				continue;
			s.passes++;
			if( type == "buffer" )
				s.hasBuffers = true;
			if( type == "sound" )
				s.hasSoundPass = true;
			if( type == "cubemap" )
				s.hasCubePass = true;
			if( type == "image" )
				s.imageCode = p.value( "code", std::string() );
		}
	return true;
}

bool ApiGetShader( const std::string& id, const std::string& key, ShaderSummary& out, std::string& err )
{
	if( !CheckKey( key, err ) )
		return false;
	HttpResult r = HttpGet( std::string( kApi ) + "shaders/" + UrlEncode( id ) + "?key=" + UrlEncode( key ) );
	if( !r.Ok() )
	{
		err = r.error;
		return false;
	}
	std::string norm;
	if( !NormalizeShadertoyJson( r.Text(), norm, err ) )
	{
		if( err.find( "not found" ) != std::string::npos || err.find( "Shader not found" ) != std::string::npos )
			err = "Shader introuvable via l'API (il doit être publié en 'Public + API') : " + id;
		return false;
	}
	return SummaryFromJson( norm, out );
}

bool ApiQuery( const std::string& term, const std::string& sort, int from, int num, const std::string& key,
			   std::vector< std::string >& ids, int& total, std::string& err )
{
	ids.clear();
	total = 0;
	if( !CheckKey( key, err ) )
		return false;
	std::string url;
	std::string t = Trim( term );
	if( t.empty() )
		url = std::string( kApi ) + "shaders?sort=" + sort;
	else
		url = std::string( kApi ) + "shaders/query/" + UrlEncode( t ) + "?sort=" + sort;
	url += "&from=" + std::to_string( from ) + "&num=" + std::to_string( num ) + "&key=" + UrlEncode( key );
	json j;
	if( !GetJson( url, j, err ) )
		return false;
	total = JInt( j, "Shaders" );
	if( j.contains( "Results" ) && j[ "Results" ].is_array() )
		for( auto& v : j[ "Results" ] )
			if( v.is_string() )
				ids.push_back( v.get< std::string >() );
	// certaines routes ignorent from/num : on pagine nous-mêmes
	if( (int)ids.size() > num )
	{
		std::vector< std::string > page;
		for( int i = from; i < (int)ids.size() && (int)page.size() < num; ++i )
			page.push_back( ids[ i ] );
		total = std::max( total, (int)ids.size() );
		ids   = page;
	}
	return true;
}

bool ApiListAll( const std::string& key, std::vector< std::string >& ids, std::string& err )
{
	static std::mutex m;
	static std::vector< std::string > cached;
	std::lock_guard< std::mutex > lock( m );
	if( !cached.empty() )
	{
		ids = cached;
		return true;
	}
	std::string cacheFile = JoinPath( MediaDir(), "all_ids.cache" );
	std::string text;
	if( ReadFileText( cacheFile, text ) )
	{
		json j = json::parse( text, nullptr, false );
		if( j.is_object() && std::time( nullptr ) - j.value( "time", (int64_t)0 ) < 24 * 3600 && j[ "ids" ].is_array() )
			for( auto& v : j[ "ids" ] )
				cached.push_back( v.get< std::string >() );
	}
	if( cached.empty() )
	{
		if( !CheckKey( key, err ) )
			return false;
		json j;
		if( !GetJson( std::string( kApi ) + "shaders?key=" + UrlEncode( key ), j, err ) )
			return false;
		if( j.contains( "Results" ) && j[ "Results" ].is_array() )
			for( auto& v : j[ "Results" ] )
				if( v.is_string() )
					cached.push_back( v.get< std::string >() );
		json save;
		save[ "time" ] = (int64_t)std::time( nullptr );
		save[ "ids" ]  = cached;
		WriteFileText( cacheFile, save.dump() );
	}
	if( cached.empty() )
	{
		err = "La liste des shaders est vide";
		return false;
	}
	ids = cached;
	return true;
}

std::string FindCachedPath( const std::string& id )
{
	if( id.empty() )
		return {};
	for( auto& e : ListShaderFiles( RootDir() ) )
		if( e.name.size() >= id.size() + 2 && e.name.find( "[" + id + "]" ) != std::string::npos )
			return e.path;
	return {};
}

std::string SaveToCache( const ShaderSummary& s )
{
	std::string existing = FindCachedPath( s.id );
	std::string path     = !existing.empty() ? existing : JoinPath( ShadertoyCacheDir(), SanitizeFileName( s.name ) + " [" + s.id + "].json" );
	WriteFileText( path, s.json );
	return path;
}

bool FetchShaderToCache( const std::string& id, std::string& path, std::string& title, std::string& err )
{
	path = FindCachedPath( id );
	if( !path.empty() )
	{
		title = FileStem( path );
		return true;
	}
	ShaderSummary s;
	if( !ApiGetShader( id, GetSettings().apiKey, s, err ) )
		return false;
	path  = SaveToCache( s );
	title = s.name;
	return true;
}

bool ImportShaderJsonFile( const std::string& file, std::string& cachedPath, std::string& title, std::string& err )
{
	std::string text;
	if( !ReadFileText( file, text ) || text.empty() )
	{
		err = "lecture impossible";
		return false;
	}
	std::string norm;
	if( !NormalizeShadertoyJson( text, norm, err ) )
		return false;
	ShaderSummary s;
	if( !SummaryFromJson( norm, s ) )
	{
		err = "JSON illisible";
		return false;
	}
	if( s.id.empty() )
	{
		static const std::regex reId( R"(\[([A-Za-z0-9]{6})\])" );
		std::smatch m;
		std::string stem = FileStem( file );
		if( std::regex_search( stem, m, reId ) )
			s.id = m[ 1 ];
	}
	if( s.name.empty() )
		s.name = FileStem( file );
	cachedPath = SaveToCache( s );
	title      = s.name;
#ifdef _WIN32
	DeleteFileW( Widen( file ).c_str() );
#endif
	return true;
}

std::vector< std::string > FindDownloadedExports()
{
	std::vector< std::string > out;
	std::string dir = DownloadsDir();
	if( dir.empty() )
		return out;
	static const std::regex re( R"(\[[A-Za-z0-9]{6}\]( \(\d+\))?\.json$)", std::regex::icase );
	std::error_code ec;
	for( auto& e : std::filesystem::directory_iterator( std::filesystem::path( Widen( dir ) ), ec ) )
	{
		if( !e.is_regular_file( ec ) )
			continue;
		std::string name = Narrow( e.path().filename().wstring() );
		if( std::regex_search( name, re ) )
			out.push_back( Narrow( e.path().wstring() ) );
	}
	return out;
}

const char* BookmarkletCode()
{
	return "javascript:(async()=>{try{"
		   "const m=location.pathname.match(/\\/(?:view|embed)\\/(\\w{6})/);"
		   "if(!m){alert('ShaderToyFX : ouvre d\\'abord un shader (shadertoy.com/view/...)');return;}"
		   "const id=m[1];"
		   "const r=await fetch('/shadertoy',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
		   "body:'s='+encodeURIComponent(JSON.stringify({shaders:[id]}))+'&nt=1&nl=1&np=1'});"
		   "const j=await r.json();const s=Array.isArray(j)?j[0]:j;"
		   "if(!s||!s.renderpass)throw new Error('réponse inattendue du site');"
		   "const n=((s.info&&s.info.name)||id).replace(/[\\\\/:*?\"<>|]/g,'_');"
		   "const a=document.createElement('a');"
		   "a.href=URL.createObjectURL(new Blob([JSON.stringify(s)],{type:'application/json'}));"
		   "a.download=n+' ['+id+'].json';document.body.appendChild(a);a.click();a.remove();"
		   "}catch(e){alert('ShaderToyFX : export impossible ('+e.message+'). Copie le code dans l\\'onglet FICHIER.');}})();";
}

bool PickRandomShader( std::string& path, std::string& title, std::string& err )
{
	Settings st = GetSettings();
	if( !Trim( st.apiKey ).empty() )
	{
		std::vector< std::string > ids;
		if( ApiListAll( st.apiKey, ids, err ) )
		{
			for( int attempt = 0; attempt < 6; ++attempt )
			{
				const std::string& id = ids[ RandomU32() % ids.size() ];
				ShaderSummary s;
				std::string e;
				if( !ApiGetShader( id, st.apiKey, s, e ) )
				{
					err = e;
					continue;
				}
				if( st.skipUnsupported && ( ( s.hasSoundPass && s.passes == 2 ) || s.hasCubePass ) )
					continue;
				path  = SaveToCache( s );
				title = s.name;
				return true;
			}
			return false;
		}
	}
	// hors-ligne / sans clé : tirage parmi les fichiers déjà présents
	auto files = ListShaderFiles( RootDir() );
	std::vector< LocalEntry > valid;
	for( auto& f : files )
		if( f.name.rfind( "_", 0 ) != 0 )
			valid.push_back( f );
	if( valid.empty() )
	{
		if( err.empty() )
			err = "Aucun shader local et pas de clé API pour en télécharger.";
		return false;
	}
	auto& f = valid[ RandomU32() % valid.size() ];
	path    = f.path;
	title   = f.name;
	err.clear();
	return true;
}
}//namespace stfx
