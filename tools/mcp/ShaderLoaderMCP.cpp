// Shader Loader - serveur MCP pour Claude Desktop (stdio, JSON-RPC 2.0).
// Lancé par Claude Desktop (extension .mcpb). Relaie les outils vers les instances du plugin dans Resolume
// par les canaux nommés locaux \\.\pipe\ShaderLoaderBridge-<pid>-<effect|source>-<module>.
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
// test hors Windows : pas de canal nommé (protocole MCP seulement)
#endif

using json = nlohmann::json;

namespace
{
const char* kVersion = "1.25.0";

const char* kInstructions = R"(Shader Loader runs Shadertoy-format GLSL shaders as effects and sources inside Resolume Arena (VJ software). These tools read and edit the shader loaded in a Shader Loader instance; changes compile and appear live in Resolume.

Workflow to improve or change an effect: get_shader -> write the complete new code of the passes you change -> update_shader (it compiles first; on errors, fix and call again) -> capture_output to look at the result -> save_shader only when the user is happy (or asks to keep it). revert_shader restores the saved file.

Shader environment:
- Desktop OpenGL, GLSL 4.10 core. Shadertoy uniforms: iResolution (vec3), iTime, iTimeDelta, iFrame (int), iMouse (vec4), iDate, iSampleRate, iChannel0..iChannel3, iChannelResolution[4].
- Every pass defines void mainImage( out vec4 fragColor, in vec2 fragCoord ). Common code is prepended to every pass. Buffer passes render to float textures; a pass reading its own buffer gets the previous frame.
- Passes and iChannel bindings are fixed (get_shader lists them): do not add, remove or rename passes, and do not change what a channel means. "audio" = Resolume's live FFT in the Shadertoy layout (512x2: y=0.25 spectrum, y=0.75 waveform). "input" = the video under the effect.
- Adjustable controls: a top-level `#define NAME 1.0`, `const float NAME = 1.0;`, `const vec3 NAME = vec3(...)` or `uniform float NAME; // min max default` becomes a slider in Resolume (24 at most). Expose the important new values this way with clear UPPERCASE names.
- Real time at 1920x1080 / 60 fps on a mid-range GPU: bounded loops, no huge iteration counts.
- Portable GLSL: initialize every local variable, no implicit int/float conversion (1.0 not 1), declare functions before use, no recursion.
The user usually writes in French: answer in French.)";

#ifdef _WIN32
std::vector< std::wstring > ListPipes()
{
	std::vector< std::wstring > out;
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW( L"\\\\.\\pipe\\*", &fd );
	if( h == INVALID_HANDLE_VALUE )
		return out;
	do
	{
		std::wstring n = fd.cFileName;
		if( n.rfind( L"ShaderLoaderBridge-", 0 ) == 0 )
			out.push_back( L"\\\\.\\pipe\\" + n );
	} while( FindNextFileW( h, &fd ) );
	FindClose( h );
	std::sort( out.begin(), out.end() );
	return out;
}

char KindLetter( const std::wstring& pipe ) { return pipe.find( L"-source" ) != std::wstring::npos ? 'S' : 'E'; }

bool Ask( const std::wstring& pipe, const json& req, json& res, std::string& err )
{
	HANDLE h = INVALID_HANDLE_VALUE;
	for( int i = 0; i < 20; ++i )
	{
		h = CreateFileW( pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr );
		if( h != INVALID_HANDLE_VALUE )
			break;
		if( GetLastError() != ERROR_PIPE_BUSY )
			break;
		WaitNamedPipeW( pipe.c_str(), 500 );
	}
	if( h == INVALID_HANDLE_VALUE )
	{
		err = "Shader Loader ne répond pas (Resolume fermé ?)";
		return false;
	}
	std::string out = req.dump() + "\n";
	DWORD n         = 0;
	WriteFile( h, out.data(), (DWORD)out.size(), &n, nullptr );
	std::string in;
	char buf[ 65536 ];
	while( ReadFile( h, buf, sizeof( buf ), &n, nullptr ) && n > 0 )
	{
		in.append( buf, n );
		if( !in.empty() && in.back() == '\n' )
			break;
	}
	CloseHandle( h );
	res = json::parse( in, nullptr, false );
	if( !res.is_object() )
	{
		err = "réponse illisible de Shader Loader";
		return false;
	}
	return true;
}

#else
std::vector< std::wstring > ListPipes() { return {}; }
char KindLetter( const std::wstring& ) { return 'E'; }
bool Ask( const std::wstring&, const json&, json&, std::string& err )
{
	err = "pas de canal nommé hors Windows";
	return false;
}
#endif

// plusieurs DLL du même type (Shader Loader + plugins exportés) : suffixe "-2", "-3"… pour les suivantes
std::string PipeSuffix( const std::vector< std::wstring >& pipes, const std::wstring& p )
{
	int ord = 0;
	for( auto& q : pipes )
	{
		if( KindLetter( q ) != KindLetter( p ) )
			continue;
		++ord;
		if( q == p )
			break;
	}
	return ord > 1 ? "-" + std::to_string( ord ) : "";
}

struct Target
{
	std::wstring pipe;
	int id = -1;// -1 : laisser le plugin choisir (instance ciblée par le panneau, ou la seule)
};

// "E1", "S2", "1" ou vide
bool Resolve( const json& args, Target& t, std::string& err )
{
	auto pipes = ListPipes();
	if( pipes.empty() )
	{
		err = "Aucune instance Shader Loader : ouvre Resolume et ajoute l'effet Shader Loader (ou la source Shader Loader Src).";
		return false;
	}
	std::string inst = args.value( "instance", std::string() );
	if( !inst.empty() )
	{
		// "E1", "S2", "E1-2" (2e DLL d'effet), "1"
		char kind = 0;
		if( inst[ 0 ] == 'E' || inst[ 0 ] == 'e' || inst[ 0 ] == 'S' || inst[ 0 ] == 's' )
		{
			kind = (char)toupper( inst[ 0 ] );
			inst = inst.substr( 1 );
		}
		std::string suffix;
		size_t dash = inst.find( '-' );
		if( dash != std::string::npos )
		{
			suffix = inst.substr( dash );
			inst   = inst.substr( 0, dash );
		}
		int id = atoi( inst.c_str() );
		for( auto& p : pipes )
			if( ( !kind || KindLetter( p ) == kind ) && PipeSuffix( pipes, p ) == suffix )
			{
				t.pipe = p;
				t.id   = id;
				return true;
			}
		err = "instance inconnue : " + args.value( "instance", std::string() ) + " (voir list_instances)";
		return false;
	}
	if( pipes.size() == 1 )
	{
		t.pipe = pipes[ 0 ];
		return true;
	}
	// plusieurs DLL (effet + source) : instance ciblée dans le panneau, sinon la seule instance
	std::vector< std::pair< std::wstring, int > > all;
	for( auto& p : pipes )
	{
		json res;
		std::string e;
		if( !Ask( p, { { "cmd", "list" } }, res, e ) )
			continue;
		for( auto& i : res.value( "instances", json::array() ) )
		{
			if( i.value( "focused_in_panel", false ) )
			{
				t.pipe = p;
				t.id   = i.value( "id", -1 );
				return true;
			}
			all.push_back( { p, i.value( "id", -1 ) } );
		}
	}
	if( all.size() == 1 )
	{
		t.pipe = all[ 0 ].first;
		t.id   = all[ 0 ].second;
		return true;
	}
	err = all.empty() ? "Aucune instance Shader Loader active dans Resolume." : "Plusieurs instances : appelle list_instances puis précise \"instance\".";
	return false;
}

json TextResult( const std::string& text, bool isError = false )
{
	json r = { { "content", json::array( { { { "type", "text" }, { "text", text } } } ) } };
	if( isError )
		r[ "isError" ] = true;
	return r;
}

json CallBridge( const json& args, json req )
{
	Target t;
	std::string err;
	if( !Resolve( args, t, err ) )
		return TextResult( err, true );
	if( t.id >= 0 )
		req[ "id" ] = t.id;
	json res;
	if( !Ask( t.pipe, req, res, err ) )
		return TextResult( err, true );
	return res;
}

const json& Tools()
{
	static const json instanceProp = { { "type", "string" },
									   { "description", "Instance to use, e.g. \"E1\" (effect) or \"S1\" (source), from list_instances. Omit to use the "
														"instance selected in the Shader Loader panel, or the only one." } };
	static const json tools        = json::array(
        { { { "name", "list_instances" },
            { "description", "List the Shader Loader instances running in Resolume (effects \"E…\" and sources \"S…\") with the loaded shader and its compile status." },
            { "inputSchema", { { "type", "object" }, { "properties", json::object() } } } },
          { { "name", "get_shader" },
            { "description", "Get the shader loaded in an instance: title, file, Common code, each pass with its code and what every iChannel is connected to, "
                             "the Resolume sliders, and the current compile status/errors." },
            { "inputSchema", { { "type", "object" }, { "properties", { { "instance", instanceProp } } } } } },
          { { "name", "update_shader" },
            { "description", "Replace the code of one or more passes (and optionally Common), compile it, and apply it live in Resolume if it compiles. "
                             "Send the COMPLETE code of each pass you change (not a diff); pass names must match get_shader. Returns compile errors to fix "
                             "if it does not compile (nothing is applied then). Not saved to disk until save_shader." },
            { "inputSchema",
              { { "type", "object" },
                { "properties",
                  { { "instance", instanceProp },
                    { "common", { { "type", "string" }, { "description", "Complete new Common code (omit to keep it)." } } },
                    { "passes",
                      { { "type", "array" },
                        { "items",
                          { { "type", "object" },
                            { "properties", { { "name", { { "type", "string" } } }, { "code", { { "type", "string" } } } } },
                            { "required", json::array( { "name", "code" } ) } } } } } } },
                { "required", json::array( { "passes" } ) } } } },
          { { "name", "capture_output" },
            { "description", "Take a picture of what the instance currently renders in Resolume (PNG, up to 640 px wide), to check the result visually. "
                             "The clip/layer must be playing." },
            { "inputSchema", { { "type", "object" }, { "properties", { { "instance", instanceProp } } } } } },
          { { "name", "save_shader" },
            { "description", "Save the shader currently applied in the instance to its file in the Shader Loader library (or to the Local folder for shaders "
                             "outside the library). Give new_name to save it as a new shader instead of overwriting." },
            { "inputSchema",
              { { "type", "object" },
                { "properties", { { "instance", instanceProp }, { "new_name", { { "type", "string" }, { "description", "Optional name for a new file." } } } } } } } },
          { { "name", "revert_shader" },
            { "description", "Reload the shader from its saved file, discarding unsaved changes." },
            { "inputSchema", { { "type", "object" }, { "properties", { { "instance", instanceProp } } } } } } } );
	return tools;
}

json CallTool( const std::string& name, const json& args )
{
	if( name == "list_instances" )
	{
		auto pipes = ListPipes();
		json all   = json::array();
		for( auto& p : pipes )
		{
			json res;
			std::string e;
			if( !Ask( p, { { "cmd", "list" } }, res, e ) )
				continue;
			for( auto i : res.value( "instances", json::array() ) )
			{
				i[ "instance" ] = std::string( 1, KindLetter( p ) ) + std::to_string( i.value( "id", 0 ) ) + PipeSuffix( pipes, p );
				i.erase( "id" );
				all.push_back( i );
			}
		}
		if( all.empty() )
			return TextResult( "Aucune instance Shader Loader : ouvre Resolume et ajoute l'effet Shader Loader (ou la source Shader Loader Src)." );
		return TextResult( all.dump( 2 ) );
	}
	json res;
	if( name == "get_shader" )
		res = CallBridge( args, { { "cmd", "get" } } );
	else if( name == "update_shader" )
	{
		json req = { { "cmd", "update" }, { "passes", args.value( "passes", json::array() ) } };
		if( args.contains( "common" ) )
			req[ "common" ] = args[ "common" ];
		res = CallBridge( args, req );
	}
	else if( name == "save_shader" )
		res = CallBridge( args, { { "cmd", "save" }, { "new_name", args.value( "new_name", std::string() ) } } );
	else if( name == "revert_shader" )
		res = CallBridge( args, { { "cmd", "revert" } } );
	else if( name == "capture_output" )
	{
		res = CallBridge( args, { { "cmd", "capture" } } );
		if( res.value( "ok", false ) && res.contains( "png_base64" ) )
			return { { "content",
					   json::array( { { { "type", "image" }, { "data", res[ "png_base64" ] }, { "mimeType", "image/png" } },
									  { { "type", "text" },
										{ "text", "Image rendue par l'instance (" + std::to_string( res.value( "width", 0 ) ) + "x" +
													  std::to_string( res.value( "height", 0 ) ) + ")." } } } ) } };
	}
	else
		return TextResult( "outil inconnu : " + name, true );
	if( res.contains( "content" ) )
		return res;// erreur déjà formatée
	bool ok = res.value( "ok", false );
	res.erase( "ok" );
	if( res.contains( "instance" ) )
		return TextResult( res[ "instance" ].dump( 2 ), !ok );
	return TextResult( res.dump( 2 ), !ok );
}

void Send( const json& j )
{
	std::string s = j.dump( -1, ' ', false, json::error_handler_t::replace ) + "\n";
	fwrite( s.data(), 1, s.size(), stdout );
	fflush( stdout );
}
}//namespace

int main()
{
#ifdef _WIN32
	_setmode( _fileno( stdin ), _O_BINARY );
	_setmode( _fileno( stdout ), _O_BINARY );
#endif
	std::string line;
	while( std::getline( std::cin, line ) )
	{
		if( !line.empty() && line.back() == '\r' )
			line.pop_back();
		if( line.empty() )
			continue;
		json msg = json::parse( line, nullptr, false );
		if( !msg.is_object() )
			continue;
		if( !msg.contains( "id" ) )
			continue;// notification
		json id            = msg[ "id" ];
		std::string method = msg.value( "method", std::string() );
		json params        = msg.value( "params", json::object() );
		json result;
		try
		{
			if( method == "initialize" )
				result = { { "protocolVersion", params.value( "protocolVersion", std::string( "2025-06-18" ) ) },
						   { "capabilities", { { "tools", json::object() }, { "prompts", json::object() } } },
						   { "serverInfo", { { "name", "shader-loader" }, { "title", "Shader Loader (Resolume)" }, { "version", kVersion } } },
						   { "instructions", kInstructions } };
			else if( method == "ping" )
				result = json::object();
			else if( method == "tools/list" )
				result = { { "tools", Tools() } };
			else if( method == "tools/call" )
				result = CallTool( params.value( "name", std::string() ), params.value( "arguments", json::object() ) );
			else if( method == "prompts/list" )
				result = { { "prompts",
							 json::array( { { { "name", "ameliorer_effet" },
											  { "title", "Améliorer l'effet Shader Loader" },
											  { "description", "Améliore le shader chargé dans Resolume selon une consigne." },
											  { "arguments", json::array( { { { "name", "consigne" }, { "description", "Ce qu'il faut changer" }, { "required", true } } } ) } } } ) } };
			else if( method == "prompts/get" )
			{
				std::string c = params.value( "arguments", json::object() ).value( "consigne", std::string() );
				result        = { { "messages",
                             json::array( { { { "role", "user" },
                                              { "content",
                                                { { "type", "text" },
                                                  { "text", "Améliore l'effet chargé dans Shader Loader (Resolume) : " + c +
                                                                "\nLis-le avec get_shader, applique la nouvelle version avec update_shader (corrige s'il ne compile pas), "
                                                                "regarde le rendu avec capture_output, puis résume ce que tu as changé. N'enregistre pas sans me demander." } } } } } ) } };
			}
			else if( method == "resources/list" )
				result = { { "resources", json::array() } };
			else
			{
				Send( { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", -32601 }, { "message", "méthode inconnue : " + method } } } } );
				continue;
			}
		}
		catch( const std::exception& e )
		{
			Send( { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", -32603 }, { "message", e.what() } } } } );
			continue;
		}
		Send( { { "jsonrpc", "2.0" }, { "id", id }, { "result", result } } );
	}
	return 0;
}
