// ShaderToyFX - plugin FFGL (effet ou source selon STFX_SOURCE)
#pragma once
#include <ffgl/FFGLPluginSDK.h>
#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "Renderer.h"
#include "Instance.h"

enum ParamIndex : unsigned int
{
	PT_SHADER = 0,
	PT_BROWSE,
	PT_RANDOM,
	PT_RANDOM_FROM,  // liste déroulante : Bibliothèque / Shadertoy en ligne / dossiers
	PT_RANDOM_FOLDER,// texte caché : nom du dossier choisi (sauvé dans la composition)
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
	PT_CH_FIRST,// pour chaque iChannel : source (option) puis image (fichier)
	PT_CH_LAST = PT_CH_FIRST + 7,
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
	void UpdateChannelParams( bool raiseEvents );
	void RequestChannelImage( int ch, const std::string& path );
	static unsigned int ChSrc( int ch ) { return PT_CH_FIRST + ch * 2; }
	static unsigned int ChImg( int ch ) { return PT_CH_FIRST + ch * 2 + 1; }
	void RefreshRandomSources( bool force, bool raiseEvents );
	std::string RandomSource() const;

	stfx::SharedPtr shared;
	stfx::Renderer renderer;
	bool glReady = false;

	std::string shaderPath;
	// images choisies pour iChannel0..3 (décodées sur un thread, envoyées au GPU dans ProcessOpenGL)
	struct ImageSlots
	{
		std::mutex m;
		std::shared_ptr< stfx::ImageData > pending[ 4 ];
		bool hasPending[ 4 ] = {};
		uint64_t gen[ 4 ]   = {};
		std::string error;
	};
	std::shared_ptr< ImageSlots > imageSlots = std::make_shared< ImageSlots >();
	std::string chImagePath[ 4 ];
	bool chUsed[ 4 ] = { true, true, true, true };
	std::vector< std::string > randomSources;// "" = bibliothèque, kRandomOnline, dossiers
	std::string randomFolder;                 // source choisie (persistée)
	std::chrono::steady_clock::time_point lastFolderScan;
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
