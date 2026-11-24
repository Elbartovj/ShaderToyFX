// ShaderToyFX - shaders Godot (canvas_item) : traduction en GLSL via le transcompileur extrait de Godot
// (GodotShaderTranscompiler, MIT, (c) dfranx ; code Godot MIT).
#pragma once
#include "SedProject.h"

#include <string>
#include <vector>

namespace stfx
{
namespace sed
{
struct GodotUniform
{
	std::string name;
	bool isSampler = false;
	bool isInt     = false;
	bool isBool    = false;
	bool isColor   = false;
	int components = 1;
	std::vector< float > defaults;
	bool hasRange = false;
	float rangeMin = 0, rangeMax = 1;
	bool hintBlack = false;
};

struct GodotShader
{
	std::string vertex, fragment;
	std::vector< GodotUniform > uniforms;
	int blendMode      = 0;// 0 mix, 1 add, 2 sub, 3 mul, 4 premult, 5 désactivé
	bool screenTexture = false;
	bool usesTime      = false;
};

// Code Godot (3.x, ou 4.x pour les syntaxes courantes) -> GLSL
bool TranscompileGodot( const std::string& source, GodotShader& out, std::string& err );

// Fichier .gdshader / .shader autonome -> projet d'une passe : un sprite plein écran dont
// la texture (TEXTURE) est l'entrée Resolume.
bool MakeGodotProject( const std::string& path, Project& out, std::string& err );
}//namespace sed
}//namespace stfx
