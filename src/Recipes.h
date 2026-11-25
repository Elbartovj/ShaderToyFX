// Shader Loader - recettes d'amélioration prêtes à l'emploi (panneau, extension Claude Desktop)
#pragma once

namespace stfx
{
namespace recipes
{
// Adapter un effet pour qu'il se diffuse / croisse en suivant la texture d'entrée de Resolume
inline const char* kTextureDiffusion =
	"Adapte cet effet pour qu'il se diffuse en suivant la texture d'entrée de Resolume, en gardant son caractère :\n"
	"1. Branche l'entrée vidéo sur chaque passe de simulation qui en a besoin (update_shader accepte \"channels\": {\"iChannel2\": \"input\"} "
	"par passe, et peut ajouter des passes Buffer A à D si l'effet n'a pas de simulation à faire évoluer).\n"
	"2. La texture guide la diffusion : nourriture = luminosité ou contours (Sobel), mélange réglable EDGES, attirance réglable FOLLOW. "
	"Elle doit orienter doucement (biais de direction quand rien d'autre ne décide), jamais écraser la dynamique d'origine : "
	"sinon la croissance s'effondre en quelques lignes.\n"
	"3. Les couleurs viennent de la texture : échantillonnées là où la matière se dépose et gardées dans le buffer, "
	"avec un mélange LIVE_COLOR vers la vidéo en direct.\n"
	"4. S'il y a des points de départ, rends réglables leur nombre et leurs positions (0..1), et relance la simulation quand ils changent "
	"(empreinte des réglages gardée dans un pixel du buffer).\n"
	"5. Garde les réglages d'origine ; expose les nouveaux en sliders MAJUSCULES avec leur plage en commentaire (// min max).\n"
	"6. Vérifie avec capture_output sur un clip qui joue une vraie image (avec des contours) ; corrige si l'image est noire, figée ou vide.";

inline const char* kExportFfgl =
	"Quand le rendu est bon, exporte-le directement en plugin FFGL avec export_plugin (nom court, 16 caractères au plus), "
	"puis dis-moi de redémarrer Resolume.";
}//namespace recipes
}//namespace stfx
