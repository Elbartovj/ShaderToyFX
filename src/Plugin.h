// ShaderToyFX - plugin FFGL (effet ou source selon STFX_SOURCE)
#pragma once
#include <ffgl/FFGLPluginSDK.h>
#include <chrono>
#include <string>
#include <vector>
#include "Renderer.h"
#include "Instance.h"

enum ParamIndex : unsigned int
{
	PT_SHADER = 0,
	PT_BROWSE,
	PT_RANDOM,
	PT_RESTART,
	PT_SPEED,
	PT_MOUSE_X,
	PT_MOUSE_Y,
	PT_MOUSE_CLICK,
	PT_SHADER_ALPHA,
	PT_AUDIO_GAIN,
	PT_FFT,
	PT_MASK_ON,
	PT_MASK_SOURCE,
	PT_MASK_THRESHOLD,
	PT_MASK_SOFTNESS,
	PT_MASK_INVERT,
	PT_USER0,
	PT_COUNT = PT_USER0 + stfx::kMaxUserParams
};

class ShaderToyFX : public CFFGLPlugin
{
public:
	ShaderToyFX();
	~ShaderToyFX() override;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult DeInitGL() override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

private:
	void ApplyPending();
	void ApplyUserParams( const std::vector< stfx::UserParam >& params, bool resetValues, bool keepHostValues );
	void ResetClock();

	stfx::SharedPtr shared;
	stfx::Renderer renderer;
	bool glReady = false;

	std::string shaderPath;
	float values[ PT_COUNT ] = {};
	std::vector< stfx::UserParam > userParams;
	bool firstShaderApplied = false;
	bool userTouched[ stfx::kMaxUserParams ] = {};// valeur restaurée par Resolume (composition)
	std::vector< float > fftScratch;
	char displayBuf[ 64 ] = {};

	std::chrono::steady_clock::time_point lastTick;
	bool clockStarted = false;
	double shaderTime = 0.0;
	float frameRate   = 60.0f;
	int frame         = 0;
	bool prevClick    = false;
	float clickPos[ 2 ] = {};
	bool clickFirstFrame = false;
};
