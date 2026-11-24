#include "Sites.h"
#include "Util.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

// data:image/png;base64,... -> fichier du cache média ; retourne la source "/media/a/<nom>"
std::string SaveDataUrl( const std::string& dataUrl, const std::string& baseName )
{
	size_t comma = dataUrl.find( ',' );
	if( dataUrl.rfind( "data:", 0 ) != 0 || comma == std::string::npos || dataUrl.find( ";base64" ) > comma )
		return {};
	std::string ext = ".png";
	if( dataUrl.find( "image/jpeg" ) != std::string::npos )
		ext = ".jpg";
	else if( dataUrl.find( "image/gif" ) != std::string::npos )
		ext = ".gif";
	else if( dataUrl.find( "image/webp" ) != std::string::npos )
		ext = ".webp";
	static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::vector< uint8_t > out;
	int val = 0, bits = -8;
	for( size_t i = comma + 1; i < dataUrl.size(); ++i )
	{
		size_t c = b64.find( dataUrl[ i ] );
		if( c == std::string::npos )
			continue;
		val = ( val << 6 ) + (int)c;
		bits += 6;
		if( bits >= 0 )
		{
			out.push_back( uint8_t( ( val >> bits ) & 0xFF ) );
			bits -= 8;
		}
	}
	if( out.empty() )
		return {};
	std::string name = SanitizeFileName( baseName ) + ext;
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
			else if( type == "sampler2D" && ( l == "u_audio" || l == "audio" || l == "u_sound" || l == "sound" || l == "u_fft" || l == "fft" ) )
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
	std::map< std::string, std::string > savedTex;
	auto texSrc = [ & ]( const std::string& tok ) -> std::string {
		auto it = savedTex.find( tok );
		if( it != savedTex.end() )
			return it->second;
		std::string src;
		if( images.contains( tok ) && images[ tok ].is_string() )
			src = SaveDataUrl( images[ tok ].get< std::string >(), "shaderoo_" + ( std::regex_match( tok, std::regex( R"(\w+)" ) ) ? tok : ShortHash( tok ) ) );
		savedTex[ tok ] = src;
		return src;
	};

	auto bufferLetter = []( const std::string& n ) -> int {
		std::smatch mm;
		if( std::regex_match( n, mm, std::regex( R"(Buff_([A-D])(?::0)?)" ) ) )
			return mm[ 1 ].str()[ 0 ] - 'A';
		return -1;
	};

	std::string common =
		"// --- uniforms propres à Shaderoo (sans équivalent dans Resolume) ---\n"
		"#define iMouseData vec4(0.0)\n#define iMouseMove vec4(0.0)\n#define iPassIndex 0\n#define iNumPasses 1\n"
		"#define iNumTriangles 0\n#define iCaptureGeom 0\n#define SHADEROO\n#define SHADEROO_FRAGMENT_SHADER\n"
		"vec4 FragColor1, FragColor2, FragColor3;\n";

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
			else if( std::regex_search( lt, std::regex( R"(\.(webm|mp4|ogv|mov)(\?|$))" ) ) )
				in.push_back( SimpleInput( (int)i, "video" ) );
			else
			{
				std::string src = texSrc( t );
				if( !src.empty() )
					in.push_back( SimpleInput( (int)i, "texture", src, true ) );
				else if( t.rfind( "Geom_", 0 ) != 0 && t.rfind( "src:", 0 ) != 0 && t.rfind( "Ref:", 0 ) != 0 )
					warnings.push_back( "Texture « " + t + " » indisponible : canal " + std::to_string( i ) + " noir." );
				if( src.empty() )
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

std::string SiteDirName( const std::string& site )
{
	if( site == "shaderkit" ) return "ShaderKit";
	if( site == "fragcoord" ) return "FragCoord";
	if( site == "shaderoo" ) return "Shaderoo";
	if( site == "neort" ) return "NEORT";
	return "Web";
}
}//namespace

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
	std::string dir = JoinPath( RootDir(), siteDir );
	MakeDirs( dir );
	savedPath = JoinPath( dir, SanitizeFileName( title.empty() ? id : title ) + " [" + site + "-" + id + "]" + ext );
	if( !WriteFileText( savedPath, out ) )
	{
		err = "écriture impossible : " + savedPath;
		return false;
	}
	std::error_code ec;
	fs::remove( P( file ), ec );
	return true;
}

const char* UniversalBookmarkletCode()
{
	return R"JS(javascript:(async()=>{ const say=m=>alert('Shader Loader : '+m); const clean=s=>(s||'').replace(/\s+/g,' ').replace(/[\\/:*?"<>|]/g,'_').trim().slice(0,80); const save=(name,blob)=>{const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download=name;document.body.appendChild(a);a.click();a.remove();}; const saveJson=(t,site,id,o)=>save(clean(t||id)+' ['+site+'-'+id+'].json',new Blob([JSON.stringify(o)],{type:'application/json'})); const toData=b=>new Promise(r=>{const f=new FileReader();f.onload=()=>r(f.result);f.readAsDataURL(b);}); const sleep=ms=>new Promise(r=>setTimeout(r,ms)); const h=location.hostname.replace(/^www\./,''),q=new URLSearchParams(location.search); try{ if(/shadertoy\.com$/.test(h)){ const m=location.pathname.match(/\/(?:view|embed)\/(\w{6})/);if(!m)return say('ouvre d\'abord un shader (shadertoy.com/view/...)'); const r=await fetch('/shadertoy',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'s='+encodeURIComponent(JSON.stringify({shaders:[m[1]]}))+'&nt=1&nl=1&np=1'}); const j=await r.json(),s=Array.isArray(j)?j[0]:j;if(!s||!s.renderpass)throw new Error('réponse inattendue'); save(clean((s.info&&s.info.name)||m[1])+' ['+m[1]+'].json',new Blob([JSON.stringify(s)],{type:'application/json'})); }else if(/shadered\.org$/.test(h)){ const id=q.get('s')||q.get('id');if(!id)return say('ouvre d\'abord un shader (shadered.org/view?s=...)'); const r=await fetch('/download?type=shader&id='+encodeURIComponent(id));if(!r.ok)throw new Error('HTTP '+r.status); const b=await r.blob(),u=new Uint8Array(await b.slice(0,2).arrayBuffer());if(u[0]!==80||u[1]!==75)throw new Error('pas d\'archive'); const t=(document.title||'').replace(/SHADERed/gi,'').replace(/^[\s\-|:]+|[\s\-|:]+$/g,''); save(clean(t||id)+' [shadered-'+id+'].zip',b); }else if(/shaderkit\.com$/.test(h)){ const m=location.pathname.match(/\/s\/([\w-]+)/),btn=document.getElementById('exportJson');if(!m||!btn)return say('ouvre d\'abord un shader (shaderkit.com/s/...)'); let cap=null;const o=HTMLAnchorElement.prototype.click;HTMLAnchorElement.prototype.click=function(){if(/^shader_state/.test(this.download||'')){cap=this.href;return;}return o.call(this);}; try{btn.click();}finally{HTMLAnchorElement.prototype.click=o;} if(!cap)throw new Error('export JSON indisponible'); const st=JSON.parse(decodeURIComponent(cap.slice(cap.indexOf(',')+1)));delete st.thumbnailUrl; saveJson(st.title,'shaderkit',m[1],{stfx:1,site:'shaderkit',id:m[1],url:location.href,data:st}); }else if(/fragcoord\.xyz$/.test(h)){ const m=location.pathname.match(/\/s\/([\w-]+)/);if(!m)return say('ouvre d\'abord un shader (fragcoord.xyz/s/...)'); const eb=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()==='Export');if(!eb)throw new Error('bouton Export introuvable'); eb.click();let sel=null;for(let i=0;i<30&&!sel;i++){await sleep(100);sel=[...document.querySelectorAll('select')].find(s=>[...s.options].some(o=>o.value==='shadertoy'));} if(!sel)throw new Error('menu d\'export introuvable'); Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype,'value').set.call(sel,'shadertoy');sel.dispatchEvent(new Event('change',{bubbles:true})); let code='',box=sel.closest('[role=dialog]')||sel.parentElement.parentElement.parentElement; for(let i=0;i<30&&!/mainImage|void\s+main/.test(code);i++){await sleep(100);const e=[...box.querySelectorAll('textarea,pre,code')].sort((a,b)=>(b.value||b.innerText).length-(a.value||a.innerText).length)[0];code=e?(e.value||e.innerText):'';} document.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true})); if(!code)throw new Error('code introuvable'); const t=document.title.replace(/\s*[—|-]\s*FragCoord.*$/i,''); const au=(document.querySelector('a[href^="/u/"]')||{}).innerText||''; save(clean(t||m[1])+' [fragcoord-'+m[1]+'].glsl',new Blob(['// '+t+' par '+au+' - '+location.href+'\n'+code],{type:'text/plain'})); }else if(/shaderoo\.org$/.test(h)){ const id=q.get('shader')||(document.getElementById('shaderIDInput')||{}).value;if(!id)return say('ouvre d\'abord un shader (shaderoo.org/?shader=...)'); const raw=await (await fetch('/index.php?cmd=loaddb&id='+encodeURIComponent(id))).text(); const ta=document.createElement('textarea');ta.innerHTML=raw;let sc=ta.value; sc=sc.replace(/<and\/>/g,'&').replace(/<plus\/>/g,'+').replace(/<modulo\/>/g,'%').replace(/<br\/>/g,'\n').replace(/<backslash\/>/g,'\\'); const images={},head=sc.split('###BEGIN:')[0]; for(const line of head.split('\n')){if(/^#/.test(line))continue;const w=line.trim().split(/\s+/);for(const n of w.slice(1)){if(/\.glsl$|^opt:|^local:/.test(n)||images[n]!==undefined||!/^\w+$/.test(n))continue; images[n]=null;try{const r=await fetch('/textures/'+n+'.png');if(r.ok&&/image/.test(r.headers.get('content-type')||''))images[n]=await toData(await r.blob());}catch(e){}}} const t=(sc.match(/###TITLE:(.*)/)||[])[1]||id; saveJson(t,'shaderoo',id,{stfx:1,site:'shaderoo',id,url:location.href,scene:sc,images}); }else if(/neort\.io$/.test(h)){ const m=location.pathname.match(/\/art\/(\w+)/);if(!m)return say('ouvre d\'abord une œuvre (neort.io/art/...)'); const raw=await (await fetch(location.pathname,{headers:{RSC:'1'}})).text(); const k=raw.indexOf('"codes":[');if(k<0)throw new Error('code introuvable'); let i=k+8,d=0,s=false,e=false;for(;i<raw.length;i++){const c=raw[i];if(s){if(e)e=false;else if(c==='\\')e=true;else if(c==='"')s=false;continue;}if(c==='"')s=true;else if(c==='[')d++;else if(c===']'&&--d===0)break;} const codes=JSON.parse(raw.slice(k+8,i+1)),enc=new TextEncoder(),dec=new TextDecoder(); for(const c of codes){if(typeof c.text==='string'&&/^\$[0-9a-f]+$/i.test(c.text)){const ref=c.text.slice(1),p=raw.search(new RegExp('(^|\\n)'+ref+':T([0-9a-f]+),'));if(p<0)continue;const mm=raw.slice(p).match(/:T([0-9a-f]+),/);const st=raw.indexOf(mm[0],p)+mm[0].length;c.text=dec.decode(enc.encode(raw.slice(st,st+parseInt(mm[1],16))).slice(0,parseInt(mm[1],16)));}} if(!codes.some(c=>/gl_FragColor|mainImage|out\s+vec4|void\s+main/.test(c.text||'')))return say('cette œuvre n\'est pas un shader GLSL (JavaScript / p5 / three.js non pris en charge)'); const tm=raw.match(new RegExp('"id":"'+m[1]+'","title":("(?:[^"\\\\]|\\\\.)*")'));const t=tm?JSON.parse(tm[1]):document.title; const um=raw.match(/"user":\{"id":"[^"]*","customId":"([^"]*)"/); saveJson(t,'neort',m[1],{stfx:1,site:'neort',id:m[1],url:location.href,title:t,author:um?um[1]:'',codes}); }else say('site non pris en charge. Sites : shadertoy.com, shadered.org, shaderkit.com, fragcoord.xyz, shaderoo.org, neort.io'); }catch(e){say('export impossible ('+e.message+'). Copie le code dans l\'onglet FICHIER.');} })();)JS";
}
}//namespace sites
}//namespace stfx
