// ShaderToyFX - import de projets SHADERed : archive .zip (téléchargement shadered.org ou fichier),
// dossier .sprj, shader Godot. Tout est rangé dans Documents\ShaderToyFX\SHADERed\<Titre [id]>\.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace stfx
{
namespace sed
{
// "https://shadered.org/view?s=XXXX", "shadered.org/embed.php?id=XXXX" ou l'id seul -> id (vide si non reconnu)
std::string ParseShaderedId( const std::string& linkOrId );
bool LooksLikeShaderedLink( const std::string& text );

// Télécharge le projet (zip) et l'extrait dans la bibliothèque. Retourne le chemin du .sprj.
bool DownloadShaderedProject( const std::string& id, std::string& sprjPath, std::string& err );

// Extrait une archive de projet dans destDir. Retourne le .sprj trouvé.
bool ExtractProjectZip( const std::vector< uint8_t >& zip, const std::string& destDir, std::string& sprjPath, std::string& err );

// Favori (bookmarklet) pour shadered.org : sur la page d'un shader, télécharge le projet en
// « Titre [shadered-ID].zip » dans les Téléchargements, où ShaderToyFX le récupère.
const char* BookmarkletCode();
// Archives « … [shadered-ID].zip » présentes à la racine des Téléchargements
std::vector< std::string > FindDownloadedProjects();
// Extrait l'archive dans la bibliothèque puis la supprime. false si le fichier n'est pas (encore) lisible.
bool ImportDownloadedProject( const std::string& zipFile, std::string& sprjPath, std::string& title, std::string& err );

// Copie un projet (.sprj et ses fichiers) ou un shader Godot dans la bibliothèque. Retourne le nouveau chemin.
bool ImportIntoLibrary( const std::string& path, std::string& newPath, std::string& err );
}//namespace sed
}//namespace stfx
