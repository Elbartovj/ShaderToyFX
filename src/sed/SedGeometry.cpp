// Géométries de SHADERed : code repris de src/SHADERed/Engine/GeometryFactory.cpp
// (licence MIT, (c) dfranx) pour des sommets identiques (positions, normales, UV, tangentes).
#include "SedGeometry.h"

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstring>

namespace stfx
{
namespace sed
{
const int GeoVertexCount[ 7 ] = {
	36,			 /* CUBE */
	6,			 /* RECTANGLE / SCREENQUAD */
	32 * 3,		 /* CIRCLE */
	3,			 /* TRIANGLE */
	20 * 20 * 6, /* SPHERE */
	6,			 /* PLANE */
	6,			 /* SCREEQUADNDC */
};

// SHADERed : gl::CreateVAO — sommets de 18 flottants, un attribut par entrée de l'input layout
static size_t LayoutSize( int v )
{
	static const int sz[] = { 3, 3, 2, 3, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4 };
	return v >= 0 && v < 14 ? sz[ v ] : 0;
}
static size_t LayoutOffset( int v )
{
	static const int off[] = { 0, 3, 6, 8, 11, 14 };
	return v >= 0 && v < 6 ? off[ v ] : 0;
}
void CreateVAO( unsigned int& vao, unsigned int vbo, const std::vector< InputLayoutItem >& layout, unsigned int instanceVBO,
				const std::vector< int >& instanceFormat )
{
	if( vao )
		glDeleteVertexArrays( 1, &vao );
	glGenVertexArrays( 1, &vao );
	glBindVertexArray( vao );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	GLuint index = 0, layOffset = 0;
	for( auto& it : layout )
	{
		GLint size    = (GLint)LayoutSize( it.value );
		size_t offset = LayoutOffset( it.value ) * sizeof( GLfloat );
		if( it.value >= 6 )
			offset = layOffset;
		glVertexAttribPointer( index, size, GL_FLOAT, GL_FALSE, 18 * sizeof( float ), (void*)offset );
		glEnableVertexAttribArray( index );
		++index;
		layOffset += size;
	}
	AddInstanceAttributes( index, instanceVBO, instanceFormat );
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
}

// Attributs par instance (buffer d'instances SHADERed : un attribut par type du format)
void AddInstanceAttributes( unsigned int firstIndex, unsigned int instanceVBO, const std::vector< int >& fmt )
{
	if( !instanceVBO || fmt.empty() )
		return;
	int stride = 0;
	for( int t : fmt )
		stride += ValueTypeBytes( t );
	glBindBuffer( GL_ARRAY_BUFFER, instanceVBO );
	int offset = 0;
	GLuint index = firstIndex;
	for( int t : fmt )
	{
		GLint cols  = 1;
		GLenum type = GL_FLOAT;
		switch( t )
		{
		case 0: cols = 1; type = GL_BYTE; break;
		case 1: cols = 2; type = GL_BYTE; break;
		case 2: cols = 3; type = GL_BYTE; break;
		case 3: cols = 4; type = GL_BYTE; break;
		case 4: cols = 1; type = GL_INT; break;
		case 5: cols = 2; type = GL_INT; break;
		case 6: cols = 3; type = GL_INT; break;
		case 7: cols = 4; type = GL_INT; break;
		case 8: cols = 1; break;
		case 9: cols = 2; break;
		case 10: cols = 3; break;
		case 11: cols = 4; break;
		case 12: cols = 2; break;
		case 13: cols = 3; break;
		case 14: cols = 4; break;
		}
		glVertexAttribPointer( index, cols, type, GL_FALSE, stride, (void*)(intptr_t)offset );
		glEnableVertexAttribArray( index );
		glVertexAttribDivisor( index, 1 );
		offset += ValueTypeBytes( t );
		++index;
	}
}

int ValueTypeBytes( int t )
{
	static const int bytes[] = { 1, 2, 3, 4, 4, 8, 12, 16, 4, 8, 12, 16, 16, 36, 64 };
	return t >= 0 && t < 15 ? bytes[ t ] : 4;
}

static void generateFace(GLfloat* verts, float radius, float sx, float sy, int x, int y)
		{
			float phi = y * sy;
			float theta = x * sx;
			// clang-format off
			GLfloat pt1[18] = {
				(radius * sin(phi) * cos(theta)),
				(radius * sin(phi) * sin(theta)),
				(radius * cos(phi)),
				0,0,0,
				theta / glm::two_pi<float>(), phi / glm::pi<float>(),
				0,0,0, 0,0,0, 1,1,1,1
			};
			// clang-format on
			glm::vec3 n = glm::normalize(glm::vec3(pt1[0], pt1[1], pt1[2]));
			pt1[3] = n.x;
			pt1[4] = n.y;
			pt1[5] = n.z;
			memcpy(verts + 2 * 18, pt1, sizeof(GLfloat) * 18);

			phi = y * sy;
			theta = (x + 1) * sx;
			// clang-format off
			GLfloat pt2[18] = {
				(radius * sin(phi) * cos(theta)),
				(radius * sin(phi) * sin(theta)),
				(radius * cos(phi)),
				0,0,0,
				theta / glm::two_pi<float>(), phi / glm::pi<float>(),
				0,0,0, 0,0,0, 1,1,1,1
			};
			// clang-format on
			n = glm::normalize(glm::vec3(pt2[0], pt2[1], pt2[2]));
			pt2[3] = n.x;
			pt2[4] = n.y;
			pt2[5] = n.z;
			memcpy(verts + 1 * 18, pt2, sizeof(GLfloat) * 18);

			phi = (y + 1) * sy;
			theta = x * sx;
			// clang-format off
			GLfloat pt3[18] = {
				(radius * sin(phi) * cos(theta)),
				(radius * sin(phi) * sin(theta)),
				(radius * cos(phi)),
				0,0,0,
				theta / glm::two_pi<float>(), phi / glm::pi<float>(),
				0,0,0, 0,0,0, 1,1,1,1
			};
			// clang-format on
			n = glm::normalize(glm::vec3(pt3[0], pt3[1], pt3[2]));
			pt3[3] = n.x;
			pt3[4] = n.y;
			pt3[5] = n.z;
			memcpy(verts + 0 * 18, pt3, sizeof(GLfloat) * 18);

			phi = y * sy;
			theta = (x + 1) * sx;
			// clang-format off
			GLfloat pt4[18] = {
				(radius * sin(phi) * cos(theta)),
				(radius * sin(phi) * sin(theta)),
				(radius * cos(phi)),
				0,0,0,
				theta / glm::two_pi<float>(), phi / glm::pi<float>(),
				0,0,0, 0,0,0, 1,1,1,1
			};
			// clang-format on
			n = glm::normalize(glm::vec3(pt4[0], pt4[1], pt4[2]));
			pt4[3] = n.x;
			pt4[4] = n.y;
			pt4[5] = n.z;
			memcpy(verts + 3 * 18, pt4, sizeof(GLfloat) * 18);

			phi = (y + 1) * sy;
			theta = x * sx;
			// clang-format off
			GLfloat pt5[18] = {
				(radius * sin(phi) * cos(theta)),
				(radius * sin(phi) * sin(theta)),
				(radius * cos(phi)),
				0,0,0,
				theta / glm::two_pi<float>(), phi / glm::pi<float>(),
				0,0,0, 0,0,0, 1,1,1,1
			};
			// clang-format on
			n = glm::normalize(glm::vec3(pt5[0], pt5[1], pt5[2]));
			pt5[3] = n.x;
			pt5[4] = n.y;
			pt5[5] = n.z;
			memcpy(verts + 4 * 18, pt5, sizeof(GLfloat) * 18);

			phi = (y + 1) * sy;
			theta = (x + 1) * sx;
			// clang-format off
			GLfloat pt6[18] = {
				(radius * sin(phi) * cos(theta)),
				(radius * sin(phi) * sin(theta)),
				(radius * cos(phi)),
				0,0,0,
				theta / glm::two_pi<float>(), phi / glm::pi<float>(),
				0,0,0, 0,0,0, 1,1,1,1
			};
			// clang-format on
			n = glm::normalize(glm::vec3(pt6[0], pt6[1], pt6[2]));
			pt6[3] = n.x;
			pt6[4] = n.y;
			pt6[5] = n.z;
			memcpy(verts + 5 * 18, pt6, sizeof(GLfloat) * 18);
		}
		static void calcBinormalAndTangents(GLfloat* verts, GLuint vertCount)
		{
			/* http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-13-normal-mapping/ */
			for (GLuint i = 0; i < vertCount; i += 3) {
				GLuint j0 = i * 18;
				GLuint j1 = (i + 1) * 18;
				GLuint j2 = (i + 2) * 18;

				// Shortcuts for vertices
				glm::vec3 v0 = glm::vec3(verts[j0 + 0], verts[j0 + 1], verts[j0 + 2]);
				glm::vec3 v1 = glm::vec3(verts[j1 + 0], verts[j1 + 1], verts[j1 + 2]);
				glm::vec3 v2 = glm::vec3(verts[j2 + 0], verts[j2 + 1], verts[j2 + 2]);

				// Shortcuts for normals
				glm::vec3 n0 = glm::vec3(verts[j0 + 3], verts[j0 + 4], verts[j0 + 5]);
				glm::vec3 n1 = glm::vec3(verts[j1 + 3], verts[j1 + 4], verts[j1 + 5]);
				glm::vec3 n2 = glm::vec3(verts[j2 + 3], verts[j2 + 4], verts[j2 + 5]);

				// Shortcuts for UVs
				glm::vec2 uv0 = glm::vec2(verts[j0 + 6], verts[j0 + 7]);
				glm::vec2 uv1 = glm::vec2(verts[j1 + 6], verts[j1 + 7]);
				glm::vec2 uv2 = glm::vec2(verts[j2 + 6], verts[j2 + 7]);

				// Edges of the triangle : position delta
				glm::vec3 deltaPos1 = v1 - v0;
				glm::vec3 deltaPos2 = v2 - v0;

				// UV delta
				glm::vec2 deltaUV1 = uv1 - uv0;
				glm::vec2 deltaUV2 = uv2 - uv0;

				float r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
				glm::vec3 t = (deltaPos1 * deltaUV2.y - deltaPos2 * deltaUV1.y) * r;
				glm::vec3 b = (deltaPos2 * deltaUV1.x - deltaPos1 * deltaUV2.x) * r;

				// Gram-Schmidt orthogonalize
				glm::vec3 t0 = glm::normalize(t - n0 * glm::dot(n0, t));
				glm::vec3 t1 = glm::normalize(t - n1 * glm::dot(n1, t));
				glm::vec3 t2 = glm::normalize(t - n2 * glm::dot(n2, t));

				// Calculate handedness
				if (glm::dot(glm::cross(n0, t0), b) < 0.0f)
					t0 = t0 * -1.0f;
				if (glm::dot(glm::cross(n1, t1), b) < 0.0f)
					t1 = t1 * -1.0f;
				if (glm::dot(glm::cross(n2, t2), b) < 0.0f)
					t2 = t2 * -1.0f;

				// Set the same tangent for all three vertices of the triangle.
				// They will be merged later, in vboindexer.cpp
				// clang-format off
				verts[j0 + 8] = t0.x; verts[j0 + 9] = t0.y; verts[j0 + 10] = t0.z;
				verts[j1 + 8] = t1.x; verts[j1 + 9] = t1.y; verts[j1 + 10] = t1.z;
				verts[j2 + 8] = t2.x; verts[j2 + 9] = t2.y; verts[j2 + 10] = t2.z;

				// Same thing for bitangents
				verts[j0 + 11] = b.x; verts[j0 + 12] = b.y; verts[j0 + 13] = b.z;
				verts[j1 + 11] = b.x; verts[j1 + 12] = b.y; verts[j1 + 13] = b.z;
				verts[j2 + 11] = b.x; verts[j2 + 12] = b.y; verts[j2 + 13] = b.z;
				// clang-format on
			}
		}

		static unsigned int CreateCube(unsigned int& vbo, float sx, float sy, float sz, const std::vector< InputLayoutItem >& inp)
		{
			float halfX = sx / 2.0f;
			float halfY = sy / 2.0f;
			float halfZ = sz / 2.0f;

			// clang-format off
			// vec3, vec3, vec2, vec3, vec3, vec4
			GLfloat cubeData[] = {
				// front face
				-halfX, -halfY, halfZ, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, -halfY, halfZ, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, halfZ, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, halfZ, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, halfZ, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, halfY, halfZ, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,

				// back face
				halfX, halfY, -halfZ, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, -halfY, -halfZ, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, -halfZ, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, halfY, -halfZ, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, -halfZ, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, -halfZ, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,

				// right face
				halfX, -halfY, halfZ, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, -halfY, -halfZ, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, -halfZ, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, -halfY, halfZ, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, -halfZ, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, halfZ, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,

				// left face
				-halfX, halfY, -halfZ, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, -halfZ, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, halfZ, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, halfY, halfZ, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, halfY, -halfZ, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, halfZ, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,

				// top face
				-halfX, halfY, halfZ, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, halfZ, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, -halfZ, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, halfY, halfZ, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, -halfZ, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, halfY, -halfZ, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,

				// bottom face
				halfX, -halfY, -halfZ, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, -halfY, halfZ, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, halfZ, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, -halfZ, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, -halfY, -halfZ, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, halfZ, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
			};
			// clang-format on

			calcBinormalAndTangents(&cubeData[0], 36);

			// create vbo
			glGenBuffers(1, &vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, 36 * 18 * sizeof(GLfloat), cubeData, GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			GLuint vao = 0;
			CreateVAO(vao, vbo, inp);

			return vao;
		}
		static unsigned int CreateCircle(unsigned int& vbo, float rx, float ry, const std::vector< InputLayoutItem >& inp)
		{
			const int numPoints = 32 * 3;
			const int numSegs = numPoints / 3;

			GLfloat circleData[numPoints * 18];

			float step = glm::two_pi<float>() / numSegs;

			for (int i = 0; i < numSegs; i++) {
				int j = i * 3 * 18;
				GLfloat* ptrData = &circleData[j];

				float xVal1 = sin(step * i);
				float yVal1 = cos(step * i);
				float xVal2 = sin(step * (i + 1));
				float yVal2 = cos(step * (i + 1));

				GLfloat point1[18] = { 0, 0, 0, 0, 0, -1, 0.5f, 0.5f, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 };
				GLfloat point2[18] = { xVal1 * rx, yVal1 * ry, 0, 0, 0, -1, xVal1 * 0.5f + 0.5f, yVal1 * 0.5f + 0.5f, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 };
				GLfloat point3[18] = { xVal2 * rx, yVal2 * ry, 0, 0, 0, -1, xVal2 * 0.5f + 0.5f, yVal2 * 0.5f + 0.5f, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 };

				memcpy(ptrData + 0, point1, 18 * sizeof(GLfloat));
				memcpy(ptrData + 18, point2, 18 * sizeof(GLfloat));
				memcpy(ptrData + 36, point3, 18 * sizeof(GLfloat));
			}

			calcBinormalAndTangents(&circleData[0], numPoints);

			// create vbo
			glGenBuffers(1, &vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, numPoints * 18 * sizeof(GLfloat), circleData, GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			GLuint vao = 0;
			CreateVAO(vao, vbo, inp);

			return vao;
		}
		static unsigned int CreatePlane(unsigned int& vbo, float sx, float sy, const std::vector< InputLayoutItem >& inp)
		{
			float halfX = sx / 2;
			float halfY = sy / 2;

			// clang-format off
			GLfloat planeData[] = {
				halfX, halfY, 0, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, -halfY, 0, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, 0, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, halfY, 0, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				halfX, halfY, 0, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
				-halfX, -halfY, 0, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
			};
			// clang-format on

			calcBinormalAndTangents(&planeData[0], 6);

			// create vbo
			glGenBuffers(1, &vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, 6 * 18 * sizeof(GLfloat), planeData, GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			GLuint vao = 0;
			CreateVAO(vao, vbo, inp);

			return vao;
		}
		static unsigned int CreateSphere(unsigned int& vbo, float r, const std::vector< InputLayoutItem >& inp)
		{
			const size_t stackCount = 20;
			const size_t sliceCount = 20;

			const size_t count = sliceCount * stackCount * 6;
			std::vector< GLfloat > sphereStore( count * 18 );
			GLfloat* sphereData = sphereStore.data();

			const float stepY = glm::pi<float>() / stackCount;
			const float stepX = glm::two_pi<float>() / sliceCount;

			for (int i = 0; i < stackCount; i++) {
				float phi = i * stepY;
				for (int j = 0; j < sliceCount; j++) {
					float theta = j * stepX;
					size_t index = (i * sliceCount + j) * 6;

					generateFace(sphereData + index * 18, r, stepX, stepY, j, i);
				}
			}

			calcBinormalAndTangents(&sphereData[0], count);

			// create vbo
			glGenBuffers(1, &vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, count * 18 * sizeof(GLfloat), sphereData, GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			GLuint vao = 0;
			CreateVAO(vao, vbo, inp);

			return vao;
		}
		static unsigned int CreateTriangle(unsigned int& vbo, float s, const std::vector< InputLayoutItem >& inp)
		{
			float rightOffs = s / tan(glm::radians(30.0f));
			// clang-format off
			GLfloat triData[] = {
				-rightOffs, -s, 0, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				rightOffs, -s, 0, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0,0,0, 0,0,0, 1,1,1,1,
				0, s, 0, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f, 0,0,0, 0,0,0, 1,1,1,1,
			};
			// clang-format on

			calcBinormalAndTangents(&triData[0], 3);

			// create vbo
			glGenBuffers(1, &vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, 3 * 18 * sizeof(GLfloat), triData, GL_STATIC_DRAW);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			GLuint vao = 0;
			CreateVAO(vao, vbo, inp);

			return vao;
		}
		static unsigned int CreateScreenQuadNDC(unsigned int& vbo, const std::vector< InputLayoutItem >& inp)
		{
			// clang-format off
			GLfloat sqData[] = {
				-1, -1, 0.0f, 0.0f,
				1, -1, 1.0f, 0.0f,
				1, 1, 1.0f, 1.0f,
				-1, -1, 0.0f, 0.0f,
				1, 1, 1.0f, 1.0f,
				-1, 1, 0.0f, 1.0f,
			};
			// clang-format on

			GLuint vao;

			// create vao
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);

			// create vbo
			glGenBuffers(1, &vbo);
			glBindBuffer(GL_ARRAY_BUFFER, vbo);

			// vbo data
			glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(GLfloat), sqData, GL_STATIC_DRAW);

			// vertex positions
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
			glEnableVertexAttribArray(0);

			// vertex texture coords
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
			glEnableVertexAttribArray(1);

			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			return vao;
		}
	
unsigned int CreateGeometry( unsigned int& vbo, GeoType type, const float size[ 3 ], const std::vector< InputLayoutItem >& inp )
{
	switch( type )
	{
	case GeoType::Cube: return CreateCube( vbo, size[ 0 ], size[ 1 ], size[ 2 ], inp );
	case GeoType::Circle: return CreateCircle( vbo, size[ 0 ], size[ 1 ], inp );
	case GeoType::Plane: return CreatePlane( vbo, size[ 0 ], size[ 1 ], inp );
	case GeoType::Rectangle: return CreatePlane( vbo, 1, 1, inp );
	case GeoType::Sphere: return CreateSphere( vbo, size[ 0 ], inp );
	case GeoType::Triangle: return CreateTriangle( vbo, size[ 0 ], inp );
	case GeoType::ScreenQuadNDC: return CreateScreenQuadNDC( vbo, inp );
	default: return 0;
	}
}
}//namespace sed
}//namespace stfx
