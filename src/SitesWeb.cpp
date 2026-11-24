// Shader Loader - accès direct aux sites de shaders (sans navigateur) : lien -> shader, recherche, pages de résultats.
// Seules des adresses publiques sont utilisées : API publique NEORT, serveur MCP public de FragCoord (lecture sans clé),
// pages HTML publiques de ShaderKit et SHADERed, chargement de scène de Shaderoo.
#include "Sites.h"
#include "Net.h"
#include "Util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>

using json = nlohmann::json;

namespace stfx
{
namespace sites
{
namespace
{
std::mutex g_hookMutex;
FetchHook g_hook;

FetchResult Fetch( const std::string& method, const std::string& url, const std::string& body = "", const std::string& headers = "",
				   int timeoutMs = 20000 )
{
	{
		std::lock_guard< std::mutex > lock( g_hookMutex );
		if( g_hook )
			return g_hook( method, url, body );
	}
	HttpResult r = HttpRequest( method, url, body, headers, timeoutMs );
	FetchResult f;
	f.status = r.status;
	f.body   = r.Text();
	f.error  = r.error;
	return f;
}

std::string Lower( std::string s )
{
	std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) { return (char)tolower( c ); } );
	return s;
}

void AppendUtf8( std::string& out, uint32_t cp )
{
	if( cp < 0x80 )
		out += (char)cp;
	else if( cp < 0x800 )
	{
		out += (char)( 0xC0 | ( cp >> 6 ) );
		out += (char)( 0x80 | ( cp & 0x3F ) );
	}
	else if( cp < 0x10000 )
	{
		out += (char)( 0xE0 | ( cp >> 12 ) );
		out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
		out += (char)( 0x80 | ( cp & 0x3F ) );
	}
	else
	{
		out += (char)( 0xF0 | ( cp >> 18 ) );
		out += (char)( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
		out += (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
		out += (char)( 0x80 | ( cp & 0x3F ) );
	}
}

// &lt; &gt; &amp; &quot; &#39; &#123; &#x7B; &nbsp;
std::string HtmlDecode( const std::string& s )
{
	std::string out;
	out.reserve( s.size() );
	for( size_t i = 0; i < s.size(); ++i )
	{
		if( s[ i ] != '&' )
		{
			out += s[ i ];
			continue;
		}
		size_t semi = s.find( ';', i );
		if( semi == std::string::npos || semi - i > 10 )
		{
			out += '&';
			continue;
		}
		std::string e = s.substr( i + 1, semi - i - 1 );
		if( e == "lt" ) out += '<';
		else if( e == "gt" ) out += '>';
		else if( e == "amp" ) out += '&';
		else if( e == "quot" ) out += '"';
		else if( e == "apos" ) out += '\'';
		else if( e == "nbsp" ) out += ' ';
		else if( e.size() > 1 && e[ 0 ] == '#' )
		{
			uint32_t cp = ( e[ 1 ] == 'x' || e[ 1 ] == 'X' ) ? (uint32_t)strtoul( e.c_str() + 2, nullptr, 16 ) : (uint32_t)strtoul( e.c_str() + 1, nullptr, 10 );
			AppendUtf8( out, cp );
		}
		else
		{
			out += '&';
			continue;
		}
		i = semi;
	}
	return out;
}

std::string StripTags( const std::string& s )
{
	std::string out;
	bool tag = false;
	for( char c : s )
	{
		if( c == '<' )
			tag = true;
		else if( c == '>' )
			tag = false;
		else if( !tag )
			out += c;
	}
	return Trim( HtmlDecode( out ) );
}

std::string Base64( const std::string& in )
{
	static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve( ( in.size() + 2 ) / 3 * 4 );
	size_t i = 0;
	for( ; i + 2 < in.size(); i += 3 )
	{
		uint32_t v = ( (uint8_t)in[ i ] << 16 ) | ( (uint8_t)in[ i + 1 ] << 8 ) | (uint8_t)in[ i + 2 ];
		out += t[ v >> 18 ];
		out += t[ ( v >> 12 ) & 63 ];
		out += t[ ( v >> 6 ) & 63 ];
		out += t[ v & 63 ];
	}
	if( i < in.size() )
	{
		uint32_t v = (uint8_t)in[ i ] << 16;
		if( i + 1 < in.size() )
			v |= (uint8_t)in[ i + 1 ] << 8;
		out += t[ v >> 18 ];
		out += t[ ( v >> 12 ) & 63 ];
		out += i + 1 < in.size() ? t[ ( v >> 6 ) & 63 ] : '=';
		out += '=';
	}
	return out;
}

// texte entre deux marqueurs, à partir de pos ; pos avance après le marqueur de fin
bool Between( const std::string& s, const std::string& a, const std::string& b, size_t& pos, std::string& out )
{
	size_t i = s.find( a, pos );
	if( i == std::string::npos )
		return false;
	i += a.size();
	size_t j = s.find( b, i );
	if( j == std::string::npos )
		return false;
	out = s.substr( i, j - i );
	pos = j + b.size();
	return true;
}

bool FinishImport( const std::string& site, const std::string& id, const json& bundle, std::string& savedPath, std::string& title, std::string& err )
{
	std::string out, siteDir;
	if( !ConvertBundle( bundle.dump(), out, title, siteDir, err ) )
		return false;
	return SaveImported( site, id, title, siteDir, out, ".json", savedPath, err );
}

std::string NetError( const FetchResult& r, const std::string& what )
{
	if( r.status == 404 )
		return what + " introuvable (404)";
	if( r.status == 403 || r.status == 429 || r.status == 503 )
		return what + " : le site refuse l'accès direct (HTTP " + std::to_string( r.status ) + "). Utilise le favori universel depuis la page.";
	return what + " : " + ( r.error.empty() ? "HTTP " + std::to_string( r.status ) : r.error );
}

//------------------------------------------------------------------ ShaderKit (page publique /s/ID)
bool ShaderKitFromHtml( const std::string& html, const std::string& id, json& bundle, std::string& err )
{
	size_t a = html.find( "id=\"seoLanding\"" );
	if( a == std::string::npos )
	{
		err = "page ShaderKit sans code (format inattendu)";
		return false;
	}
	size_t b         = html.find( "</main>", a );
	std::string page = html.substr( a, b == std::string::npos ? std::string::npos : b - a );
	json data        = json::object();
	size_t p         = 0;
	std::string h1, author;
	if( Between( page, "<h1>", "</h1>", p, h1 ) )
		data[ "title" ] = StripTags( h1 );
	p = 0;
	if( Between( page, "href=\"/u/", "\"", p, author ) )
		data[ "ownerUsername" ] = HtmlDecode( author );
	json channels = json::object(), avail = json::array();
	std::string common;
	p = 0;
	std::string heading;
	while( Between( page, "<h3>", "</h3>", p, heading ) )
	{
		heading      = StripTags( heading );
		size_t pre   = page.find( "<pre><code", p );
		size_t nextH = page.find( "<h3>", p );
		if( pre == std::string::npos || ( nextH != std::string::npos && nextH < pre ) )
			continue;
		size_t open = page.find( '>', pre + 10 );
		size_t end  = page.find( "</code></pre>", open );
		if( open == std::string::npos || end == std::string::npos )
			break;
		std::string code = HtmlDecode( page.substr( open + 1, end - open - 1 ) );
		p                = end;
		if( Trim( code ) == "Not used" )
			code.clear();
		std::smatch m;
		if( heading == "Common" )
			common = code;
		else if( heading == "Image" )
			channels[ "displayMode" ] = code;
		else if( std::regex_search( heading, m, std::regex( R"(\((iChannel[0-3])\))" ) ) && !code.empty() )
		{
			channels[ m[ 1 ].str() ] = code;
			avail.push_back( m[ 1 ].str() );
		}
	}
	if( avail.empty() && ( !channels.contains( "displayMode" ) || channels[ "displayMode" ].get< std::string >().empty() ) )
	{
		err = "aucun code dans la page ShaderKit";
		return false;
	}
	data[ "channels" ]          = channels;
	data[ "availableChannels" ] = avail;
	data[ "commonCode" ]        = common;
	bundle = { { "stfx", 1 }, { "site", "shaderkit" }, { "id", id }, { "url", "https://shaderkit.com/s/" + id }, { "data", data } };
	return true;
}

bool ImportShaderKit( const std::string& id, std::string& savedPath, std::string& title, std::string& err )
{
	FetchResult r = Fetch( "GET", "https://shaderkit.com/s/" + id );
	if( !r.Ok() )
	{
		err = NetError( r, "Shader ShaderKit" );
		return false;
	}
	json bundle;
	if( !ShaderKitFromHtml( r.body, id, bundle, err ) )
		return false;
	return FinishImport( "shaderkit", id, bundle, savedPath, title, err );
}

//------------------------------------------------------------------ Shaderoo (index.php?cmd=loaddb)
std::string ShaderooDecode( const std::string& raw )
{
	std::string s = HtmlDecode( raw );
	auto rep      = [ & ]( const std::string& a, const std::string& b ) {
        for( size_t i = s.find( a ); i != std::string::npos; i = s.find( a, i + b.size() ) )
            s.replace( i, a.size(), b );
	};
	rep( "<and/>", "&" );
	rep( "<plus/>", "+" );
	rep( "<modulo/>", "%" );
	rep( "<br/>", "\n" );
	rep( "<backslash/>", "\\" );
	return s;
}

bool ImportShaderoo( const std::string& id, std::string& savedPath, std::string& title, std::string& err )
{
	FetchResult r = Fetch( "GET", "https://shaderoo.org/index.php?cmd=loaddb&id=" + UrlEncode( id ) );
	if( !r.Ok() )
	{
		err = NetError( r, "Scène Shaderoo" );
		return false;
	}
	std::string scene = ShaderooDecode( r.body );
	if( scene.find( "###BEGIN:" ) == std::string::npos )
	{
		err = "shader Shaderoo introuvable ou vide";
		return false;
	}
	// textures intégrées de Shaderoo citées dans l'en-tête
	static const char* pngs[]   = { "rand256", "rand64", "rand16", "rand8" };
	static const char* floats[] = { "car64", "car128", "dragon128" };
	std::string head            = scene.substr( 0, scene.find( "###BEGIN:" ) );
	json images                 = json::object();
	auto wanted                 = [ & ]( const char* n ) { return std::regex_search( head, std::regex( std::string( "(^|[\\s:])" ) + n + "(\\s|$)" ) ); };
	for( const char* n : pngs )
		if( wanted( n ) )
		{
			FetchResult t = Fetch( "GET", std::string( "https://shaderoo.org/textures/" ) + n + ".png" );
			if( t.Ok() && !t.body.empty() )
				images[ n ] = "data:image/png;base64," + Base64( t.body );
		}
	for( const char* n : floats )
		if( wanted( n ) )
		{
			FetchResult t = Fetch( "GET", std::string( "https://shaderoo.org/textures/" ) + n + ".float", "", "", 60000 );
			if( t.Ok() && t.body.size() > 100 )
				images[ n ] = "data:application/octet-stream;base64," + Base64( t.body );
		}
	json bundle = { { "stfx", 1 }, { "site", "shaderoo" }, { "id", id }, { "url", "https://shaderoo.org/?shader=" + id }, { "scene", scene }, { "images", images } };
	return FinishImport( "shaderoo", id, bundle, savedPath, title, err );
}

//------------------------------------------------------------------ NEORT (api.neort.io, public)
bool ImportNeort( const std::string& id, std::string& savedPath, std::string& title, std::string& err )
{
	FetchResult r = Fetch( "GET", "https://api.neort.io/v1/art/" + UrlEncode( id ) );
	if( !r.Ok() )
	{
		err = NetError( r, "Œuvre NEORT" );
		return false;
	}
	json a = json::parse( r.body, nullptr, false );
	if( a.is_discarded() || a.value( "id", std::string() ).empty() )
	{
		err = "œuvre NEORT introuvable";
		return false;
	}
	if( a.value( "resourceType", 0 ) != 3 )
	{
		err = "Cette œuvre NEORT n'est pas un shader GLSL (JavaScript, p5.js, three.js, vidéo… non pris en charge).";
		return false;
	}
	std::string author;
	if( a.contains( "user" ) && a[ "user" ].is_object() )
		author = a[ "user" ].value( "name", std::string() );
	json bundle = { { "stfx", 1 }, { "site", "neort" }, { "id", id }, { "url", "https://neort.io/art/" + id },
					{ "title", a.value( "title", std::string( "NEORT" ) ) }, { "author", author }, { "codes", a.value( "codes", json::array() ) } };
	return FinishImport( "neort", id, bundle, savedPath, title, err );
}

//------------------------------------------------------------------ FragCoord (serveur MCP public : https://fragcoord.xyz/mcp)
bool McpCall( const std::string& tool, const json& args, json& result, std::string& err )
{
	static int counter = 1;
	json req          = { { "jsonrpc", "2.0" }, { "id", counter++ }, { "method", "tools/call" }, { "params", { { "name", tool }, { "arguments", args } } } };
	FetchResult r     = Fetch( "POST", "https://fragcoord.xyz/mcp", req.dump(),
							   "Content-Type: application/json\r\nAccept: application/json, text/event-stream\r\n" );
	if( !r.Ok() )
	{
		err = NetError( r, "FragCoord" );
		return false;
	}
	std::string body = r.body;
	// réponse en flux SSE : « data: {…} »
	size_t d = body.find( "data:" );
	if( !body.empty() && body[ 0 ] != '{' && d != std::string::npos )
	{
		size_t eol = body.find( '\n', d );
		body       = body.substr( d + 5, eol == std::string::npos ? std::string::npos : eol - d - 5 );
	}
	json j = json::parse( body, nullptr, false );
	if( j.is_discarded() || !j.contains( "result" ) )
	{
		err = j.is_object() && j.contains( "error" ) ? "FragCoord : " + j[ "error" ].value( "message", std::string( "erreur" ) ) : "réponse FragCoord illisible";
		return false;
	}
	result = j[ "result" ];
	if( result.value( "isError", false ) )
	{
		err = "FragCoord : ";
		if( result.contains( "content" ) && result[ "content" ].is_array() && !result[ "content" ].empty() )
			err += result[ "content" ][ 0 ].value( "text", std::string() );
		return false;
	}
	if( !result.contains( "structuredContent" ) )
	{
		// texte JSON dans content[0]
		if( result.contains( "content" ) && result[ "content" ].is_array() && !result[ "content" ].empty() )
		{
			json t = json::parse( result[ "content" ][ 0 ].value( "text", std::string() ), nullptr, false );
			if( !t.is_discarded() )
			{
				result = t;
				return true;
			}
		}
		err = "réponse FragCoord inattendue";
		return false;
	}
	result = result[ "structuredContent" ];
	return true;
}

bool ImportFragCoord( const std::string& id, std::string& savedPath, std::string& title, std::string& err )
{
	json sh;
	if( !McpCall( "get_shader", { { "slug", id }, { "include_source", true } }, sh, err ) )
		return false;
	if( !sh.value( "source_available", true ) )
	{
		err = "Le code de ce shader FragCoord est caché (marketplace).";
		return false;
	}
	json passes = sh.value( "passes", json::array() );
	for( auto& p : passes )
	{
		std::string lang = Lower( p.value( "language", std::string( "glsl" ) ) );
		if( lang.empty() || lang == "glsl" )
			continue;
		// golf, WGSL, HLSL… : conversion en GLSL par FragCoord
		json conv;
		std::string e;
		if( McpCall( "convert_shader", { { "source", p.value( "code", std::string() ) }, { "from", lang }, { "to", "glsl" } }, conv, e ) &&
			conv.contains( "output" ) && conv[ "output" ].is_string() )
		{
			p[ "code" ]     = conv[ "output" ];
			p[ "language" ] = "glsl";
		}
	}
	json bundle = { { "stfx", 1 },
					{ "site", "fragcoord" },
					{ "id", id },
					{ "url", "https://fragcoord.xyz/s/" + id },
					{ "title", sh.value( "title", std::string( "FragCoord" ) ) },
					{ "author", sh.value( "author", std::string() ) },
					{ "source_available", true },
					{ "passes", passes } };
	if( sh.contains( "license" ) && sh[ "license" ].is_string() )
		bundle[ "license" ] = sh[ "license" ];
	else if( sh.contains( "license" ) && sh[ "license" ].is_object() )
		bundle[ "license" ] = sh[ "license" ].value( "name", sh[ "license" ].value( "spdx", std::string() ) );
	return FinishImport( "fragcoord", id, bundle, savedPath, title, err );
}

//------------------------------------------------------------------ listes
bool QueryMatch( const WebEntry& e, const std::string& q )
{
	if( q.empty() )
		return true;
	std::string all = Lower( e.title + " " + e.author + " " + e.info );
	std::stringstream ss( Lower( q ) );
	std::string w;
	while( ss >> w )
		if( all.find( w ) == std::string::npos )
			return false;
	return true;
}

bool ShaderKitPage( int page, std::vector< WebEntry >& out, bool& more, std::string& err )
{
	FetchResult r = Fetch( "GET", "https://shaderkit.com/browse?page=" + std::to_string( page ) );
	if( !r.Ok() )
	{
		err = NetError( r, "ShaderKit" );
		return false;
	}
	size_t a = r.body.find( "id=\"seoLanding\"" );
	if( a == std::string::npos )
	{
		err = "liste ShaderKit illisible";
		return false;
	}
	size_t b         = r.body.find( "</main>", a );
	std::string list = r.body.substr( a, b == std::string::npos ? std::string::npos : b - a );
	size_t p         = 0;
	std::string li;
	while( Between( list, "<li>", "</li>", p, li ) )
	{
		WebEntry e;
		size_t q = 0;
		std::string t;
		if( !Between( li, "href=\"/s/", "\"", q, e.id ) )
			continue;
		q = 0;
		if( Between( li, "<h3>", "</h3>", q, t ) )
			e.title = StripTags( t );
		q = 0;
		if( Between( li, "href=\"/u/", "\"", q, t ) )
			e.author = HtmlDecode( t );
		q = 0;
		if( Between( li, "Tags:", "</p>", q, t ) )
			e.info = StripTags( t );
		e.thumb = "https://shaderkit.com/api/shaders/" + e.id + "/thumbnail";
		out.push_back( e );
	}
	more = list.find( "browse?page=" + std::to_string( page + 1 ) ) != std::string::npos;
	return true;
}

bool ShaderooPage( const std::string& sort, int from, int num, std::vector< WebEntry >& out, bool& more, std::string& err )
{
	FetchResult r = Fetch( "GET", "https://shaderoo.org/?cmd=browse&sort=" + sort + "&from=" + std::to_string( from ) + "&num=" + std::to_string( num ) );
	if( !r.Ok() )
	{
		err = NetError( r, "Shaderoo" );
		return false;
	}
	const std::string& h = r.body;
	size_t p             = 0;
	for( ;; )
	{
		size_t td = h.find( "<td id='td_", p );
		if( td == std::string::npos )
			break;
		size_t idEnd = h.find( '\'', td + 11 );
		size_t close = h.find( "</td>", td );
		if( idEnd == std::string::npos || close == std::string::npos )
			break;
		std::string cell = h.substr( td, close - td );
		p                = close;
		WebEntry e;
		e.id = h.substr( td + 11, idEnd - td - 11 );
		size_t q = 0;
		std::string t;
		if( Between( cell, "url(data:image/", ")", q, t ) )
			e.thumb = "data:image/" + t;
		q = 0;
		if( Between( cell, "<big>", "</big>", q, t ) )
			e.title = StripTags( t );
		q = 0;
		if( Between( cell, "username=", "'", q, t ) )
			e.author = HtmlDecode( t );
		q = 0;
		if( Between( cell, "<br/>", "views", q, t ) )
		{
			size_t lastBr = t.rfind( "<br/>" );
			e.info        = Trim( StripTags( lastBr == std::string::npos ? t : t.substr( lastBr + 5 ) ) ) + " vues";
		}
		out.push_back( e );
	}
	more = h.find( "from=" + std::to_string( from + num ) + "&" ) != std::string::npos;
	return true;
}

bool NeortTagPage( const std::string& tagId, int offset, std::vector< WebEntry >& out, bool& more, std::string& err )
{
	const int limit = 60;
	FetchResult r   = Fetch( "GET", "https://api.neort.io/v1/tag/" + tagId + "/arts?limit=" + std::to_string( limit ) + "&offset=" + std::to_string( offset ) );
	if( !r.Ok() )
	{
		err = NetError( r, "NEORT" );
		return false;
	}
	json j = json::parse( r.body, nullptr, false );
	if( j.is_object() && j.contains( "arts" ) )
		j = j[ "arts" ];
	if( !j.is_array() )
	{
		err = "liste NEORT illisible";
		return false;
	}
	for( auto& a : j )
	{
		if( a.value( "resourceType", 0 ) != 3 )
			continue;// seulement les œuvres GLSL
		WebEntry e;
		e.id    = a.value( "id", std::string() );
		e.title = a.value( "title", std::string() );
		if( a.contains( "user" ) && a[ "user" ].is_object() )
			e.author = a[ "user" ].value( "name", std::string() );
		std::string thumb = a.value( "selectedThumbFileName", std::string() );
		if( thumb.empty() )
			thumb = a.value( "thumbFileName", std::string() );
		if( !thumb.empty() )
			e.thumb = "https://d32h66pp7fue57.cloudfront.net/artThumb/" + thumb;
		std::string tags;
		if( a.contains( "tags" ) && a[ "tags" ].is_array() )
			for( auto& t : a[ "tags" ] )
				tags += ( tags.empty() ? "" : ", " ) + t.value( "name", std::string() );
		e.info = tags;
		if( !e.id.empty() )
			out.push_back( e );
	}
	more = (int)j.size() >= limit;
	return true;
}

bool ShaderedPage( const std::string& query, const std::string& sort, int page, std::vector< WebEntry >& out, bool& more, std::string& err )
{
	FetchResult r = Fetch( "GET", "https://shadered.org/shaders?query=" + UrlEncode( query ) + "&sort=" + sort + "&language=&page=" + std::to_string( page ) );
	if( !r.Ok() )
	{
		err = NetError( r, "SHADERed" );
		return false;
	}
	const std::string& h = r.body;
	size_t p             = 0;
	for( ;; )
	{
		size_t c = h.find( "class=\"shader-thumbnail\" id=\"", p );
		if( c == std::string::npos )
			break;
		size_t idStart = h.find( "id=\"", c ) + 4, idEnd = h.find( '"', idStart );
		size_t next    = h.find( "class=\"shader-thumbnail\"", idEnd );
		std::string card = h.substr( c, ( next == std::string::npos ? h.size() : next ) - c );
		p                = idEnd;
		WebEntry e;
		e.id     = h.substr( idStart, idEnd - idStart );
		e.thumb  = "https://shadered.org/thumbnails/" + e.id + "/0.png";
		size_t q = 0;
		std::string t;
		if( Between( card, "<b>", "</b>", q, t ) )
			e.title = StripTags( t );
		q = 0;
		if( Between( card, "profile?u=", "\"", q, t ) )
			e.author = HtmlDecode( t );
		q = 0;
		if( Between( card, "<span class=\"label", "</span>", q, t ) )
			e.info = StripTags( t.substr( t.find( '>' ) + 1 ) );
		out.push_back( e );
	}
	more = h.find( "page=" + std::to_string( page + 1 ) ) != std::string::npos;
	return true;
}
}//namespace

void SetFetchHook( FetchHook hook )
{
	std::lock_guard< std::mutex > lock( g_hookMutex );
	g_hook = std::move( hook );
}

const std::vector< WebSite >& WebSites()
{
	static const std::vector< WebSite > list = {
		{ "shaderkit", "ShaderKit", "https://shaderkit.com/browse", { "Récents" }, "Filtrer (titre, auteur, tags)…" },
		{ "fragcoord", "FragCoord", "https://fragcoord.xyz/explore", { "Tendance" }, "Rechercher (mots-clés, tags)…" },
		{ "shaderoo", "Shaderoo", "https://shaderoo.org/?cmd=browse&sort=hot", { "Tendance", "Récents", "Vues", "Likes" }, "Filtrer (titre, auteur)…" },
		{ "neort", "NEORT", "https://neort.io/en/explore", { "Récents" }, "Tag (vide = glsl)…" },
		{ "shadered", "SHADERed", "https://shadered.org/shaders", { "Tendance", "Récents", "Populaires" }, "Rechercher…" },
	};
	return list;
}

bool ParseSiteLink( const std::string& text, std::string& site, std::string& id )
{
	static const struct
	{
		const char* site;
		std::regex re;
	} pats[] = {
		{ "shaderkit", std::regex( R"(shaderkit\.com/(?:s|view|embed)/([A-Za-z0-9]+))", std::regex::icase ) },
		{ "fragcoord", std::regex( R"(fragcoord\.xyz/(?:s|embed)/([A-Za-z0-9_-]+))", std::regex::icase ) },
		{ "shaderoo", std::regex( R"(shaderoo\.org/.*[?&]shader=([A-Za-z0-9_-]+))", std::regex::icase ) },
		{ "neort", std::regex( R"(neort\.io/(?:[a-z]{2}/)?art/([A-Za-z0-9]+))", std::regex::icase ) },
	};
	std::string t = Trim( text );
	for( auto& p : pats )
	{
		std::smatch m;
		if( std::regex_search( t, m, p.re ) )
		{
			site = p.site;
			id   = m[ 1 ];
			if( site == "fragcoord" && id.size() > 3 && id.substr( id.size() - 3 ) == ".md" )
				id.resize( id.size() - 3 );
			return true;
		}
	}
	return false;
}

std::string SiteShaderUrl( const std::string& site, const std::string& id )
{
	if( site == "shaderkit" ) return "https://shaderkit.com/s/" + id;
	if( site == "fragcoord" ) return "https://fragcoord.xyz/s/" + id;
	if( site == "shaderoo" ) return "https://shaderoo.org/?shader=" + id;
	if( site == "neort" ) return "https://neort.io/art/" + id;
	if( site == "shadered" ) return "https://shadered.org/view?s=" + id;
	return {};
}

bool FetchAndImport( const std::string& site, const std::string& id, std::string& savedPath, std::string& title, std::string& err )
{
	if( id.empty() )
	{
		err = "identifiant vide";
		return false;
	}
	try
	{
		if( site == "shaderkit" ) return ImportShaderKit( id, savedPath, title, err );
		if( site == "fragcoord" ) return ImportFragCoord( id, savedPath, title, err );
		if( site == "shaderoo" ) return ImportShaderoo( id, savedPath, title, err );
		if( site == "neort" ) return ImportNeort( id, savedPath, title, err );
	}
	catch( const std::exception& e )
	{
		err = std::string( "erreur : " ) + e.what();
		return false;
	}
	err = "site non pris en charge : " + site;
	return false;
}

bool Browse( const std::string& site, const std::string& queryIn, int sortIdx, const std::string& cursor, std::vector< WebEntry >& out,
			 std::string& next, std::string& err )
{
	out.clear();
	next.clear();
	std::string query = Trim( queryIn );
	int c             = cursor.empty() ? 0 : atoi( cursor.c_str() );
	bool more         = false;
	try
	{
		if( site == "shaderkit" )
		{
			// pas de recherche côté site : le filtre parcourt les pages jusqu'à 24 résultats
			int page = std::max( 1, c ) - 1;
			std::set< std::string > seen;
			for( int scanned = 0; scanned < ( query.empty() ? 1 : 12 ); ++scanned )
			{
				std::vector< WebEntry > got;
				if( !ShaderKitPage( ++page, got, more, err ) )
					return false;
				for( auto& e : got )
					if( QueryMatch( e, query ) && seen.insert( e.id ).second )
						out.push_back( e );
				if( !more || out.size() >= 24 )
					break;
			}
			if( more )
				next = std::to_string( page + 1 );
			return true;
		}
		if( site == "shaderoo" )
		{
			static const char* sorts[] = { "hot", "date", "views", "likes" };
			const char* sort           = sorts[ std::clamp( sortIdx, 0, 3 ) ];
			const int num              = 25;
			int from                   = c;
			for( int scanned = 0; scanned < ( query.empty() ? 1 : 12 ); ++scanned )
			{
				std::vector< WebEntry > got;
				if( !ShaderooPage( sort, from, num, got, more, err ) )
					return false;
				from += num;
				for( auto& e : got )
					if( QueryMatch( e, query ) )
						out.push_back( e );
				if( !more || out.size() >= 24 )
					break;
			}
			if( more )
				next = std::to_string( from );
			return true;
		}
		if( site == "neort" )
		{
			std::string tagId = "bmjbjcc3p9f7m1g018u0";// tag « glsl »
			if( !query.empty() )
			{
				FetchResult r = Fetch( "GET", "https://api.neort.io/v1/tags?name=" + UrlEncode( query ) );
				json j        = r.Ok() ? json::parse( r.body, nullptr, false ) : json();
				if( !j.is_array() || j.empty() )
				{
					err = r.Ok() ? "Aucun tag NEORT « " + query + " »." : NetError( r, "NEORT" );
					return false;
				}
				tagId = j[ 0 ].value( "id", tagId );
			}
			int offset = c;
			for( int scanned = 0; scanned < 6; ++scanned )
			{
				if( !NeortTagPage( tagId, offset, out, more, err ) )
					return false;
				offset += 60;
				if( !more || out.size() >= 20 )
					break;
			}
			if( more )
				next = std::to_string( offset );
			return true;
		}
		if( site == "fragcoord" )
		{
			const int limit = 25;
			json res;
			if( !McpCall( "search_shaders", { { "query", query }, { "limit", limit }, { "offset", c } }, res, err ) )
				return false;
			for( auto& s : res.value( "shaders", json::array() ) )
			{
				WebEntry e;
				e.id     = s.value( "slug", std::string() );
				e.title  = s.value( "title", std::string() );
				e.author = s.value( "author", std::string() );
				std::string tags;
				for( auto& t : s.value( "tags", json::array() ) )
					if( t.is_string() )
						tags += ( tags.empty() ? "" : ", " ) + t.get< std::string >();
				e.info = std::to_string( s.value( "likes", 0 ) ) + " likes · " + tags;
				if( !e.id.empty() )
					out.push_back( e );
			}
			int total = res.value( "total", 0 );
			if( c + limit < total && c + limit <= 1000 )
				next = std::to_string( c + limit );
			return true;
		}
		if( site == "shadered" )
		{
			static const char* sorts[] = { "hot", "new", "popular" };
			if( !ShaderedPage( query, sorts[ std::clamp( sortIdx, 0, 2 ) ], c, out, more, err ) )
				return false;
			if( more )
				next = std::to_string( c + 1 );
			return true;
		}
	}
	catch( const std::exception& e )
	{
		err = std::string( "erreur : " ) + e.what();
		return false;
	}
	err = "site inconnu : " + site;
	return false;
}
}//namespace sites
}//namespace stfx
