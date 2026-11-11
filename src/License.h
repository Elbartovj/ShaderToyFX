// ShaderToyFX - détection (indicative) de la licence d'un shader à partir de ses commentaires
#pragma once
#include <string>

namespace stfx
{
enum class LicenseKind
{
	Commercial,   // CC0, domaine public, MIT, BSD, Apache… : usage commercial autorisé
	Attribution,  // CC BY / CC BY-SA : commercial autorisé en créditant l'auteur
	NonCommercial,// CC BY-NC(-SA), licence par défaut de Shadertoy
	Restricted,   // "all rights reserved", "do not use"…
	Unknown       // rien d'exploitable trouvé : à vérifier
};

struct LicenseInfo
{
	LicenseKind kind = LicenseKind::Unknown;
	std::string label;   // ex. "CC BY-NC-SA 3.0 (défaut Shadertoy)"
	std::string evidence;// ligne du code où la licence a été trouvée
	std::string author;
};

// text = description + code de toutes les passes ; fromShadertoy = vient du site (licence par défaut applicable)
LicenseInfo DetectLicense( const std::string& text, bool fromShadertoy );

// Analyse un fichier .json/.glsl (résultat mis en cache par chemin + date de modification)
LicenseInfo LicenseForFile( const std::string& path );

// Analyse un JSON Shadertoy normalisé
LicenseInfo LicenseForJson( const std::string& normalizedJson );

const char* LicenseShortName( LicenseKind k );// "Commercial OK", "Non commercial", …
bool LicenseAllowsCommercial( LicenseKind k );
}//namespace stfx
