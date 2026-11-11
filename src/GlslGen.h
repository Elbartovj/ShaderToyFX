// ShaderToyFX - génération du fragment shader GLSL 4.10 à partir d'une passe Shadertoy
#pragma once
#include <string>
#include "ShaderDoc.h"

namespace stfx
{
// Les erreurs du compilateur sont rapportées avec le numéro de "source" :
// 1 = Common, 2 = code de la passe (grâce aux directives #line).
// glslVersion : 420 (packHalf2x16 & co natifs) ou 410 (+ GL_ARB_shading_language_packing)
std::string BuildFragmentSource( const ShaderDoc& doc, const PassDesc& pass, int glslVersion = 410 );
extern const char* kVertexSource;

// Rend les erreurs du driver plus lisibles : "2(14)" -> "Image, ligne 14"
std::string PrettifyLog( const std::string& log, const std::string& passName );
}//namespace stfx
