// ShaderToyFX - aperçu animé basse résolution pour la bibliothèque du panneau.
// Un thread dédié possède son propre contexte OpenGL (fenêtre cachée) : la compilation et le rendu
// n'interrompent ni le panneau ni Resolume.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stfx
{
namespace Preview
{
// À appeler à chaque image du panneau tant que l'aperçu est affiché. Sans appel pendant ~0,5 s,
// le rendu s'arrête tout seul (onglet fermé, panneau caché…). path vide = rien à afficher.
void Request( const std::string& path, int width, int height );
// Relance le temps du shader à zéro
void Restart();

struct Frame
{
	std::vector< uint8_t > rgba;// lignes de haut en bas
	int w = 0, h = 0;
	uint64_t serial = 0;// change à chaque nouvelle image
	std::string path;   // shader rendu
};
// Copie la dernière image si elle est plus récente que lastSerial
bool TakeFrame( uint64_t lastSerial, Frame& out );

struct Status
{
	std::string path;   // shader affiché
	bool compiling = false;
	std::string error;  // vide si OK
	float fps      = 0;
	float time     = 0;
};
Status GetStatus();

// Arrête le thread et libère le contexte OpenGL (fermeture du panneau)
void Shutdown();
}//namespace Preview
}//namespace stfx
