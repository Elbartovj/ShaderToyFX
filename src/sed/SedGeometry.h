// ShaderToyFX - géométries des projets SHADERed (cube, sphère, quad plein écran…)
#pragma once
#include "SedProject.h"

#include <vector>

namespace stfx
{
namespace sed
{
extern const int GeoVertexCount[ 7 ];
// Crée le VBO + VAO d'une géométrie selon l'input layout de la passe. Retourne le VAO.
unsigned int CreateGeometry( unsigned int& vbo, GeoType type, const float size[ 3 ], const std::vector< InputLayoutItem >& inp );
// VAO pour des sommets au format SHADERed (18 flottants par sommet)
void CreateVAO( unsigned int& vao, unsigned int vbo, const std::vector< InputLayoutItem >& layout, unsigned int instanceVBO = 0,
				const std::vector< int >& instanceFormat = {} );
void AddInstanceAttributes( unsigned int firstIndex, unsigned int instanceVBO, const std::vector< int >& fmt );
int ValueTypeBytes( int valueType );
}//namespace sed
}//namespace stfx
