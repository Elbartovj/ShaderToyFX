// Shader Loader - améliorateur de shaders avec l'API Claude (voir Claude.h)
#include "Claude.h"
#include "GlslGen.h"
#include "Net.h"
#include "Util.h"
#include "sed/SedCompiler.h"

#include <nlohmann/json.hpp>

#include <regex>

using json = nlohmann::json;

namespace stfx
{
namespace claude
{
namespace
{
const char* kSystem = R"SYS(You improve real-time GLSL fragment shaders for Shader Loader, a Resolume Arena (VJ software) FFGL plugin that runs shaders in the Shadertoy format.

Environment:
- Desktop OpenGL, GLSL 4.10 core. Shadertoy uniforms: iResolution (vec3), iTime, iTimeDelta, iFrame (int), iMouse (vec4), iDate, iSampleRate, iChannel0..iChannel3, iChannelResolution[4], iChannelTime[4].
- Every pass defines: void mainImage( out vec4 fragColor, in vec2 fragCoord ). Common code is prepended to every pass.
- Buffer passes (Buffer A..D) render to float textures; a pass reading its own buffer gets the previous frame (feedback).
- The passes and what each iChannel is connected to are fixed (listed below): do not add or remove passes and do not change channel meaning. An "audio" channel is Resolume's live audio FFT in the Shadertoy layout (512x2: row y=0.25 spectrum, y=0.75 waveform). An "input" channel is the video under the effect in Resolume.
- Adjustable controls: a top-level `#define NAME 1.0`, `const float NAME = 1.0;`, `const vec3 NAME = vec3(...)` or `uniform float NAME; // min max default` becomes a slider in Resolume (24 at most). When it helps the request, expose the key new values this way with clear UPPERCASE names, and keep existing controls working.
- Must run at 60 fps at 1920x1080 on a mid-range GPU: keep loops bounded, avoid huge iteration counts.
- Write portable GLSL: initialize every local variable, no implicit int/float conversions (write 1.0 not 1), no recursion, declare functions before use.

Answer in exactly this format, with the complete code of every pass (not a diff), no markdown fences:
<summary>One or two sentences in French describing what you changed.</summary>
<common>complete Common code</common>   (only if the shader has a Common section)
<pass name="PASS NAME">complete code of that pass</pass>   (one per pass, same names and order as given)
)SYS";

const char* ChannelLabel( const ChannelDesc& c )
{
	static const char* letters[] = { "Buffer A", "Buffer B", "Buffer C", "Buffer D" };
	switch( c.kind )
	{
	case ChannelKind::Input: return "input (video from Resolume)";
	case ChannelKind::Audio: return "audio (Resolume FFT)";
	case ChannelKind::Keyboard: return "keyboard (always empty)";
	case ChannelKind::Buffer: return c.buffer >= 0 && c.buffer < 4 ? letters[ c.buffer ] : "buffer";
	case ChannelKind::Texture: return "image texture";
	case ChannelKind::Cubemap: return "cubemap texture (samplerCube)";
	case ChannelKind::Volume: return "3D texture (sampler3D)";
	default: return "nothing (black)";
	}
}

std::string DescribeShader( const ShaderDoc& doc )
{
	std::string s = "Shader \"" + doc.title + "\"" + ( doc.author.empty() ? "" : " by " + doc.author ) + ".\nChannels:\n";
	for( auto& p : doc.passes )
	{
		s += "- " + p.name + ":";
		bool any = false;
		for( int i = 0; i < 4; ++i )
			if( p.ch[ i ].kind != ChannelKind::None )
			{
				s += std::string( " iChannel" ) + char( '0' + i ) + " = " + ChannelLabel( p.ch[ i ] ) + ";";
				any = true;
			}
		s += any ? "\n" : " none\n";
	}
	s += "\nCurrent code:\n";
	if( !Trim( doc.common ).empty() )
		s += "<common>\n" + doc.common + "\n</common>\n";
	for( auto& p : doc.passes )
		s += "<pass name=\"" + p.name + "\">\n" + p.code + "\n</pass>\n";
	return s;
}

std::string StripFences( std::string code )
{
	code = std::regex_replace( code, std::regex( R"(^\s*```[a-zA-Z]*[ \t]*\n)" ), "" );
	code = std::regex_replace( code, std::regex( R"(\n?```\s*$)" ), "" );
	// une seule ligne vide de part et d'autre au plus
	size_t a = code.find_first_not_of( "\r\n" );
	code     = a == std::string::npos ? std::string() : code.substr( a );
	while( !code.empty() && ( code.back() == '\n' || code.back() == '\r' || code.back() == ' ' ) )
		code.pop_back();
	return code + "\n";
}

bool Between( const std::string& s, const std::string& a, const std::string& b, size_t from, std::string& out, size_t* endPos = nullptr )
{
	size_t i = s.find( a, from );
	if( i == std::string::npos )
		return false;
	i += a.size();
	size_t j = s.find( b, i );
	if( j == std::string::npos )
		return false;
	out = s.substr( i, j - i );
	if( endPos )
		*endPos = j + b.size();
	return true;
}

// Réponse -> nouveau code ; err si une passe manque
bool ApplyAnswer( const std::string& text, ShaderDoc& doc, std::string& summary, std::string& err )
{
	std::string t;
	if( Between( text, "<summary>", "</summary>", 0, t ) )
		summary = Trim( t );
	if( Between( text, "<common>", "</common>", 0, t ) )
		doc.common = StripFences( t );
	int found = 0;
	for( auto& p : doc.passes )
	{
		std::string tag = "<pass name=\"" + p.name + "\">";
		if( Between( text, tag, "</pass>", 0, t ) )
		{
			p.code = StripFences( t );
			++found;
		}
	}
	if( found == 0 )
	{
		// une seule passe rendue sans balises <pass> : on accepte un bloc de code nu
		if( doc.passes.size() == 1 && text.find( "mainImage" ) != std::string::npos && text.find( "<pass" ) == std::string::npos )
		{
			std::string body = text;
			size_t s         = body.find( "</summary>" );
			if( s != std::string::npos )
				body = body.substr( s + 10 );
			doc.passes[ 0 ].code = StripFences( body );
			return true;
		}
		err = "réponse de Claude sans code reconnaissable";
		return false;
	}
	return true;
}

bool CallApi( const std::string& apiKey, const std::string& model, const json& messages, std::string& answer, std::string& err )
{
	json body = { { "model", model }, { "max_tokens", 16000 }, { "system", kSystem }, { "messages", messages } };
	std::string headers = "Content-Type: application/json\r\nx-api-key: " + Trim( apiKey ) + "\r\nanthropic-version: 2023-06-01\r\n";
	HttpResult r        = HttpRequest( "POST", "https://api.anthropic.com/v1/messages", body.dump(), headers, 300000 );
	json j              = json::parse( r.Text(), nullptr, false );
	if( j.is_object() && j.contains( "error" ) && j[ "error" ].is_object() )
	{
		std::string msg = j[ "error" ].value( "message", std::string( "erreur" ) );
		if( r.status == 401 )
			err = "clé API refusée (" + msg + ")";
		else if( r.status == 429 )
			err = "limite d'utilisation atteinte, réessaie dans un moment (" + msg + ")";
		else if( r.status == 529 )
			err = "Claude est surchargé, réessaie dans un moment";
		else
			err = "API Claude : " + msg;
		return false;
	}
	if( !r.error.empty() && r.status == 0 )
	{
		err = "connexion impossible : " + r.error;
		return false;
	}
	if( !j.is_object() || !j.contains( "content" ) )
	{
		err = "réponse inattendue de l'API (HTTP " + std::to_string( r.status ) + ")";
		return false;
	}
	answer.clear();
	for( auto& c : j[ "content" ] )
		if( c.value( "type", std::string() ) == "text" )
			answer += c.value( "text", std::string() );
	if( j.value( "stop_reason", std::string() ) == "max_tokens" )
	{
		err = "réponse coupée (shader trop long pour une seule réponse)";
		return false;
	}
	if( answer.empty() )
	{
		err = "réponse vide";
		return false;
	}
	return true;
}
}//namespace

const std::vector< Model >& Models()
{
	static const std::vector< Model > list = {
		{ "claude-sonnet-5-5", "Claude Sonnet 5.5 (rapide)" },
		{ "claude-opus-5-5", "Claude Opus 5.5 (le plus capable)" },
		{ "claude-haiku-5-5", "Claude Haiku 5.5 (économique)" },
	};
	return list;
}

std::string CheckCompiles( ShaderDoc& doc )
{
	ExtractUserParams( doc );
	std::string all;
	for( auto& p : doc.passes )
	{
		std::string log;
		if( !sed::ValidateGlsl( BuildFragmentSource( doc, p ), log ) )
			all += "--- " + p.name + " ---\n" + log + "\n";
	}
	return all;
}

Result Improve( const std::string& apiKey, const std::string& model, const std::string& prompt, const ShaderDoc& doc,
				const std::function< void( const std::string& ) >& progress, const std::atomic< bool >& cancel )
{
	Result res;
	if( Trim( apiKey ).empty() )
	{
		res.error = "clé API Claude manquante (onglet PRÉFÉRENCES)";
		return res;
	}
	if( doc.IsShadered() )
	{
		res.error = "les projets SHADERed / Godot ne sont pas pris en charge";
		return res;
	}
	json messages = json::array();
	messages.push_back( { { "role", "user" }, { "content", "Request: " + prompt + "\n\n" + DescribeShader( doc ) } } );
	try
	{
		for( int attempt = 1; attempt <= 3; ++attempt )
		{
			if( cancel )
			{
				res.error = "annulé";
				return res;
			}
			res.attempts = attempt;
			progress( attempt == 1 ? "Claude réécrit le shader…" : "Claude corrige les erreurs de compilation (essai " + std::to_string( attempt ) + ")…" );
			std::string answer;
			if( !CallApi( apiKey, model, messages, answer, res.error ) )
				return res;
			if( cancel )
			{
				res.error = "annulé";
				return res;
			}
			ShaderDoc candidate = doc;
			std::string summary;
			if( !ApplyAnswer( answer, candidate, summary, res.error ) )
				return res;
			progress( "Vérification de la compilation…" );
			std::string log = CheckCompiles( candidate );
			res.doc         = candidate;
			if( !summary.empty() )
				res.summary = summary;
			res.compileLog = log;
			if( log.empty() )
			{
				res.ok = true;
				res.error.clear();
				return res;
			}
			messages.push_back( { { "role", "assistant" }, { "content", answer } } );
			messages.push_back( { { "role", "user" },
								  { "content", "The shader does not compile:\n" + log.substr( 0, 4000 ) +
												   "\nFix these errors and send the complete shader again in the same format." } } );
		}
	}
	catch( const std::exception& e )
	{
		res.error = std::string( "erreur : " ) + e.what();
		return res;
	}
	// toujours des erreurs : on rend quand même le dernier essai (à corriger à la main)
	res.ok    = true;
	res.error = "le résultat ne compile pas encore";
	return res;
}
}//namespace claude
}//namespace stfx
