#include "ShaderDoc.h"
#include "Util.h"
#include "Net.h"
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <regex>
#include <sstream>
#include <map>
#include <cstring>
#include <algorithm>

using nlohmann::json;

namespace stfx
{
const PassDesc* ShaderDoc::ImagePass() const
{
	for( auto& p : passes )
		if( p.isImage )
			return &p;
	return nullptr;
}

int ShaderDoc::BufferCount() const
{
	int n = 0;
	for( auto& p : passes )
		if( !p.isImage )
			++n;
	return n;
}

static std::string JStr( const json& j, const char* key, const std::string& def = "" )
{
	auto it = j.find( key );
	if( it == j.end() || it->is_null() )
		return def;
	if( it->is_string() )
		return it->get< std::string >();
	return it->dump();
}

static std::string IdToString( const json& v )
{
	if( v.is_string() )
		return v.get< std::string >();
	if( v.is_null() )
		return {};
	return v.dump();
}

bool NormalizeShadertoyJson( const std::string& text, std::string& normalized, std::string& err )
{
	json j = json::parse( text, nullptr, false );
	if( j.is_discarded() )
	{
		err = "JSON invalide";
		return false;
	}
	if( j.is_object() && j.contains( "Error" ) )
	{
		err = "Shadertoy : " + JStr( j, "Error" );
		return false;
	}
	if( j.is_object() && j.contains( "Shader" ) )
		j = j[ "Shader" ];
	else if( j.is_array() && !j.empty() )
		j = j[ 0 ];
	if( !j.is_object() || !j.contains( "renderpass" ) )
	{
		err = "Ce JSON ne ressemble pas à un shader Shadertoy (pas de 'renderpass')";
		return false;
	}
	normalized = j.dump( 1, '\t' );
	return true;
}

//------------------------------------------------------------------ annotations GLSL
// // @iChannel0 input|audio|keyboard|none|<fichier image>
static void ParseChannelAnnotations( const std::string& code, PassDesc& pass, const std::string& baseDir )
{
	static const std::regex re( R"(@iChannel([0-3])\s+([^\s]+))" );
	for( std::sregex_iterator it( code.begin(), code.end(), re ), end; it != end; ++it )
	{
		int ch          = std::stoi( ( *it )[ 1 ] );
		std::string val = ( *it )[ 2 ];
		std::string lv  = ToLower( val );
		ChannelDesc& c  = pass.ch[ ch ];
		c               = ChannelDesc();
		if( lv == "input" || lv == "video" || lv == "webcam" )
			c.kind = ChannelKind::Input;
		else if( lv == "audio" || lv == "music" || lv == "fft" )
			c.kind = ChannelKind::Audio;
		else if( lv == "keyboard" )
			c.kind = ChannelKind::Keyboard;
		else if( lv == "none" )
			c.kind = ChannelKind::None;
		else if( lv.rfind( "buffer", 0 ) == 0 && lv.size() == 7 )
		{
			c.kind   = ChannelKind::Buffer;
			c.buffer = lv[ 6 ] - 'a';
		}
		else
		{
			c.kind = ChannelKind::Texture;
			c.src  = FileExists( val ) ? val : JoinPath( baseDir, val );
			c.repeat = true;
			c.filter = Filter::Mipmap;
		}
	}
}

static Filter ParseFilter( const std::string& f )
{
	if( f == "nearest" )
		return Filter::Nearest;
	if( f == "mipmap" )
		return Filter::Mipmap;
	return Filter::Linear;
}

static bool ParseJsonDoc( const std::string& text, ShaderDoc& doc, std::string& err )
{
	std::string norm;
	if( !NormalizeShadertoyJson( text, norm, err ) )
		return false;
	json j = json::parse( norm );

	if( j.contains( "info" ) && j[ "info" ].is_object() )
	{
		const json& info = j[ "info" ];
		doc.id           = JStr( info, "id" );
		doc.title        = JStr( info, "name" );
		doc.author       = JStr( info, "username" );
		doc.description  = JStr( info, "description" );
	}

	const json& rp = j[ "renderpass" ];
	if( !rp.is_array() )
	{
		err = "renderpass invalide";
		return false;
	}

	// 1) table id de sortie -> index de buffer
	std::map< std::string, int > outputToBuffer;
	std::vector< std::pair< int, int > > bufferPasses;// (jsonIndex, letterIndex)
	int imageJson = -1;
	for( int i = 0; i < (int)rp.size(); ++i )
	{
		const json& p    = rp[ i ];
		std::string type = ToLower( JStr( p, "type" ) );
		std::string name = JStr( p, "name" );
		if( type == "common" )
			doc.common = JStr( p, "code" );
		else if( type == "image" )
			imageJson = i;
		else if( type == "buffer" )
		{
			int letter = (int)bufferPasses.size();
			if( name.size() >= 8 && ToLower( name.substr( 0, 7 ) ) == "buffer " )
			{
				int l = std::tolower( (unsigned char)name[ 7 ] ) - 'a';
				if( l >= 0 && l < 4 )
					letter = l;
			}
			bufferPasses.push_back( { i, letter } );
		}
		else if( type == "sound" )
			doc.warnings.push_back( "La passe 'Sound' est ignorée (pas de sortie audio dans un effet vidéo)." );
		else if( type == "cubemap" )
			doc.warnings.push_back( "La passe 'Cube A' n'est pas supportée : les canaux qui la lisent seront noirs." );
	}
	if( imageJson < 0 )
	{
		err = "Aucune passe Image dans ce shader";
		return false;
	}
	// ordre d'exécution Shadertoy : A, B, C, D
	std::sort( bufferPasses.begin(), bufferPasses.end(), []( auto& a, auto& b ) { return a.second < b.second; } );
	std::map< int, int > letterToIndex;
	for( int k = 0; k < (int)bufferPasses.size() && k < 4; ++k )
	{
		const json& p = rp[ bufferPasses[ k ].first ];
		letterToIndex[ bufferPasses[ k ].second ] = k;
		if( p.contains( "outputs" ) && p[ "outputs" ].is_array() )
			for( auto& o : p[ "outputs" ] )
				if( o.contains( "id" ) )
					outputToBuffer[ IdToString( o[ "id" ] ) ] = k;
	}
	if( bufferPasses.size() > 4 )
		doc.warnings.push_back( "Plus de 4 buffers : les suivants sont ignorés." );

	auto buildPass = [ & ]( int jsonIndex, bool isImage, int bufferIndex ) {
		const json& p = rp[ jsonIndex ];
		PassDesc pass;
		pass.name        = JStr( p, "name", isImage ? "Image" : "Buffer" );
		pass.isImage     = isImage;
		pass.bufferIndex = bufferIndex;
		pass.jsonIndex   = jsonIndex;
		pass.code        = JStr( p, "code" );
		if( p.contains( "inputs" ) && p[ "inputs" ].is_array() )
		{
			for( auto& in : p[ "inputs" ] )
			{
				int channel = in.value( "channel", -1 );
				if( channel < 0 || channel > 3 )
					continue;
				ChannelDesc c;
				std::string ctype = ToLower( JStr( in, "ctype", JStr( in, "type" ) ) );
				c.src             = JStr( in, "src", JStr( in, "filepath" ) );
				if( in.contains( "sampler" ) && in[ "sampler" ].is_object() )
				{
					const json& s = in[ "sampler" ];
					c.filter      = ParseFilter( ToLower( JStr( s, "filter", "linear" ) ) );
					c.repeat      = ToLower( JStr( s, "wrap", "clamp" ) ) == "repeat";
					c.vflip       = ToLower( JStr( s, "vflip", "true" ) ) == "true";
				}
				if( ctype == "buffer" )
				{
					c.kind   = ChannelKind::Buffer;
					auto it  = outputToBuffer.find( IdToString( in[ "id" ] ) );
					c.buffer = -1;
					if( it != outputToBuffer.end() )
						c.buffer = it->second;
					else
					{
						// repli : "/media/previz/buffer01.png" -> B ; ids numériques 257..260
						std::smatch m;
						static const std::regex reBuf( R"(buffer0?([0-3]))" );
						int letter = -1;
						if( std::regex_search( c.src, m, reBuf ) )
							letter = std::stoi( m[ 1 ] );
						else
						{
							std::string sid = IdToString( in[ "id" ] );
							if( !sid.empty() && std::all_of( sid.begin(), sid.end(), ::isdigit ) )
								letter = std::stoi( sid ) - 257;
						}
						auto lt = letterToIndex.find( letter );
						if( lt != letterToIndex.end() )
							c.buffer = lt->second;
					}
					if( c.buffer < 0 )
						c.kind = ChannelKind::None;
				}
				else if( ctype == "texture" )
					c.kind = ChannelKind::Texture;
				else if( ctype == "cubemap" )
				{
					c.kind = ChannelKind::Cubemap;
					if( c.src.find( "/media/previz/" ) != std::string::npos )
						c.kind = ChannelKind::None;// = sortie de "Cube A" (non supportée)
				}
				else if( ctype == "volume" )
					c.kind = ChannelKind::Volume;
				else if( ctype == "music" || ctype == "musicstream" || ctype == "mic" )
					c.kind = ChannelKind::Audio;
				else if( ctype == "video" || ctype == "webcam" )
					c.kind = ChannelKind::Input;
				else if( ctype == "keyboard" )
					c.kind = ChannelKind::Keyboard;
				else
					c.kind = ChannelKind::None;
				pass.ch[ channel ] = c;
			}
		}
		// canaux libres de la passe Image -> entrée Resolume (pratique pour les effets)
		if( isImage )
			for( auto& c : pass.ch )
				if( c.kind == ChannelKind::None && c.src.empty() )
					c.kind = ChannelKind::Input;
		// les annotations @iChannelN restent prioritaires
		ParseChannelAnnotations( pass.code, pass, ShadertoyCacheDir() );
		return pass;
	};

	for( int k = 0; k < (int)bufferPasses.size() && k < 4; ++k )
		doc.passes.push_back( buildPass( bufferPasses[ k ].first, false, k ) );
	doc.passes.push_back( buildPass( imageJson, true, -1 ) );
	doc.rawJson = norm;
	return true;
}

static bool ParseGlslDoc( const std::string& text, const std::string& pathHint, ShaderDoc& doc, std::string& err )
{
	if( text.find( "mainImage" ) == std::string::npos )
	{
		err = "Pas de fonction mainImage() : ce n'est pas un shader au format Shadertoy";
		if( text.find( "/*{" ) != std::string::npos )
			err += " (ça ressemble à un shader ISF, non supporté)";
		return false;
	}
	PassDesc pass;
	pass.name    = "Image";
	pass.isImage = true;
	pass.code    = text;
	// valeurs par défaut pour un .glsl : 0 = entrée Resolume, 1 = audio, 2/3 = entrée
	pass.ch[ 0 ].kind = ChannelKind::Input;
	pass.ch[ 1 ].kind = ChannelKind::Audio;
	pass.ch[ 2 ].kind = ChannelKind::Input;
	pass.ch[ 3 ].kind = ChannelKind::Input;
	ParseChannelAnnotations( text, pass, pathHint.empty() ? LocalDir() : ParentDir( pathHint ) );
	doc.passes.push_back( pass );
	doc.title = pathHint.empty() ? "Code collé" : FileStem( pathHint );
	return true;
}

bool ParseShaderSource( const std::string& text, const std::string& pathHint, ShaderDoc& doc, std::string& err )
{
	doc            = ShaderDoc();
	doc.sourcePath = pathHint;
	size_t first   = text.find_first_not_of( " \t\r\n" );
	bool ok;
	if( first != std::string::npos && ( text[ first ] == '{' || text[ first ] == '[' ) )
		ok = ParseJsonDoc( text, doc, err );
	else
		ok = ParseGlslDoc( text, pathHint, doc, err );
	if( ok )
	{
		if( doc.title.empty() )
			doc.title = pathHint.empty() ? "Sans titre" : FileStem( pathHint );
		ExtractUserParams( doc );
	}
	return ok;
}

//------------------------------------------------------------------ médias
static std::string MediaLocalPath( const std::string& src )
{
	std::string name = src;
	size_t slash     = name.find_last_of( "/\\" );
	if( slash != std::string::npos )
		name = name.substr( slash + 1 );
	size_t q = name.find( '?' );
	if( q != std::string::npos )
		name = name.substr( 0, q );
	return JoinPath( MediaDir(), SanitizeFileName( name ) );
}

static bool FetchMedia( const std::string& src, bool allowDownload, std::vector< uint8_t >& bytes, std::string& err )
{
	if( src.empty() )
	{
		err = "source vide";
		return false;
	}
	// fichier local direct ?
	if( src[ 0 ] != '/' || FileExists( src ) )
	{
		if( ReadFileBytes( src, bytes ) )
			return true;
	}
	std::string local = MediaLocalPath( src );
	if( ReadFileBytes( local, bytes ) && !bytes.empty() )
		return true;
	if( !allowDownload )
	{
		err = "média absent du cache : " + src;
		return false;
	}
	std::string url = src;
	if( url.rfind( "http", 0 ) != 0 )
		url = "https://www.shadertoy.com" + ( url[ 0 ] == '/' ? url : "/" + url );
	HttpResult r = HttpGet( url );
	if( !r.Ok() || r.body.empty() )
	{
		err = "téléchargement de " + src + " impossible (" + r.error + ")";
		return false;
	}
	bytes = std::move( r.body );
	WriteFileBytes( local, bytes.data(), bytes.size() );
	return true;
}

static void FlipRows( uint8_t* data, int w, int h, int bpp )
{
	std::vector< uint8_t > tmp( (size_t)w * bpp );
	for( int y = 0; y < h / 2; ++y )
	{
		uint8_t* a = data + (size_t)y * w * bpp;
		uint8_t* b = data + (size_t)( h - 1 - y ) * w * bpp;
		memcpy( tmp.data(), a, tmp.size() );
		memcpy( a, b, tmp.size() );
		memcpy( b, tmp.data(), tmp.size() );
	}
}

static std::shared_ptr< ImageData > DecodeImage( const std::vector< uint8_t >& bytes, bool vflip )
{
	int w = 0, h = 0, n = 0;
	unsigned char* px = stbi_load_from_memory( bytes.data(), (int)bytes.size(), &w, &h, &n, 4 );
	if( !px )
		return nullptr;
	auto img    = std::make_shared< ImageData >();
	img->width  = w;
	img->height = h;
	img->comps  = 4;
	img->data.assign( px, px + (size_t)w * h * 4 );
	stbi_image_free( px );
	if( vflip )
		FlipRows( img->data.data(), w, h, 4 );
	return img;
}

static std::shared_ptr< ImageData > DecodeVolume( const std::vector< uint8_t >& b )
{
	if( b.size() < 20 )
		return nullptr;
	auto rd32 = [ & ]( size_t o ) { return (uint32_t)b[ o ] | ( (uint32_t)b[ o + 1 ] << 8 ) | ( (uint32_t)b[ o + 2 ] << 16 ) | ( (uint32_t)b[ o + 3 ] << 24 ); };
	auto img     = std::make_shared< ImageData >();
	img->width   = (int)rd32( 4 );
	img->height  = (int)rd32( 8 );
	img->depth   = (int)rd32( 12 );
	img->comps   = b[ 16 ];
	int format   = b[ 18 ] | ( b[ 19 ] << 8 );
	img->isFloat = format == 10;
	if( img->comps != 1 && img->comps != 4 )
		return nullptr;
	size_t bytesPer = img->isFloat ? 4 : 1;
	size_t need     = (size_t)img->width * img->height * img->depth * img->comps * bytesPer;
	if( img->width <= 0 || img->height <= 0 || img->depth <= 0 || b.size() < 20 + need )
		return nullptr;
	img->data.assign( b.begin() + 20, b.begin() + 20 + need );
	return img;
}

std::shared_ptr< ImageData > LoadImageFile( const std::string& path, bool vflip, std::string& err )
{
	std::vector< uint8_t > bytes;
	if( !ReadFileBytes( path, bytes ) || bytes.empty() )
	{
		err = "Impossible de lire l'image " + path;
		return nullptr;
	}
	auto img = DecodeImage( bytes, vflip );
	if( !img )
		err = "Format d'image non reconnu : " + path;
	return img;
}

void ResolveMedia( ShaderDoc& doc, bool allowDownload )
{
	std::map< std::string, std::shared_ptr< const ImageData > > cache;
	for( auto& pass : doc.passes )
	{
		for( int ci = 0; ci < 4; ++ci )
		{
			ChannelDesc& c = pass.ch[ ci ];
			if( c.kind != ChannelKind::Texture && c.kind != ChannelKind::Cubemap && c.kind != ChannelKind::Volume )
				continue;
			std::string key = c.src + ( c.vflip ? "#f" : "#n" );
			auto it         = cache.find( key );
			if( it != cache.end() )
			{
				c.image = it->second;
				continue;
			}
			std::string err;
			std::shared_ptr< ImageData > img;
			if( c.kind == ChannelKind::Texture )
			{
				std::vector< uint8_t > bytes;
				if( FetchMedia( c.src, allowDownload, bytes, err ) )
					img = DecodeImage( bytes, c.vflip );
			}
			else if( c.kind == ChannelKind::Volume )
			{
				std::vector< uint8_t > bytes;
				if( FetchMedia( c.src, allowDownload, bytes, err ) )
					img = DecodeVolume( bytes );
			}
			else// cubemap : base, base_1 .. base_5
			{
				size_t dot       = c.src.find_last_of( '.' );
				std::string stem = dot == std::string::npos ? c.src : c.src.substr( 0, dot );
				std::string ext  = dot == std::string::npos ? "" : c.src.substr( dot );
				for( int f = 0; f < 6; ++f )
				{
					std::vector< uint8_t > bytes;
					std::string faceSrc = f == 0 ? c.src : stem + "_" + std::to_string( f ) + ext;
					if( !FetchMedia( faceSrc, allowDownload, bytes, err ) )
					{
						img.reset();
						break;
					}
					auto face = DecodeImage( bytes, c.vflip );
					if( !face || ( img && ( face->width != img->width || face->height != img->height ) ) )
					{
						img.reset();
						break;
					}
					if( !img )
					{
						img         = std::make_shared< ImageData >( *face );
						img->faces  = 6;
						img->data.reserve( face->data.size() * 6 );
					}
					else
						img->data.insert( img->data.end(), face->data.begin(), face->data.end() );
				}
			}
			if( !img )
				doc.warnings.push_back( pass.name + " iChannel" + std::to_string( ci ) + " : " + ( err.empty() ? "image illisible " + c.src : err ) );
			cache[ key ] = img;
			c.image      = img;
		}
	}
}

bool LoadShaderFile( const std::string& path, ShaderDoc& doc, std::string& err )
{
	std::string text;
	std::string realPath = path;
	if( !FileExists( realPath ) )
	{
		// le fichier a peut-être été rangé dans un autre dossier de la bibliothèque
		std::string moved = FindByFileName( path );
		if( !moved.empty() )
			realPath = moved;
	}
	if( !ReadFileText( realPath, text ) )
	{
		err = "Impossible de lire " + path;
		return false;
	}
	if( !ParseShaderSource( text, realPath, doc, err ) )
		return false;
	ResolveMedia( doc, true );
	return true;
}

std::string SerializeDoc( const ShaderDoc& doc, std::string& extension )
{
	if( !doc.rawJson.empty() )
	{
		json j = json::parse( doc.rawJson, nullptr, false );
		if( !j.is_discarded() )
		{
			auto& rp = j[ "renderpass" ];
			for( auto& p : rp )
				if( ToLower( JStr( p, "type" ) ) == "common" )
					p[ "code" ] = doc.common;
			for( auto& pass : doc.passes )
				if( pass.jsonIndex >= 0 && pass.jsonIndex < (int)rp.size() )
					rp[ pass.jsonIndex ][ "code" ] = pass.code;
			if( j.contains( "info" ) )
				j[ "info" ][ "name" ] = doc.title;
			extension = ".json";
			return j.dump( 1, '\t' );
		}
	}
	extension = ".glsl";
	const PassDesc* img = doc.ImagePass();
	return img ? img->code : std::string();
}

ShaderDoc DefaultDoc( bool isSource )
{
	ShaderDoc d;
	PassDesc p;
	p.name    = "Image";
	p.isImage = true;
	for( auto& c : p.ch )
		c.kind = ChannelKind::Input;
	if( isSource )
	{
		d.title = "Défaut";
		p.code  = "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n"
				  "{\n"
				  "    vec2 uv = fragCoord / iResolution.xy;\n"
				  "    vec3 col = 0.5 + 0.5 * cos( iTime + uv.xyx + vec3( 0, 2, 4 ) );\n"
				  "    fragColor = vec4( col, 1.0 );\n"
				  "}\n";
	}
	else
	{
		d.title = "Passthrough";
		p.code  = "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n"
				  "{\n"
				  "    fragColor = texture( iChannel0, fragCoord / iResolution.xy );\n"
				  "}\n";
	}
	d.passes.push_back( p );
	return d;
}
}//namespace stfx
