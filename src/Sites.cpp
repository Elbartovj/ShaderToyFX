#include "Sites.h"
#include "Util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace stfx
{
namespace sites
{
namespace
{
fs::path P( const std::string& s ) { return fs::path( Widen( s ) ); }

//------------------------------------------------------------------ JSON Shadertoy
json BufferInput( int channel, int letter, bool nearest = false )
{
	return { { "channel", channel },
			 { "ctype", "buffer" },
			 { "id", 257 + letter },
			 { "src", "/media/previz/buffer0" + std::to_string( letter ) + ".png" },
			 { "sampler", { { "filter", nearest ? "nearest" : "linear" }, { "wrap", "clamp" }, { "vflip", "true" } } } };
}
json SimpleInput( int channel, const std::string& ctype, const std::string& src = "", bool repeat = false )
{
	return { { "channel", channel },
			 { "ctype", ctype },
			 { "src", src },
			 { "sampler", { { "filter", ctype == "texture" ? "mipmap" : "linear" }, { "wrap", repeat ? "repeat" : "clamp" }, { "vflip", "true" } } } };
}
json MakePass( const std::string& type, const std::string& name, const std::string& code, const json& inputs, int bufferLetter )
{
	json p = { { "type", type }, { "name", name }, { "code", code }, { "inputs", inputs.is_null() ? json::array() : inputs } };
	if( bufferLetter >= 0 )
		p[ "outputs" ] = json::array( { { { "id", 257 + bufferLetter }, { "channel", 0 } } } );
	else if( type == "image" )
		p[ "outputs" ] = json::array( { { { "id", "4dfGRr" }, { "channel", 0 } } } );
	return p;
}
json MakeShader( const std::string& id, const std::string& title, const std::string& author, const std::string& description,
				 const json& passes )
{
	return { { "info", { { "id", id }, { "name", title }, { "username", author }, { "description", description } } },
			 { "renderpass", passes } };
}

std::string ReplaceIdent( const std::string& code, const std::string& name, const std::string& repl )
{
	// identifiant entier, pas un membre (« .time ») : la capture garde le caractère précédent
	std::regex re( "(^|[^.\\w])" + name + "\\b" );
	return std::regex_replace( code, re, "$1" + repl );
}

// data:<mime>;base64,... -> octets
bool DecodeDataUrl( const std::string& dataUrl, std::vector< uint8_t >& out, std::string& mime )
{
	size_t comma = dataUrl.find( ',' );
	if( dataUrl.rfind( "data:", 0 ) != 0 || comma == std::string::npos || dataUrl.find( ";base64" ) > comma )
		return false;
	mime = dataUrl.substr( 5, dataUrl.find( ';' ) - 5 );
	static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	out.clear();
	out.reserve( ( dataUrl.size() - comma ) * 3 / 4 );
	int val = 0, bits = -8;
	for( size_t i = comma + 1; i < dataUrl.size(); ++i )
	{
		size_t c = b64.find( dataUrl[ i ] );
		if( c == std::string::npos )
			continue;
		val = ( ( val << 6 ) + (int)c ) & 0xFFFFFF;
		bits += 6;
		if( bits >= 0 )
		{
			out.push_back( uint8_t( ( val >> bits ) & 0xFF ) );
			bits -= 8;
		}
	}
	return !out.empty();
}

// data:image/png;base64,... -> fichier du cache média ; retourne la source "/media/a/<nom>"
std::string SaveDataUrl( const std::string& dataUrl, const std::string& baseName )
{
	std::vector< uint8_t > out;
	std::string mime;
	if( !DecodeDataUrl( dataUrl, out, mime ) )
		return {};
	std::string ext = ".png";
	if( mime == "image/jpeg" )
		ext = ".jpg";
	else if( mime == "image/gif" )
		ext = ".gif";
	else if( mime == "image/webp" )
		ext = ".webp";
	std::string name = SanitizeFileName( baseName ) + ext;
	MakeDirs( MediaDir() );
	WriteFileBytes( JoinPath( MediaDir(), name ), out.data(), out.size() );
	return "/media/a/" + name;
}

// Texture flottante Shaderoo (« tile3d:64x64x64 … float32 ») -> volume au format Shadertoy (.bin)
// enregistré dans le cache média ; retourne la source "/media/a/<nom>.bin", vide si illisible.
std::string SaveShaderooFloatVolume( const std::vector< uint8_t >& b, const std::string& baseName )
{
	size_t hdrEnd = std::string::npos;
	for( size_t i = 0; i + 1 < b.size() && i < 4096; ++i )
		if( b[ i ] == '\n' && b[ i + 1 ] == '\n' )
		{
			hdrEnd = i + 2;
			break;
		}
	if( hdrEnd == std::string::npos )
		return {};
	std::string hdr( b.begin(), b.begin() + hdrEnd );
	int width = 0, height = 0, channels = 1, bits = 32, tw = 0, th = 0, td = 0;
	std::stringstream ss( hdr );
	std::string l;
	while( std::getline( ss, l ) )
	{
		if( l.rfind( "width:", 0 ) == 0 ) width = atoi( l.c_str() + 6 );
		else if( l.rfind( "height:", 0 ) == 0 ) height = atoi( l.c_str() + 7 );
		else if( l.rfind( "channels:", 0 ) == 0 ) channels = atoi( l.c_str() + 9 );
		else if( l.rfind( "bits_per_channel:", 0 ) == 0 ) bits = atoi( l.c_str() + 17 );
		else if( l.rfind( "tile3d:", 0 ) == 0 ) sscanf( l.c_str() + 7, "%dx%dx%d", &tw, &th, &td );
	}
	if( bits != 32 || ( channels != 1 && channels != 4 ) || tw <= 0 || th <= 0 || td <= 0 || width < tw )
		return {};
	size_t px = (size_t)channels * 4;
	if( b.size() < hdrEnd + (size_t)width * height * px )
		return {};
	int xnum = width / tw;
	std::vector< uint8_t > out( 20 + (size_t)tw * th * td * px );
	auto w32 = [ & ]( size_t o, uint32_t v ) { for( int k = 0; k < 4; ++k ) out[ o + k ] = uint8_t( v >> ( 8 * k ) ); };
	memcpy( out.data(), "BIN\0", 4 );
	w32( 4, tw );
	w32( 8, th );
	w32( 12, td );
	out[ 16 ] = (uint8_t)channels;
	out[ 17 ] = 0;
	out[ 18 ] = 10;// flottant
	out[ 19 ] = 0;
	for( int z = 0; z < td; ++z )
	{
		int X = z % xnum, Y = z / xnum;
		for( int y = 0; y < th; ++y )
		{
			size_t src = hdrEnd + ( (size_t)X * tw + (size_t)Y * th * width + (size_t)y * width ) * px;
			size_t dst = 20 + ( (size_t)y * tw + (size_t)z * tw * th ) * px;
			if( src + tw * px > b.size() )
				return {};
			memcpy( out.data() + dst, b.data() + src, tw * px );
		}
	}
	std::string name = SanitizeFileName( baseName ) + ".bin";
	MakeDirs( MediaDir() );
	WriteFileBytes( JoinPath( MediaDir(), name ), out.data(), out.size() );
	return "/media/a/" + name;
}

std::string ShortHash( const std::string& s )
{
	uint32_t h = 2166136261u;
	for( unsigned char c : s )
		h = ( h ^ c ) * 16777619u;
	char buf[ 16 ];
	snprintf( buf, sizeof( buf ), "%08x", h );
	return buf;
}

std::string GuessRangeComment( float v )
{
	char buf[ 96 ];
	if( v == 0.0f )
		snprintf( buf, sizeof( buf ), "-1 1 0" );
	else if( v > 0 )
		snprintf( buf, sizeof( buf ), "0 %g %g", v * 2.0f, v );
	else
		snprintf( buf, sizeof( buf ), "%g %g %g", v * 2.0f, -v * 2.0f, v );
	return buf;
}

//------------------------------------------------------------------ GLSL « main() »
struct MainStyle
{
	std::string code;
	bool feedback = false;// lit l'image précédente (backbuffer)
	bool audio    = false;
	int inputs    = 0;    // autres samplers -> entrée Resolume (iChannel2, iChannel3)
};

bool ConvertMain( const std::string& src, MainStyle& out, std::string& err )
{
	static const std::regex reMain( R"(\bvoid\s+main\s*\(\s*(void)?\s*\))" );
	if( !std::regex_search( src, reMain ) )
	{
		err = "pas de fonction main()";
		return false;
	}
	std::string code = src;
	// directives propres au GLSL ES / WebGL
	code = std::regex_replace( code, std::regex( R"((^|\n)[ \t]*#version[^\n]*)" ), "$1" );
	code = std::regex_replace( code, std::regex( R"((^|\n)[ \t]*#extension[^\n]*)" ), "$1" );

	std::string globals = "// --- converti par Shader Loader (shader « main() ») ---\n";
	std::string assigns;
	std::string outName = "stfx_glFragColor";
	std::set< std::string > declaredGlobals;
	auto global = [ & ]( const std::string& type, const std::string& name ) {
		if( declaredGlobals.insert( name ).second )
			globals += type + " " + name + ";\n";
	};

	// uniforms connus -> variables globales remplies à chaque image
	static const std::regex reUni( R"((^|\n)[ \t]*uniform\s+(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+([^;]+);([^\n]*))" );
	std::string rebuilt;
	size_t last = 0;
	int nextInput = 2;
	for( std::sregex_iterator it( code.begin(), code.end(), reUni ), end; it != end; ++it )
	{
		const std::smatch& m = *it;
		rebuilt += code.substr( last, m.position( 0 ) - last );
		last = m.position( 0 ) + m.length( 0 );
		std::string type = m[ 2 ].str(), names = m[ 3 ].str();
		std::string kept;
		std::stringstream ss( names );
		std::string n;
		while( std::getline( ss, n, ',' ) )
		{
			n            = Trim( n );
			std::string l = ToLower( n );
			std::string repl, value;
			if( type == "float" && ( l == "time" || l == "u_time" || l == "itime" || l == "utime" || l == "iglobaltime" ) )
				value = "iTime";
			else if( type == "float" && ( l == "u_delta" || l == "timedelta" || l == "itimedelta" || l == "u_timedelta" ) )
				value = "iTimeDelta";
			else if( ( type == "vec2" || type == "vec3" ) && ( l == "resolution" || l == "u_resolution" || l == "uresolution" || l == "iresolution" || l == "res" ) )
				value = type == "vec2" ? "iResolution.xy" : "iResolution";
			else if( type == "vec2" && l == "mouse" )
				value = "( iMouse.xy / iResolution.xy )";// GLSLSandbox / NEORT : 0..1
			else if( type == "vec2" && ( l == "u_mouse" || l == "imouse" || l == "umouse" ) )
				value = "iMouse.xy";
			else if( type == "vec4" && ( l == "imouse" || l == "u_mouse" ) )
				value = "iMouse";
			else if( ( type == "int" || type == "float" ) && ( l == "frame" || l == "u_frame" || l == "iframe" || l == "framecount" ) )
				value = type == "int" ? "iFrame" : "float( iFrame )";
			else if( type == "vec4" && ( l == "date" || l == "u_date" || l == "idate" ) )
				value = "iDate";
			else if( type == "sampler2D" && ( l == "backbuffer" || l == "u_backbuffer" || l == "prevframe" || l == "u_prevframe" || l == "u_buffer0" ) )
			{
				repl         = "iChannel0";
				out.feedback = true;
			}
			else if( type == "sampler2D" && std::regex_search( l, std::regex( "audio|sound|fft|spectrum|music|freq" ) ) &&
					 !std::regex_search( l, std::regex( "^(u_)?(tex|texture|image|img)\\d*$" ) ) )
			{
				repl      = "iChannel1";
				out.audio = true;
			}
			else if( type == "sampler2D" && nextInput < 4 && n.rfind( "iChannel", 0 ) != 0 )
			{
				repl = "iChannel" + std::to_string( nextInput++ );
				++out.inputs;
			}
			if( !value.empty() )
			{
				std::string g = "stfx_u_" + n;
				global( type, g );
				assigns += "\t" + g + " = " + value + ";\n";
				code      = code;// le renommage est fait après la reconstruction
				repl      = g;
			}
			if( repl.empty() )
				kept += ( kept.empty() ? "" : ", " ) + n;
			else
				globals += "#define " + n + " " + repl + "\n";
		}
		rebuilt += m[ 1 ].str();
		if( !kept.empty() )
			rebuilt += "uniform " + type + " " + kept + ";" + m[ 4 ].str();
	}
	rebuilt += code.substr( last );
	code = rebuilt;

	// sortie : gl_FragColor ou « out vec4 X; »
	static const std::regex reOut( R"((^|\n)[ \t]*(?:layout\s*\([^)]*\)\s*)?out\s+(?:(?:lowp|mediump|highp)\s+)?vec4\s+(\w+)\s*;)" );
	std::smatch om;
	if( std::regex_search( code, om, reOut ) )
	{
		outName = om[ 2 ].str();
		code    = std::regex_replace( code, reOut, "$1vec4 " + outName + ";", std::regex_constants::format_first_only );
	}
	else
	{
		global( "vec4", "stfx_glFragColor" );
		globals += "#define gl_FragColor stfx_glFragColor\n";
	}
	// varyings d'un vertex shader plein écran : coordonnées 0..1
	static const std::regex reVary( R"((^|\n)[ \t]*(?:varying|in)\s+(?:(?:lowp|mediump|highp)\s+)?vec2\s+(\w+)\s*;)" );
	for( std::sregex_iterator it( code.begin(), code.end(), reVary ), end; it != end; ++it )
	{
		std::string n = ( *it )[ 2 ].str();
		assigns += "\t" + n + " = fragCoord / iResolution.xy;\n";
	}
	code = std::regex_replace( code, reVary, "$1vec2 $2;" );

	code = std::regex_replace( code, reMain, "void stfx_user_main()" );
	out.code = globals + "#line 1\n" + code +
			   "\n\n// --- appel Shadertoy ---\nvoid mainImage( out vec4 fragColor, in vec2 fragCoord )\n{\n" + assigns + "\t" + outName +
			   " = vec4( 0.0, 0.0, 0.0, 1.0 );\n\tstfx_user_main();\n\tfragColor = " + outName + ";\n}\n";
	return true;
}

json MainStyleShader( const MainStyle& ms, const std::string& id, const std::string& title, const std::string& author,
					  const std::string& description )
{
	json passes = json::array();
	json in     = json::array();
	if( ms.audio )
		in.push_back( SimpleInput( 1, "music" ) );
	for( int k = 0; k < ms.inputs; ++k )
		in.push_back( SimpleInput( 2 + k, "video" ) );
	if( ms.feedback )
	{
		// le shader dessine dans le Buffer A, qui se relit lui-même (image précédente)
		json bin = in;
		bin.push_back( BufferInput( 0, 0 ) );
		passes.push_back( MakePass( "buffer", "Buffer A", ms.code, bin, 0 ) );
		passes.push_back( MakePass( "image", "Image",
									"void mainImage( out vec4 fragColor, in vec2 fragCoord )\n{\n\tfragColor = texture( iChannel0, fragCoord / iResolution.xy );\n}\n",
									json::array( { BufferInput( 0, 0 ) } ), -1 ) );
	}
	else
		passes.push_back( MakePass( "image", "Image", ms.code, in, -1 ) );
	return MakeShader( id, title, author, description, passes );
}

//------------------------------------------------------------------ ShaderKit
bool ConvertShaderKit( const json& b, json& out, std::string& title, std::string& err )
{
	const json& d = b.value( "data", json::object() );
	if( !d.contains( "channels" ) || !d[ "channels" ].is_object() )
	{
		err = "export ShaderKit sans code";
		return false;
	}
	title = d.value( "title", std::string( "ShaderKit" ) );
	std::string common = d.value( "commonCode", std::string() );
	const json& ch = d[ "channels" ];
	bool nearest   = d.value( "magFilter", std::string() ) == "NEAREST";

	// uniforms ShaderKit (sliders) : « uniform float nom; // min max valeur » ; la déclaration
	// « float nom = valeur; » du code est retirée, comme le fait ShaderKit
	std::string header = "// iResolution est un vec2 sur ShaderKit\n#define iResolution (iResolution.xy)\n";
	std::vector< std::string > uniNames;
	if( d.contains( "uniforms" ) && d[ "uniforms" ].is_array() )
		for( auto& u : d[ "uniforms" ] )
		{
			std::string n = u.value( "name", std::string() );
			if( n.empty() || !std::regex_match( n, std::regex( R"([A-Za-z_]\w*)" ) ) )
				continue;
			float v    = u.value( "value", 0.0f );
			float mn   = u.contains( "min" ) && u[ "min" ].is_number() ? u[ "min" ].get< float >() : ( v == 0 ? -1.0f : std::min( 0.0f, v * 2 ) );
			float mx   = u.contains( "max" ) && u[ "max" ].is_number() ? u[ "max" ].get< float >() : ( v == 0 ? 1.0f : std::max( 0.0f, v * 2 ) );
			char buf[ 160 ];
			snprintf( buf, sizeof( buf ), "uniform float %s; // %g %g %g\n", n.c_str(), mn, mx, v );
			header += buf;
			uniNames.push_back( n );
		}
	auto stripDecls = [ & ]( std::string code ) {
		for( auto& n : uniNames )
			code = std::regex_replace( code, std::regex( "(^|\\n)[ \\t]*(const\\s+)?float\\s+" + n + "\\s*=\\s*[^;\\n]*;[^\\n]*" ), "$1" );
		return code;
	};
	common = header + stripDecls( common );

	std::vector< int > letters;
	if( d.contains( "availableChannels" ) && d[ "availableChannels" ].is_array() )
		for( auto& c : d[ "availableChannels" ] )
		{
			std::smatch m;
			std::string s = c.get< std::string >();
			if( std::regex_match( s, m, std::regex( R"(iChannel(\d+))" ) ) )
			{
				int k = std::stoi( m[ 1 ] );
				if( k < 4 && ch.contains( s ) )
					letters.push_back( k );
			}
		}
	json inputs = json::array();
	for( int k : letters )
		inputs.push_back( BufferInput( k, k, nearest ) );

	json passes = json::array();
	passes.push_back( MakePass( "common", "Common", common, json::array(), -1 ) );
	for( int k : letters )
	{
		static const char* names[] = { "Buffer A", "Buffer B", "Buffer C", "Buffer D" };
		passes.push_back( MakePass( "buffer", names[ k ], stripDecls( ch[ "iChannel" + std::to_string( k ) ].get< std::string >() ), inputs, k ) );
	}
	std::string image = ch.contains( "displayMode" ) ? ch[ "displayMode" ].get< std::string >() : std::string();
	if( image.empty() && !letters.empty() )
		image = "void mainImage( out vec4 fragColor, in vec2 fragCoord ) { fragColor = texture( iChannel0, fragCoord / iResolution.xy ); }";
	passes.push_back( MakePass( "image", "Image", stripDecls( image ), inputs, -1 ) );
	out = MakeShader( "shaderkit-" + b.value( "id", std::string() ), title, d.value( "ownerUsername", std::string() ),
					  d.value( "description", std::string() ) + "\n" + b.value( "url", std::string() ), passes );
	return true;
}

//------------------------------------------------------------------ Shaderoo
bool ConvertShaderoo( const json& b, json& out, std::string& title, std::string& err, std::vector< std::string >& warnings )
{
	std::string scene = b.value( "scene", std::string() );
	if( scene.empty() )
	{
		err = "scène Shaderoo vide";
		return false;
	}
	// sections « ###BEGIN:Fichier.glsl »
	std::map< std::string, std::string > files;
	std::string head = scene;
	size_t pos       = scene.find( "###BEGIN:" );
	if( pos != std::string::npos )
	{
		head = scene.substr( 0, pos );
		while( pos != std::string::npos )
		{
			size_t nl   = scene.find( '\n', pos );
			size_t next = scene.find( "###BEGIN:", pos + 9 );
			std::string name = Trim( scene.substr( pos + 9, ( nl == std::string::npos ? scene.size() : nl ) - pos - 9 ) );
			std::string body = nl == std::string::npos ? std::string() : scene.substr( nl + 1, ( next == std::string::npos ? scene.size() : next ) - nl - 1 );
			// images encodées dans des commentaires (illisibles ici)
			body          = std::regex_replace( body, std::regex( R"(//##IMG##[^\n]*)" ), "" );
			files[ name ] = body;
			pos           = next;
		}
	}
	std::string author, description;
	std::smatch m;
	if( std::regex_search( head, m, std::regex( R"(###TITLE:([^\n]*))" ) ) )
		title = Trim( m[ 1 ] );
	if( std::regex_search( head, m, std::regex( R"(###USER:([^\n]*))" ) ) )
		author = Trim( m[ 1 ] );
	if( std::regex_search( head, m, std::regex( R"(###DESCRIPTION:([^\n]*))" ) ) )
		description = m[ 1 ];

	struct Entry
	{
		std::string name, file;
		std::vector< std::string > tex;
		std::map< std::string, std::string > uniforms;
		bool fixedSize = false;
	};
	std::vector< Entry > entries;
	std::stringstream hs( head );
	std::string line;
	while( std::getline( hs, line ) )
	{
		line = Trim( line );
		if( line.empty() || line[ 0 ] == '#' || line.rfind( "Timeline:", 0 ) == 0 )
			continue;
		std::stringstream ls( line );
		std::string tok;
		ls >> tok;
		if( tok.size() < 2 || tok.back() != ':' )
			continue;
		Entry e;
		e.name = tok.substr( 0, tok.size() - 1 );
		while( ls >> tok )
		{
			if( tok.rfind( "local:", 0 ) == 0 )
				e.file = tok.substr( 6 );
			else if( tok.rfind( "opt:", 0 ) == 0 )
			{
				std::stringstream os( tok.substr( 4 ) );
				std::string kv;
				while( std::getline( os, kv, ';' ) )
				{
					size_t eq = kv.find( '=' );
					if( eq == std::string::npos )
						continue;
					std::string k = kv.substr( 0, eq ), v = kv.substr( eq + 1 );
					if( k.rfind( "uniform.", 0 ) == 0 )
						e.uniforms[ k.substr( 8 ) ] = v;
					else if( ( k == "width" || k == "height" ) && v != "auto" )
						e.fixedSize = true;
				}
			}
			else if( tok.size() < 6 || tok.substr( tok.size() - 5 ) != ".glsl" )
				e.tex.push_back( tok );
		}
		entries.push_back( e );
	}

	std::function< std::string( const std::string&, int ) > expand = [ & ]( const std::string& code, int depth ) -> std::string {
		if( depth > 8 )
			return code;
		static const std::regex reInc( R"re((^|\n)[ \t]*#include\s+"?([\w\.]+)"?[^\n]*)re" );
		std::string res;
		size_t lastPos = 0;
		for( std::sregex_iterator it( code.begin(), code.end(), reInc ), end; it != end; ++it )
		{
			res += code.substr( lastPos, it->position( 0 ) - lastPos ) + ( *it )[ 1 ].str();
			lastPos      = it->position( 0 ) + it->length( 0 );
			std::string n = ( *it )[ 2 ].str();
			auto f       = files.find( n );
			if( f == files.end() )
				f = files.find( n + ".glsl" );
			if( f != files.end() )
				res += "\n" + expand( f->second, depth + 1 ) + "\n";
			else
				warnings.push_back( "#include " + n + " introuvable" );
		}
		return res + code.substr( lastPos );
	};

	json images = b.value( "images", json::object() );
	struct TexInfo
	{
		std::string src;
		bool volume = false;
	};
	std::map< std::string, TexInfo > savedTex;
	auto texSrc = [ & ]( const std::string& tok ) -> TexInfo {
		auto it = savedTex.find( tok );
		if( it != savedTex.end() )
			return it->second;
		TexInfo ti;
		std::string base = "shaderoo_" + ( std::regex_match( tok, std::regex( R"(\w+)" ) ) ? tok : ShortHash( tok ) );
		if( images.contains( tok ) && images[ tok ].is_string() )
		{
			const std::string du = images[ tok ].get< std::string >();
			if( du.rfind( "data:image/", 0 ) == 0 )
				ti.src = SaveDataUrl( du, base );
			else
			{
				std::vector< uint8_t > bytes;
				std::string mime;
				if( DecodeDataUrl( du, bytes, mime ) )
				{
					ti.src    = SaveShaderooFloatVolume( bytes, base );
					ti.volume = !ti.src.empty();
				}
			}
		}
		else if( std::regex_search( ToLower( tok ), std::regex( R"(^https?://[^ ]+\.(png|jpe?g|gif|webp|bmp)(\?|$))" ) ) )
			ti.src = tok;// image en ligne : téléchargée au chargement
		savedTex[ tok ] = ti;
		return ti;
	};

	auto bufferLetter = []( const std::string& n ) -> int {
		std::smatch mm;
		if( std::regex_match( n, mm, std::regex( R"((?:Ref:)?Buff_([A-D])(?::0)?(?::Tex0)?)" ) ) )
			return mm[ 1 ].str()[ 0 ] - 'A';
		return -1;
	};

	std::string common =
		"// --- uniforms propres à Shaderoo (sans équivalent dans Resolume) ---\n"
		"#define iMouseData vec4(0.0)\n#define iMouseMove vec4(0.0)\n#define iPassIndex 0\n#define iNumPasses 1\n"
		"#define iNumTriangles 0\n#define iCaptureGeom 0\n#define SHADEROO\n#define SHADEROO_FRAGMENT_SHADER\n"
		"vec4 FragColor1, FragColor2, FragColor3;\n"
		"#define iChannel0Resolution ( iChannelResolution[ 0 ].xy )\n#define iChannel1Resolution ( iChannelResolution[ 1 ].xy )\n"
		"#define iChannel2Resolution ( iChannelResolution[ 2 ].xy )\n#define iChannel3Resolution ( iChannelResolution[ 3 ].xy )\n";

	json passes = json::array();
	bool haveImage = false;
	for( auto& e : entries )
	{
		bool isImage = e.name == "Image";
		int letter   = bufferLetter( e.name );
		if( !isImage && letter < 0 )
		{
			if( e.name.rfind( "Geom_", 0 ) == 0 )
				warnings.push_back( "Passe géométrique « " + e.name + " » (maillage de triangles) non prise en charge : les canaux qui la lisent seront noirs." );
			continue;// Include_*, Geom_*…
		}
		auto f = files.find( e.file );
		if( f == files.end() )
		{
			warnings.push_back( "Fichier " + e.file + " introuvable" );
			continue;
		}
		std::string code = expand( f->second, 0 );
		if( code.find( "mainGeom" ) != std::string::npos && code.find( "mainImage" ) == std::string::npos )
		{
			if( isImage )
			{
				err = "Ce shader Shaderoo dessine des triangles (mainGeom) : non pris en charge.";
				return false;
			}
			continue;
		}
		// valeurs des uniforms réglées dans Shaderoo -> plage du slider
		for( auto& u : e.uniforms )
		{
			if( u.first.find( '.' ) != std::string::npos )
				continue;
			float v = (float)atof( u.second.c_str() );
			code    = std::regex_replace( code, std::regex( "(uniform\\s+float\\s+" + u.first + "\\s*;)[^\\n]*" ),
											  "$1 // " + GuessRangeComment( v ) );
		}
		// « #ShaderooNumPasses N » : la passe tourne N fois par image sur Shaderoo ; ici une fois par image,
		// avec iPassIndex qui avance d'une image à l'autre (même résultat, N fois plus lent à se construire)
		{
			std::smatch nm;
			static const std::regex reNum( R"((^|\n)[ \t]*#[ \t]*ShaderooNumPasses[ \t]+(\d+)[^\n]*)" );
			if( std::regex_search( code, nm, reNum ) )
			{
				std::string n = nm[ 2 ].str();
				code = std::regex_replace( code, reNum,
										   "$1#undef iPassIndex\n#define iPassIndex ( iFrame % " + n + " )\n#undef iNumPasses\n#define iNumPasses " + n );
				warnings.push_back( "« " + e.name + " » est calculé " + n + " fois par image sur Shaderoo : ici une fois par image, son résultat se construit sur " + n + " images." );
			}
		}
		code = std::regex_replace( code, std::regex( R"((^|\n)[ \t]*#[ \t]*define[ \t]+iChannel[0-3]Resolution\b[^\n]*)" ), "$1" );
		if( e.fixedSize )
			warnings.push_back( "« " + e.name + " » a une résolution fixe sur Shaderoo : il est calculé ici à la résolution de la composition." );
		json in = json::array();
		for( size_t i = 0; i < e.tex.size(); ++i )
		{
			const std::string& t = e.tex[ i ];
			if( i >= 4 )
			{
				warnings.push_back( "« " + e.name + " » utilise plus de 4 textures : les suivantes sont ignorées." );
				break;
			}
			int bl = bufferLetter( t );
			std::string lt = ToLower( t );
			if( bl >= 0 )
				in.push_back( BufferInput( (int)i, bl ) );
			else if( std::regex_search( lt, std::regex( R"(\.(webm|mp4|ogv|mov)(\?|$))" ) ) || lt == "webcam" || lt == "video" )
				in.push_back( SimpleInput( (int)i, "video" ) );
			else if( std::regex_search( lt, std::regex( R"(\.(mp3|ogg|oga|wav|m4a|flac|aac|opus)(\?|$)|soundcloud\.com)" ) ) ||
					 lt == "mic" || lt == "microphone" || lt == "audio" || lt == "sound" || lt == "music" || lt == "fft" )
				in.push_back( SimpleInput( (int)i, "music" ) );// son -> spectre audio de Resolume
			else if( lt == "keyboard" )
				in.push_back( SimpleInput( (int)i, "keyboard" ) );
			else
			{
				TexInfo ti = texSrc( t );
				if( !ti.src.empty() )
					in.push_back( SimpleInput( (int)i, ti.volume ? "volume" : "texture", ti.src, true ) );
				else if( t.rfind( "Geom_", 0 ) != 0 && t.rfind( "src:", 0 ) != 0 && t.rfind( "Ref:", 0 ) != 0 )
					warnings.push_back( "Texture « " + t + " » indisponible : canal " + std::to_string( i ) + " noir." );
				if( ti.src.empty() )
					in.push_back( { { "channel", (int)i }, { "ctype", "none" }, { "src", "none" } } );
			}
		}
		if( isImage )
		{
			haveImage = true;
			passes.push_back( MakePass( "image", "Image", code, in, -1 ) );
		}
		else
		{
			static const char* names[] = { "Buffer A", "Buffer B", "Buffer C", "Buffer D" };
			passes.push_back( MakePass( "buffer", names[ letter ], code, in, letter ) );
		}
	}
	if( !haveImage )
	{
		err = "Pas de passe Image dans ce shader Shaderoo.";
		return false;
	}
	passes.insert( passes.begin(), MakePass( "common", "Common", common, json::array(), -1 ) );
	out = MakeShader( "shaderoo-" + b.value( "id", std::string() ), title, author, description + "\n" + b.value( "url", std::string() ), passes );
	return true;
}

//------------------------------------------------------------------ NEORT
bool ConvertNeort( const json& b, json& out, std::string& title, std::string& err )
{
	title              = b.value( "title", std::string( "NEORT" ) );
	std::string author = b.value( "author", std::string() );
	std::string frag;
	if( b.contains( "codes" ) && b[ "codes" ].is_array() )
		for( auto& c : b[ "codes" ] )
		{
			std::string t = c.value( "text", std::string() );
			if( t.find( "gl_Position" ) != std::string::npos && t.find( "gl_FragColor" ) == std::string::npos )
				continue;// vertex shader
			if( t.find( "gl_FragColor" ) != std::string::npos || t.find( "mainImage" ) != std::string::npos ||
				std::regex_search( t, std::regex( R"(out\s+vec4)" ) ) )
				frag = t;
		}
	if( frag.empty() )
	{
		err = "Pas de fragment shader GLSL dans cette œuvre NEORT (JavaScript / p5 / three.js non pris en charge).";
		return false;
	}
	std::string id = "neort-" + b.value( "id", std::string() );
	std::string desc = b.value( "url", std::string() );
	if( frag.find( "mainImage" ) != std::string::npos && frag.find( "void main" ) == std::string::npos )
	{
		out = MakeShader( id, title, author, desc, json::array( { MakePass( "image", "Image", frag, json::array(), -1 ) } ) );
		return true;
	}
	MainStyle ms;
	if( !ConvertMain( frag, ms, err ) )
		return false;
	out = MainStyleShader( ms, id, title, author, desc );
	return true;
}

//------------------------------------------------------------------ FragCoord (code natif : u_time, u_resolution, u_pass1…)
// Paquet : { passes: [ { name, type: common|main|pass, language: glsl, code } ], title, author, url, license }
bool ConvertFragCoord( const json& b, json& out, std::string& title, std::string& err, std::vector< std::string >& warnings )
{
	title              = b.value( "title", std::string( "FragCoord" ) );
	std::string author = b.value( "author", std::string() );
	if( !b.contains( "passes" ) || !b[ "passes" ].is_array() )
	{
		err = "shader FragCoord sans code";
		return false;
	}
	std::string common;
	bool haveMain = false;
	std::string mainCode;
	struct Aux
	{
		std::string name, code;
		int number = 0;
	};
	std::vector< Aux > aux;
	for( auto& p : b[ "passes" ] )
	{
		std::string type = p.value( "type", std::string() ), code = p.value( "code", std::string() ), name = p.value( "name", std::string() );
		std::string lang = ToLower( p.value( "language", std::string( "glsl" ) ) );
		if( lang != "glsl" && lang != "" )
		{
			warnings.push_back( "Passe « " + name + " » écrite en " + lang + " : non convertie." );
			continue;
		}
		if( type == "common" )
			common += code + "\n";
		else if( type == "main" )
		{
			haveMain = true;
			mainCode = code;
		}
		else if( type == "pass" )
		{
			Aux a;
			a.name = name;
			a.code = code;
			std::smatch m;
			a.number = std::regex_search( name, m, std::regex( R"((\d+)\s*$)" ) ) ? std::stoi( m[ 1 ] ) : (int)aux.size() + 1;
			aux.push_back( a );
		}
		else
			warnings.push_back( "Passe « " + name + " » de type " + type + " non prise en charge." );
	}
	if( !haveMain )
	{
		err = b.value( "source_available", true ) ? "pas de passe Main" : "le code de ce shader est caché (marketplace)";
		return false;
	}
	std::sort( aux.begin(), aux.end(), []( const Aux& a, const Aux& c ) { return a.number < c.number; } );
	if( aux.size() > 4 )
	{
		warnings.push_back( "Plus de 4 passes auxiliaires : les suivantes sont ignorées." );
		aux.resize( 4 );
	}
	std::map< std::string, int > passLetter;// u_passN -> lettre du buffer
	for( size_t i = 0; i < aux.size(); ++i )
		passLetter[ "u_pass" + std::to_string( aux[ i ].number ) ] = (int)i;

	// uniforms intégrés : déclaration retirée, remplacée par une macro
	static const std::regex reBuiltinDecl(
		R"((^|\n)[ \t]*uniform\s+(?:(?:lowp|mediump|highp)\s+)?\w+\s+(u_resolution|u_time|u_time_delta|u_frame|u_mouse|u_drag|u_scroll|u_date|u_refresh_rate|u_camera_pos|u_camera_dir|u_camera_view|u_color_gamut|u_recursion|u_max_recursion)\s*;[^\n]*)" );
	static const std::regex reOutDecl( R"((^|\n)[ \t]*(?:layout\s*\([^)]*\)\s*)?out\s+(?:(?:lowp|mediump|highp)\s+)?vec4\s+fragColor\d*\s*;[^\n]*)" );
	static const std::regex reSampler( R"((^|\n)[ \t]*uniform\s+(?:(?:lowp|mediump|highp)\s+)?sampler(2D|3D|Cube)\s+(\w+)\s*;([^\n]*))" );
	static const std::regex reCustom( R"((^|\n)[ \t]*uniform\s+(?:(?:lowp|mediump|highp)\s+)?(float|int|bool|vec2|vec3|vec4)\s+(\w+)\s*;[ \t]*//([^\n]*))" );
	std::map< std::string, std::string > customTex;// nom -> URL
	auto clean = [ & ]( std::string code ) {
		code = std::regex_replace( code, std::regex( R"((^|\n)[ \t]*#version[^\n]*)" ), "$1" );
		code = std::regex_replace( code, reBuiltinDecl, "$1" );
		code = std::regex_replace( code, reOutDecl, "$1" );
		// samplers : déclaration retirée (macro vers un iChannel), URL éventuelle mémorisée
		std::string rebuilt;
		size_t last = 0;
		for( std::sregex_iterator it( code.begin(), code.end(), reSampler ), end; it != end; ++it )
		{
			const std::smatch& m = *it;
			rebuilt += code.substr( last, m.position( 0 ) - last ) + m[ 1 ].str();
			last = m.position( 0 ) + m.length( 0 );
			std::string n = m[ 3 ], c = Trim( m[ 4 ].str() );
			if( c.rfind( "//", 0 ) == 0 )
				c = Trim( c.substr( 2 ) );
			if( m[ 2 ] != "2D" )
				warnings.push_back( "Texture « " + n + " » (sampler" + m[ 2 ].str() + ") non prise en charge." );
			else if( !passLetter.count( n ) && n != "u_main" && n != "u_audio" && n != "u_webcam" && n != "u_keyboard" )
				customTex[ n ] = c;
		}
		code = rebuilt + code.substr( last );
		// réglages : « uniform float x; //1.0, [0.0, 6.0] » -> slider Shader Loader (min max défaut)
		rebuilt.clear();
		last = 0;
		for( std::sregex_iterator it( code.begin(), code.end(), reCustom ), end; it != end; ++it )
		{
			const std::smatch& m = *it;
			rebuilt += code.substr( last, m.position( 0 ) - last ) + m[ 1 ].str();
			last = m.position( 0 ) + m.length( 0 );
			std::string type = m[ 2 ], n = m[ 3 ], c = m[ 4 ];
			std::vector< float > vals, range;
			std::string before = c, inside;
			size_t lb = c.find( '[' ), rb = c.find( ']' );
			if( lb != std::string::npos && rb != std::string::npos && rb > lb )
			{
				before = c.substr( 0, lb );
				inside = c.substr( lb + 1, rb - lb - 1 );
			}
			auto nums = []( const std::string& t, std::vector< float >& v ) {
				static const std::regex reN( R"([-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?)" );
				for( std::sregex_iterator i( t.begin(), t.end(), reN ), e; i != e; ++i )
					v.push_back( (float)atof( i->str().c_str() ) );
			};
			nums( before, vals );
			nums( inside, range );
			std::string decl = "uniform " + type + " " + n + ";";
			if( ( type == "float" || type == "int" ) && !vals.empty() )
			{
				char buf[ 128 ];
				float v = vals[ 0 ];
				float mn = range.size() >= 2 ? range[ 0 ] : ( v == 0 ? -1.0f : std::min( 0.0f, v * 2 ) );
				float mx = range.size() >= 2 ? range[ 1 ] : ( v == 0 ? 1.0f : std::max( 0.0f, v * 2 ) );
				snprintf( buf, sizeof( buf ), " // %g %g %g", mn, mx, v );
				decl += buf;
			}
			else if( type.rfind( "vec", 0 ) == 0 && !vals.empty() )
			{
				int nComp = type[ 3 ] - '0';
				std::string args;
				for( int k = 0; k < nComp; ++k )
				{
					char buf[ 32 ];
					snprintf( buf, sizeof( buf ), "%g", vals[ std::min( (size_t)k, vals.size() - 1 ) ] );
					std::string f = buf;
					if( f.find_first_of( ".e" ) == std::string::npos )
						f += ".0";
					args += ( k ? ", " : "" ) + f;
				}
				decl = "const " + type + " " + n + " = " + type + "( " + args + " );";
			}
			else if( type == "bool" && !vals.empty() )
				decl = std::string( "const bool " ) + n + " = " + ( vals[ 0 ] != 0 ? "true" : "false" ) + ";";
			rebuilt += decl;
		}
		return rebuilt + code.substr( last );
	};
	common = clean( common );
	// Common converti depuis le « golf » : fonction main() vide à retirer (elle doublerait celle des passes)
	common   = std::regex_replace( common, std::regex( R"(\bvoid\s+main\s*\(\s*\)\s*\{[^{}]*\})" ), "" );
	mainCode = clean( mainCode );
	for( auto& a : aux )
		a.code = clean( a.code );

	// u_audio de FragCoord : 512x512, x = fréquence, R = FFT pondérée, G = forme d'onde, B = FFT brute.
	// Le spectre de Resolume est au format Shadertoy (ligne 0 = FFT, ligne 1 = forme d'onde) : on adapte les lectures.
	std::vector< std::string > audioNames = { "u_audio" };
	for( auto& kv : customTex )
		if( std::regex_search( ToLower( kv.second ), std::regex( R"(\.(mp3|ogg|oga|wav|m4a|flac|aac|opus)(\?|$))" ) ) )
			audioNames.push_back( kv.first );
	bool audioWrapped = false;
	auto wrapAudio    = [ & ]( std::string& code ) {
		for( auto& n : audioNames )
		{
			std::string before = code;
			code = std::regex_replace( code, std::regex( "\\b(texture2D|texture)\\s*\\(\\s*" + n + "\\s*," ), "stfx_fcAudio( " + n + "," );
			code = std::regex_replace( code, std::regex( "\\btextureLod\\s*\\(\\s*" + n + "\\s*," ), "stfx_fcAudioLod( " + n + "," );
			code = std::regex_replace( code, std::regex( "\\btexelFetch\\s*\\(\\s*" + n + "\\s*," ), "stfx_fcAudioFetch( " + n + "," );
			audioWrapped = audioWrapped || code != before;
		}
	};
	wrapAudio( common );
	wrapAudio( mainCode );
	for( auto& a : aux )
		wrapAudio( a.code );

	auto uses = []( const std::string& code, const std::string& n ) { return std::regex_search( code, std::regex( "\\b" + n + "\\b" ) ); };
	bool mainFeedback = uses( common, "u_main" ) || uses( mainCode, "u_main" );
	for( auto& a : aux )
		mainFeedback = mainFeedback || uses( a.code, "u_main" );
	int mainLetter = -1;
	if( mainFeedback )
	{
		if( aux.size() < 4 )
			mainLetter = (int)aux.size();
		else
			warnings.push_back( "Main relit sa propre image (u_main) mais les 4 buffers sont pris : u_main sera noir." );
	}

	std::vector< std::string > samplerNames;
	for( auto& kv : passLetter )
		samplerNames.push_back( kv.first );
	for( const char* n : { "u_main", "u_audio", "u_webcam", "u_keyboard" } )
		samplerNames.push_back( n );
	for( auto& kv : customTex )
		samplerNames.push_back( kv.first );

	auto inputFor = [ & ]( const std::string& n, int ch ) -> json {
		auto pl = passLetter.find( n );
		if( pl != passLetter.end() )
			return BufferInput( ch, pl->second );
		if( n == "u_main" )
			return mainLetter >= 0 ? BufferInput( ch, mainLetter ) : json( { { "channel", ch }, { "ctype", "none" }, { "src", "none" } } );
		if( n == "u_audio" )
			return SimpleInput( ch, "music" );
		if( n == "u_webcam" )
			return SimpleInput( ch, "video" );
		if( n == "u_keyboard" )
			return SimpleInput( ch, "keyboard" );
		std::string url = customTex.count( n ) ? customTex[ n ] : std::string();
		if( std::regex_search( ToLower( url ), std::regex( R"(\.(mp3|ogg|oga|wav|m4a|flac|aac|opus)(\?|$))" ) ) )
			return SimpleInput( ch, "music" );// fichier son -> spectre audio de Resolume
		if( url.rfind( "data:image/", 0 ) == 0 )
			url = SaveDataUrl( url, "fragcoord_" + ShortHash( url ) );
		else if( url.rfind( "http", 0 ) != 0 )
		{
			if( !url.empty() )
				warnings.push_back( "Texture « " + n + " » (" + url + ") indisponible : canal noir." );
			url.clear();
		}
		if( url.empty() )
			return json( { { "channel", ch }, { "ctype", "none" }, { "src", "none" } } );
		return SimpleInput( ch, "texture", url, true );
	};

	// canaux : les samplers cités dans Common ont le même canal dans toutes les passes
	std::map< std::string, int > global;
	for( auto& n : samplerNames )
		if( uses( common, n ) )
		{
			if( global.size() >= 4 )
			{
				warnings.push_back( "Plus de 4 textures dans Common : « " + n + " » est ignorée." );
				continue;
			}
			int ch          = (int)global.size();
			global[ n ]     = ch;
		}
	std::string header = "// --- converti par Shader Loader depuis FragCoord ---\n"
						 "#define u_resolution ( iResolution.xy )\n#define u_time iTime\n#define u_time_delta iTimeDelta\n#define u_frame iFrame\n"
						 "#define u_mouse iMouse\n#define u_date iDate\n#define u_drag vec2( 0.0 )\n#define u_scroll 0.0\n#define u_refresh_rate 60.0\n"
						 "#define u_camera_pos vec3( 0.0, 0.0, 3.0 )\n#define u_camera_dir vec3( 0.0, 0.0, -1.0 )\n#define u_camera_view mat4( 1.0 )\n"
						 "#define u_color_gamut 0\n#define u_recursion 0\n#define u_max_recursion 1\n";
	for( auto& kv : global )
		header += "#define " + kv.first + " iChannel" + std::to_string( kv.second ) + "\n";
	if( audioWrapped )
		header += "vec4 stfx_fcAudio( sampler2D s, vec2 uv ) { float f = texture( s, vec2( uv.x, 0.25 ) ).x; float w = texture( s, vec2( uv.x, 0.75 ) ).x; return vec4( f, w, f, 1.0 ); }\n"
				  "vec4 stfx_fcAudio( sampler2D s, vec2 uv, float b ) { return stfx_fcAudio( s, uv ); }\n"
				  "vec4 stfx_fcAudioLod( sampler2D s, vec2 uv, float l ) { return stfx_fcAudio( s, uv ); }\n"
				  "vec4 stfx_fcAudioFetch( sampler2D s, ivec2 p, int l ) { return stfx_fcAudio( s, ( vec2( p ) + 0.5 ) / 512.0 ); }\n";

	auto makePassCode = [ & ]( const std::string& code, const std::string& passName, json& inputs ) -> std::string {
		std::map< std::string, int > local = global;
		std::set< int > usedCh;
		for( auto& kv : global )
			usedCh.insert( kv.second );
		std::string defs;
		for( auto& n : samplerNames )
		{
			if( local.count( n ) || !uses( code, n ) )
				continue;
			int ch = 0;
			while( ch < 4 && usedCh.count( ch ) )
				++ch;
			if( ch >= 4 )
			{
				warnings.push_back( "« " + passName + " » lit plus de 4 textures : « " + n + " » est ignorée." );
				defs += "#define " + n + " iChannel3\n";
				continue;
			}
			usedCh.insert( ch );
			local[ n ] = ch;
			defs += "#define " + n + " iChannel" + std::to_string( ch ) + "\n";
		}
		inputs = json::array();
		for( auto& kv : local )
			if( uses( common, kv.first ) || uses( code, kv.first ) )
				inputs.push_back( inputFor( kv.first, kv.second ) );
		if( code.find( "mainImage" ) != std::string::npos )
			return defs + code;
		static const std::regex reMain( R"(\bvoid\s+main\s*\(\s*(void)?\s*\))" );
		if( !std::regex_search( code, reMain ) )
			return defs + code;
		std::string extra;
		if( std::regex_search( code, std::regex( R"(\bfragColor[1-7]\b)" ) ) )
		{
			extra = "vec4 fragColor1, fragColor2, fragColor3, fragColor4, fragColor5, fragColor6, fragColor7;\n";
			warnings.push_back( "« " + passName + " » écrit plusieurs sorties (fragColor1…) : seule la première est gardée." );
		}
		return defs + "vec4 stfx_fragColor;\n" + extra + "#define fragColor stfx_fragColor\n#define gl_FragColor stfx_fragColor\n#line 1\n" +
			   std::regex_replace( code, reMain, "void stfx_user_main()" ) +
			   "\n#undef fragColor\n#undef gl_FragColor\nvoid mainImage( out vec4 stfx_out, in vec2 stfx_coord )\n{\n\tstfx_fragColor = vec4( 0.0, 0.0, 0.0, 1.0 );\n\tstfx_user_main();\n\tstfx_out = stfx_fragColor;\n}\n";
	};

	static const char* names[] = { "Buffer A", "Buffer B", "Buffer C", "Buffer D" };
	json passes = json::array();
	passes.push_back( MakePass( "common", "Common", header + common, json::array(), -1 ) );
	for( size_t i = 0; i < aux.size(); ++i )
	{
		json in;
		std::string code = makePassCode( aux[ i ].code, aux[ i ].name, in );
		passes.push_back( MakePass( "buffer", names[ i ], code, in, (int)i ) );
	}
	json in;
	std::string code = makePassCode( mainCode, "Main", in );
	if( mainLetter >= 0 )
	{
		passes.push_back( MakePass( "buffer", names[ mainLetter ], code, in, mainLetter ) );
		passes.push_back( MakePass( "image", "Image",
									"void mainImage( out vec4 fragColor, in vec2 fragCoord )\n{\n\tfragColor = texture( iChannel0, fragCoord / iResolution.xy );\n}\n",
									json::array( { BufferInput( 0, mainLetter ) } ), -1 ) );
	}
	else
		passes.push_back( MakePass( "image", "Image", code, in, -1 ) );
	std::string desc = b.value( "url", std::string() );
	if( b.contains( "license" ) && b[ "license" ].is_string() )
		desc += "\nLicence : " + b[ "license" ].get< std::string >();
	out = MakeShader( "fragcoord-" + b.value( "id", std::string() ), title, author, desc, passes );
	return true;
}

// Paramètre de mainImage redéclaré dans le corps (« vec2 fragCoord = gl_FragCoord.xy; », export FragCoord) :
// erreur sur les pilotes OpenGL, donc le paramètre est renommé.
std::string FixShadowImpl( const std::string& text )
{
	std::smatch sig;
	static const std::regex reSig( R"(void\s+mainImage\s*\(\s*out\s+vec4\s+(\w+)\s*,\s*in\s+vec2\s+(\w+)\s*\))" );
	if( !std::regex_search( text, sig, reSig ) )
		return text;
	std::string body = text.substr( sig.position( 0 ) + sig.length( 0 ) );
	std::string col = sig[ 1 ], coord = sig[ 2 ];
	bool rCoord = std::regex_search( body, std::regex( "\\bvec2\\s+" + coord + "\\s*[=;]" ) );
	bool rCol   = std::regex_search( body, std::regex( "\\bvec4\\s+" + col + "\\s*[=;]" ) );
	if( !rCoord && !rCol )
		return text;
	return text.substr( 0, sig.position( 0 ) ) + "void mainImage( out vec4 " + ( rCol ? std::string( "stfx_fragColorOut" ) : col ) + ", in vec2 " +
		   ( rCoord ? std::string( "stfx_fragCoordIn" ) : coord ) + " )" + body;
}

std::string SiteDirName( const std::string& site )
{
	if( site == "shaderkit" ) return "ShaderKit";
	if( site == "fragcoord" ) return "FragCoord";
	if( site == "shaderoo" ) return "Shaderoo";
	if( site == "neort" ) return "NEORT";
	return "Web";
}
}//namespace

std::string FixMainImageShadowing( const std::string& text ) { return FixShadowImpl( text ); }

bool ConvertMainStyleGlsl( const std::string& code, const std::string& title, const std::string& author, std::string& shadertoyJson,
						   std::string& err )
{
	if( code.find( "mainImage" ) != std::string::npos )
	{
		err = "déjà au format Shadertoy";
		return false;
	}
	MainStyle ms;
	if( !ConvertMain( code, ms, err ) )
		return false;
	shadertoyJson = MainStyleShader( ms, "", title, author, "" ).dump( 1, '\t' );
	return true;
}

bool ConvertBundle( const std::string& bundleJson, std::string& shadertoyJson, std::string& title, std::string& siteDir, std::string& err )
{
	json b = json::parse( bundleJson, nullptr, false );
	if( b.is_discarded() || !b.is_object() || !b.contains( "site" ) )
	{
		err = "export illisible";
		return false;
	}
	std::string site = b.value( "site", std::string() );
	siteDir          = SiteDirName( site );
	json out;
	std::vector< std::string > warnings;
	bool ok = false;
	if( site == "shaderkit" )
		ok = ConvertShaderKit( b, out, title, err );
	else if( site == "shaderoo" )
		ok = ConvertShaderoo( b, out, title, err, warnings );
	else if( site == "neort" )
		ok = ConvertNeort( b, out, title, err );
	else if( site == "fragcoord" )
		ok = ConvertFragCoord( b, out, title, err, warnings );
	else
		err = "site inconnu : " + site;
	if( !ok )
		return false;
	if( !warnings.empty() )
	{
		std::string w;
		for( auto& s : warnings )
			w += "// Shader Loader : " + s + "\n";
		for( auto& p : out[ "renderpass" ] )
			if( p.value( "type", std::string() ) == "image" )
				p[ "code" ] = w + p[ "code" ].get< std::string >();
	}
	shadertoyJson = out.dump( 1, '\t' );
	return true;
}

bool SaveImported( const std::string& site, const std::string& id, const std::string& title, const std::string& siteDir,
				   const std::string& content, const std::string& ext, std::string& savedPath, std::string& err )
{
	std::string dir = JoinPath( RootDir(), siteDir.empty() ? SiteDirName( site ) : siteDir );
	MakeDirs( dir );
	savedPath = JoinPath( dir, SanitizeFileName( title.empty() ? id : title ) + " [" + site + "-" + id + "]" + ext );
	if( !WriteFileText( savedPath, content ) )
	{
		err = "écriture impossible : " + savedPath;
		return false;
	}
	return true;
}

std::vector< std::string > FindDownloadedExports()
{
	std::vector< std::string > out;
	std::string dir = DownloadsDir();
	if( dir.empty() )
		return out;
	static const std::regex re( R"(\[(shaderkit|fragcoord|shaderoo|neort)-[A-Za-z0-9_\-]+\]( \(\d+\))?\.(json|glsl)$)", std::regex::icase );
	std::error_code ec;
	for( auto& e : fs::directory_iterator( P( dir ), ec ) )
	{
		if( !e.is_regular_file( ec ) )
			continue;
		std::string name = Narrow( e.path().filename().wstring() );
		if( std::regex_search( name, re ) )
			out.push_back( Narrow( e.path().wstring() ) );
	}
	return out;
}

bool ImportDownloadedExport( const std::string& file, std::string& savedPath, std::string& title, std::string& err )
{
	std::string text;
	if( !ReadFileText( file, text ) || text.empty() )
	{
		err = "lecture impossible";
		return false;
	}
	std::string stem = std::regex_replace( FileStem( file ), std::regex( R"( \(\d+\)$)" ), "" );
	std::smatch m;
	std::string site, id;
	if( std::regex_search( stem, m, std::regex( R"(\[(\w+)-([A-Za-z0-9_\-]+)\]$)" ) ) )
	{
		site = ToLower( m[ 1 ] );
		id   = m[ 2 ];
	}
	title = Trim( std::regex_replace( stem, std::regex( R"(\s*\[[^\]]*\]$)" ), "" ) );
	std::string out, siteDir = SiteDirName( site ), ext = ".json";
	if( FileExtLower( file ) == ".glsl" )
	{
		// FragCoord : export « ShaderToy » (mainImage) ; u_audio -> spectre de Resolume
		if( text.find( "mainImage" ) != std::string::npos )
		{
			std::string audio = "uniform sampler2D u_audio;";
			size_t a          = text.find( audio );
			if( a != std::string::npos )
			{
				size_t eol = text.find( '\n', a );
				text.replace( a, ( eol == std::string::npos ? text.size() : eol ) - a, "// @iChannel1 audio\n#define u_audio iChannel1" );
			}
			text = FixShadowImpl( text );
			out = text;
			ext = ".glsl";
		}
		else if( !ConvertMainStyleGlsl( text, title, "", out, err ) )
			return false;
	}
	else
	{
		json b = json::parse( text, nullptr, false );
		if( b.is_discarded() )
		{
			err = "JSON incomplet";// fichier encore en cours d'écriture
			return false;
		}
		std::string t2;
		if( !ConvertBundle( text, out, t2, siteDir, err ) )
			return false;
		if( !t2.empty() )
			title = t2;
	}
	if( !SaveImported( site, id, title, siteDir, out, ext, savedPath, err ) )
		return false;
	std::error_code ec;
	fs::remove( P( file ), ec );
	return true;
}

const char* UniversalBookmarkletCode()
{
	return R"JS(javascript:(async()=>{ const say=m=>alert('Shader Loader : '+m); const clean=s=>(s||'').replace(/\s+/g,' ').replace(/[\\/:*?"<>|]/g,'_').trim().slice(0,80); const save=(name,blob)=>{const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;document.body.appendChild(a);a.click();a.remove();}; const saveJson=(t,site,id,o)=>save(clean(t||id)+' ['+site+'-'+id+'].json',new Blob([JSON.stringify(o)],{type:'application/json'})); const toData=b=>new Promise(r=>{const f=new FileReader();f.onload=()=>r(f.result);f.readAsDataURL(b);}); const sleep=ms=>new Promise(r=>setTimeout(r,ms)); const h=location.hostname.replace(/^www\./,''),q=new URLSearchParams(location.search); try{ if(/shadertoy\.com$/.test(h)){ const m=location.pathname.match(/\/(?:view|embed)\/(\w{6})/);if(!m)return say('ouvre d\'abord un shader (shadertoy.com/view/...)'); const r=await fetch('/shadertoy',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'s='+encodeURIComponent(JSON.stringify({shaders:[m[1]]}))+'&nt=1&nl=1&np=1'}); const j=await r.json(),s=Array.isArray(j)?j[0]:j;if(!s||!s.renderpass)throw new Error('réponse inattendue'); save(clean((s.info&&s.info.name)||m[1])+' ['+m[1]+'].json',new Blob([JSON.stringify(s)],{type:'application/json'})); }else if(/shadered\.org$/.test(h)){ const id=q.get('s')||q.get('id');if(!id)return say('ouvre d\'abord un shader (shadered.org/view?s=...)'); const r=await fetch('/download?type=shader&id='+encodeURIComponent(id));if(!r.ok)throw new Error('HTTP '+r.status); const b=await r.blob(),u=new Uint8Array(await b.slice(0,2).arrayBuffer());if(u[0]!==80||u[1]!==75)throw new Error('pas d\'archive'); const t=(document.title||'').replace(/SHADERed/gi,'').replace(/^[\s\-|:]+|[\s\-|:]+$/g,''); save(clean(t||id)+' [shadered-'+id+'].zip',b); }else if(/shaderkit\.com$/.test(h)){ const m=location.pathname.match(/\/s\/([\w-]+)/),btn=document.getElementById('exportJson');if(!m||!btn)return say('ouvre d\'abord un shader (shaderkit.com/s/...)'); let cap=null;const o=HTMLAnchorElement.prototype.click;HTMLAnchorElement.prototype.click=function(){if(/^shader_state/.test(this.download||'')){cap=this.href;return;}return o.call(this);}; try{btn.click();}finally{HTMLAnchorElement.prototype.click=o;} if(!cap)throw new Error('export JSON indisponible'); const st=JSON.parse(decodeURIComponent(cap.slice(cap.indexOf(',')+1)));delete st.thumbnailUrl; saveJson(st.title,'shaderkit',m[1],{stfx:1,site:'shaderkit',id:m[1],url:location.href,data:st}); }else if(/fragcoord\.xyz$/.test(h)){ const m=location.pathname.match(/\/s\/([\w-]+)/);if(!m)return say('ouvre d\'abord un shader (fragcoord.xyz/s/...)'); let n=1;const mcp=async(name,args)=>{const r=await fetch('/mcp',{method:'POST',headers:{'Content-Type':'application/json','Accept':'application/json, text/event-stream'},body:JSON.stringify({jsonrpc:'2.0',id:n++,method:'tools/call',params:{name,arguments:args}})});let t=await r.text();if(t[0]!=='{'){const d=t.match(/data:\s*(\{.*\})/);t=d?d[1]:t;}const j=JSON.parse(t);if(!j.result||j.result.isError)throw new Error((j.result&&j.result.content&&j.result.content[0].text)||(j.error&&j.error.message)||'erreur FragCoord');return j.result.structuredContent;}; const sh=await mcp('get_shader',{slug:m[1],include_source:true}); if(sh.source_available===false)return say('le code de ce shader est caché (marketplace)'); for(const p of sh.passes||[]){const l=(p.language||'glsl').toLowerCase();if(l!=='glsl'){try{const c=await mcp('convert_shader',{source:p.code,from:l,to:'glsl'});if(c&&c.output){p.code=c.output;p.language='glsl';}}catch(e){}}} const lic=sh.license&&typeof sh.license==='object'?(sh.license.name||sh.license.spdx):sh.license; saveJson(sh.title,'fragcoord',m[1],{stfx:1,site:'fragcoord',id:m[1],url:location.origin+'/s/'+m[1],title:sh.title,author:sh.author,license:lic||null,source_available:true,passes:sh.passes}); }else if(/shaderoo\.org$/.test(h)){ const id=q.get('shader')||(document.getElementById('shaderIDInput')||{}).value;if(!id)return say('ouvre d\'abord un shader (shaderoo.org/?shader=...)'); const raw=await (await fetch('/index.php?cmd=loaddb&id='+encodeURIComponent(id))).text(); const ta=document.createElement('textarea');ta.innerHTML=raw;let sc=ta.value; sc=sc.replace(/<and\/>/g,'&').replace(/<plus\/>/g,'+').replace(/<modulo\/>/g,'%').replace(/<br\/>/g,'\n').replace(/<backslash\/>/g,'\\'); const images={},head=sc.split('###BEGIN:')[0]; for(const line of head.split('\n')){if(/^#/.test(line))continue;const w=line.trim().split(/\s+/);for(const n of w.slice(1)){if(/\.glsl$|^opt:|^local:/.test(n)||images[n]!==undefined||!/^\w+$/.test(n))continue; images[n]=null;try{const r=await fetch('/textures/'+n+'.png');if(r.ok&&/image/.test(r.headers.get('content-type')||''))images[n]=await toData(await r.blob());else{const f=await fetch('/textures/'+n+'.float');if(f.ok){const bl=await f.blob();if(bl.size>100)images[n]=await toData(new Blob([bl],{type:'application/octet-stream'}));}}}catch(e){}}} const t=(sc.match(/###TITLE:(.*)/)||[])[1]||id; saveJson(t,'shaderoo',id,{stfx:1,site:'shaderoo',id,url:location.href,scene:sc,images}); }else if(/neort\.io$/.test(h)){ const m=location.pathname.match(/\/art\/(\w+)/);if(!m)return say('ouvre d\'abord une œuvre (neort.io/art/...)'); const raw=await (await fetch(location.pathname,{headers:{RSC:'1'}})).text(); const k=raw.indexOf('"codes":[');if(k<0)throw new Error('code introuvable'); let i=k+8,d=0,s=false,e=false;for(;i<raw.length;i++){const c=raw[i];if(s){if(e)e=false;else if(c==='\\')e=true;else if(c==='"')s=false;continue;}if(c==='"')s=true;else if(c==='[')d++;else if(c===']'&&--d===0)break;} const codes=JSON.parse(raw.slice(k+8,i+1)),enc=new TextEncoder(),dec=new TextDecoder(); for(const c of codes){if(typeof c.text==='string'&&/^\$[0-9a-f]+$/i.test(c.text)){const ref=c.text.slice(1),p=raw.search(new RegExp('(^|\\n)'+ref+':T([0-9a-f]+),'));if(p<0)continue;const mm=raw.slice(p).match(/:T([0-9a-f]+),/);const st=raw.indexOf(mm[0],p)+mm[0].length;c.text=dec.decode(enc.encode(raw.slice(st,st+parseInt(mm[1],16))).slice(0,parseInt(mm[1],16)));}} if(!codes.some(c=>/gl_FragColor|mainImage|out\s+vec4|void\s+main/.test(c.text||'')))return say('cette œuvre n\'est pas un shader GLSL (JavaScript / p5 / three.js non pris en charge)'); const tm=raw.match(new RegExp('"id":"'+m[1]+'","title":("(?:[^"\\\\]|\\\\.)*")'));const t=tm?JSON.parse(tm[1]):document.title; const um=raw.match(/"user":\{"id":"[^"]*","customId":"([^"]*)"/); saveJson(t,'neort',m[1],{stfx:1,site:'neort',id:m[1],url:location.href,title:t,author:um?um[1]:'',codes}); }else say('site non pris en charge. Sites : shadertoy.com, shadered.org, shaderkit.com, fragcoord.xyz, shaderoo.org, neort.io'); }catch(e){say('export impossible ('+e.message+'). Copie le code dans l\'onglet FICHIER.');} })();)JS";
}
}//namespace sites
}//namespace stfx
