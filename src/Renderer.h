// ShaderToyFX - moteur de rendu OpenGL multi-passes compatible Shadertoy
#pragma once
#include <ffgl/FFGL.h>
#include <ffglex/FFGLScreenQuad.h>
#include <string>
#include <vector>
#include "ShaderDoc.h"

namespace stfx
{
struct FrameInputs
{
	GLuint hostFbo = 0;
	int vpX = 0, vpY = 0, width = 1, height = 1;
	GLuint inputTex = 0;
	int inputW = 0, inputH = 0;
	float time = 0, timeDelta = 0, frameRate = 60;
	int frame = 0;
	float mouse[ 4 ] = {};
	float date[ 4 ]  = {};
	float sampleRate = 44100;
	float user[ kMaxUserParams ] = {};
	bool shaderAlpha = false;
	int maskMode     = 0;// 0 = off, 1 = luminance, 2 = max RGB, 3 = alpha du shader x luminance
	float maskThreshold = 0.05f;
	float maskSoftness  = 0.15f;
	bool maskInvert     = false;
};

class Renderer
{
public:
	bool InitGL();
	void DeInitGL();

	// Compile toutes les passes. En cas d'échec, l'ancien shader reste actif et log contient les erreurs.
	// Si des paramètres automatiques (#define/const) empêchent la compilation, ils sont retirés de doc.params.
	bool Build( ShaderDoc& doc, std::string& log );
	void Render( const FrameInputs& in );
	void UpdateAudio( const float* fft, int count, float gain );
	void ResetBuffers();
	bool HasProgram() const { return !passes.empty(); }

private:
	struct Channel
	{
		ChannelKind kind = ChannelKind::None;
		int buffer       = -1;
		GLuint tex       = 0;// texture possédée (image/cube/volume)
		GLenum target    = GL_TEXTURE_2D;
		int w = 0, h = 0, d = 0;
		Filter filter = Filter::Linear;
	};
	struct Pass
	{
		GLuint program  = 0;
		bool isImage    = false;
		int bufferIndex = -1;
		Channel ch[ 4 ];
		GLuint samplers[ 4 ] = {};
		GLint uRes = -1, uTime = -1, uTimeDelta = -1, uFrameRate = -1, uFrame = -1, uChannelTime = -1, uChannelRes = -1,
			  uMouse = -1, uDate = -1, uSampleRate = -1, uOffset = -1, uForceAlpha = -1, uMask = -1;
		GLint uChannel[ 4 ] = { -1, -1, -1, -1 };
		std::vector< GLint > uUser;// une location par groupe d'uniforms (userUniforms)
	};
	struct Buffer
	{
		GLuint fbo[ 2 ] = {};
		GLuint tex[ 2 ] = {};
		int cur = 0, w = 0, h = 0;
		bool wantMips = false;
	};

	void DestroyPasses( std::vector< Pass >& list, std::vector< GLuint >& textures );
	void DestroyBuffers();
	void EnsureBuffer( int index, int w, int h );
	GLuint UploadImage( const ImageData& img, ChannelKind kind, Filter filter, GLenum& target );
	static GLuint Compile( GLenum type, const std::string& src, std::string& log );
	int glslVersion = 410;// 420 si le driver le permet (fonctions GLSL ES 3.0 type packHalf2x16)

	struct UserUniform
	{
		std::string name;
		int vecSize = 1;
		bool isInt  = false;
		int param[ 4 ] = { -1, -1, -1, -1 };// index dans FrameInputs::user pour chaque composante
	};
	bool BuildOnce( const ShaderDoc& doc, std::string& log );
	std::vector< UserUniform > userUniforms;
	std::vector< Pass > passes;
	std::vector< GLuint > ownedTextures;
	Buffer buffers[ 4 ];
	GLuint audioTex = 0, keyboardTex = 0, black2D = 0, blackCube = 0, black3D = 0;
	GLuint vertexShader = 0;
	ffglex::FFGLScreenQuad quad;
	std::vector< float > audioRows;
};
}//namespace stfx
