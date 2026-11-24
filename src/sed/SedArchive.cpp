#include "SedArchive.h"
#include "SedProject.h"
#include "../Net.h"
#include "../Util.h"

#include <miniz.h>

#include <algorithm>
#include <filesystem>
#include <regex>
#include <cstring>

namespace fs = std::filesystem;

namespace stfx
{
namespace sed
{
static fs::path P( const std::string& utf8 ) { return fs::path( Widen( utf8 ) ); }

std::string ParseShaderedId( const std::string& text )
{
	std::string t = Trim( text );
	static const std::regex reLink( R"(shadered\.org/(?:view\?s=|embed\.php\?id=|download\?type=shader&id=)([A-Za-z0-9_\-]+))" );
	std::smatch m;
	if( std::regex_search( t, m, reLink ) )
		return m[ 1 ].str();
	return {};
}

bool LooksLikeShaderedLink( const std::string& text )
{
	return !ParseShaderedId( text ).empty();
}

static std::string HtmlTitle( const std::string& html )
{
	static const std::regex reOg( R"(<meta[^>]+property=["']og:title["'][^>]+content=["']([^"']+)["'])", std::regex::icase );
	static const std::regex reTitle( R"(<title>([^<]+)</title>)", std::regex::icase );
	std::smatch m;
	std::string t;
	if( std::regex_search( html, m, reOg ) )
		t = m[ 1 ].str();
	else if( std::regex_search( html, m, reTitle ) )
		t = m[ 1 ].str();
	// « Titre - SHADERed » -> « Titre »
	for( const char* suffix : { " - SHADERed", " | SHADERed", "SHADERed - " } )
	{
		size_t p = t.find( suffix );
		if( p != std::string::npos )
			t = p == 0 ? t.substr( strlen( suffix ) ) : t.substr( 0, p );
	}
	return Trim( t );
}

bool ExtractProjectZip( const std::vector< uint8_t >& zip, const std::string& destDir, std::string& sprjPath, std::string& err )
{
	mz_zip_archive ar{};
	if( !mz_zip_reader_init_mem( &ar, zip.data(), zip.size(), 0 ) )
	{
		err = "Archive SHADERed illisible (le téléchargement a peut-être été refusé).";
		return false;
	}
	// préfixe commun (archive avec un seul dossier racine)
	mz_uint n = mz_zip_reader_get_num_files( &ar );
	std::vector< std::string > names;
	for( mz_uint i = 0; i < n; ++i )
	{
		mz_zip_archive_file_stat st;
		if( mz_zip_reader_file_stat( &ar, i, &st ) )
			names.push_back( st.m_filename );
		else
			names.push_back( "" );
	}
	std::string prefix;
	{
		std::string first;
		for( auto& s : names )
			if( !s.empty() )
			{
				first = s;
				break;
			}
		size_t slash = first.find_first_of( "/\\" );
		if( slash != std::string::npos )
		{
			std::string cand = first.substr( 0, slash + 1 );
			bool all         = true;
			for( auto& s : names )
				all = all && ( s.empty() || s.rfind( cand, 0 ) == 0 );
			if( all )
				prefix = cand;
		}
	}
	MakeDirs( destDir );
	std::error_code ec;
	for( mz_uint i = 0; i < n; ++i )
	{
		std::string name = names[ i ];
		if( name.empty() || mz_zip_reader_is_file_a_directory( &ar, i ) )
			continue;
		if( !prefix.empty() )
			name = name.substr( prefix.size() );
		std::replace( name.begin(), name.end(), '\\', '/' );
		// pas de chemins sortant du dossier
		if( name.find( ".." ) != std::string::npos || ( !name.empty() && ( name[ 0 ] == '/' || ( name.size() > 1 && name[ 1 ] == ':' ) ) ) )
			continue;
		size_t size = 0;
		void* data  = mz_zip_reader_extract_to_heap( &ar, i, &size, 0 );
		if( !data )
			continue;
		std::string out = JoinPath( destDir, name );
		MakeDirs( ParentDir( out ) );
		WriteFileBytes( out, data, size );
		mz_free( data );
		if( FileExtLower( out ) == ".sprj" && ( sprjPath.empty() || name.find( '/' ) == std::string::npos ) )
			sprjPath = out;
	}
	mz_zip_reader_end( &ar );
	if( sprjPath.empty() )
	{
		err = "L'archive ne contient pas de projet SHADERed (.sprj).";
		return false;
	}
	return true;
}

bool DownloadShaderedProject( const std::string& id, std::string& sprjPath, std::string& err )
{
	if( id.empty() )
	{
		err = "Lien shadered.org non reconnu.";
		return false;
	}
	// titre pour le nom du dossier (facultatif)
	std::string title;
	{
		HttpResult page = HttpGet( "https://shadered.org/view?s=" + id, 10000 );
		if( page.Ok() )
			title = HtmlTitle( page.Text() );
	}
	HttpResult r = HttpGet( "https://shadered.org/download?type=shader&id=" + id, 30000 );
	if( !r.Ok() )
	{
		err = "Téléchargement impossible depuis shadered.org (" + ( r.error.empty() ? "HTTP " + std::to_string( r.status ) : r.error ) + ").";
		return false;
	}
	std::string folder = SanitizeFileName( ( title.empty() ? std::string( "SHADERed" ) : title ) + " [" + id + "]" );
	std::string dest   = JoinPath( ShaderedDir(), folder );
	std::error_code ec;
	fs::remove_all( P( dest ), ec );// nouvelle version du même projet
	return ExtractProjectZip( r.body, dest, sprjPath, err );
}

const char* BookmarkletCode()
{
	// une seule ligne : c'est l'adresse (URL) du favori
	return R"JS(javascript:(async()=>{try{const q=new URLSearchParams(location.search);const id=q.get('s')||q.get('id');if(!/shadered\.org$/i.test(location.hostname)||!id){alert('ShaderToyFX : ouvre d\'abord un shader sur shadered.org (shadered.org/view?s=...)');return;}const r=await fetch('/download?type=shader&id='+encodeURIComponent(id));if(!r.ok)throw new Error('HTTP '+r.status);const b=await r.blob();const u=new Uint8Array(await b.slice(0,2).arrayBuffer());if(u[0]!==80||u[1]!==75)throw new Error('le site n\'a pas renvoyé d\'archive');const clean=s=>(s||'').replace(/SHADERed/gi,'').replace(/^[\s\-|:–—]+|[\s\-|:–—]+$/g,'').trim();const h=document.querySelector('h1,h2,h3');let t=clean(document.title)||clean(h&&h.textContent)||id;t=t.replace(/[\\/:*?"<>|]/g,'_').slice(0,80);const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download=t+' [shadered-'+id+'].zip';document.body.appendChild(a);a.click();a.remove();}catch(e){alert('ShaderToyFX : export impossible ('+e.message+'). Colle plutôt le lien dans l\'onglet LIEN.');}})();)JS";
}

std::vector< std::string > FindDownloadedProjects()
{
	std::vector< std::string > out;
	std::string dir = DownloadsDir();
	if( dir.empty() )
		return out;
	static const std::regex re( R"(\[shadered-[A-Za-z0-9_\-]+\]( \(\d+\))?\.zip$)", std::regex::icase );
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

bool ImportDownloadedProject( const std::string& zipFile, std::string& sprjPath, std::string& title, std::string& err )
{
	std::vector< uint8_t > data;
	if( !ReadFileBytes( zipFile, data ) || data.size() < 22 )
	{
		err = "lecture impossible";
		return false;
	}
	// « Titre [shadered-ID] (2).zip » -> « Titre [ID] »
	std::string stem = FileStem( zipFile );
	static const std::regex reDup( R"( \(\d+\)$)" );
	stem = std::regex_replace( stem, reDup, "" );
	static const std::regex reTag( R"(\[shadered-([A-Za-z0-9_\-]+)\])" );
	std::smatch m;
	std::string id;
	if( std::regex_search( stem, m, reTag ) )
		id = m[ 1 ].str();
	title = Trim( std::regex_replace( stem, reTag, "" ) );
	if( title.empty() )
		title = "SHADERed";
	std::string dest = JoinPath( ShaderedDir(), SanitizeFileName( title + ( id.empty() ? "" : " [" + id + "]" ) ) );
	std::error_code ec;
	fs::remove_all( P( dest ), ec );
	sprjPath.clear();
	if( !ExtractProjectZip( data, dest, sprjPath, err ) )
	{
		fs::remove_all( P( dest ), ec );
		return false;
	}
	fs::remove( P( zipFile ), ec );
	return true;
}

bool ImportIntoLibrary( const std::string& path, std::string& newPath, std::string& err )
{
	std::error_code ec;
	std::string ext = FileExtLower( path );
	if( ext == ".zip" )
	{
		std::vector< uint8_t > data;
		if( !ReadFileBytes( path, data ) )
		{
			err = "Impossible de lire " + path;
			return false;
		}
		std::string dest = JoinPath( ShaderedDir(), SanitizeFileName( FileStem( path ) ) );
		fs::remove_all( P( dest ), ec );
		return ExtractProjectZip( data, dest, newPath, err );
	}
	if( ext == ".gdshader" || ext == ".shader" )
	{
		MakeDirs( JoinPath( ShaderedDir(), "Godot" ) );
		newPath = JoinPath( JoinPath( ShaderedDir(), "Godot" ), Narrow( P( path ).filename().wstring() ) );
		if( P( newPath ) != P( path ) )
			fs::copy_file( P( path ), P( newPath ), fs::copy_options::overwrite_existing, ec );
		return !ec;
	}
	if( ext != ".sprj" )
	{
		err = "Format non reconnu (attendu : .sprj, .zip, .gdshader).";
		return false;
	}
	// déjà dans la bibliothèque : rien à copier
	std::string root = ToLower( Narrow( P( RootDir() ).lexically_normal().wstring() ) );
	std::string src  = ToLower( Narrow( P( path ).lexically_normal().wstring() ) );
	if( src.rfind( root, 0 ) == 0 )
	{
		newPath = path;
		return true;
	}
	Project proj;
	if( !LoadProject( path, proj, err ) )
		return false;
	std::string srcDir = ParentDir( path );
	std::string name   = FileStem( path );
	if( name == "project" || name == "proj" )
		name = Narrow( P( srcDir ).filename().wstring() );
	std::string dest = JoinPath( ShaderedDir(), SanitizeFileName( name ) );
	fs::remove_all( P( dest ), ec );
	MakeDirs( dest );

	// dossier entier s'il reste raisonnable, sinon seulement les fichiers référencés
	uintmax_t total = 0;
	int count       = 0;
	for( auto it = fs::recursive_directory_iterator( P( srcDir ), fs::directory_options::skip_permission_denied, ec );
		 it != fs::recursive_directory_iterator() && count <= 400 && total <= 64ull * 1024 * 1024; it.increment( ec ) )
		if( it->is_regular_file( ec ) )
		{
			total += it->file_size( ec );
			++count;
		}
	if( count <= 400 && total <= 64ull * 1024 * 1024 )
		fs::copy( P( srcDir ), P( dest ), fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec );
	else
	{
		std::vector< std::string > files = { path };
		for( auto& p : proj.passes )
			for( auto* f : { &p.vsPath, &p.psPath, &p.gsPath, &p.tcsPath, &p.tesPath, &p.csPath, &p.godotPath } )
				if( !f->empty() )
					files.push_back( *f );
		for( auto& o : proj.objects )
		{
			if( !o.path.empty() )
				files.push_back( o.path );
			for( auto& c : o.cube )
				if( !c.empty() )
					files.push_back( c );
			if( o.kind == ObjectKind::Buffer )
				files.push_back( JoinPath( srcDir, "buffers/" + o.name + ".buf" ) );
		}
		for( auto& p : proj.passes )
			for( auto& it : p.items )
				if( !it.modelPath.empty() )
					files.push_back( it.modelPath );
		for( auto& f : files )
		{
			auto rel = fs::relative( P( f ), P( srcDir ), ec );
			if( ec || rel.empty() || Narrow( rel.wstring() ).rfind( "..", 0 ) == 0 )
				continue;
			fs::create_directories( ( P( dest ) / rel ).parent_path(), ec );
			fs::copy_file( P( f ), P( dest ) / rel, fs::copy_options::overwrite_existing, ec );
		}
	}
	newPath = Narrow( ( P( dest ) / P( path ).filename() ).wstring() );
	if( !FileExists( newPath ) )
	{
		err = "Copie du projet impossible.";
		return false;
	}
	return true;
}
}//namespace sed
}//namespace stfx
