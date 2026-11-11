// ShaderToyFX - détection des paramètres réglables d'un shader
//
// Les shaders Shadertoy n'ont pas d'"inputs" : leurs réglages sont des #define et des constantes
// globales. On les transforme en uniforms (donc en sliders Resolume) quand c'est sans danger :
// une constante utilisée comme taille de tableau, dans un #if, un case ou l'initialiseur d'une autre
// constante doit rester constante, elle est donc ignorée. En dernier recours, le Renderer retire
// les paramètres qui empêchent la compilation.
#include "ShaderDoc.h"
#include "Util.h"
#include <regex>
#include <map>
#include <set>
#include <cmath>
#include <algorithm>

namespace stfx
{
namespace
{
const char* kNum = R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)";

// remplace les commentaires par des espaces (mêmes positions, retours à la ligne conservés)
std::string StripComments( const std::string& src )
{
	std::string s = src;
	size_t i      = 0;
	while( i < s.size() )
	{
		if( s[ i ] == '/' && i + 1 < s.size() && s[ i + 1 ] == '/' )
		{
			while( i < s.size() && s[ i ] != '\n' )
				s[ i++ ] = ' ';
		}
		else if( s[ i ] == '/' && i + 1 < s.size() && s[ i + 1 ] == '*' )
		{
			s[ i++ ] = ' ';
			s[ i++ ] = ' ';
			while( i < s.size() && !( s[ i ] == '*' && i + 1 < s.size() && s[ i + 1 ] == '/' ) )
			{
				if( s[ i ] != '\n' )
					s[ i ] = ' ';
				++i;
			}
			if( i < s.size() )
			{
				s[ i++ ] = ' ';
				if( i < s.size() )
					s[ i++ ] = ' ';
			}
		}
		else
			++i;
	}
	return s;
}

struct Statement
{
	std::string text;
	int depth = 0;
};

// découpe le code (sans commentaires ni lignes de préprocesseur) en instructions ; ';', '{', '}' séparent
std::vector< Statement > SplitStatements( const std::string& stripped )
{
	std::vector< Statement > out;
	int depth = 0, startDepth = 0;
	std::string cur;
	bool lineStart = true, inPre = false;
	for( char c : stripped )
	{
		if( lineStart && c != ' ' && c != '\t' && c != '\n' )
		{
			inPre     = c == '#';
			lineStart = false;
		}
		if( c == '\n' )
		{
			lineStart = true;
			inPre     = false;
			cur += ' ';
			continue;
		}
		if( inPre )
			continue;
		if( c == ';' || c == '{' || c == '}' )
		{
			if( !Trim( cur ).empty() )
				out.push_back( { cur, startDepth } );
			cur.clear();
			if( c == '{' )
				++depth;
			if( c == '}' )
				depth = std::max( 0, depth - 1 );
			startDepth = depth;
			continue;
		}
		if( Trim( cur ).empty() )
			startDepth = depth;
		cur += c;
	}
	if( !Trim( cur ).empty() )
		out.push_back( { cur, startDepth } );
	return out;
}

std::vector< std::string > Lines( const std::string& s )
{
	std::vector< std::string > out;
	size_t a = 0;
	for( ;; )
	{
		size_t b = s.find( '\n', a );
		out.push_back( s.substr( a, b == std::string::npos ? std::string::npos : b - a ) );
		if( b == std::string::npos )
			break;
		a = b + 1;
	}
	return out;
}

bool IsBlacklisted( const std::string& name )
{
	static const std::regex re( R"(^(m_)?(pi|tau|twopi|two_pi|pi2|pi_2|hpi|half_pi|halfpi|inv_pi|phi|sqrt2|sqrt3|e|deg2rad|rad2deg|inf|infinity|tmax|tmin|far|near|zero|one|two|half)$)", std::regex::icase );
	return std::regex_match( name, re ) || name.rfind( "stfx_", 0 ) == 0 || name.rfind( "GL_", 0 ) == 0 ||
		   ( name.size() > 1 && name[ 0 ] == 'i' && std::isupper( (unsigned char)name[ 1 ] ) );
}

// plage par défaut à partir de la valeur
void GuessRange( UserParam& p, float v )
{
	p.def = v;
	if( p.isInt )
	{
		p.minV = std::min( 0.0f, v * 2.0f );
		p.maxV = std::max( { v * 2.0f, v + 8.0f, 1.0f } );
		return;
	}
	if( v == 0.0f )
	{
		p.minV = -1.0f;
		p.maxV = 1.0f;
	}
	else if( v > 0.0f )
	{
		p.minV = 0.0f;
		p.maxV = v * 2.0f;
	}
	else
	{
		p.minV = v * 2.0f;
		p.maxV = 0.0f;
	}
}

// "// min max [défaut]" en fin de ligne
void ApplyComment( UserParam& p, const std::string& comment )
{
	static const std::regex reNum( kNum );
	std::vector< float > nums;
	for( std::sregex_iterator n( comment.begin(), comment.end(), reNum ), e; n != e; ++n )
	{
		try
		{
			nums.push_back( std::stof( n->str() ) );
		}
		catch( ... )
		{
		}
	}
	if( nums.size() >= 2 && nums[ 1 ] > nums[ 0 ] )
	{
		p.minV = nums[ 0 ];
		p.maxV = nums[ 1 ];
		if( nums.size() >= 3 )
			p.def = nums[ 2 ];
		p.def = std::clamp( p.def, p.minV, p.maxV );
	}
}

struct Candidate
{
	std::string name;
	ParamSource source;
	int vecSize = 1;
	bool isInt  = false;
	std::vector< float > values;
	std::string comment;
};

bool ParseFloat( const std::string& s, float& v )
{
	try
	{
		std::string t = Trim( s );
		if( !t.empty() && ( t.back() == 'f' || t.back() == 'F' ) )
			t.pop_back();
		size_t used = 0;
		v           = std::stof( t, &used );
		return used == t.size();
	}
	catch( ... )
	{
		return false;
	}
}

const std::regex& ReDefine()
{
	static const std::regex re( std::string( R"(^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+\(?[ \t]*()" ) + kNum + R"()[fF]?[ \t]*\)?[ \t]*$)" );
	return re;
}
const std::regex& ReConstScalar()
{
	static const std::regex re( std::string( R"(^[ \t]*const[ \t]+(?:(?:highp|mediump|lowp)[ \t]+)?(float|int)[ \t]+([A-Za-z_]\w*)[ \t]*=[ \t]*\(?[ \t]*()" ) + kNum + R"()[fF]?[ \t]*\)?[ \t]*;[ \t]*$)" );
	return re;
}
const std::regex& ReConstVec()
{
	static const std::regex re( R"(^[ \t]*const[ \t]+(?:(?:highp|mediump|lowp)[ \t]+)?vec([234])[ \t]+([A-Za-z_]\w*)[ \t]*=[ \t]*vec([234])[ \t]*\(([^;()]*)\)[ \t]*;[ \t]*$)" );
	return re;
}

// cherche les déclarations candidates dans un code source ; ligne par ligne, au niveau global
void FindCandidates( const std::string& code, std::vector< Candidate >& out )
{
	std::string stripped = StripComments( code );
	auto lines           = Lines( stripped );
	auto orig            = Lines( code );
	int depth            = 0;
	for( size_t li = 0; li < lines.size(); ++li )
	{
		const std::string& line = lines[ li ];
		std::string comment;
		if( li < orig.size() && orig[ li ].size() > line.size() )
			comment = orig[ li ].substr( line.size() );
		else if( li < orig.size() )
		{
			size_t c = orig[ li ].find( "//" );
			if( c != std::string::npos )
				comment = orig[ li ].substr( c + 2 );
		}
		std::smatch m;
		if( std::regex_match( line, m, ReDefine() ) )
		{
			Candidate c;
			c.name   = m[ 1 ];
			c.source = ParamSource::Define;
			std::string v = m[ 2 ];
			c.isInt  = v.find_first_of( ".eE" ) == std::string::npos;
			float f;
			if( ParseFloat( v, f ) )
			{
				c.values  = { f };
				c.comment = comment;
				out.push_back( c );
			}
		}
		else if( depth == 0 && std::regex_match( line, m, ReConstScalar() ) )
		{
			Candidate c;
			c.isInt  = m[ 1 ] == "int";
			c.name   = m[ 2 ];
			c.source = ParamSource::Const;
			float f;
			if( ParseFloat( m[ 3 ], f ) )
			{
				c.values  = { f };
				c.comment = comment;
				out.push_back( c );
			}
		}
		else if( depth == 0 && std::regex_match( line, m, ReConstVec() ) && m[ 1 ] == m[ 3 ] )
		{
			Candidate c;
			c.vecSize = std::stoi( m[ 1 ] );
			c.name    = m[ 2 ];
			c.source  = ParamSource::Const;
			std::string args = m[ 4 ];
			std::vector< float > vals;
			bool ok  = true;
			size_t a = 0;
			for( ;; )
			{
				size_t b = args.find( ',', a );
				float f;
				if( !ParseFloat( args.substr( a, b == std::string::npos ? std::string::npos : b - a ), f ) )
				{
					ok = false;
					break;
				}
				vals.push_back( f );
				if( b == std::string::npos )
					break;
				a = b + 1;
			}
			if( ok && vals.size() == 1 )
				vals.assign( c.vecSize, vals[ 0 ] );
			if( ok && (int)vals.size() == c.vecSize )
			{
				c.values  = vals;
				c.comment = comment;
				out.push_back( c );
			}
		}
		// profondeur d'accolades (hors préprocesseur)
		size_t first = line.find_first_not_of( " \t" );
		if( first == std::string::npos || line[ first ] != '#' )
			for( char ch : line )
			{
				if( ch == '{' )
					++depth;
				else if( ch == '}' )
					depth = std::max( 0, depth - 1 );
			}
	}
}

// la constante doit-elle rester constante ? (taille de tableau, #if, case, initialiseur constant…)
bool UsedInConstContext( const std::string& name, const std::vector< std::string >& strippedSources,
						 const std::vector< std::vector< Statement > >& statements, int& uses, int depthGuard = 0 )
{
	std::regex word( "\\b" + name + "\\b" );
	std::regex inBrackets( "\\[[^\\]\\n]*\\b" + name + "\\b[^\\]\\n]*\\]" );
	std::regex caseLabel( "\\bcase\\s+\\(?\\s*" + name + "\\b" );
	std::regex ownDecl( "^\\s*(const\\s+)?((highp|mediump|lowp)\\s+)?(float|int|vec[234])\\s+" + name + "\\s*=" );
	uses = 0;
	for( const std::string& src : strippedSources )
	{
		for( const std::string& line : Lines( src ) )
		{
			size_t first = line.find_first_not_of( " \t" );
			bool pre     = first != std::string::npos && line[ first ] == '#';
			if( !std::regex_search( line, word ) )
				continue;
			if( pre )
			{
				static const std::regex reCond( R"(^\s*#\s*(if|ifdef|ifndef|elif|undef))" );
				if( std::regex_search( line, reCond ) )
					return true;
				static const std::regex reDef( R"(^\s*#\s*define\s+(\w+))" );
				std::smatch m;
				if( std::regex_search( line, m, reDef ) && m[ 1 ] == name )
					continue;// sa propre définition
				// utilisée dans une autre macro : la macro hérite des contraintes
				++uses;
				if( std::regex_search( line, m, reDef ) && depthGuard < 4 )
				{
					int sub = 0;
					if( UsedInConstContext( m[ 1 ], strippedSources, statements, sub, depthGuard + 1 ) )
						return true;
				}
				continue;
			}
			if( std::regex_search( line, inBrackets ) || std::regex_search( line, caseLabel ) )
				return true;
			if( line.find( "layout" ) != std::string::npos )
				return true;
		}
	}
	for( const auto& list : statements )
	{
		for( const Statement& st : list )
		{
			if( !std::regex_search( st.text, word ) )
				continue;
			std::string t = Trim( st.text );
			if( std::regex_search( t, ownDecl ) )
				continue;
			++uses;
			size_t eq = t.find( '=' );
			bool isConst = t.rfind( "const", 0 ) == 0;
			if( eq != std::string::npos && ( isConst || st.depth == 0 ) )
			{
				// initialiseur d'une constante, ou d'une variable globale : doit être constant
				if( std::regex_search( t.substr( eq ), word ) )
					return true;
			}
		}
	}
	return false;
}
}//namespace

void ExtractUserParams( ShaderDoc& doc )
{
	doc.params.clear();
	std::vector< std::string > sources;
	sources.push_back( doc.common );
	for( auto& p : doc.passes )
		sources.push_back( p.code );

	// 1) uniforms explicites : "uniform float nom; // min max défaut"
	static const std::regex reUniform( R"(uniform\s+(?:(?:highp|mediump|lowp)\s+)?(float|int)\s+([A-Za-z_]\w*)\s*;([^\n]*))" );
	std::set< std::string > seen;
	for( const std::string& src : sources )
	{
		std::string stripped = StripComments( src );
		for( std::sregex_iterator it( stripped.begin(), stripped.end(), reUniform ), end; it != end; ++it )
		{
			std::string name = ( *it )[ 2 ];
			if( IsBlacklisted( name ) && name.rfind( "i", 0 ) == 0 )
				continue;
			if( seen.count( name ) )
				continue;
			seen.insert( name );
			UserParam p;
			p.name    = name;
			p.uniform = name;
			p.source  = ParamSource::Uniform;
			p.isInt   = ( *it )[ 1 ] == "int";
			// le commentaire est dans le texte d'origine, même position
			size_t pos   = (size_t)it->position( 0 ) + (size_t)it->length( 0 ) - ( *it )[ 3 ].length();
			size_t eol   = src.find( '\n', pos );
			std::string comment = src.substr( pos, eol == std::string::npos ? std::string::npos : eol - pos );
			if( p.isInt )
			{
				p.minV = 0;
				p.maxV = 10;
				p.def  = 1;
			}
			ApplyComment( p, comment );
			{
				// un seul nombre en commentaire = valeur par défaut
				static const std::regex reNum( kNum );
				std::vector< float > nums;
				for( std::sregex_iterator n( comment.begin(), comment.end(), reNum ), e; n != e; ++n )
				{
					float f;
					if( ParseFloat( n->str(), f ) )
						nums.push_back( f );
				}
				if( nums.size() == 1 )
				{
					p.def = nums[ 0 ];
					if( p.def > p.maxV )
						p.maxV = p.def * 2.0f;
					if( p.def < p.minV )
						p.minV = p.def * 2.0f;
				}
			}
			doc.params.push_back( p );
		}
	}

	// 2) #define et constantes globales
	std::vector< Candidate > cands;
	for( const std::string& src : sources )
		FindCandidates( src, cands );
	if( cands.empty() )
		return;

	std::vector< std::string > stripped;
	std::vector< std::vector< Statement > > statements;
	for( const std::string& src : sources )
	{
		stripped.push_back( StripComments( src ) );
		statements.push_back( SplitStatements( stripped.back() ) );
	}

	// noms définis plusieurs fois avec des valeurs différentes : ambigus
	std::map< std::string, std::vector< const Candidate* > > byName;
	for( auto& c : cands )
		byName[ c.name ].push_back( &c );

	int used = (int)doc.params.size();
	std::set< std::string > done;
	for( auto& c : cands )
	{
		if( done.count( c.name ) || seen.count( c.name ) || IsBlacklisted( c.name ) )
			continue;
		done.insert( c.name );
		bool ambiguous = false;
		for( const Candidate* o : byName[ c.name ] )
			if( o->values != c.values || o->source != c.source || o->vecSize != c.vecSize || o->isInt != c.isInt )
				ambiguous = true;
		if( ambiguous )
			continue;
		int uses = 0;
		if( UsedInConstContext( c.name, stripped, statements, uses ) || uses == 0 )
			continue;
		if( used + c.vecSize > kMaxUserParams )
			break;

		bool color = ( c.vecSize >= 3 ) && std::all_of( c.values.begin(), c.values.end(), []( float v ) { return v >= 0.0f && v <= 1.0f; } );
		static const char* xyzw = "xyzw";
		static const char* rgba = "rgba";
		for( int k = 0; k < c.vecSize; ++k )
		{
			UserParam p;
			p.uniform = c.name;
			p.source  = c.source;
			p.vecSize = c.vecSize;
			p.comp    = c.vecSize > 1 ? k : -1;
			p.isInt   = c.isInt;
			p.name    = c.vecSize > 1 ? c.name + "." + ( color ? rgba[ k ] : xyzw[ k ] ) : c.name;
			if( color )
			{
				p.minV = 0;
				p.maxV = 1;
				p.def  = c.values[ k ];
			}
			else
				GuessRange( p, c.values[ k ] );
			if( c.vecSize == 1 )
				ApplyComment( p, c.comment );
			doc.params.push_back( p );
			++used;
		}
	}
}

std::string ApplyParamRewrites( const std::string& code, const std::vector< UserParam >& params )
{
	std::map< std::string, const UserParam* > rewrite;
	for( auto& p : params )
		if( p.source != ParamSource::Uniform )
			rewrite[ p.uniform ] = &p;
	if( rewrite.empty() )
		return code;

	std::string stripped = StripComments( code );
	auto lines           = Lines( stripped );
	auto orig            = Lines( code );
	std::string out;
	int depth = 0;
	for( size_t li = 0; li < lines.size(); ++li )
	{
		const std::string& line = lines[ li ];
		std::string result      = li < orig.size() ? orig[ li ] : line;
		std::smatch m;
		std::string name;
		if( std::regex_match( line, m, ReDefine() ) )
			name = m[ 1 ];
		else if( depth == 0 && std::regex_match( line, m, ReConstScalar() ) )
			name = m[ 2 ];
		else if( depth == 0 && std::regex_match( line, m, ReConstVec() ) )
			name = m[ 2 ];
		auto it = rewrite.find( name );
		if( !name.empty() && it != rewrite.end() )
		{
			const UserParam& p = *it->second;
			std::string type   = p.vecSize > 1 ? "vec" + std::to_string( p.vecSize ) : ( p.isInt ? "int" : "float" );
			result             = "uniform " + type + " " + p.uniform + "; // ShaderToyFX : réglable dans Resolume";
		}
		out += result;
		if( li + 1 < lines.size() )
			out += '\n';
		size_t first = line.find_first_not_of( " \t" );
		if( first == std::string::npos || line[ first ] != '#' )
			for( char ch : line )
			{
				if( ch == '{' )
					++depth;
				else if( ch == '}' )
					depth = std::max( 0, depth - 1 );
			}
	}
	return out;
}
}//namespace stfx
