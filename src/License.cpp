#include "License.h"
#include "Util.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <map>
#include <mutex>
#include <filesystem>

namespace stfx
{
const char* LicenseShortName( LicenseKind k )
{
	switch( k )
	{
	case LicenseKind::Commercial: return "Commercial OK";
	case LicenseKind::Attribution: return "Commercial (créditer)";
	case LicenseKind::NonCommercial: return "Non commercial";
	case LicenseKind::Restricted: return "Interdit";
	default: return "À vérifier";
	}
}

bool LicenseAllowsCommercial( LicenseKind k )
{
	return k == LicenseKind::Commercial || k == LicenseKind::Attribution;
}

namespace
{
struct Rule
{
	const char* pattern;
	LicenseKind kind;
	const char* label;
};

// l'ordre compte : la première règle trouvée l'emporte
const Rule kRules[] = {
	{ R"(\bby[- ]?nc\b|non[- ]?commercial|noncommercial)", LicenseKind::NonCommercial, "Creative Commons NC (pas d'usage commercial)" },
	{ R"(all rights reserved|tous droits r[ée]serv[ée]s|do not (copy|reuse|redistribute)|do not use (this|my) (shader|code|work)|not for commercial|no commercial|personal use only|private use only)", LicenseKind::Restricted, "Droits réservés / usage restreint" },
	{ R"(\bcc[- ]?0\b|creative commons zero|public[- ]domain|domaine public|\bunlicense\b|\bwtfpl\b)", LicenseKind::Commercial, "Domaine public / CC0" },
	{ R"(\bmit\b[- ]?licen[sc]e|licen[sc]e[ :]*\bmit\b|\(mit\)|\bmit\b\s*(\n|$))", LicenseKind::Commercial, "MIT" },
	{ R"(\bbsd\b|\bapache\b|\bzlib\b|\bisc licen[sc]e)", LicenseKind::Commercial, "Licence permissive (BSD/Apache/zlib)" },
	{ R"(\bgpl\b|\blgpl\b|gnu general public)", LicenseKind::Commercial, "GPL (copyleft)" },
	{ R"(cc[- ]?by[- ]?sa\b|attribution[- ]sharealike|attribution[- ]share alike)", LicenseKind::Attribution, "CC BY-SA (créditer, partage identique)" },
	{ R"(cc[- ]?by\b|creative commons attribution)", LicenseKind::Attribution, "CC BY (créditer l'auteur)" },
	{ R"(free to use|use (it )?(as|for) (you|whatever)|do whatever you want|feel free to use|use freely|libre de droits)", LicenseKind::Commercial, "Usage libre (mention de l'auteur)" },
	{ R"(\bcommercial\b|contact me|ask me|demande[rz]?-moi)", LicenseKind::Unknown, "Mention à lire (contacter l'auteur ?)" },
	{ R"(\blicen[sc]e\b)", LicenseKind::Unknown, "Licence mentionnée, non reconnue" },
};
}//namespace

LicenseInfo DetectLicense( const std::string& text, bool fromShadertoy )
{
	LicenseInfo info;
	std::string lower = ToLower( text );
	for( const Rule& r : kRules )
	{
		std::regex re( r.pattern, std::regex::icase );
		std::smatch m;
		if( std::regex_search( lower, m, re ) )
		{
			info.kind  = r.kind;
			info.label = r.label;
			// ligne correspondante, dans le texte d'origine
			size_t pos   = (size_t)m.position( 0 );
			size_t start = text.rfind( '\n', pos );
			start        = start == std::string::npos ? 0 : start + 1;
			size_t end   = text.find( '\n', pos );
			std::string line = Trim( text.substr( start, end == std::string::npos ? std::string::npos : end - start ) );
			if( line.size() > 140 )
				line = line.substr( 0, 137 ) + "...";
			info.evidence = line;
			return info;
		}
	}
	if( fromShadertoy )
	{
		info.kind  = LicenseKind::NonCommercial;
		info.label = "CC BY-NC-SA 3.0 (licence par défaut de Shadertoy)";
	}
	else
	{
		info.kind  = LicenseKind::Unknown;
		info.label = "Aucune licence indiquée (ta création ?)";
	}
	return info;
}

LicenseInfo LicenseForJson( const std::string& normalizedJson )
{
	auto j = nlohmann::json::parse( normalizedJson, nullptr, false );
	std::string text, author;
	bool fromShadertoy = false;
	if( j.is_object() )
	{
		if( j.contains( "info" ) && j[ "info" ].is_object() )
		{
			const auto& i = j[ "info" ];
			text += i.value( "description", std::string() ) + "\n";
			author        = i.value( "username", std::string() );
			fromShadertoy = !i.value( "id", std::string() ).empty();
		}
		if( j.contains( "renderpass" ) && j[ "renderpass" ].is_array() )
			for( auto& p : j[ "renderpass" ] )
				if( p.contains( "code" ) && p[ "code" ].is_string() )
					text += p[ "code" ].get< std::string >() + "\n";
	}
	LicenseInfo info = DetectLicense( text, fromShadertoy );
	info.author      = author;
	return info;
}

LicenseInfo LicenseForFile( const std::string& path )
{
	static std::mutex m;
	static std::map< std::string, std::pair< int64_t, LicenseInfo > > cache;
	std::error_code ec;
	int64_t stamp = (int64_t)std::filesystem::last_write_time( std::filesystem::path( Widen( path ) ), ec ).time_since_epoch().count();
	{
		std::lock_guard< std::mutex > lock( m );
		auto it = cache.find( path );
		if( it != cache.end() && it->second.first == stamp )
			return it->second.second;
	}
	std::string text;
	LicenseInfo info;
	if( ReadFileText( path, text ) )
	{
		size_t first = text.find_first_not_of( " \t\r\n" );
		if( first != std::string::npos && ( text[ first ] == '{' || text[ first ] == '[' ) )
		{
			auto j = nlohmann::json::parse( text, nullptr, false );
			if( j.is_object() && j.contains( "Shader" ) )
				j = j[ "Shader" ];
			else if( j.is_array() && !j.empty() )
				j = j[ 0 ];
			info = LicenseForJson( j.dump() );
		}
		else
			info = DetectLicense( text, false );
	}
	std::lock_guard< std::mutex > lock( m );
	cache[ path ] = { stamp, info };
	return info;
}
}//namespace stfx
