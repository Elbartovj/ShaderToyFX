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
	bool saveDefaultsRequested = false;// valeurs actuelles des sliders -> valeurs par défaut du fichier

	// état exposé au panneau
	bool busy = false;
	std::string status;
	std::string log;     // erreurs de compilation / avertissements
	bool lastBuildOk = true;
	std::shared_ptr< const ShaderDoc > current;
	uint64_t docVersion = 0;
	uint64_t buildSerial = 0;// +1 à chaque compilation terminée (réussie ou non)

	// capture de l'image rendue (pont Claude Desktop) : demandée par captureRequested, faite par le thread de rendu
	bool captureRequested = false;
	std::vector< uint8_t > captureRgba;// lignes de haut en bas
	int captureW = 0, captureH = 0;
	uint64_t captureSerial = 0;
	std::string moduleKind;// "effect" ou "source"

	// export en plugin FFGL autonome (fait par le thread de rendu : il connaît les valeurs des sliders)
	bool exportRequested = false;
	std::string exportName, exportPath;
	bool exportSource = false, exportUseCurrent = true;
	std::string exportResult;// message final (succès ou erreur)
	bool exportOk = false;
	uint64_t exportSerial = 0;
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
// Projet shadered.org (id ou lien) : téléchargé dans Documents\ShaderToyFX\SHADERed puis chargé
void RequestLoadShadered( const SharedPtr& s, const std::string& id );
// Projet .sprj / .zip / shader Godot ouvert depuis le disque : copié dans la bibliothèque puis chargé
void RequestImportShadered( const SharedPtr& s, const std::string& path );
// source : voir PickRandomShader ("" = bibliothèque, dossier relatif, ou kRandomOnline)
void RequestRandom( const SharedPtr& s, const std::string& source );
void RequestApplyDoc( const SharedPtr& s, ShaderDoc doc, bool userInitiated );
void RequestRestart( const SharedPtr& s );
void RequestSaveDefaults( const SharedPtr& s );
void RequestExportPlugin( const SharedPtr& s, const std::string& name, const std::string& path, bool source, bool useCurrentValues );
InstanceSnapshot Snapshot( const SharedPtr& s );

// Registre global des instances vivantes
void PinModuleForThreads();
void RegisterInstance( const SharedPtr& s );
void UnregisterInstance( const SharedPtr& s );
std::vector< SharedPtr > ListInstances();
// instance ciblée par le panneau (pour le pont Claude Desktop)
void SetFocusedInstance( const SharedPtr& s );
SharedPtr FocusedInstance();
}//namespace stfx
