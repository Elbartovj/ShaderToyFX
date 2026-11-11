// ShaderToyFX - état partagé d'une instance de plugin (thread-safe), utilisé par le panneau
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include "ShaderDoc.h"

namespace stfx
{
struct InstanceShared
{
	std::mutex m;
	int id    = 0;
	bool alive = true;

	// requête en attente pour le thread de rendu
	uint64_t requestGen = 0;
	std::unique_ptr< ShaderDoc > pendingDoc;
	bool pendingUserInitiated = false;// => met à jour le paramètre "Shader" et les valeurs par défaut
	bool restartRequested     = false;

	// état exposé au panneau
	bool busy = false;
	std::string status;
	std::string log;     // erreurs de compilation / avertissements
	bool lastBuildOk = true;
	std::shared_ptr< const ShaderDoc > current;
	uint64_t docVersion = 0;
};
using SharedPtr = std::shared_ptr< InstanceShared >;

struct InstanceSnapshot
{
	int id = 0;
	bool alive = false, busy = false, lastBuildOk = true;
	std::string status, log;
	std::shared_ptr< const ShaderDoc > current;
	uint64_t docVersion = 0;
};

// Requêtes asynchrones (téléchargement/lecture sur un thread de travail)
void RequestLoadFile( const SharedPtr& s, const std::string& path, bool userInitiated );
void RequestLoadShadertoy( const SharedPtr& s, const std::string& id );
void RequestRandom( const SharedPtr& s );
void RequestApplyDoc( const SharedPtr& s, ShaderDoc doc, bool userInitiated );
void RequestRestart( const SharedPtr& s );
InstanceSnapshot Snapshot( const SharedPtr& s );

// Registre global des instances vivantes
void RegisterInstance( const SharedPtr& s );
void UnregisterInstance( const SharedPtr& s );
std::vector< SharedPtr > ListInstances();
}//namespace stfx
