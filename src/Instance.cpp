#include "Instance.h"
#include "ShadertoyApi.h"
#include "Util.h"
#include <thread>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace stfx
{
// Les threads de travail sont détachés : on épingle la DLL pour qu'elle ne soit
// jamais déchargée pendant qu'un téléchargement est en cours.
static void PinModule()
{
#ifdef _WIN32
	static std::once_flag once;
	std::call_once( once, [] {
		HMODULE h = nullptr;
		GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&PinModule, &h );
	} );
#endif
}

template< typename Fn >
static void RunAsync( const SharedPtr& s, const std::string& status, Fn work )
{
	PinModule();
	uint64_t gen;
	{
		std::lock_guard< std::mutex > lock( s->m );
		gen       = ++s->requestGen;
		s->busy   = true;
		s->status = status;
	}
	std::weak_ptr< InstanceShared > weak = s;
	std::thread( [ weak, gen, work ]() {
		ShaderDoc doc;
		std::string err;
		bool userInitiated = true;
		bool ok            = false;
		try
		{
			ok = work( doc, err, userInitiated );
		}
		catch( const std::exception& e )
		{
			err = std::string( "Exception : " ) + e.what();
		}
		auto sp = weak.lock();
		if( !sp )
			return;
		std::lock_guard< std::mutex > lock( sp->m );
		if( !sp->alive || gen != sp->requestGen )
			return;// une requête plus récente a été faite entre temps
		if( ok )
		{
			sp->pendingDoc           = std::make_unique< ShaderDoc >( std::move( doc ) );
			sp->pendingUserInitiated = userInitiated;
			sp->status               = "Compilation…";
		}
		else
		{
			sp->busy        = false;
			sp->status      = "Erreur";
			sp->log         = err;
			sp->lastBuildOk = false;
		}
	} ).detach();
}

void RequestLoadFile( const SharedPtr& s, const std::string& path, bool userInitiated )
{
	RunAsync( s, "Chargement de " + FileStem( path ) + "…", [ path, userInitiated ]( ShaderDoc& doc, std::string& err, bool& user ) {
		user = userInitiated;
		return LoadShaderFile( path, doc, err );
	} );
}

void RequestLoadShadertoy( const SharedPtr& s, const std::string& id )
{
	RunAsync( s, "Téléchargement de " + id + "…", [ id ]( ShaderDoc& doc, std::string& err, bool& user ) {
		user = true;
		std::string path, title;
		if( !FetchShaderToCache( id, path, title, err ) )
			return false;
		return LoadShaderFile( path, doc, err );
	} );
}

void RequestRandom( const SharedPtr& s, const std::string& source )
{
	std::string exclude;
	{
		std::lock_guard< std::mutex > lock( s->m );
		if( s->current )
			exclude = s->current->sourcePath;// évite de retomber sur le shader en cours
	}
	std::string label = source.empty() ? "bibliothèque" : source == kRandomOnline ? "Shadertoy" : source;
	RunAsync( s, "Aléatoire (" + label + ")…", [ source, exclude ]( ShaderDoc& doc, std::string& err, bool& user ) {
		user = true;
		std::string path, title;
		if( !PickRandomShader( source, exclude, path, title, err ) )
			return false;
		return LoadShaderFile( path, doc, err );
	} );
}

void RequestApplyDoc( const SharedPtr& s, ShaderDoc doc, bool userInitiated )
{
	std::lock_guard< std::mutex > lock( s->m );
	++s->requestGen;
	ExtractUserParams( doc );
	s->pendingDoc           = std::make_unique< ShaderDoc >( std::move( doc ) );
	s->pendingUserInitiated = userInitiated;
	s->busy                 = true;
	s->status               = "Compilation…";
}

void RequestRestart( const SharedPtr& s )
{
	std::lock_guard< std::mutex > lock( s->m );
	s->restartRequested = true;
}

InstanceSnapshot Snapshot( const SharedPtr& s )
{
	InstanceSnapshot r;
	if( !s )
		return r;
	std::lock_guard< std::mutex > lock( s->m );
	r.id          = s->id;
	r.alive       = s->alive;
	r.busy        = s->busy;
	r.lastBuildOk = s->lastBuildOk;
	r.status      = s->status;
	r.log         = s->log;
	r.current     = s->current;
	r.docVersion  = s->docVersion;
	return r;
}

//------------------------------------------------------------------ registre
static std::mutex s_regMutex;
static std::vector< std::weak_ptr< InstanceShared > > s_instances;

void RegisterInstance( const SharedPtr& s )
{
	std::lock_guard< std::mutex > lock( s_regMutex );
	static int nextId = 1;
	s->id = nextId++;
	s_instances.push_back( s );
}

void UnregisterInstance( const SharedPtr& s )
{
	{
		std::lock_guard< std::mutex > l( s->m );
		s->alive = false;
	}
	std::lock_guard< std::mutex > lock( s_regMutex );
	s_instances.erase( std::remove_if( s_instances.begin(), s_instances.end(), [ & ]( const std::weak_ptr< InstanceShared >& w ) {
						   auto sp = w.lock();
						   return !sp || sp == s;
					   } ),
					   s_instances.end() );
}

std::vector< SharedPtr > ListInstances()
{
	std::lock_guard< std::mutex > lock( s_regMutex );
	std::vector< SharedPtr > r;
	for( auto& w : s_instances )
		if( auto sp = w.lock() )
			r.push_back( sp );
	return r;
}
}//namespace stfx
