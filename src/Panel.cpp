#include "Panel.h"
#include "ShadertoyApi.h"
#include "ShaderDoc.h"
#include "Util.h"
#include "Net.h"
#include "License.h"
#include "Preview.h"
#include "sed/SedArchive.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx11.h>
#include <stb_image.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include <deque>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

using namespace stfx;

namespace
{
const UINT WM_STFX_WAKE = WM_APP + 1;
const UINT WM_STFX_QUIT = WM_APP + 2;

//------------------------------------------------------------------ communication avec le thread du panneau
std::mutex g_openMutex;
std::weak_ptr< InstanceShared > g_requestedTarget;
bool g_openRequested = false;
std::atomic< bool > g_running{ false };
std::atomic< HWND > g_hwnd{ nullptr };

//------------------------------------------------------------------ D3D11
ID3D11Device* g_device           = nullptr;
ID3D11DeviceContext* g_context   = nullptr;
IDXGISwapChain* g_swapChain      = nullptr;
ID3D11RenderTargetView* g_rtv    = nullptr;
UINT g_resizeW = 0, g_resizeH = 0;
bool g_visible = false;

void CreateRTV()
{
	ID3D11Texture2D* back = nullptr;
	g_swapChain->GetBuffer( 0, IID_PPV_ARGS( &back ) );
	if( back )
	{
		g_device->CreateRenderTargetView( back, nullptr, &g_rtv );
		back->Release();
	}
}
void ReleaseRTV()
{
	if( g_rtv )
	{
		g_rtv->Release();
		g_rtv = nullptr;
	}
}
bool CreateDevice( HWND hwnd )
{
	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount                        = 2;
	sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator   = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow                       = hwnd;
	sd.SampleDesc.Count                   = 1;
	sd.Windowed                           = TRUE;
	sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;
	const D3D_FEATURE_LEVEL levels[]      = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
	D3D_FEATURE_LEVEL got;
	HRESULT hr = D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context );
	if( hr == DXGI_ERROR_UNSUPPORTED )
		hr = D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context );
	if( FAILED( hr ) )
		return false;
	CreateRTV();
	return true;
}
void DestroyDevice()
{
	ReleaseRTV();
	if( g_swapChain )
		g_swapChain->Release();
	if( g_context )
		g_context->Release();
	if( g_device )
		g_device->Release();
	g_swapChain = nullptr;
	g_context   = nullptr;
	g_device    = nullptr;
}

ID3D11ShaderResourceView* CreateTexture( const uint8_t* rgba, int w, int h )
{
	D3D11_TEXTURE2D_DESC desc{};
	desc.Width            = w;
	desc.Height           = h;
	desc.MipLevels        = 1;
	desc.ArraySize        = 1;
	desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage            = D3D11_USAGE_DEFAULT;
	desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sub{};
	sub.pSysMem          = rgba;
	sub.SysMemPitch      = w * 4;
	ID3D11Texture2D* tex = nullptr;
	if( FAILED( g_device->CreateTexture2D( &desc, &sub, &tex ) ) )
		return nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	g_device->CreateShaderResourceView( tex, nullptr, &srv );
	tex->Release();
	return srv;
}

//------------------------------------------------------------------ aperçu animé (image reçue du thread Preview)
ID3D11Texture2D* g_previewTex       = nullptr;
ID3D11ShaderResourceView* g_previewSrv = nullptr;
int g_previewW = 0, g_previewH = 0;
uint64_t g_previewSerial = 0;
Preview::Frame g_previewFrame;

void ReleasePreviewTexture()
{
	if( g_previewSrv )
		g_previewSrv->Release();
	if( g_previewTex )
		g_previewTex->Release();
	g_previewSrv = nullptr;
	g_previewTex = nullptr;
	g_previewW = g_previewH = 0;
}

void UploadPreview()
{
	if( !Preview::TakeFrame( g_previewSerial, g_previewFrame ) )
		return;
	g_previewSerial = g_previewFrame.serial;
	if( !g_previewTex || g_previewW != g_previewFrame.w || g_previewH != g_previewFrame.h )
	{
		ReleasePreviewTexture();
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width            = g_previewFrame.w;
		desc.Height           = g_previewFrame.h;
		desc.MipLevels        = 1;
		desc.ArraySize        = 1;
		desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage            = D3D11_USAGE_DEFAULT;
		desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
		if( FAILED( g_device->CreateTexture2D( &desc, nullptr, &g_previewTex ) ) )
			return;
		g_device->CreateShaderResourceView( g_previewTex, nullptr, &g_previewSrv );
		g_previewW = g_previewFrame.w;
		g_previewH = g_previewFrame.h;
	}
	g_context->UpdateSubresource( g_previewTex, 0, nullptr, g_previewFrame.rgba.data(), g_previewFrame.w * 4, 0 );
}

//------------------------------------------------------------------ miniatures
struct Thumb
{
	bool requested = false;
	ID3D11ShaderResourceView* srv = nullptr;
	int w = 0, h = 0;
};
struct DecodedThumb
{
	std::string id;
	std::vector< uint8_t > rgba;
	int w = 0, h = 0;
};
std::map< std::string, Thumb > g_thumbs;
std::mutex g_thumbMutex;
std::deque< DecodedThumb > g_thumbQueue;

void RequestThumb( const std::string& id )
{
	Thumb& t = g_thumbs[ id ];
	if( t.requested )
		return;
	t.requested = true;
	std::thread( [ id ] {
		std::string local = JoinPath( MediaDir(), "thumb_" + id + ".jpg" );
		std::vector< uint8_t > bytes;
		if( !ReadFileBytes( local, bytes ) || bytes.empty() )
		{
			HttpResult r = HttpGet( "https://www.shadertoy.com/media/shaders/" + id + ".jpg" );
			if( !r.Ok() )
				return;
			bytes = std::move( r.body );
			WriteFileBytes( local, bytes.data(), bytes.size() );
		}
		int w, h, n;
		unsigned char* px = stbi_load_from_memory( bytes.data(), (int)bytes.size(), &w, &h, &n, 4 );
		if( !px )
			return;
		DecodedThumb d;
		d.id = id;
		d.w  = w;
		d.h  = h;
		d.rgba.assign( px, px + (size_t)w * h * 4 );
		stbi_image_free( px );
		std::lock_guard< std::mutex > lock( g_thumbMutex );
		g_thumbQueue.push_back( std::move( d ) );
		if( HWND hwnd = g_hwnd.load() )
			PostMessageW( hwnd, WM_STFX_WAKE, 0, 0 );
	} ).detach();
}

void UploadThumbs()
{
	std::lock_guard< std::mutex > lock( g_thumbMutex );
	while( !g_thumbQueue.empty() )
	{
		DecodedThumb& d = g_thumbQueue.front();
		Thumb& t        = g_thumbs[ d.id ];
		if( !t.srv )
		{
			t.srv = CreateTexture( d.rgba.data(), d.w, d.h );
			t.w   = d.w;
			t.h   = d.h;
		}
		g_thumbQueue.pop_front();
	}
}

void ReleaseThumbs()
{
	for( auto& kv : g_thumbs )
		if( kv.second.srv )
			kv.second.srv->Release();
	g_thumbs.clear();
}

//------------------------------------------------------------------ recherche Shadertoy
struct SearchState
{
	std::mutex m;
	uint64_t gen = 0;
	bool running = false;
	std::string error;
	int total = 0;
	std::vector< std::string > ids;
	std::map< std::string, std::shared_ptr< ShaderSummary > > info;// cache global
	std::set< std::string > failed;
};
SearchState g_search;

void StartSearch( const std::string& term, const std::string& sort, int page, int perPage )
{
	uint64_t gen;
	{
		std::lock_guard< std::mutex > lock( g_search.m );
		gen              = ++g_search.gen;
		g_search.running = true;
		g_search.error.clear();
		g_search.ids.clear();
		g_search.total = 0;
	}
	std::string key = GetSettings().apiKey;
	std::thread( [ = ] {
		std::vector< std::string > ids;
		int total = 0;
		std::string err;
		bool ok = ApiQuery( term, sort, page * perPage, perPage, key, ids, total, err );
		{
			std::lock_guard< std::mutex > lock( g_search.m );
			if( gen != g_search.gen )
				return;
			g_search.ids   = ids;
			g_search.total = total;
			g_search.error = ok ? ( ids.empty() ? std::string( "Aucun résultat." ) : std::string() ) : err;
		}
		if( HWND hwnd = g_hwnd.load() )
			PostMessageW( hwnd, WM_STFX_WAKE, 0, 0 );
		for( auto& id : ids )
		{
			{
				std::lock_guard< std::mutex > lock( g_search.m );
				if( gen != g_search.gen )
					return;
				if( g_search.info.count( id ) )
					continue;
			}
			auto s = std::make_shared< ShaderSummary >();
			std::string e;
			bool got = ApiGetShader( id, key, *s, e );
			std::lock_guard< std::mutex > lock( g_search.m );
			if( got )
				g_search.info[ id ] = s;
			else
				g_search.failed.insert( id );
			if( HWND hwnd = g_hwnd.load() )
				PostMessageW( hwnd, WM_STFX_WAKE, 0, 0 );
		}
		std::lock_guard< std::mutex > lock( g_search.m );
		if( gen == g_search.gen )
			g_search.running = false;
	} ).detach();
}

//------------------------------------------------------------------ état de l'interface
struct UiState
{
	std::weak_ptr< InstanceShared > target;
	ImFont* uiFont   = nullptr;
	ImFont* codeFont = nullptr;
	bool topmost     = false;
	int forceTab     = -1;

	// Shadertoy
	std::string searchText;
	int sortIdx = 0;
	int page    = 0;
	std::string selectedId;
	bool searchedOnce = false;

	// Lien
	std::string link;

	// Fichier / code collé
	std::string pastedName = "Mon shader";
	std::string pastedCode = "void mainImage( out vec4 fragColor, in vec2 fragCoord )\n{\n    vec2 uv = fragCoord / iResolution.xy;\n    fragColor = vec4( uv, 0.5 + 0.5 * sin( iTime ), 1.0 );\n}\n";
	std::string lastBrowsed;

	// Local
	std::vector< LocalEntry > localFiles;
	std::vector< LicenseInfo > localLicenses;
	std::vector< std::string > folders;
	bool localDirty = true;
	std::string localFilter;
	std::string localSelected;          // dernier shader cliqué (pour « Charger »)
	std::set< std::string > localSel;   // sélection multiple
	std::string localAnchor;            // point de départ pour Maj+clic
	bool confirmRecycle = false;
	std::string folderFilter = "*";// "*" = tous, "" = racine
	int licenseFilter        = 0;  // 0 tous, 1 commercial OK, 2 non commercial / interdit, 3 à vérifier
	std::string newFolderName;
	bool previewOn = true;
	int previewRes = 1;// 0 = 160x90, 1 = 256x144, 2 = 384x216
	float previewW = 0;// largeur de la colonne d'aperçu (0 = défaut)

	// Éditeur
	uint64_t editorDocVersion = (uint64_t)-1;
	int editorTargetId        = -1;
	std::shared_ptr< const ShaderDoc > editorBase;
	std::string editCommon;
	std::vector< std::string > editPasses;
	bool editorDirty = false;
	bool editorSelectImage = false;

	// Préférences
	Settings settings;
	bool showKey = false;
	std::string message;
	float messageTime = 0;
};
UiState* g_ui = nullptr;

const char* kSortKeys[]   = { "popular", "newest", "love", "hot", "name" };
const char* kSortLabels[] = { "Populaires", "Récents", "Les plus aimés", "Tendance", "Nom" };

void Toast( const std::string& msg )
{
	g_ui->message     = msg;
	g_ui->messageTime = (float)ImGui::GetTime();
}

SharedPtr Target()
{
	auto sp = g_ui->target.lock();
	if( sp )
	{
		std::lock_guard< std::mutex > lock( sp->m );
		if( !sp->alive )
			return nullptr;
	}
	return sp;
}

void LoadSummary( const ShaderSummary& s )
{
	SharedPtr t = Target();
	if( !t )
	{
		Toast( "Aucune instance ShaderToyFX active." );
		return;
	}
	std::string path = SaveToCache( s );
	RequestLoadFile( t, path, true );
	g_ui->localDirty = true;
	Toast( "Chargement : " + s.name );
}

void LoadPath( const std::string& path )
{
	SharedPtr t = Target();
	if( !t )
	{
		Toast( "Aucune instance ShaderToyFX active." );
		return;
	}
	RequestLoadFile( t, path, true );
	Toast( "Chargement : " + FileStem( path ) );
}

std::string BrowseForFile( HWND owner )
{
	wchar_t file[ 1024 ] = {};
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof( ofn );
	ofn.hwndOwner   = owner;
	ofn.lpstrFilter = L"Shaders (*.json;*.glsl;*.frag;*.txt;*.sprj;*.zip;*.gdshader;*.shader)\0*.json;*.glsl;*.frag;*.txt;*.sprj;*.zip;*.gdshader;*.shader\0"
					  L"Projets SHADERed (*.sprj;*.zip)\0*.sprj;*.zip\0Shaders Godot (*.gdshader;*.shader)\0*.gdshader;*.shader\0Tous les fichiers\0*.*\0";
	ofn.lpstrFile   = file;
	ofn.nMaxFile    = 1024;
	std::wstring init = Widen( RootDir() );
	ofn.lpstrInitialDir = init.c_str();
	ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
	if( GetOpenFileNameW( &ofn ) )
		return Narrow( file );
	return {};
}

//------------------------------------------------------------------ mode sans clé : import depuis Téléchargements
std::map< std::string, int > g_importFailures;
ULONGLONG g_lastScan = 0;

void ScanDownloads()
{
	if( !g_ui->settings.watchDownloads )
		return;
	ULONGLONG now = GetTickCount64();
	if( now - g_lastScan < 1500 )
		return;
	g_lastScan = now;
	for( const std::string& file : FindDownloadedExports() )
	{
		int& fails = g_importFailures[ file ];
		if( fails >= 5 )
			continue;
		std::string cached, title, err;
		if( !ImportShaderJsonFile( file, cached, title, err ) )
		{
			++fails;// fichier peut-être encore en cours d'écriture : on réessaie
			continue;
		}
		g_importFailures.erase( file );
		g_ui->localDirty = true;
		if( g_ui->settings.autoLoadDownloads && Target() )
			LoadPath( cached );
		else
			Toast( "Importé depuis Téléchargements : " + title );
	}
}

std::string SearchUrl( const std::string& term )
{
	std::string t = Trim( term );
	return t.empty() ? "https://www.shadertoy.com/browse" : "https://www.shadertoy.com/results?query=" + UrlEncode( t );
}

void NoKeyHelp()
{
	ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.16f, 0.14f, 0.10f, 1 ) );
	ImGui::BeginChild( "nokey", ImVec2( 0, 0 ), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY );
	ImGui::TextColored( ImVec4( 1, 0.8f, 0.4f, 1 ), "Mode sans clé API : import via ton navigateur" );
	ImGui::TextWrapped( "1. Clique « Copier le bookmarklet », crée un favori dans ton navigateur et colle le texte comme adresse (URL)." );
	ImGui::TextWrapped( "2. Ouvre un shader sur shadertoy.com et clique ce favori : le shader est téléchargé en « Titre [id].json »." );
	ImGui::TextWrapped( "3. ShaderToyFX le récupère dans tes Téléchargements, le range dans Documents\\ShaderToyFX\\Shadertoy et le charge." );
	if( ImGui::Button( "Copier le bookmarklet" ) )
	{
		ImGui::SetClipboardText( BookmarkletCode() );
		Toast( "Bookmarklet copié : colle-le comme URL d'un nouveau favori." );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Ouvrir shadertoy.com" ) )
		OpenUrl( SearchUrl( g_ui->searchText ) );
	ImGui::SameLine();
	ImGui::Checkbox( "Charger automatiquement", &g_ui->settings.autoLoadDownloads );
	if( DownloadsDir().empty() || !g_ui->settings.watchDownloads )
		ImGui::TextColored( ImVec4( 1, 0.4f, 0.4f, 1 ), "Surveillance des Téléchargements désactivée (Préférences)." );
	else
		ImGui::TextDisabled( "Surveillé : %s", DownloadsDir().c_str() );
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void LicenseBadge( const LicenseInfo& li );

void HelpMarker( const char* text )
{
	ImGui::SameLine();
	ImGui::TextDisabled( "(?)" );
	if( ImGui::BeginItemTooltip() )
	{
		ImGui::PushTextWrapPos( ImGui::GetFontSize() * 35.0f );
		ImGui::TextUnformatted( text );
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

void DrawThumb( const std::string& id, float maxW )
{
	RequestThumb( id );
	Thumb& t = g_thumbs[ id ];
	float h  = maxW * 9.0f / 16.0f;
	if( t.srv && t.w > 0 )
	{
		h = maxW * (float)t.h / (float)t.w;
		ImGui::Image( ImTextureRef( (ImTextureID)(intptr_t)t.srv ), ImVec2( maxW, h ) );
	}
	else
		ImGui::Dummy( ImVec2( maxW, h ) );
}

//------------------------------------------------------------------ onglet SHADERTOY
void TabShadertoy( HWND )
{
	Settings& st = g_ui->settings;
	bool noKey = Trim( st.apiKey ).empty();
	if( noKey )
		NoKeyHelp();

	ImGui::SetNextItemWidth( -ImGui::GetFontSize() * 22 );
	bool enter = ImGui::InputTextWithHint( "##search", "Rechercher (vide = tout parcourir)…", &g_ui->searchText, ImGuiInputTextFlags_EnterReturnsTrue );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( ImGui::GetFontSize() * 8 );
	bool sortChanged = ImGui::Combo( "##sort", &g_ui->sortIdx, kSortLabels, IM_ARRAYSIZE( kSortLabels ) );
	ImGui::SameLine();
	if( noKey && ( ImGui::Button( "Rechercher" ) || enter ) )
		OpenUrl( SearchUrl( g_ui->searchText ) );
	else if( !noKey && ( ImGui::Button( "Rechercher" ) || enter || sortChanged ) )
	{
		g_ui->page         = 0;
		g_ui->searchedOnce = true;
		StartSearch( g_ui->searchText, kSortKeys[ g_ui->sortIdx ], 0, st.perPage );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Aléatoire" ) )
	{
		if( SharedPtr t = Target() )
		{
			RequestRandom( t, Trim( g_ui->settings.apiKey ).empty() ? std::string() : std::string( kRandomOnline ) );
			Toast( "Shader aléatoire demandé…" );
		}
	}

	std::vector< std::string > ids;
	std::map< std::string, std::shared_ptr< ShaderSummary > > info;
	std::set< std::string > failed;
	bool running;
	int total;
	std::string error;
	{
		std::lock_guard< std::mutex > lock( g_search.m );
		ids     = g_search.ids;
		running = g_search.running;
		total   = g_search.total;
		error   = g_search.error;
		failed  = g_search.failed;
		for( auto& id : ids )
		{
			auto it = g_search.info.find( id );
			if( it != g_search.info.end() )
				info[ id ] = it->second;
		}
		if( !g_ui->selectedId.empty() )
		{
			auto it = g_search.info.find( g_ui->selectedId );
			if( it != g_search.info.end() )
				info[ g_ui->selectedId ] = it->second;
		}
	}

	float avail = ImGui::GetContentRegionAvail().x;
	float leftW = avail * 0.55f;
	float footer = ImGui::GetFrameHeightWithSpacing() + 4;

	ImGui::BeginChild( "results", ImVec2( leftW, -footer ), ImGuiChildFlags_None );
	if( !error.empty() )
		ImGui::TextColored( ImVec4( 1, 0.4f, 0.4f, 1 ), "%s", error.c_str() );
	else if( noKey )
		ImGui::TextDisabled( "Sans clé, la recherche s'ouvre dans ton navigateur. Aléatoire pioche dans tes shaders locaux." );
	else if( !g_ui->searchedOnce )
		ImGui::TextDisabled( "Lance une recherche, ou laisse vide pour parcourir tout Shadertoy." );
	ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
	if( ImGui::BeginTable( "tbl", 5, tf ) )
	{
		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableSetupColumn( "TITRE", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableSetupColumn( "AUTEUR", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7 );
		ImGui::TableSetupColumn( "VUES", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4 );
		ImGui::TableSetupColumn( "LIKES", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3 );
		ImGui::TableSetupColumn( "PASSES", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3 );
		ImGui::TableHeadersRow();
		for( auto& id : ids )
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::PushID( id.c_str() );
			auto it                  = info.find( id );
			ShaderSummary* s         = it != info.end() ? it->second.get() : nullptr;
			std::string label        = s ? s->name : ( failed.count( id ) ? id + " (indisponible)" : id + " …" );
			bool selected            = g_ui->selectedId == id;
			if( ImGui::Selectable( label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) )
			{
				g_ui->selectedId = id;
				if( s && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
					LoadSummary( *s );
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted( s ? s->author.c_str() : "" );
			ImGui::TableNextColumn();
			if( s )
				ImGui::Text( "%d", s->views );
			ImGui::TableNextColumn();
			if( s )
				ImGui::Text( "%d", s->likes );
			ImGui::TableNextColumn();
			if( s )
				ImGui::Text( "%d%s", s->passes, s->hasBuffers ? "+" : "" );
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild( "detail", ImVec2( 0, -footer ), ImGuiChildFlags_Borders );
	auto sel = info.find( g_ui->selectedId );
	if( sel != info.end() )
	{
		ShaderSummary& s = *sel->second;
		DrawThumb( s.id, std::min( ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 22 ) );
		ImGui::TextWrapped( "%s", s.name.c_str() );
		ImGui::TextDisabled( "par %s  ·  %d vues  ·  %d likes  ·  id %s", s.author.c_str(), s.views, s.likes, s.id.c_str() );
		if( !s.tags.empty() )
			ImGui::TextDisabled( "tags : %s", s.tags.c_str() );
		LicenseBadge( LicenseForJson( s.json ) );
		if( s.hasSoundPass || s.hasCubePass )
			ImGui::TextColored( ImVec4( 1, 0.75f, 0.3f, 1 ), "Contient une passe %s non supportée.", s.hasCubePass ? "Cube" : "Sound" );
		if( ImGui::Button( "Charger le shader" ) )
			LoadSummary( s );
		ImGui::SameLine();
		if( ImGui::Button( "Voir sur shadertoy.com" ) )
			OpenUrl( "https://www.shadertoy.com/view/" + s.id );
		ImGui::Separator();
		ImGui::PushFont( g_ui->codeFont, 0.0f );
		ImGui::InputTextMultiline( "##code", &s.imageCode, ImVec2( -1, -1 ), ImGuiInputTextFlags_ReadOnly );
		ImGui::PopFont();
	}
	else
		ImGui::TextDisabled( "Sélectionne un shader pour voir son code." );
	ImGui::EndChild();

	// pagination
	int pages = std::max( 1, ( total + st.perPage - 1 ) / st.perPage );
	ImGui::BeginDisabled( g_ui->page <= 0 );
	if( ImGui::Button( "< Préc." ) )
	{
		--g_ui->page;
		StartSearch( g_ui->searchText, kSortKeys[ g_ui->sortIdx ], g_ui->page, st.perPage );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Text( "Page %d / %d", g_ui->page + 1, pages );
	ImGui::SameLine();
	ImGui::BeginDisabled( g_ui->page + 1 >= pages );
	if( ImGui::Button( "Suiv. >" ) )
	{
		++g_ui->page;
		StartSearch( g_ui->searchText, kSortKeys[ g_ui->sortIdx ], g_ui->page, st.perPage );
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled( "%d shaders%s", total, running ? "  · chargement…" : "" );
}

//------------------------------------------------------------------ onglet LIEN
void TabLink()
{
	ImGui::TextWrapped( "Colle un lien Shadertoy (https://www.shadertoy.com/view/XXXXXX), un id à 6 caractères, "
						"ou un lien SHADERed (https://shadered.org/view?s=XXXXXXXXXX)." );
	ImGui::Spacing();
	ImGui::SetNextItemWidth( -ImGui::GetFontSize() * 12 );
	bool enter = ImGui::InputTextWithHint( "##link", "https://www.shadertoy.com/view/…", &g_ui->link, ImGuiInputTextFlags_EnterReturnsTrue );
	ImGui::SameLine();
	if( ImGui::Button( "Coller" ) )
		if( const char* clip = ImGui::GetClipboardText() )
			g_ui->link = clip;
	ImGui::SameLine();
	// lien SHADERed : projet complet (GLSL, GLSL ES, HLSL, Vulkan, Godot) téléchargé depuis shadered.org
	std::string sedId = sed::ParseShaderedId( g_ui->link );
	if( !sedId.empty() )
	{
		if( ImGui::Button( "Charger" ) || enter )
			if( SharedPtr t = Target() )
			{
				RequestLoadShadered( t, sedId );
				g_ui->localDirty = true;
				Toast( "Téléchargement SHADERed " + sedId + "…" );
			}
		ImGui::TextDisabled( "Projet SHADERed : %s", sedId.c_str() );
		ImGui::Spacing();
		ImGui::TextWrapped( "Le projet est enregistré dans Documents\\ShaderToyFX\\SHADERed et apparaît dans la BIBLIOTHÈQUE. "
							"Les shaders Rust et C++ demandent un compilateur externe : exporte-les en GLSL/HLSL depuis SHADERed." );
		return;
	}
	std::string id     = ExtractShaderId( g_ui->link );
	bool noKey         = Trim( g_ui->settings.apiKey ).empty();
	std::string cached = id.empty() ? std::string() : FindCachedPath( id );
	ImGui::BeginDisabled( id.empty() );
	if( ImGui::Button( "Charger" ) || ( enter && !id.empty() ) )
	{
		if( !cached.empty() )
			LoadPath( cached );
		else if( noKey )
		{
			OpenUrl( "https://www.shadertoy.com/view/" + id );
			Toast( "Page ouverte : clique ton favori ShaderToyFX pour l'importer." );
		}
		else if( SharedPtr t = Target() )
		{
			RequestLoadShadertoy( t, id );
			g_ui->localDirty = true;
			Toast( "Téléchargement de " + id + "…" );
		}
	}
	ImGui::EndDisabled();
	if( !g_ui->link.empty() && id.empty() )
		ImGui::TextColored( ImVec4( 1, 0.4f, 0.4f, 1 ), "Lien non reconnu." );
	else if( !id.empty() )
	{
		ImGui::TextDisabled( "id : %s", id.c_str() );
		if( !cached.empty() )
			ImGui::TextDisabled( "déjà en cache : %s", FileStem( cached ).c_str() );
	}
	ImGui::Spacing();
	ImGui::Separator();
	if( noKey )
	{
		ImGui::Spacing();
		NoKeyHelp();
	}
	else
		ImGui::TextWrapped( "Avec la clé API, seuls les shaders publiés en \"Public + API\" sont accessibles. "
							"Pour les autres, utilise le bookmarklet (mode sans clé) ou colle le code dans l'onglet FICHIER." );
}

//------------------------------------------------------------------ onglet FICHIER
void TabFile( HWND hwnd )
{
	if( ImGui::Button( "Parcourir…" ) )
	{
		std::string p = BrowseForFile( hwnd );
		if( !p.empty() )
		{
			g_ui->lastBrowsed = p;
			std::string ext   = FileExtLower( p );
			// projets SHADERed / shaders Godot : copiés dans la bibliothèque (avec textures et modèles)
			if( ext == ".sprj" || ext == ".zip" || IsShaderedPath( p ) )
			{
				if( SharedPtr t = Target() )
				{
					RequestImportShadered( t, p );
					g_ui->localDirty = true;
					Toast( "Import de " + FileStem( p ) + "…" );
				}
			}
			else
				LoadPath( p );
		}
	}
	HelpMarker( "Formats : .json (export ou API Shadertoy, multi-passes) et .glsl/.frag/.txt (une seule passe avec mainImage).\n"
				"SHADERed : projet .sprj (GLSL, GLSL ES, HLSL, GLSL Vulkan, Godot) ou archive .zip téléchargée sur shadered.org.\n"
				"Godot : shader canvas_item .gdshader / .shader (TEXTURE = entrée Resolume, uniforms = sliders).\n"
				"Dans un .glsl : iChannel0 = entrée Resolume, iChannel1 = audio. Annotations possibles :\n"
				"  // @iChannel2 texture.png   // @iChannel0 audio\n"
				"Paramètres : uniform float vitesse; // 0 5 1   (min max défaut) -> slider dans Resolume." );
	if( !g_ui->lastBrowsed.empty() )
	{
		ImGui::SameLine();
		ImGui::TextDisabled( "%s", g_ui->lastBrowsed.c_str() );
	}
	ImGui::Separator();
	ImGui::Text( "Coller du code Shadertoy (passe Image) :" );
	ImGui::SetNextItemWidth( ImGui::GetFontSize() * 16 );
	ImGui::InputText( "Nom", &g_ui->pastedName );
	ImGui::SameLine();
	if( ImGui::Button( "Enregistrer dans Local et charger" ) )
	{
		std::string path = JoinPath( LocalDir(), SanitizeFileName( g_ui->pastedName ) + ".glsl" );
		if( WriteFileText( path, g_ui->pastedCode ) )
		{
			g_ui->localDirty = true;
			LoadPath( path );
		}
		else
			Toast( "Écriture impossible : " + path );
	}
	ImGui::PushFont( g_ui->codeFont, 0.0f );
	ImGui::InputTextMultiline( "##paste", &g_ui->pastedCode, ImVec2( -1, -ImGui::GetFrameHeightWithSpacing() ), ImGuiInputTextFlags_AllowTabInput );
	ImGui::PopFont();
}

//------------------------------------------------------------------ onglet LOCAL
ImVec4 LicenseColor( LicenseKind k )
{
	switch( k )
	{
	case LicenseKind::Commercial: return ImVec4( 0.35f, 0.85f, 0.40f, 1 );
	case LicenseKind::Attribution: return ImVec4( 0.65f, 0.90f, 0.40f, 1 );
	case LicenseKind::NonCommercial: return ImVec4( 1.00f, 0.45f, 0.40f, 1 );
	case LicenseKind::Restricted: return ImVec4( 0.95f, 0.25f, 0.30f, 1 );
	default: return ImVec4( 1.00f, 0.75f, 0.30f, 1 );
	}
}

void LicenseBadge( const LicenseInfo& li )
{
	ImVec4 col = LicenseColor( li.kind );
	ImVec2 p   = ImGui::GetCursorScreenPos();
	float r    = ImGui::GetFontSize() * 0.28f;
	ImGui::GetWindowDrawList()->AddCircleFilled( ImVec2( p.x + r + 1, p.y + ImGui::GetTextLineHeight() * 0.5f ), r, ImGui::GetColorU32( col ) );
	ImGui::Dummy( ImVec2( r * 2 + 4, 0 ) );
	ImGui::SameLine( 0, 0 );
	ImGui::TextColored( col, "%s", LicenseShortName( li.kind ) );
	if( ImGui::BeginItemTooltip() )
	{
		ImGui::PushTextWrapPos( ImGui::GetFontSize() * 32 );
		ImGui::TextColored( col, "%s", li.label.c_str() );
		if( !li.evidence.empty() )
			ImGui::TextDisabled( "Trouvé dans le code : %s", li.evidence.c_str() );
		ImGui::Separator();
		ImGui::TextDisabled( "Détection automatique, indicative : relis toujours les commentaires du shader. "
							 "Sur Shadertoy, sans mention, la licence est CC BY-NC-SA 3.0 (pas d'usage commercial)." );
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

bool LicenseMatches( LicenseKind k, int filter )
{
	switch( filter )
	{
	case 1: return LicenseAllowsCommercial( k );
	case 2: return k == LicenseKind::NonCommercial || k == LicenseKind::Restricted;
	case 3: return k == LicenseKind::Unknown;
	default: return true;
	}
}

std::string FolderLabel( const std::string& rel )
{
	return rel.empty() ? std::string( "(racine)" ) : rel;
}

// Déplace un shader et met à jour les instances qui l'utilisent
bool MoveShader( const std::string& path, const std::string& folder, bool quiet = false )
{
	std::string moved = MoveToFolder( path, folder );
	if( moved.empty() )
	{
		Toast( "Déplacement impossible : " + FileStem( path ) );
		return false;
	}
	if( moved == path )
		return false;
	for( auto& inst : ListInstances() )
	{
		InstanceSnapshot sn = Snapshot( inst );
		if( sn.current && ToLower( sn.current->sourcePath ) == ToLower( path ) )
			RequestLoadFile( inst, moved, true );// met à jour le paramètre Shader de Resolume
	}
	if( g_ui->localSelected == path )
		g_ui->localSelected = moved;
	if( g_ui->localSel.erase( path ) )
		g_ui->localSel.insert( moved );
	g_ui->localDirty = true;
	if( !quiet )
		Toast( FileStem( path ) + " → " + FolderLabel( folder ) );
	return true;
}

// payload du glisser-déposer : chemins séparés par '\n'
void MoveShaders( const std::vector< std::string >& paths, const std::string& folder )
{
	if( paths.size() == 1 )
	{
		MoveShader( paths[ 0 ], folder );
		return;
	}
	int n = 0;
	for( auto& p : paths )
		if( MoveShader( p, folder, true ) )
			++n;
	Toast( std::to_string( n ) + " shaders → " + FolderLabel( folder ) );
}

std::vector< std::string > SplitLines( const std::string& s )
{
	std::vector< std::string > out;
	size_t a = 0;
	while( a < s.size() )
	{
		size_t b = s.find( '\n', a );
		if( b == std::string::npos )
			b = s.size();
		if( b > a )
			out.push_back( s.substr( a, b - a ) );
		a = b + 1;
	}
	return out;
}

std::vector< std::string > SelectedPaths()
{
	return std::vector< std::string >( g_ui->localSel.begin(), g_ui->localSel.end() );
}

const char* const kFavKey = "\x01" "favoris";// dossier virtuel « Favoris »

// Étoile dessinée (la police de l'interface n'a pas forcément le glyphe ★)
void DrawStar( ImVec2 p, float size, bool on, bool hov )
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec2 c( p.x + size * 0.5f, p.y + size * 0.52f );
	float R = size * 0.47f, r = R * 0.45f;
	ImVec2 pts[ 10 ];
	for( int i = 0; i < 10; ++i )
	{
		float a   = -1.5707963f + i * 0.6283185f;
		float rad = ( i & 1 ) ? r : R;
		pts[ i ]  = ImVec2( c.x + std::cos( a ) * rad, c.y + std::sin( a ) * rad );
	}
	ImU32 gold = IM_COL32( 255, 196, 40, 255 );
	ImU32 col  = on ? gold : ( hov ? IM_COL32( 220, 220, 220, 255 ) : IM_COL32( 110, 110, 110, 255 ) );
	if( on )
		for( int i = 0; i < 10; ++i )
			dl->AddTriangleFilled( c, pts[ i ], pts[ ( i + 1 ) % 10 ], gold );
	dl->AddPolyline( pts, 10, col, ImDrawFlags_Closed, 1.2f );
}

// Étoile cliquable
bool StarToggle( const char* id, bool on, float size )
{
	ImVec2 p     = ImGui::GetCursorScreenPos();
	bool clicked = ImGui::InvisibleButton( id, ImVec2( size, size ) );
	bool hov     = ImGui::IsItemHovered();
	DrawStar( p, size, on, hov );
	if( hov )
		ImGui::SetTooltip( on ? "Retirer des favoris" : "Ajouter aux favoris" );
	return clicked;
}

// Ajoute / retire des favoris une liste de shaders (tous deviennent favoris si l'un ne l'est pas)
void ToggleFavorites( const std::vector< std::string >& paths )
{
	bool allFav = true;
	for( auto& p : paths )
		allFav = allFav && IsFavorite( p );
	for( auto& p : paths )
		SetFavorite( p, !allFav );
	if( paths.size() == 1 )
		Toast( FileStem( paths[ 0 ] ) + ( allFav ? " retiré des favoris" : " ajouté aux favoris" ) );
	else
		Toast( std::to_string( paths.size() ) + ( allFav ? " shaders retirés des favoris" : " shaders ajoutés aux favoris" ) );
}

void FolderEntry( const char* label, const std::string& key, int count )
{
	std::string text = std::string( label ) + "  (" + std::to_string( count ) + ")";
	if( ImGui::Selectable( text.c_str(), g_ui->folderFilter == key ) )
		g_ui->folderFilter = key;
	// déposer un shader sur un dossier pour l'y ranger
	if( key != "*" && ImGui::BeginDragDropTarget() )
	{
		if( const ImGuiPayload* pl = ImGui::AcceptDragDropPayload( "STFX_SHADER" ) )
		{
			auto paths = SplitLines( std::string( (const char*)pl->Data, pl->DataSize ) );
			if( key == kFavKey )
			{
				// déposer sur « Favoris » ajoute aux favoris sans déplacer le fichier
				for( auto& p : paths )
					SetFavorite( p, true );
				Toast( std::to_string( paths.size() ) + " shader(s) ajouté(s) aux favoris" );
			}
			else
				MoveShaders( paths, key );
		}
		ImGui::EndDragDropTarget();
	}
}

// Colonne d'aperçu animé du shader sélectionné
void PreviewPane( float paneW, float footer )
{
	ImGui::BeginChild( "preview", ImVec2( paneW, -footer ), ImGuiChildFlags_Borders );
	ImGui::TextDisabled( "APERÇU" );

	// shader à montrer : le dernier cliqué s'il est sélectionné, sinon le seul sélectionné
	std::string pv;
	auto& sel = g_ui->localSel;
	if( !g_ui->localSelected.empty() && sel.count( g_ui->localSelected ) )
		pv = g_ui->localSelected;
	else if( sel.size() == 1 )
		pv = *sel.begin();

	static const int kRes[ 3 ][ 2 ] = { { 160, 90 }, { 256, 144 }, { 384, 216 } };
	const int* res = kRes[ std::clamp( g_ui->previewRes, 0, 2 ) ];
	if( !pv.empty() && g_ui->previewOn )
		Preview::Request( pv, res[ 0 ], res[ 1 ] );

	float iw = ImGui::GetContentRegionAvail().x;
	float ih = iw * 9.0f / 16.0f;
	ImVec2 p0 = ImGui::GetCursorScreenPos();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled( p0, ImVec2( p0.x + iw, p0.y + ih ), IM_COL32( 0, 0, 0, 255 ) );
	Preview::Status st = Preview::GetStatus();
	bool showImage     = !pv.empty() && g_previewSrv && g_previewFrame.path == pv && !( st.path == pv && ( st.compiling || !st.error.empty() ) );
	if( showImage )
		ImGui::Image( ImTextureRef( (ImTextureID)(intptr_t)g_previewSrv ), ImVec2( iw, ih ) );
	else
		ImGui::Dummy( ImVec2( iw, ih ) );
	if( showImage && ImGui::BeginItemTooltip() )
	{
		ImGui::TextUnformatted( "Paramètres à leur valeur d'origine. Les effets (shaders qui lisent l'entrée\n"
								"Resolume) sont montrés sur une image de test." );
		ImGui::EndTooltip();
	}
	if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && !pv.empty() )
		LoadPath( pv );
	// message au centre de l'image
	auto centerText = [ & ]( const char* text, ImU32 col ) {
		ImVec2 ts = ImGui::CalcTextSize( text, nullptr, false, iw - 16 );
		dl->AddText( ImGui::GetFont(), ImGui::GetFontSize(), ImVec2( p0.x + ( iw - ts.x ) * 0.5f, p0.y + ( ih - ts.y ) * 0.5f ), col, text, nullptr, iw - 16 );
	};
	if( pv.empty() )
		centerText( "Clique un shader", IM_COL32( 140, 140, 140, 255 ) );
	else if( !g_ui->previewOn && !showImage )
		centerText( "Aperçu en pause", IM_COL32( 140, 140, 140, 255 ) );
	else if( st.path != pv || st.compiling )
		centerText( "Compilation…", IM_COL32( 200, 200, 200, 255 ) );
	else if( !st.error.empty() )
		centerText( "Erreur de compilation", IM_COL32( 255, 110, 110, 255 ) );

	if( !pv.empty() )
	{
		bool fav = IsFavorite( pv );
		if( StarToggle( "##pvfav", fav, ImGui::GetFrameHeight() ) )
			ToggleFavorites( { pv } );
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextWrapped( "%s", FileStem( pv ).c_str() );

		if( st.path == pv && !st.error.empty() )
		{
			ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1.0f, 0.45f, 0.45f, 1.0f ) );
			ImGui::TextWrapped( "%s", st.error.c_str() );
			ImGui::PopStyleColor();
		}
		else if( st.path == pv && !st.compiling && g_ui->previewOn )
			ImGui::TextDisabled( "%.0f i/s  ·  %dx%d  ·  %.1f s", st.fps, res[ 0 ], res[ 1 ], st.time );

		if( ImGui::Button( "Charger" ) )
			LoadPath( pv );
		ImGui::SameLine();
		if( ImGui::Button( fav ? "Retirer des favoris" : "Favori" ) )
			ToggleFavorites( { pv } );
		ImGui::SameLine();
		ImGui::BeginDisabled( !g_ui->previewOn );
		if( ImGui::Button( "Rejouer" ) )
			Preview::Restart();
		ImGui::EndDisabled();
	}

	ImGui::Spacing();
	ImGui::Checkbox( "Animé", &g_ui->previewOn );
	if( ImGui::BeginItemTooltip() )
	{
		ImGui::TextUnformatted( "Décoche pour figer l'aperçu (n'utilise plus le GPU)." );
		ImGui::EndTooltip();
	}
	ImGui::SameLine();
	const char* resItems[] = { "160x90", "256x144", "384x216" };
	ImGui::SetNextItemWidth( -1 );
	ImGui::Combo( "##pvres", &g_ui->previewRes, resItems, IM_ARRAYSIZE( resItems ) );
	ImGui::EndChild();
}

void TabLocal()
{
	if( g_ui->localDirty )
	{
		g_ui->localFiles = ListShaderFiles( RootDir() );
		g_ui->folders    = ListFolders();
		g_ui->localLicenses.clear();
		for( auto& e : g_ui->localFiles )
			g_ui->localLicenses.push_back( LicenseForFile( e.path ) );
		g_ui->localDirty = false;
	}
	auto& files = g_ui->localFiles;
	auto& lics  = g_ui->localLicenses;

	// barre d'outils
	ImGui::SetNextItemWidth( ImGui::GetFontSize() * 14 );
	ImGui::InputTextWithHint( "##filter", "Filtrer par nom…", &g_ui->localFilter );
	ImGui::SameLine();
	const char* licItems[] = { "Toutes les licences", "Commercial OK", "Non commercial / interdit", "À vérifier" };
	ImGui::SetNextItemWidth( ImGui::GetFontSize() * 12 );
	ImGui::Combo( "##lic", &g_ui->licenseFilter, licItems, IM_ARRAYSIZE( licItems ) );
	ImGui::SameLine();
	if( ImGui::Button( "Actualiser" ) )
		g_ui->localDirty = true;
	ImGui::SameLine();
	{
		bool favs       = g_ui->folderFilter == kFavKey;
		std::string src = g_ui->folderFilter == "*" ? std::string() : favs ? std::string( kRandomFavorites ) : g_ui->folderFilter;
		std::string lbl = favs ? "Aléatoire dans les favoris" : src.empty() ? "Aléatoire (tout)" : "Aléatoire dans « " + src + " »";
		if( ImGui::Button( lbl.c_str() ) )
		{
			if( SharedPtr t = Target() )
			{
				RequestRandom( t, src );
				Toast( lbl + "…" );
			}
		}
		if( ImGui::BeginItemTooltip() )
		{
			ImGui::TextUnformatted( "Charge un shader au hasard dans le dossier sélectionné à gauche (sous-dossiers compris).\n"
									"Dans Resolume, le réglage « Random From » fait la même chose pour le bouton Random Shadertoy." );
			ImGui::EndTooltip();
		}
	}
	ImGui::SameLine();
	if( ImGui::Button( "Ouvrir le dossier" ) )
		OpenInExplorer( g_ui->folderFilter == "*" || g_ui->folderFilter.empty() || g_ui->folderFilter == kFavKey ? RootDir() : JoinPath( RootDir(), g_ui->folderFilter ) );

	float footer = ImGui::GetFrameHeightWithSpacing() * 2 + 8;

	// colonne des dossiers
	ImGui::BeginChild( "folders", ImVec2( ImGui::GetFontSize() * 14, -footer ), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX );
	ImGui::TextDisabled( "DOSSIERS" );
	std::map< std::string, int > counts;
	int favCount = 0;
	for( auto& e : files )
	{
		counts[ e.relFolder ]++;
		if( IsFavorite( e.path ) )
			++favCount;
	}
	FolderEntry( "Tous", "*", (int)files.size() );
	FolderEntry( "      Favoris", kFavKey, favCount );
	DrawStar( ImGui::GetItemRectMin(), ImGui::GetTextLineHeight(), true, false );
	if( counts.count( "" ) )
		FolderEntry( "(racine)", "", counts[ "" ] );
	for( auto& f : g_ui->folders )
		FolderEntry( f.c_str(), f, counts.count( f ) ? counts[ f ] : 0 );
	ImGui::Separator();
	ImGui::SetNextItemWidth( -1 );
	bool create = ImGui::InputTextWithHint( "##newfolder", "Nouveau dossier…", &g_ui->newFolderName, ImGuiInputTextFlags_EnterReturnsTrue );
	if( ImGui::Button( "Créer" ) || create )
	{
		std::string name = SanitizeFileName( g_ui->newFolderName );
		if( !Trim( g_ui->newFolderName ).empty() )
		{
			MakeDirs( JoinPath( RootDir(), name ) );
			g_ui->folderFilter  = name;
			g_ui->newFolderName.clear();
			g_ui->localDirty = true;
		}
	}
	bool canDelete = g_ui->folderFilter != "*" && g_ui->folderFilter != kFavKey && !g_ui->folderFilter.empty() && !counts.count( g_ui->folderFilter ) &&
					 g_ui->folderFilter != "Shadertoy" && g_ui->folderFilter != "Local";
	ImGui::SameLine();
	ImGui::BeginDisabled( !canDelete );
	if( ImGui::Button( "Supprimer (vide)" ) )
	{
		std::error_code ec;
		std::filesystem::remove( std::filesystem::path( Widen( JoinPath( RootDir(), g_ui->folderFilter ) ) ), ec );
		g_ui->folderFilter = "*";
		g_ui->localDirty   = true;
	}
	ImGui::EndDisabled();
	ImGui::PushStyleColor( ImGuiCol_Text, ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) );
	ImGui::TextWrapped( "Glisse un shader sur un dossier pour l'y ranger, ou sur Favoris pour le marquer." );
	ImGui::PopStyleColor();
	ImGui::EndChild();

	ImGui::SameLine();

	// liste des shaders visibles
	std::string filter = ToLower( g_ui->localFilter );
	std::vector< size_t > visible;
	int okCount = 0;
	for( size_t i = 0; i < files.size(); ++i )
	{
		const LocalEntry& e   = files[ i ];
		const LicenseInfo& li = lics[ i ];
		if( g_ui->folderFilter == kFavKey )
		{
			if( !IsFavorite( e.path ) )
				continue;
		}
		else if( g_ui->folderFilter != "*" && e.relFolder != g_ui->folderFilter )
			continue;
		if( !filter.empty() && ToLower( e.name ).find( filter ) == std::string::npos && ToLower( li.author ).find( filter ) == std::string::npos )
			continue;
		if( !LicenseMatches( li.kind, g_ui->licenseFilter ) )
			continue;
		visible.push_back( i );
		if( LicenseAllowsCommercial( li.kind ) )
			++okCount;
	}
	// nettoie la sélection des fichiers disparus
	{
		std::set< std::string > existing;
		for( auto& e : files )
			existing.insert( e.path );
		for( auto it = g_ui->localSel.begin(); it != g_ui->localSel.end(); )
			it = existing.count( *it ) ? std::next( it ) : g_ui->localSel.erase( it );
	}

	auto& sel = g_ui->localSel;
	float previewPaneW = std::max( ImGui::GetFontSize() * 17.0f, 270.0f );
	bool tableOpen     = ImGui::BeginTable( "local", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
										ImVec2( -( previewPaneW + ImGui::GetStyle().ItemSpacing.x ), -footer ) );
	if( tableOpen )
	{
		// Ctrl+A : tout sélectionner (liste visible)
		if( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !ImGui::GetIO().WantTextInput &&
			ImGui::IsKeyDown( ImGuiMod_Ctrl ) && ImGui::IsKeyPressed( ImGuiKey_A ) )
			for( size_t i : visible )
				sel.insert( files[ i ].path );

		ImGui::TableSetupScrollFreeze( 0, 1 );
		ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, ImGui::GetTextLineHeight() );
		ImGui::TableSetupColumn( "NOM", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableSetupColumn( "LICENCE", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 12 );
		ImGui::TableSetupColumn( "AUTEUR", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7 );
		ImGui::TableSetupColumn( "DOSSIER", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8 );
		ImGui::TableHeadersRow();
		for( size_t vi = 0; vi < visible.size(); ++vi )
		{
			const LocalEntry& e   = files[ visible[ vi ] ];
			const LicenseInfo& li = lics[ visible[ vi ] ];
			bool isSel            = sel.count( e.path ) > 0;
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 1 );
			ImGui::PushID( e.path.c_str() );
			if( ImGui::Selectable( e.name.c_str(), isSel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap ) )
			{
				ImGuiIO& io = ImGui::GetIO();
				if( io.KeyShift && !g_ui->localAnchor.empty() )
				{
					// Maj+clic : plage depuis le dernier clic
					size_t a = vi;
					for( size_t k = 0; k < visible.size(); ++k )
						if( files[ visible[ k ] ].path == g_ui->localAnchor )
							a = k;
					if( !io.KeyCtrl )
						sel.clear();
					for( size_t k = std::min( a, vi ); k <= std::max( a, vi ); ++k )
						sel.insert( files[ visible[ k ] ].path );
				}
				else if( io.KeyCtrl )
				{
					// Ctrl+clic : ajoute / retire
					if( isSel )
						sel.erase( e.path );
					else
						sel.insert( e.path );
					g_ui->localAnchor = e.path;
				}
				else
				{
					sel.clear();
					sel.insert( e.path );
					g_ui->localAnchor = e.path;
					if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
						LoadPath( e.path );
				}
				g_ui->localSelected = e.path;
			}
			if( ImGui::BeginDragDropSource() )
			{
				// on glisse toute la sélection si la ligne en fait partie, sinon juste cette ligne
				std::vector< std::string > paths = sel.count( e.path ) ? SelectedPaths() : std::vector< std::string >{ e.path };
				std::string payload;
				for( auto& p : paths )
					payload += p + "\n";
				ImGui::SetDragDropPayload( "STFX_SHADER", payload.data(), payload.size() );
				if( paths.size() == 1 )
					ImGui::Text( "Ranger « %s »…", e.name.c_str() );
				else
					ImGui::Text( "Ranger %d shaders…", (int)paths.size() );
				ImGui::EndDragDropSource();
			}
			if( ImGui::BeginPopupContextItem() )
			{
				if( !sel.count( e.path ) )
				{
					sel.clear();
					sel.insert( e.path );
					g_ui->localAnchor = e.path;
				}
				g_ui->localSelected = e.path;
				int n = (int)sel.size();
				if( ImGui::MenuItem( "Charger", nullptr, false, n == 1 ) )
					LoadPath( e.path );
				{
					bool allFav = true;
					for( auto& p : sel )
						allFav = allFav && IsFavorite( p );
					if( ImGui::MenuItem( allFav ? "Retirer des favoris" : "Ajouter aux favoris" ) )
						ToggleFavorites( SelectedPaths() );
				}
				std::string moveLabel = n > 1 ? "Déplacer les " + std::to_string( n ) + " shaders vers" : std::string( "Déplacer vers" );
				if( ImGui::BeginMenu( moveLabel.c_str() ) )
				{
					for( auto& f : g_ui->folders )
						if( ImGui::MenuItem( f.c_str() ) )
							MoveShaders( SelectedPaths(), f );
					ImGui::EndMenu();
				}
				if( ImGui::MenuItem( "Afficher dans l'explorateur" ) )
					OpenInExplorer( ParentDir( e.path ) );
				ImGui::Separator();
				std::string recLabel = n > 1 ? "Mettre les " + std::to_string( n ) + " shaders à la corbeille" : std::string( "Mettre à la corbeille" );
				if( ImGui::MenuItem( recLabel.c_str() ) )
					g_ui->confirmRecycle = true;
				ImGui::EndPopup();
			}
			// étoile (soumise après la ligne pour rester cliquable par-dessus)
			ImGui::TableSetColumnIndex( 0 );
			if( StarToggle( "##fav", IsFavorite( e.path ), ImGui::GetTextLineHeight() ) )
				ToggleFavorites( { e.path } );
			ImGui::TableSetColumnIndex( 2 );
			LicenseBadge( li );
			ImGui::TableNextColumn();
			ImGui::TextUnformatted( li.author.c_str() );
			ImGui::TableNextColumn();
			ImGui::TextDisabled( "%s", FolderLabel( e.relFolder ).c_str() );
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if( tableOpen )
		ImGui::SameLine();
	PreviewPane( previewPaneW, footer );

	// confirmation corbeille
	if( g_ui->confirmRecycle )
	{
		ImGui::OpenPopup( "Corbeille" );
		g_ui->confirmRecycle = false;
	}
	if( ImGui::BeginPopupModal( "Corbeille", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
	{
		ImGui::Text( "Mettre %d shader(s) à la corbeille Windows ?", (int)sel.size() );
		ImGui::TextDisabled( "Tu pourras les restaurer depuis la corbeille." );
		if( ImGui::Button( "Oui, à la corbeille" ) )
		{
			int n = 0;
			for( auto& p : SelectedPaths() )
				if( RecycleFile( p ) )
					++n;
			sel.clear();
			g_ui->localSelected.clear();
			g_ui->localDirty = true;
			Toast( std::to_string( n ) + " shader(s) mis à la corbeille." );
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if( ImGui::Button( "Annuler" ) )
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// actions sur la sélection
	int nSel = (int)sel.size();
	ImGui::TextDisabled( "%d shader(s) affiché(s), dont %d utilisable(s) commercialement  ·  %d sélectionné(s)  ·  Ctrl+clic, Maj+clic, Ctrl+A pour sélectionner plusieurs.",
						 (int)visible.size(), okCount, nSel );
	ImGui::BeginDisabled( nSel != 1 );
	if( ImGui::Button( "Charger" ) )
		LoadPath( *sel.begin() );
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( nSel == 0 );
	ImGui::SetNextItemWidth( ImGui::GetFontSize() * 12 );
	std::string comboLabel = nSel > 1 ? "Déplacer les " + std::to_string( nSel ) + "…" : std::string( "Déplacer vers…" );
	if( ImGui::BeginCombo( "##moveto", comboLabel.c_str() ) )
	{
		for( auto& f : g_ui->folders )
			if( ImGui::Selectable( f.c_str() ) )
				MoveShaders( SelectedPaths(), f );
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if( ImGui::Button( "Favoris ±" ) )
		ToggleFavorites( SelectedPaths() );
	if( ImGui::BeginItemTooltip() )
	{
		ImGui::TextUnformatted( "Ajoute la sélection aux favoris (ou l'en retire si tout y est déjà)." );
		ImGui::EndTooltip();
	}
	ImGui::SameLine();
	if( ImGui::Button( "Corbeille…" ) )
		g_ui->confirmRecycle = true;
	ImGui::SameLine();
	if( ImGui::Button( "Tout désélectionner" ) )
		sel.clear();
	ImGui::EndDisabled();
}

//------------------------------------------------------------------ onglet ÉDITEUR
void SyncEditor( const InstanceSnapshot& snap )
{
	if( !snap.current )
		return;
	bool targetChanged = snap.id != g_ui->editorTargetId;
	if( !targetChanged && ( snap.docVersion == g_ui->editorDocVersion || g_ui->editorDirty ) )
		return;
	g_ui->editorTargetId   = snap.id;
	g_ui->editorDocVersion = snap.docVersion;
	g_ui->editorBase       = snap.current;
	g_ui->editCommon       = snap.current->common;
	g_ui->editPasses.clear();
	for( auto& p : snap.current->passes )
		g_ui->editPasses.push_back( p.code );
	g_ui->editorDirty       = false;
	g_ui->editorSelectImage = true;
}

ShaderDoc EditedDoc()
{
	ShaderDoc doc = *g_ui->editorBase;
	doc.common    = g_ui->editCommon;
	for( size_t i = 0; i < doc.passes.size() && i < g_ui->editPasses.size(); ++i )
		doc.passes[ i ].code = g_ui->editPasses[ i ];
	doc.warnings.clear();
	return doc;
}

void TabEditor( const InstanceSnapshot& snap )
{
	SyncEditor( snap );
	if( !g_ui->editorBase )
	{
		ImGui::TextDisabled( "Aucun shader chargé." );
		return;
	}
	const ShaderDoc& base = *g_ui->editorBase;
	if( base.IsShadered() )
	{
		// projet SHADERed / shader Godot : plusieurs fichiers, langages variés -> édition dans leur éditeur
		ImGui::Text( "%s", base.title.c_str() );
		ImGui::TextDisabled( "Projet %s  ·  %s", base.languages.c_str(), base.sourcePath.c_str() );
		ImGui::Spacing();
		ImGui::TextWrapped( "Ce shader vient de SHADERed ou de Godot : modifie ses fichiers dans SHADERed (ou un éditeur de texte), "
							"puis recharge-le ici. Les variables du projet et les uniforms Godot sont des sliders dans Resolume." );
		if( ImGui::Button( "Ouvrir le dossier" ) )
			OpenInExplorer( ParentDir( base.sourcePath ) );
		ImGui::SameLine();
		if( ImGui::Button( "Recharger" ) )
			LoadPath( base.sourcePath );
		for( auto& w : base.warnings )
			ImGui::TextColored( ImVec4( 1.0f, 0.75f, 0.3f, 1.0f ), "%s", w.c_str() );
		return;
	}
	ImGui::Text( "%s", base.title.c_str() );
	if( !base.sourcePath.empty() )
	{
		ImGui::SameLine();
		LicenseBadge( LicenseForFile( base.sourcePath ) );
	}
	if( !base.sourcePath.empty() )
	{
		ImGui::SameLine();
		ImGui::TextDisabled( "%s", base.sourcePath.c_str() );
	}

	bool apply = ImGui::Button( "Appliquer (Ctrl+Entrée)" ) ||
				 ( ImGui::IsKeyDown( ImGuiMod_Ctrl ) && ImGui::IsKeyPressed( ImGuiKey_Enter ) );
	ImGui::SameLine();
	bool save = ImGui::Button( "Enregistrer (Ctrl+S)" ) || ( ImGui::IsKeyDown( ImGuiMod_Ctrl ) && ImGui::IsKeyPressed( ImGuiKey_S ) );
	ImGui::SameLine();
	if( ImGui::Button( "Annuler les modifs" ) )
	{
		g_ui->editorDirty      = false;
		g_ui->editorDocVersion = (uint64_t)-1;
	}
	if( g_ui->editorDirty )
	{
		ImGui::SameLine();
		ImGui::TextColored( ImVec4( 1, 0.75f, 0.3f, 1 ), "modifié" );
	}

	SharedPtr t = Target();
	if( apply && t )
		RequestApplyDoc( t, EditedDoc(), false );
	if( save && t )
	{
		ShaderDoc doc = EditedDoc();
		std::string ext;
		std::string text = SerializeDoc( doc, ext );
		std::string path = doc.sourcePath;
		bool inRoot      = !path.empty() && ToLower( path ).rfind( ToLower( RootDir() ), 0 ) == 0 && FileExtLower( path ) == ext;
		if( !inRoot )
			path = JoinPath( LocalDir(), SanitizeFileName( doc.title ) + ext );
		if( WriteFileText( path, text ) )
		{
			doc.sourcePath = path;
			RequestApplyDoc( t, doc, true );
			g_ui->editorDirty = false;
			g_ui->localDirty  = true;
			Toast( "Enregistré : " + path );
		}
		else
			Toast( "Écriture impossible : " + path );
	}

	// paramètres détectés (sliders "Shader Params" dans Resolume)
	{
		std::string hdr = "Paramètres réglables dans Resolume (" + std::to_string( base.params.size() ) + ")###params";
		if( ImGui::CollapsingHeader( hdr.c_str() ) )
		{
			if( base.params.empty() )
				ImGui::TextDisabled( "Aucun : ajoute par ex. « uniform float vitesse; // 0 5 1 » ou un « #define VITESSE 1.0 »." );
			else if( ImGui::BeginTable( "ptab", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp ) )
			{
				ImGui::TableSetupColumn( "SLIDER" );
				ImGui::TableSetupColumn( "PLAGE" );
				ImGui::TableSetupColumn( "DÉFAUT" );
				ImGui::TableSetupColumn( "SOURCE" );
				ImGui::TableHeadersRow();
				for( auto& p : base.params )
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted( p.name.c_str() );
					ImGui::TableNextColumn();
					ImGui::Text( p.isInt ? "%.0f … %.0f" : "%.3g … %.3g", p.minV, p.maxV );
					ImGui::TableNextColumn();
					ImGui::Text( p.isInt ? "%.0f" : "%.3g", p.def );
					ImGui::TableNextColumn();
					ImGui::TextDisabled( "%s", p.source == ParamSource::Uniform ? "uniform" : p.source == ParamSource::Define ? "#define" : "const" );
				}
				ImGui::EndTable();
			}
			ImGui::TextDisabled( "Plage personnalisée : ajoute « // min max défaut » en fin de ligne, puis Appliquer. Max %d sliders.", kMaxUserParams );
		}
	}

	if( ImGui::BeginTabBar( "passes" ) )
	{
		ImGui::PushFont( g_ui->codeFont, 0.0f );
		ImVec2 size( -1, -1 );
		ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
		if( !base.common.empty() || !base.rawJson.empty() )
		{
			if( ImGui::BeginTabItem( "Common" ) )
			{
				if( ImGui::InputTextMultiline( "##common", &g_ui->editCommon, size, flags ) )
					g_ui->editorDirty = true;
				ImGui::EndTabItem();
			}
		}
		for( size_t i = 0; i < base.passes.size() && i < g_ui->editPasses.size(); ++i )
		{
			ImGuiTabItemFlags tif = ( base.passes[ i ].isImage && g_ui->editorSelectImage ) ? ImGuiTabItemFlags_SetSelected : 0;
			if( ImGui::BeginTabItem( base.passes[ i ].name.c_str(), nullptr, tif ) )
			{
				ImGui::PushID( (int)i );
				if( ImGui::InputTextMultiline( "##pass", &g_ui->editPasses[ i ], size, flags ) )
					g_ui->editorDirty = true;
				ImGui::PopID();
				ImGui::EndTabItem();
			}
		}
		ImGui::PopFont();
		ImGui::EndTabBar();
		g_ui->editorSelectImage = false;
	}
}

//------------------------------------------------------------------ onglet PRÉFÉRENCES
void TabPrefs()
{
	Settings& st = g_ui->settings;
	ImGui::SeparatorText( "Clé API Shadertoy" );
	ImGui::TextWrapped( "Nécessaire pour rechercher, charger par lien et le bouton Random. Crée-la gratuitement "
						"sur shadertoy.com (compte > Config > Your Apps)." );
	ImGui::SetNextItemWidth( ImGui::GetFontSize() * 16 );
	ImGui::InputText( "##key", &st.apiKey, g_ui->showKey ? 0 : ImGuiInputTextFlags_Password );
	ImGui::SameLine();
	ImGui::Checkbox( "Afficher", &g_ui->showKey );
	ImGui::SameLine();
	if( ImGui::Button( "Créer une clé…" ) )
		OpenUrl( "https://www.shadertoy.com/myapps" );

	ImGui::TextDisabled( "Facultative : sans clé, utilise le bookmarklet (onglet SHADERTOY)." );

	ImGui::SeparatorText( "Import depuis le navigateur" );
	ImGui::Checkbox( "Surveiller le dossier Téléchargements", &st.watchDownloads );
	ImGui::SameLine();
	ImGui::Checkbox( "Charger automatiquement", &st.autoLoadDownloads );
	if( ImGui::Button( "Copier le bookmarklet" ) )
	{
		ImGui::SetClipboardText( BookmarkletCode() );
		Toast( "Bookmarklet copié." );
	}

	ImGui::SeparatorText( "Navigation" );
	ImGui::SetNextItemWidth( ImGui::GetFontSize() * 8 );
	ImGui::SliderInt( "Résultats par page", &st.perPage, 5, 100 );
	ImGui::Checkbox( "Random : éviter les shaders avec passe Sound/Cube", &st.skipUnsupported );
	if( ImGui::Checkbox( "Fenêtre toujours au premier plan", &g_ui->topmost ) )
		if( HWND h = g_hwnd.load() )
			SetWindowPos( h, g_ui->topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );

	ImGui::SeparatorText( "Dossiers" );
	ImGui::TextDisabled( "%s", RootDir().c_str() );
	if( ImGui::Button( "Ouvrir" ) )
		OpenInExplorer( RootDir() );

	ImGui::Spacing();
	if( ImGui::Button( "Enregistrer les préférences" ) )
	{
		st.apiKey = Trim( st.apiKey );
		SaveSettings( st );
		Toast( "Préférences enregistrées." );
	}
}

//------------------------------------------------------------------ fenêtre principale
void DrawUi( HWND hwnd )
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos( vp->WorkPos );
	ImGui::SetNextWindowSize( vp->WorkSize );
	ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
	ImGui::Begin( "##main", nullptr, wf );

	// cible
	auto instances     = ListInstances();
	SharedPtr target   = Target();
	if( !target && !instances.empty() )
	{
		g_ui->target = instances.back();
		target       = instances.back();
	}
	InstanceSnapshot snap = Snapshot( target );

	ImGui::TextDisabled( "Instance :" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( ImGui::GetFontSize() * 14 );
	std::string curLabel = target ? "#" + std::to_string( snap.id ) + "  " + ( snap.current ? snap.current->title : "" ) : "(aucune)";
	if( ImGui::BeginCombo( "##inst", curLabel.c_str() ) )
	{
		for( auto& inst : instances )
		{
			InstanceSnapshot s = Snapshot( inst );
			std::string label  = "#" + std::to_string( s.id ) + "  " + ( s.current ? s.current->title : "" );
			if( ImGui::Selectable( label.c_str(), inst == target ) )
				g_ui->target = inst;
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if( snap.busy )
		ImGui::TextColored( ImVec4( 0.5f, 0.8f, 1, 1 ), "%s", snap.status.c_str() );
	else if( !snap.lastBuildOk )
		ImGui::TextColored( ImVec4( 1, 0.4f, 0.4f, 1 ), "%s", snap.status.c_str() );
	else
		ImGui::TextDisabled( "%s", snap.status.c_str() );
	if( target )
	{
		ImGui::SameLine( ImGui::GetWindowWidth() - ImGui::GetFontSize() * 9 );
		if( ImGui::SmallButton( "Restart" ) )
			RequestRestart( target );
	}

	// message temporaire
	if( !g_ui->message.empty() && ImGui::GetTime() - g_ui->messageTime < 4.0 )
		ImGui::TextColored( ImVec4( 0.6f, 1, 0.6f, 1 ), "%s", g_ui->message.c_str() );

	float logH = 0;
	if( !snap.log.empty() )
		logH = ImGui::GetTextLineHeightWithSpacing() * std::min( 8, 1 + (int)std::count( snap.log.begin(), snap.log.end(), '\n' ) ) + 12;

	ImGui::BeginChild( "tabs", ImVec2( 0, -logH ) );
	if( ImGui::BeginTabBar( "main" ) )
	{
		const char* names[] = { "SHADERTOY", "LIEN", "FICHIER", "LOCAL", "PRÉFÉRENCES", "ÉDITEUR" };
		int order[]         = { 0, 1, 2, 3, 5, 4 };// affichage : … LOCAL, ÉDITEUR, PRÉFÉRENCES
		for( int k = 0; k < 6; ++k )
		{
			int i                 = order[ k ];
			ImGuiTabItemFlags tif = g_ui->forceTab == i ? ImGuiTabItemFlags_SetSelected : 0;
			if( ImGui::BeginTabItem( names[ i ], nullptr, tif ) )
			{
				ImGui::Spacing();
				switch( i )
				{
				case 0: TabShadertoy( hwnd ); break;
				case 1: TabLink(); break;
				case 2: TabFile( hwnd ); break;
				case 3: TabLocal(); break;
				case 4: TabPrefs(); break;
				case 5: TabEditor( snap ); break;
				}
				ImGui::EndTabItem();
			}
		}
		g_ui->forceTab = -1;
		ImGui::EndTabBar();
	}
	ImGui::EndChild();

	if( !snap.log.empty() )
	{
		ImGui::BeginChild( "log", ImVec2( 0, 0 ), ImGuiChildFlags_Borders );
		ImGui::PushFont( g_ui->codeFont, 0.0f );
		ImVec4 col = snap.lastBuildOk ? ImVec4( 1, 0.8f, 0.4f, 1 ) : ImVec4( 1, 0.45f, 0.45f, 1 );
		ImGui::PushStyleColor( ImGuiCol_Text, col );
		ImGui::TextUnformatted( snap.log.c_str() );
		ImGui::PopStyleColor();
		ImGui::PopFont();
		ImGui::EndChild();
	}
	ImGui::End();
}

void ApplyStyle()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& s     = ImGui::GetStyle();
	s.WindowRounding  = 0;
	s.FrameRounding   = 2;
	s.TabRounding     = 2;
	s.GrabRounding    = 2;
	s.FramePadding    = ImVec2( 6, 4 );
	s.ItemSpacing     = ImVec2( 6, 5 );
	ImVec4* c         = s.Colors;
	ImVec4 accent     = ImVec4( 0.22f, 0.45f, 0.72f, 1.0f );
	c[ ImGuiCol_WindowBg ]       = ImVec4( 0.11f, 0.11f, 0.11f, 1 );
	c[ ImGuiCol_ChildBg ]        = ImVec4( 0.13f, 0.13f, 0.13f, 1 );
	c[ ImGuiCol_FrameBg ]        = ImVec4( 0.17f, 0.17f, 0.17f, 1 );
	c[ ImGuiCol_Button ]         = ImVec4( 0.20f, 0.20f, 0.20f, 1 );
	c[ ImGuiCol_ButtonHovered ]  = ImVec4( 0.28f, 0.28f, 0.28f, 1 );
	c[ ImGuiCol_ButtonActive ]   = accent;
	c[ ImGuiCol_Tab ]            = ImVec4( 0.16f, 0.16f, 0.16f, 1 );
	c[ ImGuiCol_TabSelected ]    = accent;
	c[ ImGuiCol_TabHovered ]     = ImVec4( 0.30f, 0.50f, 0.75f, 1 );
	c[ ImGuiCol_Header ]         = ImVec4( 0.22f, 0.45f, 0.72f, 0.55f );
	c[ ImGuiCol_HeaderHovered ]  = ImVec4( 0.22f, 0.45f, 0.72f, 0.75f );
	c[ ImGuiCol_HeaderActive ]   = accent;
	c[ ImGuiCol_TableHeaderBg ]  = ImVec4( 0.16f, 0.16f, 0.16f, 1 );
}

LRESULT WINAPI WndProc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
	if( ImGui_ImplWin32_WndProcHandler( hwnd, msg, wp, lp ) )
		return true;
	switch( msg )
	{
	case WM_SIZE:
		if( wp != SIZE_MINIMIZED )
		{
			g_resizeW = LOWORD( lp );
			g_resizeH = HIWORD( lp );
		}
		return 0;
	case WM_CLOSE:
		// on cache simplement la fenêtre : l'état (recherche, éditeur) est conservé
		ShowWindow( hwnd, SW_HIDE );
		g_visible = false;
		return 0;
	case WM_SYSCOMMAND:
		if( ( wp & 0xfff0 ) == SC_KEYMENU )
			return 0;
		break;
	case WM_DESTROY:
		PostQuitMessage( 0 );
		return 0;
	}
	return DefWindowProcW( hwnd, msg, wp, lp );
}

HINSTANCE ModuleHandle()
{
	HMODULE h = nullptr;
	GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&WndProc, &h );
	return h;
}

void BringToFront( HWND hwnd )
{
	ShowWindow( hwnd, IsIconic( hwnd ) ? SW_RESTORE : SW_SHOW );
	SetWindowPos( hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW );
	if( !g_ui->topmost )
		SetWindowPos( hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW );
	SetForegroundWindow( hwnd );
	g_visible = true;
}

void PanelThread()
{
	HINSTANCE inst = ModuleHandle();
	WNDCLASSEXW wc{ sizeof( wc ), CS_CLASSDC, WndProc, 0, 0, inst, nullptr, LoadCursor( nullptr, IDC_ARROW ), nullptr, nullptr, L"ShaderToyFX_Panel", nullptr };
	RegisterClassExW( &wc );

	// taille selon le DPI du moniteur principal (sans changer la DPI-awareness de Resolume)
	HDC screen  = GetDC( nullptr );
	float scale = screen ? GetDeviceCaps( screen, LOGPIXELSX ) / 96.0f : 1.0f;
	if( screen )
		ReleaseDC( nullptr, screen );
	scale = std::max( 1.0f, scale );

	HWND hwnd = CreateWindowExW( 0, wc.lpszClassName, L"ShaderToyFX — Shader Library", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
								 (int)( 1180 * scale ), (int)( 720 * scale ), nullptr, nullptr, inst, nullptr );
	if( !hwnd || !CreateDevice( hwnd ) )
	{
		DestroyDevice();
		if( hwnd )
			DestroyWindow( hwnd );
		UnregisterClassW( wc.lpszClassName, inst );
		g_running = false;
		return;
	}
	g_hwnd = hwnd;

	IMGUI_CHECKVERSION();
	ImGuiContext* ctx = ImGui::CreateContext();
	ImGuiIO& io       = ImGui::GetIO();
	io.IniFilename    = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ApplyStyle();
	ImGui::GetStyle().ScaleAllSizes( scale );
	ImGui::GetStyle().FontScaleDpi = scale;

	UiState ui;
	ui.settings = GetSettings();
	for( int i = 0; i < IM_ARRAYSIZE( kSortKeys ); ++i )
		if( ui.settings.sort == kSortKeys[ i ] )
			ui.sortIdx = i;
	g_ui = &ui;

	wchar_t winDir[ MAX_PATH ] = {};
	GetWindowsDirectoryW( winDir, MAX_PATH );
	std::string fonts = Narrow( winDir ) + "\\Fonts\\";
	if( FileExists( fonts + "segoeui.ttf" ) )
		ui.uiFont = io.Fonts->AddFontFromFileTTF( ( fonts + "segoeui.ttf" ).c_str(), 16.0f );
	if( !ui.uiFont )
		ui.uiFont = io.Fonts->AddFontDefault();
	if( FileExists( fonts + "consola.ttf" ) )
		ui.codeFont = io.Fonts->AddFontFromFileTTF( ( fonts + "consola.ttf" ).c_str(), 15.0f );
	if( !ui.codeFont )
		ui.codeFont = ui.uiFont;

	ImGui_ImplWin32_Init( hwnd );
	ImGui_ImplDX11_Init( g_device, g_context );
	SetTimer( hwnd, 1, 1500, nullptr );// réveille la boucle pour surveiller les Téléchargements

	bool done = false;
	while( !done )
	{
		// requêtes d'ouverture venant des instances
		{
			std::lock_guard< std::mutex > lock( g_openMutex );
			if( g_openRequested )
			{
				g_openRequested = false;
				if( auto t = g_requestedTarget.lock() )
					ui.target = t;
				BringToFront( hwnd );
			}
		}

		ScanDownloads();

		MSG msg;
		if( !g_visible )
		{
			// fenêtre cachée : on dort jusqu'au prochain message
			if( GetMessageW( &msg, nullptr, 0, 0 ) <= 0 )
				break;
			if( msg.message == WM_STFX_QUIT )
				break;
			TranslateMessage( &msg );
			DispatchMessageW( &msg );
			continue;
		}
		while( PeekMessageW( &msg, nullptr, 0, 0, PM_REMOVE ) )
		{
			if( msg.message == WM_QUIT || msg.message == WM_STFX_QUIT )
				done = true;
			TranslateMessage( &msg );
			DispatchMessageW( &msg );
		}
		if( done )
			break;
		if( IsIconic( hwnd ) )
		{
			WaitMessage();
			continue;
		}
		if( g_resizeW && g_resizeH )
		{
			ReleaseRTV();
			g_swapChain->ResizeBuffers( 0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0 );
			g_resizeW = g_resizeH = 0;
			CreateRTV();
		}
		UploadThumbs();
		UploadPreview();

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		ImGui::PushFont( ui.uiFont, 0.0f );
		DrawUi( hwnd );
		ImGui::PopFont();
		ImGui::Render();
		const float clear[ 4 ] = { 0.1f, 0.1f, 0.1f, 1.0f };
		g_context->OMSetRenderTargets( 1, &g_rtv, nullptr );
		g_context->ClearRenderTargetView( g_rtv, clear );
		ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
		g_swapChain->Present( 1, 0 );
	}

	KillTimer( hwnd, 1 );
	g_hwnd = nullptr;
	Preview::Shutdown();
	ReleasePreviewTexture();
	ReleaseThumbs();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext( ctx );
	DestroyDevice();
	DestroyWindow( hwnd );
	UnregisterClassW( wc.lpszClassName, inst );
	g_ui      = nullptr;
	g_visible = false;
	g_running = false;
}
}//namespace

namespace Panel
{
void Open( const SharedPtr& target )
{
	{
		std::lock_guard< std::mutex > lock( g_openMutex );
		g_requestedTarget = target;
		g_openRequested   = true;
	}
	bool expected = false;
	if( g_running.compare_exchange_strong( expected, true ) )
	{
		// épingle la DLL : le thread de la fenêtre ne doit pas survivre à un déchargement
		HMODULE h = nullptr;
		GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&Panel::Open, &h );
		std::thread( PanelThread ).detach();
	}
	else if( HWND hwnd = g_hwnd.load() )
		PostMessageW( hwnd, WM_STFX_WAKE, 0, 0 );
}

void OnInstanceRemoved()
{
	if( !ListInstances().empty() )
		return;
	if( HWND hwnd = g_hwnd.load() )
		PostMessageW( hwnd, WM_STFX_QUIT, 0, 0 );
}
}//namespace Panel
