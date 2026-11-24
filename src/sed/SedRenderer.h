// ShaderToyFX - exécution d'un projet SHADERed dans le contexte OpenGL de Resolume.
// Reprend la boucle de RenderEngine::Render de SHADERed : passes, render textures, états de rendu,
// variables système / fonctions, géométries, modèles .obj, passes compute et passes Godot (canvas_item).
#pragma once
#include "SedProject.h"
#include "SedCompiler.h"

#include <GL/glew.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace stfx
{
struct UserParam;
namespace sed
{
struct Frame
{
	int width = 1, height = 1;
	float time = 0, timeDelta = 0;
	int frame = 0;
	float mouse[ 4 ] = {};// pixels (comme iMouse) : x, y, clic x, clic y
	const float* user = nullptr;// valeurs réelles des sliders (ShaderDoc::params)
	int userCount = 0;
	GLuint inputTex = 0;// entrée Resolume déjà recadrée à la taille de sortie
	int inputW = 0, inputH = 0;
	// remplacement des textures du projet (ordre de Project::objects, textures images seulement)
	int texMode[ 4 ] = {};// 0 = celle du projet, 1 = entrée Resolume, 2 = image choisie, 3 = audio, 4 = noir
	GLuint texImage[ 4 ] = {};
	GLuint audioTex = 0;// 512x2 (spectre / forme d'onde)
	// remplacement des modèles .obj du projet (ordre d'apparition) ; vide = modèle d'origine
	std::string modelPath[ 2 ];
	bool modelFit = true;// recale le modèle choisi sur la taille et la position de l'original
};

class Renderer
{
public:
	~Renderer();
	// Compile tout le projet. false = log contient les erreurs (l'ancien projet reste actif).
	bool Build( const Project& project, std::string& log );
	void Render( const Frame& f );
	void UpdateModels( const Frame& f );
	void Release();
	void ResetTime();

	GLuint OutputTexture() const { return windowColor; }
	int OutputWidth() const { return winW; }
	int OutputHeight() const { return winH; }
	bool UsesAlpha() const { return proj && proj->useAlpha; }
	bool Ready() const { return proj != nullptr; }
	// textures images du projet (pour les libellés du panneau)
	const std::vector< std::string >& ReplaceableTextures() const { return replaceable; }
	// modèles .obj distincts du projet, dans l'ordre (2 au plus sont remplaçables)
	static std::vector< std::string > ModelFiles( const Project& p );

private:
	struct RT
	{
		std::string name;
		GLuint color = 0, depth = 0;
		int w = 0, h = 0;
		const Object* obj = nullptr;
	};
	struct GpuObject
	{
		std::string name;
		ObjectKind kind = ObjectKind::Texture;
		GLuint tex      = 0;
		GLenum target   = GL_TEXTURE_2D;
		GLuint buffer   = 0;// buffer
		int w = 0, h = 0, d = 0;
		int replaceIndex = -1;// 0..3 si remplaçable par un paramètre de l'effet
		std::vector< int > bufferFormat;
		int bufferSize = 0;
		GLenum format  = GL_RGBA8;
	};
	struct GpuItem
	{
		const Item* item = nullptr;
		GLuint vao = 0, vbo = 0;
		int vertexCount = 0;
		GLuint instanceBuffer = 0;
		std::vector< int > instanceFormat;
		GLuint spriteTex = 0;
		int modelIndex   = -1;// 0..1 si remplaçable par un paramètre de l'effet
		std::string modelKey;// fichier actuellement chargé (+ recalage)
		float boxMin[ 3 ] = {}, boxMax[ 3 ] = {};
		bool hasBox = false;
	};
	struct GpuVar
	{
		const Variable* var = nullptr;
		GLint loc           = -1;
		float data[ 16 ]    = {};
		float prev[ 16 ]    = {};
	};
	struct GpuPass
	{
		const Pass* pass = nullptr;
		GLuint program   = 0;
		GLuint fbo       = 0;
		std::vector< GpuVar > vars;
		std::vector< GpuItem > items;
		std::vector< std::string > samplers;// GLSL : ordre des samplers -> unités
		bool glslSamplers = false;
		std::vector< int > rts;// index dans rts (-1 = fenêtre)
		GLuint depth = 0;
		int rtW = 0, rtH = 0;
		// Godot
		GLint gdProj = -1, gdWorld = -1, gdTime = -1, gdPixel = -1, gdTexture = -1, gdScreen = -1, gdTexPixel = -1, gdNormalTex = -1;
		int gdBlend = 0;
		bool gdScreenTexture = false;
		struct GdUniform
		{
			std::string name;
			GLint loc = -1;
			int type  = 0;// 0 float…, 10 sampler
			int count = 1;
			std::vector< float > value;
			GLuint tex = 0;
			int unit   = 0;
			int userParam = -1;
		};
		std::vector< GdUniform > gdUniforms;
	};

	bool BuildPass( const Pass& p, GpuPass& gp, std::string& log );
	bool BuildGodotPass( const Pass& p, GpuPass& gp, std::string& log );
	void CreateObjects();
	void ResizeTargets( int w, int h );
	void UpdateFbo( GpuPass& gp );
	void ComputeVar( GpuPass& gp, GpuVar& v, const GpuItem* item, const Frame& f );
	void UploadVar( const GpuVar& v );
	GLuint ObjectTexture( const std::string& name, GLenum& target, const Frame& f );
	void BindTextures( GpuPass& gp, const Frame& f );
	void DrawGodot( GpuPass& gp, const Frame& f );
	void DestroyAll();

	std::shared_ptr< Project > proj;
	std::vector< GpuPass > passes;
	std::vector< GpuObject > objects;
	std::vector< RT > rts;
	std::vector< std::string > replaceable;
	GLuint windowFbo = 0, windowColor = 0, windowDepth = 0;
	int winW = 0, winH = 0;
	GLuint white = 0, black = 0, keyboard = 0;
	GLuint screenCopy = 0, screenCopyFbo = 0;// SCREEN_TEXTURE (Godot)
	// entrée Resolume retournée (convention Godot : UV (0,0) en haut à gauche)
	GLuint FlippedInput( const Frame& f );
	GLuint flipTex = 0, flipFbo = 0, flipReadFbo = 0;
	int flipW = 0, flipH = 0, flipFrame = -1;
	int screenW = 0, screenH = 0;
	std::map< std::string, std::vector< float > > geoTransforms, prevGeoTransforms;
	float prevViewport[ 2 ] = { 1, 1 };
};
}//namespace sed
}//namespace stfx
