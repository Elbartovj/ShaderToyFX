// ShaderToyFX - modèle CPU d'un shader Shadertoy (passes, entrées, médias)
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace stfx
{
enum class ChannelKind
{
	None,
	Input,   // la texture d'entrée Resolume (clip/couche sous l'effet)
	Audio,   // FFT Resolume -> texture 512x2 façon Shadertoy
	Keyboard,// texture 256x3 vide
	Buffer,  // Buffer A..D
	Texture, // image 2D
	Cubemap, // 6 faces
	Volume   // texture 3D (.bin Shadertoy)
};

enum class Filter
{
	Nearest,
	Linear,
	Mipmap
};

struct ImageData
{
	int width  = 0;
	int height = 0;
	int depth  = 1;// >1 pour les volumes
	int faces  = 1;// 6 pour les cubemaps
	int comps  = 4;// 1 ou 4 composantes
	bool isFloat = false;
	std::vector< uint8_t > data;// faces contiguës
};

struct ChannelDesc
{
	ChannelKind kind = ChannelKind::None;
	int buffer       = -1;// index du buffer (0..3) si kind == Buffer
	Filter filter    = Filter::Linear;
	bool repeat      = false;
	bool vflip       = true;
	std::string src; // chemin média Shadertoy (/media/a/xxx.png) ou fichier local
	std::shared_ptr< const ImageData > image;
};

struct PassDesc
{
	std::string name;     // "Image", "Buffer A", ...
	bool isImage    = false;
	int bufferIndex = -1; // 0..3 pour Buffer A..D
	int jsonIndex   = -1; // index dans renderpass[] du JSON d'origine
	std::string code;
	ChannelDesc ch[ 4 ];
};

// Paramètre exposé dans Resolume (slider). Sources :
//  - uniform float/int déclaré par l'utilisateur ("uniform float vitesse; // 0 5 1")
//  - #define NOM 1.5 et const float/int/vecN globaux, convertis en uniforms à la compilation
enum class ParamSource
{
	Uniform,
	Define,
	Const
};

struct UserParam
{
	std::string name;   // nom affiché ("SPEED", "COLOR.g")
	std::string uniform;// identifiant GLSL
	ParamSource source = ParamSource::Uniform;
	int comp    = -1;// composante 0..3 pour un vecN, -1 = scalaire
	int vecSize = 1;
	bool isInt  = false;
	float minV  = 0.0f;
	float maxV  = 1.0f;
	float def   = 0.5f;
};

static const int kMaxUserParams = 24;

struct ShaderDoc
{
	std::string id;// id Shadertoy (6 caractères) si connu
	std::string title;
	std::string author;
	std::string description;
	std::string sourcePath;// fichier d'où vient le shader
	std::string common;
	std::vector< PassDesc > passes;// buffers dans l'ordre d'exécution, puis Image en dernier
	std::vector< UserParam > params;
	std::vector< std::string > warnings;
	std::string rawJson;// JSON Shadertoy normalisé (vide pour un .glsl)

	const PassDesc* ImagePass() const;
	int BufferCount() const;
};

// Analyse un texte (JSON Shadertoy ou GLSL "mainImage") -> ShaderDoc (sans médias)
bool ParseShaderSource( const std::string& text, const std::string& pathHint, ShaderDoc& doc, std::string& err );

// Charge les images/cubemaps/volumes référencés (cache local, téléchargement si absent)
void ResolveMedia( ShaderDoc& doc, bool allowDownload );

// Lecture + analyse + médias
bool LoadShaderFile( const std::string& path, ShaderDoc& doc, std::string& err );

// Détecte les paramètres réglables (uniforms, #define et const numériques) -> doc.params
void ExtractUserParams( ShaderDoc& doc );
// Réécrit le code pour transformer les #define / const retenus en uniforms (numéros de ligne conservés)
std::string ApplyParamRewrites( const std::string& code, const std::vector< UserParam >& params );

// Re-sérialise le shader (après édition) : JSON si d'origine JSON, sinon GLSL
std::string SerializeDoc( const ShaderDoc& doc, std::string& extension );

// Le shader par défaut (passthrough pour l'effet, dégradé pour la source)
ShaderDoc DefaultDoc( bool isSource );

// Normalise une réponse d'API / un export Shadertoy en objet "Shader" JSON (texte)
bool NormalizeShadertoyJson( const std::string& text, std::string& normalized, std::string& err );
}//namespace stfx
