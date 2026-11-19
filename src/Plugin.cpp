#include "Plugin.h"
#include "Panel.h"
#include "Util.h"
#include "ShadertoyApi.h"
#include <thread>
#include <ffgl/FFGLLog.h>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <cmath>

using namespace stfx;

#ifdef STFX_SOURCE
static const bool kIsSource = true;
static CFFGLPluginInfo PluginInfo(
	PluginFactory< ShaderToyFX >,
	"STFS",
	"ShaderToyFX Src",
	2, 1,
	1, 0,
	FF_SOURCE,
	"Shadertoy shaders as a Resolume source (library: Browse Library)",
	"ShaderToyFX - open source" );
#else
static const bool kIsSource = false;
static CFFGLPluginInfo PluginInfo(
	PluginFactory< ShaderToyFX >,
	"STFX",
	"ShaderToyFX",
	2, 1,
	1, 0,
	FF_EFFECT,
	"Shadertoy shaders as a Resolume effect (library: Browse Library)",
	"ShaderToyFX - open source" );
#endif

static const unsigned int kFftSize = 512;

ShaderToyFX::ShaderToyFX()
{
	SetMinInputs( kIsSource ? 0 : 1 );
	SetMaxInputs( kIsSource ? 0 : 1 );

	SetFileParamInfo( PT_SHADER, "Shader", { "json", "glsl", "frag", "txt" }, "" );
	SetParamInfo( PT_BROWSE, "Browse Library", FF_TYPE_EVENT, false );
	SetParamInfo( PT_RANDOM, "Random Shadertoy", FF_TYPE_EVENT, false );
	SetParamInfo( PT_RESTART, "Restart", FF_TYPE_EVENT, false );
	SetOptionParamInfo( PT_RANDOM_FROM, "Random From", 1, 0.0f );
	SetParamElementInfo( PT_RANDOM_FROM, 0, "Bibliothèque (tout)", 0.0f );
	SetParamInfo( PT_RANDOM_FOLDER, "Random Folder", FF_TYPE_TEXT, "" );
	SetParamVisibility( PT_RANDOM_FOLDER, false, false );
	SetParamGroup( PT_SHADER, "Load" );
	SetParamGroup( PT_BROWSE, "Load" );
	SetParamGroup( PT_RANDOM, "Load" );
	SetParamGroup( PT_RANDOM_FROM, "Load" );
	SetParamGroup( PT_RANDOM_FOLDER, "Load" );
	SetParamGroup( PT_RESTART, "Load" );

	SetParamInfo( PT_SPEED, "Speed", FF_TYPE_STANDARD, 0.25f );// 0..4, défaut 1
	SetParamInfo( PT_MOUSE_X, "Mouse X", FF_TYPE_STANDARD, 0.5f );
	SetParamInfo( PT_MOUSE_Y, "Mouse Y", FF_TYPE_STANDARD, 0.5f );
	SetParamInfo( PT_MOUSE_CLICK, "Mouse Click", FF_TYPE_BOOLEAN, false );
	SetParamInfo( PT_SHADER_ALPHA, "Shader Alpha", FF_TYPE_BOOLEAN, false );
	for( unsigned int i = PT_SPEED; i <= PT_SHADER_ALPHA; ++i )
		SetParamGroup( i, "Controls" );

	SetParamInfo( PT_AUDIO_GAIN, "Audio Gain", FF_TYPE_STANDARD, 0.25f );// 0..4, défaut 1
	SetBufferParamInfo( PT_FFT, "Audio FFT", kFftSize, FF_USAGE_FFT );
	SetParamGroup( PT_AUDIO_GAIN, "Audio" );
	SetParamGroup( PT_FFT, "Audio" );

	// Auto Mask : rend transparentes les zones sombres (ou claires) de la sortie
	SetParamInfo( PT_MASK_ON, "Auto Mask", FF_TYPE_BOOLEAN, false );
	SetOptionParamInfo( PT_MASK_SOURCE, "Mask Source", 3, 0.0f );
	SetParamElementInfo( PT_MASK_SOURCE, 0, "Luminance", 0.0f );
	SetParamElementInfo( PT_MASK_SOURCE, 1, "Max RGB", 1.0f );
	SetParamElementInfo( PT_MASK_SOURCE, 2, "Shader Alpha x Luma", 2.0f );
	SetParamInfo( PT_MASK_THRESHOLD, "Mask Threshold", FF_TYPE_STANDARD, 0.05f );
	SetParamInfo( PT_MASK_SOFTNESS, "Mask Softness", FF_TYPE_STANDARD, 0.15f );
	SetParamInfo( PT_MASK_INVERT, "Mask Invert", FF_TYPE_BOOLEAN, false );
	for( unsigned int i = PT_MASK_ON; i <= PT_MASK_INVERT; ++i )
		SetParamGroup( i, "Auto Mask" );

	// Inputs : remplacer la texture de chaque iChannel par l'entrée Resolume ou une image au choix
	for( int ch = 0; ch < 4; ++ch )
	{
		std::string n = "iChannel" + std::to_string( ch );
		SetOptionParamInfo( ChSrc( ch ), n.c_str(), CH_MODE_COUNT, 0.0f );
		SetParamElementInfo( ChSrc( ch ), CH_SHADER, "Shader (défaut)", (float)CH_SHADER );
		SetParamElementInfo( ChSrc( ch ), CH_INPUT, "Entrée Resolume", (float)CH_INPUT );
		SetParamElementInfo( ChSrc( ch ), CH_IMAGE, "Image (fichier)", (float)CH_IMAGE );
		SetParamElementInfo( ChSrc( ch ), CH_AUDIO, "Audio FFT", (float)CH_AUDIO );
		SetParamElementInfo( ChSrc( ch ), CH_BLACK, "Noir", (float)CH_BLACK );
		SetFileParamInfo( ChImg( ch ), ( n + " Image" ).c_str(), { "png", "jpg", "jpeg", "bmp", "tga", "gif", "hdr", "psd" }, "" );
		SetParamGroup( ChSrc( ch ), "Inputs" );
		SetParamGroup( ChImg( ch ), "Inputs" );
		SetParamVisibility( ChImg( ch ), false, false );
	}
	SetOptionParamInfo( PT_INPUT_FIT, "Input Fit", 4, 0.0f );
	SetParamElementInfo( PT_INPUT_FIT, 0, "Étirer", 0.0f );
	SetParamElementInfo( PT_INPUT_FIT, 1, "Contenir (bandes)", 1.0f );
	SetParamElementInfo( PT_INPUT_FIT, 2, "Remplir (rogner)", 2.0f );
	SetParamElementInfo( PT_INPUT_FIT, 3, "Pixel 1:1", 3.0f );
	SetParamGroup( PT_INPUT_FIT, "Inputs" );
	SetParamInfo( PT_ASPECT_FIX, "Aspect Fix", FF_TYPE_BOOLEAN, false );
	SetParamGroup( PT_ASPECT_FIX, "Inputs" );
	if( kIsSource )
		SetParamVisibility( PT_INPUT_FIT, false, false );

	for( unsigned int i = 0; i < (unsigned)kMaxUserParams; ++i )
	{
		unsigned int idx = PT_USER0 + i;
		std::string name = "Param " + std::to_string( i + 1 );
		SetParamInfo( idx, name.c_str(), FF_TYPE_STANDARD, 0.5f );
		SetParamGroup( idx, "Shader Params" );
		SetParamVisibility( idx, false, false );
	}

	values[ PT_SPEED ]      = 0.25f;
	values[ PT_MOUSE_X ]    = 0.5f;
	values[ PT_MOUSE_Y ]    = 0.5f;
	values[ PT_AUDIO_GAIN ] = 0.25f;
	values[ PT_MASK_THRESHOLD ] = 0.05f;
	values[ PT_MASK_SOFTNESS ]  = 0.15f;
	for( int i = 0; i < kMaxUserParams; ++i )
		values[ PT_USER0 + i ] = 0.5f;

	shared = std::make_shared< InstanceShared >();
	RefreshRandomSources( true, false );
}

void ShaderToyFX::RequestChannelImage( int ch, const std::string& path )
{
	uint64_t gen;
	{
		std::lock_guard< std::mutex > lock( imageSlots->m );
		gen = ++imageSlots->gen[ ch ];
		if( path.empty() )
		{
			imageSlots->pending[ ch ].reset();
			imageSlots->hasPending[ ch ] = true;
			return;
		}
	}
	PinModuleForThreads();
	std::weak_ptr< ImageSlots > weak = imageSlots;
	std::thread( [ weak, ch, gen, path ]() {
		std::string err;
		auto img = LoadImageFile( path, true, err );
		auto sp  = weak.lock();
		if( !sp )
			return;
		std::lock_guard< std::mutex > lock( sp->m );
		if( gen != sp->gen[ ch ] )
			return;
		sp->pending[ ch ]    = img;
		sp->hasPending[ ch ] = true;
		if( !img )
			sp->error = "iChannel" + std::to_string( ch ) + " : " + err;
	} ).detach();
}

// Affiche les réglages iChannel utilisés par le shader, avec ce qu'ils contiennent à l'origine
void ShaderToyFX::UpdateChannelParams( bool raiseEvents )
{
	std::shared_ptr< const ShaderDoc > doc;
	{
		std::lock_guard< std::mutex > lock( shared->m );
		doc = shared->current;
	}
	for( int ch = 0; ch < 4; ++ch )
	{
		std::string tag = "iChannel" + std::to_string( ch );
		bool used       = false;
		std::string what;
		if( doc )
		{
			for( auto& p : doc->passes )
			{
				if( p.code.find( tag ) == std::string::npos && doc->common.find( tag ) == std::string::npos )
					continue;
				used = true;
				if( !what.empty() && !p.isImage )
					continue;
				switch( p.ch[ ch ].kind )
				{
				case ChannelKind::Texture: what = "texture"; break;
				case ChannelKind::Input: what = "entrée"; break;
				case ChannelKind::Audio: what = "audio"; break;
				case ChannelKind::Buffer: what = std::string( "buffer " ) + (char)( 'A' + std::max( 0, p.ch[ ch ].buffer ) ); break;
				case ChannelKind::Cubemap: what = "cubemap"; break;
				case ChannelKind::Volume: what = "volume"; break;
				case ChannelKind::Keyboard: what = "clavier"; break;
				default: what = "vide"; break;
				}
			}
		}
		chUsed[ ch ] = used;
		SetParamVisibility( ChSrc( ch ), used, raiseEvents );
		SetParamVisibility( ChImg( ch ), used && values[ ChSrc( ch ) ] == (float)CH_IMAGE, raiseEvents );
		if( used )
			SetParamDisplayName( ChSrc( ch ), tag + " (" + what + ")", raiseEvents );
	}
}

std::string ShaderToyFX::RandomSource() const
{
	return randomFolder;
}

// Met à jour la liste « Random From » (dossiers de la bibliothèque) en gardant le choix par son nom
void ShaderToyFX::RefreshRandomSources( bool force, bool raiseEvents )
{
	auto now = std::chrono::steady_clock::now();
	if( !force && now - lastFolderScan < std::chrono::seconds( 3 ) )
		return;
	lastFolderScan = now;

	std::vector< std::string > sources = { "" };
	if( !Trim( GetSettings().apiKey ).empty() )
		sources.push_back( kRandomOnline );
	for( auto& f : ListFolders() )
		sources.push_back( f );
	// un dossier choisi mais absent (renommé ?) reste dans la liste pour ne pas perdre le réglage
	if( !randomFolder.empty() && std::find( sources.begin(), sources.end(), randomFolder ) == sources.end() )
		sources.push_back( randomFolder );

	bool changed = sources != randomSources;
	if( changed )
	{
		randomSources = sources;
		std::vector< std::string > names;
		std::vector< float > vals;
		for( size_t i = 0; i < sources.size(); ++i )
		{
			const std::string& src = sources[ i ];
			names.push_back( src.empty() ? "Bibliothèque (tout)" : src == kRandomOnline ? "Shadertoy en ligne" : src );
			vals.push_back( (float)i );
		}
		SetParamElements( PT_RANDOM_FROM, names, vals, raiseEvents );
	}
	// l'index affiché suit le nom mémorisé
	auto it   = std::find( randomSources.begin(), randomSources.end(), randomFolder );
	float idx = it == randomSources.end() ? 0.0f : (float)( it - randomSources.begin() );
	if( it == randomSources.end() )
		randomFolder.clear();
	if( values[ PT_RANDOM_FROM ] != idx )
	{
		values[ PT_RANDOM_FROM ] = idx;
		if( raiseEvents )
			RaiseParamEvent( PT_RANDOM_FROM, FF_EVENT_FLAG_VALUE );
	}
}

ShaderToyFX::~ShaderToyFX()
{
	if( shared )
	{
		UnregisterInstance( shared );
		Panel::OnInstanceRemoved();
	}
}

FFResult ShaderToyFX::InitGL( const FFGLViewportStruct* vp )
{
	if( !renderer.InitGL() )
	{
		FFGLLog::LogToHost( "ShaderToyFX: échec de l'initialisation OpenGL" );
		return FF_FAIL;
	}
	glReady = true;
	RegisterInstance( shared );

	// shader par défaut si rien n'est demandé
	std::string log;
	ShaderDoc def = DefaultDoc( kIsSource );
	if( renderer.Build( def, log ) )
	{
		std::lock_guard< std::mutex > lock( shared->m );
		if( !shared->current )
		{
			shared->current = std::make_shared< const ShaderDoc >( std::move( def ) );
			shared->docVersion++;
			shared->status = "Prêt";
		}
	}
	else
		FFGLLog::LogToHost( ( "ShaderToyFX: shader par défaut invalide\n" + log ).c_str() );
	ResetClock();
	return CFFGLPlugin::InitGL( vp );
}

FFResult ShaderToyFX::DeInitGL()
{
	renderer.DeInitGL();
	glReady = false;
	return FF_SUCCESS;
}

void ShaderToyFX::ResetClock()
{
	shaderTime   = 0.0;
	frame        = 0;
	clockStarted = false;
}

void ShaderToyFX::ApplyUserParams( const std::vector< UserParam >& params, bool resetValues, bool keepHostValues )
{
	userParams = params;
	for( unsigned int i = 0; i < (unsigned)kMaxUserParams; ++i )
	{
		unsigned int idx = PT_USER0 + i;
		bool visible     = i < params.size();
		SetParamVisibility( idx, visible, true );
		if( visible )
		{
			SetParamDisplayName( idx, params[ i ].name, true );
			if( resetValues && !( keepHostValues && userTouched[ i ] ) )
			{
				const UserParam& p = params[ i ];
				values[ idx ]      = std::clamp( ( p.def - p.minV ) / ( p.maxV - p.minV ), 0.0f, 1.0f );
				RaiseParamEvent( idx, FF_EVENT_FLAG_VALUE );
			}
		}
	}
}

void ShaderToyFX::ApplyPending()
{
	std::unique_ptr< ShaderDoc > doc;
	bool userInitiated = false, restart = false;
	{
		std::lock_guard< std::mutex > lock( shared->m );
		doc                 = std::move( shared->pendingDoc );
		userInitiated       = shared->pendingUserInitiated;
		restart             = shared->restartRequested;
		shared->restartRequested = false;
	}
	if( restart )
	{
		ResetClock();
		renderer.ResetBuffers();
	}
	if( !doc )
		return;

	std::string log;
	bool ok = renderer.Build( *doc, log );
	std::string warnings;
	for( auto& w : doc->warnings )
		warnings += "- " + w + "\n";

	if( ok )
	{
		bool sameParams = userParams.size() == doc->params.size() &&
						  std::equal( userParams.begin(), userParams.end(), doc->params.begin(), []( const UserParam& a, const UserParam& b ) { return a.name == b.name && a.minV == b.minV && a.maxV == b.maxV; } );
		// 1er shader d'une instance chargé par Resolume (composition) : on garde les valeurs restaurées
		bool keepHost = !firstShaderApplied && !userInitiated;
		ApplyUserParams( doc->params, !sameParams, keepHost );
		firstShaderApplied = true;
		for( auto& t : userTouched )
			t = false;
		if( userInitiated && !doc->sourcePath.empty() && doc->sourcePath != shaderPath )
		{
			shaderPath = doc->sourcePath;
			RaiseParamEvent( PT_SHADER, FF_EVENT_FLAG_VALUE );// Resolume enregistre le chemin dans la composition
		}
		if( !sameParams || userInitiated )
			ResetClock();
	}

	{
		std::lock_guard< std::mutex > lock( shared->m );
		shared->busy = false;
		if( ok )
		{
			shared->current = std::shared_ptr< const ShaderDoc >( doc.release() );
			shared->docVersion++;
			shared->lastBuildOk = true;
			shared->status      = "OK : " + shared->current->title;
			shared->log         = warnings;
		}
		else
		{
			shared->lastBuildOk = false;
			shared->status      = "Erreur de compilation : " + doc->title;
			shared->log         = log + ( warnings.empty() ? "" : "\n" + warnings );
			FFGLLog::LogToHost( ( "ShaderToyFX: " + shared->status + "\n" + log ).c_str() );
		}
	}
	if( ok )
		UpdateChannelParams( true );
}

FFResult ShaderToyFX::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( !glReady || !pGL )
		return FF_FAIL;

	ApplyPending();
	RefreshRandomSources( false, true );

	// horloge
	auto now = std::chrono::steady_clock::now();
	float dt = 1.0f / 60.0f;
	if( clockStarted )
		dt = std::chrono::duration< float >( now - lastTick ).count();
	clockStarted = true;
	lastTick     = now;
	dt           = std::clamp( dt, 0.0f, 0.25f );
	if( dt > 0.0f )
		frameRate = frameRate * 0.9f + ( 1.0f / std::max( dt, 1e-4f ) ) * 0.1f;
	float speed = values[ PT_SPEED ] * 4.0f;
	shaderTime += (double)dt * speed;

	// audio
	if( ParamInfo* info = FindParamInfo( PT_FFT ) )
	{
		fftScratch.resize( info->elements.size() );
		for( size_t i = 0; i < info->elements.size(); ++i )
			fftScratch[ i ] = info->elements[ i ].value;
		renderer.UpdateAudio( fftScratch.data(), (int)fftScratch.size(), values[ PT_AUDIO_GAIN ] * 4.0f );
	}

	FrameInputs in;
	in.hostFbo   = pGL->HostFBO;
	in.vpX       = (int)currentViewport.x;
	in.vpY       = (int)currentViewport.y;
	in.width     = std::max( 1, (int)currentViewport.width );
	in.height    = std::max( 1, (int)currentViewport.height );
	in.time      = (float)shaderTime;
	in.timeDelta = dt * speed;
	in.frameRate = frameRate;
	in.frame     = frame;
	in.sampleRate = sampleRate > 0 ? (float)sampleRate : 44100.0f;
	in.shaderAlpha = values[ PT_SHADER_ALPHA ] > 0.5f;
	{
		std::lock_guard< std::mutex > lock( imageSlots->m );
		for( int ch = 0; ch < 4; ++ch )
			if( imageSlots->hasPending[ ch ] )
			{
				renderer.SetChannelImage( ch, imageSlots->pending[ ch ].get() );
				imageSlots->pending[ ch ].reset();
				imageSlots->hasPending[ ch ] = false;
			}
		if( !imageSlots->error.empty() )
		{
			std::lock_guard< std::mutex > l2( shared->m );
			shared->log += "- " + imageSlots->error + "\n";
			imageSlots->error.clear();
		}
	}
	for( int ch = 0; ch < 4; ++ch )
		in.chMode[ ch ] = (int)values[ ChSrc( ch ) ];
	in.aspectFix = values[ PT_ASPECT_FIX ] > 0.5f;
	if( values[ PT_MASK_ON ] > 0.5f )
	{
		in.maskMode      = 1 + (int)std::lround( std::clamp( values[ PT_MASK_SOURCE ], 0.0f, 2.0f ) );
		in.maskThreshold = values[ PT_MASK_THRESHOLD ];
		in.maskSoftness  = values[ PT_MASK_SOFTNESS ];
		in.maskInvert    = values[ PT_MASK_INVERT ] > 0.5f;
	}
	if( !kIsSource && pGL->numInputTextures > 0 && pGL->inputTextures[ 0 ] )
	{
		in.inputTex = pGL->inputTextures[ 0 ]->Handle;
		in.inputW   = (int)pGL->inputTextures[ 0 ]->Width;
		in.inputH   = (int)pGL->inputTextures[ 0 ]->Height;
		in.inputHwW = (int)pGL->inputTextures[ 0 ]->HardwareWidth;
		in.inputHwH = (int)pGL->inputTextures[ 0 ]->HardwareHeight;
		in.inputFit = (int)std::lround( values[ PT_INPUT_FIT ] );
	}

	// souris façon Shadertoy : xy = position, zw = position du clic (signe = état)
	float mx  = values[ PT_MOUSE_X ] * in.width;
	float my  = values[ PT_MOUSE_Y ] * in.height;
	bool down = values[ PT_MOUSE_CLICK ] > 0.5f;
	if( down && !prevClick )
	{
		clickPos[ 0 ]   = mx;
		clickPos[ 1 ]   = my;
		clickFirstFrame = true;
	}
	in.mouse[ 0 ] = mx;
	in.mouse[ 1 ] = my;
	in.mouse[ 2 ] = down ? clickPos[ 0 ] : -std::abs( clickPos[ 0 ] );
	in.mouse[ 3 ] = clickFirstFrame ? clickPos[ 1 ] : -std::abs( clickPos[ 1 ] );
	clickFirstFrame = false;
	prevClick       = down;

	// date
	std::time_t t = std::time( nullptr );
	std::tm lt{};
#ifdef _WIN32
	localtime_s( &lt, &t );
#else
	localtime_r( &t, &lt );
#endif
	auto sinceMidnight = (float)( lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec );
	float frac         = (float)( std::chrono::duration_cast< std::chrono::milliseconds >( now.time_since_epoch() ).count() % 1000 ) / 1000.0f;
	in.date[ 0 ]       = (float)( lt.tm_year + 1900 );
	in.date[ 1 ]       = (float)lt.tm_mon;
	in.date[ 2 ]       = (float)lt.tm_mday;
	in.date[ 3 ]       = sinceMidnight + frac;

	for( size_t i = 0; i < (size_t)kMaxUserParams; ++i )
	{
		float n = values[ PT_USER0 + i ];
		if( i < userParams.size() )
			in.user[ i ] = userParams[ i ].minV + n * ( userParams[ i ].maxV - userParams[ i ].minV );
		else
			in.user[ i ] = n;
	}

	renderer.Render( in );
	++frame;
	return FF_SUCCESS;
}

FFResult ShaderToyFX::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT || index == PT_SHADER || index == PT_FFT || index == PT_RANDOM_FOLDER )
		return index == PT_FFT ? FF_SUCCESS : FF_FAIL;
	if( index >= PT_CH_FIRST && index <= PT_CH_LAST )
	{
		if( ( index - PT_CH_FIRST ) % 2 == 1 )
			return FF_FAIL;// paramètre fichier
		int ch          = ( index - PT_CH_FIRST ) / 2;
		values[ index ] = std::clamp( std::round( value ), 0.0f, (float)( CH_MODE_COUNT - 1 ) );
		// le sélecteur d'image n'apparaît qu'en mode « Image »
		SetParamVisibility( ChImg( ch ), chUsed[ ch ] && values[ index ] == (float)CH_IMAGE, glReady );
		return FF_SUCCESS;
	}
	if( index == PT_RANDOM_FROM )
	{
		values[ index ] = value;
		size_t i        = (size_t)std::lround( std::max( 0.0f, value ) );
		std::string sel = i < randomSources.size() ? randomSources[ i ] : std::string();
		if( sel != randomFolder )
		{
			randomFolder = sel;
			RaiseParamEvent( PT_RANDOM_FOLDER, FF_EVENT_FLAG_VALUE );
		}
		return FF_SUCCESS;
	}
	bool rising = value > 0.5f && values[ index ] <= 0.5f;
	if( glReady && index >= PT_USER0 && index < PT_COUNT )
		userTouched[ index - PT_USER0 ] = true;// (avant InitGL = valeurs par défaut de FFGL : ignorées)
	values[ index ] = value;
	if( !rising )
		return FF_SUCCESS;
	switch( index )
	{
	case PT_BROWSE:
		Panel::Open( shared );
		break;
	case PT_RANDOM:
		RequestRandom( shared, RandomSource() );
		break;
	case PT_RESTART:
		RequestRestart( shared );
		break;
	default:
		break;
	}
	return FF_SUCCESS;
}

float ShaderToyFX::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? values[ index ] : 0.0f;
}

FFResult ShaderToyFX::SetTextParameter( unsigned int index, const char* value )
{
	if( index >= PT_CH_FIRST && index <= PT_CH_LAST && ( index - PT_CH_FIRST ) % 2 == 1 )
	{
		int ch = ( index - PT_CH_FIRST ) / 2;
		std::string path = value ? value : "";
		if( path != chImagePath[ ch ] )
		{
			chImagePath[ ch ] = path;
			RequestChannelImage( ch, path );
		}
		return FF_SUCCESS;
	}
	if( index == PT_RANDOM_FOLDER )
	{
		randomFolder = value ? value : "";
		RefreshRandomSources( true, glReady );// recale la liste déroulante sur ce nom
		return FF_SUCCESS;
	}
	if( index != PT_SHADER )
		return FF_FAIL;
	std::string path = value ? value : "";
	if( path == shaderPath )
		return FF_SUCCESS;
	shaderPath = path;
	if( !path.empty() )
	{
		std::shared_ptr< const ShaderDoc > cur;
		{
			std::lock_guard< std::mutex > lock( shared->m );
			cur = shared->current;
		}
		if( !cur || cur->sourcePath != path )
			RequestLoadFile( shared, path, false );
	}
	return FF_SUCCESS;
}

char* ShaderToyFX::GetTextParameter( unsigned int index )
{
	if( index >= PT_CH_FIRST && index <= PT_CH_LAST && ( index - PT_CH_FIRST ) % 2 == 1 )
		return const_cast< char* >( chImagePath[ ( index - PT_CH_FIRST ) / 2 ].c_str() );
	if( index == PT_RANDOM_FOLDER )
		return const_cast< char* >( randomFolder.c_str() );
	if( index != PT_SHADER )
		return (char*)FF_FAIL;
	return const_cast< char* >( shaderPath.c_str() );
}

char* ShaderToyFX::GetParameterDisplay( unsigned int index )
{
	float v = index < PT_COUNT ? values[ index ] : 0.0f;
	if( index == PT_SPEED || index == PT_AUDIO_GAIN )
		snprintf( displayBuf, sizeof( displayBuf ), "%.2fx", v * 4.0f );
	else if( index >= PT_USER0 && index < PT_COUNT && index - PT_USER0 < userParams.size() )
	{
		const UserParam& p = userParams[ index - PT_USER0 ];
		float real         = p.minV + v * ( p.maxV - p.minV );
		if( p.isInt )
			snprintf( displayBuf, sizeof( displayBuf ), "%d", (int)std::lround( real ) );
		else
			snprintf( displayBuf, sizeof( displayBuf ), "%.3f", real );
	}
	else
		return CFFGLPlugin::GetParameterDisplay( index );
	return displayBuf;
}
