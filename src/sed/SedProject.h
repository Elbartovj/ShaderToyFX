// ShaderToyFX - projets SHADERed (https://shadered.org, MIT, dfranx).
// Modèle de données d'un fichier .sprj (format v2) : passes, variables, géométries, objets.
// La sémantique suit celle de SHADERed (ProjectParser / RenderEngine) pour un rendu identique.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace stfx
{
namespace sed
{
enum class Lang
{
	GLSL,      // GLSL desktop (#version 330…)
	GLSLES,    // GLSL ES (#version 300 es, version web de SHADERed)
	VulkanGLSL,// .vk / GLSL Vulkan (#version 450 + bindings)
	HLSL,      // .hlsl
	Godot,     // canvas_item Godot (.shader / .gdshader)
	Rust,      // rust-gpu (.rs) : nécessite un compilateur externe
	Cpp,       // C++ Circle (.cpp/.cxx) : nécessite un compilateur externe
	Slang,     // Slang : nécessite un compilateur externe
	Unknown
};
const char* LangName( Lang l );
Lang LangFromPath( const std::string& path, const std::string& source );

enum class ValueType
{
	Bool1, Bool2, Bool3, Bool4,
	Int1, Int2, Int3, Int4,
	Float1, Float2, Float3, Float4,
	Float2x2, Float3x3, Float4x4,
	Count
};
int ValueCount( ValueType t );// nombre de composantes (16 pour float4x4)
int ValueColumns( ValueType t );

enum class SystemVar
{
	None, Time, TimeDelta, FrameIndex, ViewportSize, MousePosition, View, Projection, ViewProjection,
	Orthographic, ViewOrthographic, GeometryTransform, IsPicked, IsSavingToFile, CameraPosition,
	CameraPosition3, CameraDirection3, KeysWASD, Mouse, MouseButton, PickPosition, PluginVariable,
	Count
};

enum class FunctionVar
{
	None, Pointer, CameraSnapshot, ObjectProperty, MatrixIdentity, MatrixLookAtLH, MatrixLookToLH,
	MatrixOrthographicLH, MatrixPerspectiveFovLH, MatrixPerspectiveLH, MatrixRotationAxis, MatrixRotationNormal,
	MatrixRotationRollPitchYaw, MatrixRotationX, MatrixRotationY, MatrixRotationZ, MatrixScaling,
	MatrixTranslation, ScalarCos, ScalarSin, VectorNormalize, PluginFunction,
	Count
};

struct Variable
{
	std::string name;
	ValueType type   = ValueType::Float1;
	SystemVar system = SystemVar::None;
	FunctionVar func = FunctionVar::None;
	bool lastFrame   = false;
	bool invert      = false;
	float value[ 16 ] = {};   // valeur (ou arguments de la fonction)
	std::string args[ 2 ];    // arguments texte (Pointer, CameraSnapshot, ObjectProperty)
	int userParam = -1;       // index dans ShaderDoc::params de la 1re composante (-1 = pas de slider)
};

enum class GeoType
{
	Cube, Rectangle /* "ScreenQuad" */, Circle, Triangle, Sphere, Plane, ScreenQuadNDC, Count
};

struct RenderState
{
	bool wireframe = false, cull = true, cullFront = false, ccw = true;
	bool blend = false, alphaToCoverage = false;
	uint32_t srcRGB = 0x0302 /*GL_SRC_ALPHA*/, dstRGB = 0x0303 /*GL_ONE_MINUS_SRC_ALPHA*/, opRGB = 0x8006 /*GL_FUNC_ADD*/;
	uint32_t srcA = 0x0302, dstA = 0x0303, opA = 0x8006;
	float blendFactor[ 4 ] = { 0, 0, 0, 0 };
	bool depthTest = true, depthClamp = false, depthMask = true;
	uint32_t depthFunc = 0x0201 /*GL_LESS*/;
	float depthBias = 0;
	bool stencilTest = false;
	uint32_t stencilMask = 0xFF, stencilRef = 0;
	uint32_t stFrontFunc = 0x0207, stBackFunc = 0x0207;// GL_ALWAYS
	uint32_t stFrontPass = 0x1E00, stFrontFail = 0x1E00, stFrontDepthFail = 0x1E00;// GL_KEEP
	uint32_t stBackPass = 0x1E00, stBackFail = 0x1E00, stBackDepthFail = 0x1E00;
};

enum class ItemKind
{
	Geometry, RenderState, Model, VertexBuffer, GodotSprite, Unsupported
};

struct Item
{
	std::string name;
	ItemKind kind = ItemKind::Geometry;
	// géométrie / modèle / vertex buffer
	GeoType geo = GeoType::Cube;
	float size[ 3 ] = { 1, 1, 1 };
	float scale[ 3 ] = { 1, 1, 1 };
	float rotation[ 3 ] = { 0, 0, 0 };// pitch (x), yaw (y), roll (z) en radians
	float position[ 3 ] = { 0, 0, 0 };
	uint32_t topology = 0x0004;// GL_TRIANGLES
	bool instanced    = false;
	int instanceCount = 0;
	std::string instanceBuffer;// objet buffer (attributs par instance)
	std::string vertexBuffer;  // objet buffer (vertex buffer)
	std::string modelPath;     // .obj
	std::string modelGroup;
	bool modelGroupOnly = false;
	RenderState state;
	// sprite Godot
	std::string spriteTexture;
	float spriteColor[ 4 ] = { 1, 1, 1, 1 };
	bool flipH = false, flipV = false, visible = true;
	float spriteRotationDeg = 0;
};

struct InputLayoutItem
{
	int value = 0;// 0 Position, 1 Normal, 2 Texcoord, 3 Tangent, 4 Binormal, 5 Color, 6.. BufferFloat…BufferInt4
	std::string semantic;
};

struct ItemValue// valeur de variable propre à un item
{
	std::string variable, item;
	float value[ 16 ] = {};
};

struct Macro
{
	std::string name, value;
	bool active = true;
};

enum class PassKind
{
	Shader, Compute, Audio, Godot, Unsupported
};

struct Pass
{
	std::string name;
	PassKind kind = PassKind::Shader;
	bool active   = true;
	std::string vsPath, psPath, gsPath, tcsPath, tesPath, csPath;// chemins absolus
	std::string vsEntry = "main", psEntry = "main", gsEntry = "main", tcsEntry = "main", tesEntry = "main", csEntry = "main";
	bool gsUsed = false, tsUsed = false;
	int patchVerts = 1;
	std::vector< std::string > renderTextures;// vide = fenêtre ; "Window" = fenêtre
	std::vector< Variable > variables;
	std::vector< InputLayoutItem > inputLayout;
	std::vector< Macro > macros;
	std::vector< Item > items;
	std::vector< ItemValue > itemValues;
	std::vector< std::string > textures;// objets liés aux unités de texture (index = slot)
	std::vector< std::string > buffers; // objets liés en UBO/SSBO/image (index = slot)
	int groupSize[ 3 ] = { 1, 1, 1 };   // passe compute
	// passe Godot
	std::string godotPath;
	struct GodotUniform
	{
		std::string name, type;
		std::vector< float > value;
		std::string texture;
		int userParam = -1;// premier slider Resolume (-1 = aucun)
	};
	std::vector< GodotUniform > godotUniforms;
};

enum class ObjectKind
{
	Texture, Cubemap, Texture3D, KeyboardTexture, AudioTexture, RenderTexture, Image, Image3D, Buffer, Unsupported
};

struct Object
{
	std::string name;// nom (chemin relatif pour une texture)
	ObjectKind kind = ObjectKind::Texture;
	std::string path;// chemin absolu du fichier
	std::string cube[ 6 ];// left, top, front, bottom, right, back
	bool vflip      = false;
	uint32_t minFilter = 0x2703 /*GL_LINEAR_MIPMAP_LINEAR*/, magFilter = 0x2601 /*GL_LINEAR*/;
	uint32_t wrapS = 0x2901 /*GL_REPEAT*/, wrapT = 0x2901, wrapR = 0x2901;
	// render texture / image
	uint32_t format = 0x8058;// GL_RGBA8
	float ratio[ 2 ] = { 1, 1 };
	int fixed[ 3 ]   = { -1, -1, 1 };
	bool clear       = true;
	float clearColor[ 4 ] = { 0, 0, 0, 0 };
	// buffer
	int bufferSize = 0;
	std::string bufferFormat;
	std::vector< uint8_t > bufferData;
};

struct Project
{
	std::string path;   // fichier .sprj
	std::string dir;    // dossier du projet
	std::vector< Pass > passes;
	std::vector< Object > objects;
	std::vector< std::string > includePaths;
	// caméra
	bool fpCamera = false;
	float camDistance = 7, camYaw = 0, camPitch = 0, camRoll = 0;
	float camPos[ 3 ] = { 0, 0, 7 };
	std::map< std::string, std::vector< float > > cameraSnapshots;// matrices 4x4 (colonnes)
	float clearColor[ 4 ] = { 0, 0, 0, 0 };
	bool useAlpha = false;
	std::vector< std::string > warnings;
	std::vector< std::string > plugins;// greffons SHADERed requis

	const Object* FindObject( const std::string& name ) const;
	Pass* FindPass( const std::string& name );
};

// Lit un .sprj. Les chemins relatifs sont résolus par rapport au dossier du projet.
bool LoadProject( const std::string& sprjPath, Project& out, std::string& err );

// Lit une variable de type valeur depuis un nœud XML <row><value>… (utilisé aussi pour les uniforms Godot)
ValueType ValueTypeFromName( const std::string& name );
SystemVar SystemVarFromName( const std::string& name );
}//namespace sed
}//namespace stfx
