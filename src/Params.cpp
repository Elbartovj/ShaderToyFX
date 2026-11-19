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
	// plage symétrique : le milieu du slider (valeur par défaut dans Resolume) = valeur du shader
	p.def = v;
	if( p.isInt )
	{
		if( v >= 2.0f )
		{
			p.minV = 1.0f;// jamais 0 : évite les divisions par zéro / boucles vides
			p.maxV = 2.0f * v - 1.0f;
		}
		else if( v == 1.0f )
		{
			p.minV = 0.0f;
			p.maxV = 2.0f;
		}
		else if( v == 0.0f )
		{
			p.minV = -4.0f;
			p.maxV = 4.0f;
		}
		else
		{
			p.minV = 2.0f * v;
			p.maxV = 0.0f;
		}
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

// Remplace par des lignes vides le code des blocs de préprocesseur inactifs
// (#ifdef NOM non défini, #if 0, #else d'un bloc actif…). Les expressions #if inconnues sont gardées.
std::string MaskInactive( const std::string& code, std::set< std::string >& defined )
{
	struct Frame
	{
		bool parent, active, taken, known;
	};
	std::vector< Frame > stack;
	auto isActive = [ & ]() { return stack.empty() || stack.back().active; };
	static const std::regex reDir( R"(^\s*#\s*(\w+)\s*(.*)$)" );
	static const std::regex reDefined( R"(^(!)?\s*defined\s*\(?\s*(\w+)\s*\)?\s*$)" );
	std::string stripped = StripComments( code );
	auto lines           = Lines( stripped );
	auto orig            = Lines( code );
	std::string out;
	for( size_t i = 0; i < lines.size(); ++i )
	{
		std::smatch m;
		bool keep = isActive();
		if( std::regex_match( lines[ i ], m, reDir ) )
		{
			std::string d = m[ 1 ], arg = Trim( m[ 2 ] );
			std::string name = arg.substr( 0, arg.find_first_of( " \t(" ) );
			auto push = [ & ]( int cond ) {// 1 vrai, 0 faux, -1 inconnu
				bool parent = isActive();
				Frame f;
				f.parent = parent;
				f.known  = cond >= 0;
				f.active = parent && cond != 0;
				f.taken  = cond == 1;
				stack.push_back( f );
			};
			if( d == "ifdef" )
				push( defined.count( name ) ? 1 : 0 );
			else if( d == "ifndef" )
				push( defined.count( name ) ? 0 : 1 );
			else if( d == "if" )
			{
				std::smatch dm;
				if( arg == "0" )
					push( 0 );
				else if( arg == "1" )
					push( 1 );
				else if( std::regex_match( arg, dm, reDefined ) )
					push( ( defined.count( dm[ 2 ] ) > 0 ) != dm[ 1 ].matched ? 1 : 0 );
				else
					push( -1 );
			}
			else if( d == "elif" && !stack.empty() )
			{
				Frame& f = stack.back();
				f.active = f.parent && !( f.known && f.taken );
				f.known  = false;
			}
			else if( d == "else" && !stack.empty() )
			{
				Frame& f = stack.back();
				f.active = f.parent && !( f.known && f.taken );
				if( !f.known )
					f.active = f.parent;// branche inconnue : on garde les deux
			}
			else if( d == "endif" && !stack.empty() )
				stack.pop_back();
			else if( d == "define" && isActive() && !name.empty() )
				defined.insert( name );
			else if( d == "undef" && isActive() )
				defined.erase( name );
			keep = true;// les directives elles-mêmes restent
			if( d == "define" && !isActive() )
				keep = false;
		}
		out += keep && i < orig.size() ? orig[ i ] : std::string();
		if( i + 1 < lines.size() )
			out += '\n';
	}
	return out;
}

// Efface (espaces) le corps des fonctions jamais appelées depuis mainImage, dans tous les sources
void BlankUnreachableFunctions( std::vector< std::string >& stripped )
{
	struct Fn
	{
		size_t src, open, close;
		std::string name;
		size_t header;// début du nom de la fonction
	};
	std::vector< Fn > fns;
	static const std::regex reName( R"(([A-Za-z_]\w*)\s*$)" );
	for( size_t si = 0; si < stripped.size(); ++si )
	{
		const std::string& t = stripped[ si ];
		int depth            = 0;
		bool lineStart = true, pre = false;
		for( size_t i = 0; i < t.size(); ++i )
		{
			char c = t[ i ];
			if( c == '\n' )
			{
				lineStart = true;
				pre       = false;
				continue;
			}
			if( lineStart && c != ' ' && c != '\t' )
			{
				pre       = c == '#';
				lineStart = false;
			}
			if( pre )
				continue;
			if( c == '{' )
			{
				if( depth == 0 )
				{
					// "nom( ... ) {" ?
					size_t j = t.find_last_not_of( " \t\n", i == 0 ? 0 : i - 1 );
					if( j != std::string::npos && t[ j ] == ')' )
					{
						int pd = 0;
						size_t k = j;
						for( ;; )
						{
							if( t[ k ] == ')' )
								++pd;
							else if( t[ k ] == '(' && --pd == 0 )
								break;
							if( k == 0 )
								break;
							--k;
						}
						std::smatch m;
						std::string before = t.substr( 0, k );
						if( std::regex_search( before, m, reName ) )
						{
							// fin du corps
							int d    = 0;
							size_t e = i;
							for( ; e < t.size(); ++e )
							{
								if( t[ e ] == '{' )
									++d;
								else if( t[ e ] == '}' && --d == 0 )
									break;
							}
							fns.push_back( { si, i, e, m[ 1 ], (size_t)m.position( 1 ) } );
						}
					}
				}
				++depth;
			}
			else if( c == '}' )
				depth = std::max( 0, depth - 1 );
		}
	}
	if( fns.empty() )
		return;
	std::set< std::string > names;
	for( auto& f : fns )
		names.insert( f.name );
	// racines : mainImage + toute fonction citée dans une macro (#define) ou hors fonction
	std::set< std::string > reach = { "mainImage" };
	auto scanIds = [ & ]( const std::string& text, std::vector< std::string >& found ) {
		static const std::regex reId( R"([A-Za-z_]\w*)" );
		for( std::sregex_iterator it( text.begin(), text.end(), reId ), end; it != end; ++it )
			if( names.count( it->str() ) )
				found.push_back( it->str() );
	};
	for( size_t si = 0; si < stripped.size(); ++si )
	{
		std::string outside = stripped[ si ];
		for( auto& f : fns )
			if( f.src == si )
				for( size_t k = f.header; k <= f.close && k < outside.size(); ++k )
					if( outside[ k ] != '\n' )
						outside[ k ] = ' ';
		std::vector< std::string > found;
		scanIds( outside, found );// macros + déclarations globales
		for( auto& n : found )
			reach.insert( n );
	}
	bool grew = true;
	while( grew )
	{
		grew = false;
		for( auto& f : fns )
		{
			if( !reach.count( f.name ) )
				continue;
			std::vector< std::string > found;
			scanIds( stripped[ f.src ].substr( f.open, f.close - f.open + 1 ), found );
			for( auto& n : found )
				if( reach.insert( n ).second )
					grew = true;
		}
	}
	for( auto& f : fns )
		if( !reach.count( f.name ) )
			for( size_t k = f.open; k <= f.close && k < stripped[ f.src ].size(); ++k )
				if( stripped[ f.src ][ k ] != '\n' )
					stripped[ f.src ][ k ] = ' ';
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
				if( std::regex_search( line, m, reDef ) && depthGuard < 4 )
				{
					int sub = 0;
					if( UsedInConstContext( m[ 1 ], strippedSources, statements, sub, depthGuard + 1 ) )
						return true;
					uses += sub;// la macro n'est utile que si elle est utilisée
				}
				else
					++uses;
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

	// 2) #define et constantes globales (hors blocs #ifdef inactifs : un réglage sans effet n'est pas exposé)
	{
		std::vector< std::string > masked;
		std::set< std::string > defCommon;
		masked.push_back( MaskInactive( sources[ 0 ], defCommon ) );
		for( size_t k = 1; k < sources.size(); ++k )
		{
			std::set< std::string > defs = defCommon;
			masked.push_back( MaskInactive( sources[ k ], defs ) );
		}
		sources = masked;
	}
	std::vector< Candidate > cands;
	for( const std::string& src : sources )
		FindCandidates( src, cands );
	if( cands.empty() )
		return;

	std::vector< std::string > stripped;
	std::vector< std::vector< Statement > > statements;
	for( const std::string& src : sources )
		stripped.push_back( StripComments( src ) );
	BlankUnreachableFunctions( stripped );// un réglage utilisé seulement par du code mort n'a aucun effet
	for( const std::string& st : stripped )
		statements.push_back( SplitStatements( st ) );

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
		if( c.name.rfind( "KEY_", 0 ) == 0 )
			continue;// code de touche clavier
		if( c.vecSize > 1 && std::all_of( c.values.begin(), c.values.end(), []( float v ) { return v == 0.0f || v == 1.0f || v == -1.0f; } ) )
			continue;// vecteur de direction / décalage de voisinage, pas un réglage
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
				p.def  = c.values[ k ];
				p.minV = p.def - 0.5f;
				p.maxV = p.def + 0.5f;
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

std::string ZeroInitLocals( const std::string& code )
{
	static const std::regex reDecl( R"(^(\s*)((?:(?:highp|mediump|lowp)\s+)?)(float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|bvec[234]|mat[234])(\s+)([A-Za-z_]\w*)\s*;)" );
	std::string stripped = StripComments( code );
	auto lines           = Lines( stripped );
	auto orig            = Lines( code );
	std::string out;
	int depth = 0;
	for( size_t li = 0; li < lines.size(); ++li )
	{
		std::string result = li < orig.size() ? orig[ li ] : lines[ li ];
		std::smatch m;
		if( depth > 0 && std::regex_search( lines[ li ], m, reDecl ) && m.position( 0 ) == 0 )
		{
			std::string type = m[ 3 ];
			std::string init = m[ 1 ].str() + m[ 2 ].str() + type + m[ 4 ].str() + m[ 5 ].str() + " = " + type + "(0);";
			result           = init + result.substr( (size_t)m.length( 0 ) );
		}
		out += result;
		if( li + 1 < lines.size() )
			out += '\n';
		size_t first = lines[ li ].find_first_not_of( " \t" );
		if( first == std::string::npos || lines[ li ][ first ] != '#' )
			for( char ch : lines[ li ] )
			{
				if( ch == '{' )
					++depth;
				else if( ch == '}' )
					depth = std::max( 0, depth - 1 );
			}
	}
	return out;
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
