// Tables de noms reprises de SHADERed (src/SHADERed/Objects/Names.cpp, licence MIT, (c) dfranx)

static const char* const TOPOLOGY_ITEM_NAMES[] = {
	"Undefined",
	"PointList",
	"LineList",
	"LineStrip",
	"TriangleList",
	"TriangleStrip",
	"LineListAdjecent",
	"LineStripAdjecent",
	"TriangleListAdjecent",
	"TriangleStripAdjecent",
	"Patches"
};

static const unsigned int TOPOLOGY_ITEM_VALUES[] = {
	0xFFFFFFFFu, // "Undefined"
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_LINES_ADJACENCY,
	GL_LINE_STRIP_ADJACENCY,
	GL_TRIANGLES_ADJACENCY,
	GL_TRIANGLE_STRIP_ADJACENCY,
	GL_PATCHES
};

static const char* const BLEND_NAMES[] = {
	"",
	"Zero",
	"One",
	"Source Color",
	"Inverse Source Color",
	"Source Alpha",
	"Inverse Source Alpha",
	"Destination Alpha",
	"Inverse Destination Alpha",
	"Destination Color",
	"Inverse Destination Color",
	"Saturated Source Alpha", // 11
	"", "",
	"Blend Factor",
	"Inverse Blend Factor",
	"Source 1 Color",
	"Inverse Source 1 Color",
	"Source 1 Alpha",
	"Inverse Source 1 Alpha"
};

static const unsigned int BLEND_VALUES[] = {
	0xFFFFFFFFu,
	GL_ZERO,
	GL_ONE,
	GL_SRC_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_DST_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA_SATURATE, // 11
	0xFFFFFFFFu,
	0xFFFFFFFFu,
	GL_CONSTANT_COLOR,
	GL_ONE_MINUS_CONSTANT_COLOR,
	0xFFFFFFFFu,
	0xFFFFFFFFu,
	0xFFFFFFFFu,
	0xFFFFFFFFu,
};

static const char* const BLEND_OPERATOR_NAMES[] = {
	"",
	"Add",
	"Subtract",
	"Reverse Subtract",
	"Min",
	"Max"
};

static const unsigned int BLEND_OPERATOR_VALUES[] = {
	0xFFFFFFFFu,
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_FUNC_REVERSE_SUBTRACT,
	GL_MIN,
	GL_MAX
};

static const char* const COMPARISON_FUNCTION_NAMES[] = {
	"",
	"Never",
	"Less",
	"Equal",
	"LessEqual",
	"Greater",
	"NotEqual",
	"GreaterEqual",
	"Always"
};

static const unsigned int COMPARISON_FUNCTION_VALUES[] = {
	0xFFFFFFFFu,
	GL_NEVER,
	GL_LESS,
	GL_EQUAL,
	GL_LEQUAL,
	GL_GREATER,
	GL_NOTEQUAL,
	GL_GEQUAL,
	GL_ALWAYS
};

static const char* const STENCIL_OPERATION_NAMES[] = {
	"",
	"Keep",
	"Zero",
	"Replace",
	"Increase Saturated",
	"Decrease Saturated",
	"Invert",
	"Increase",
	"Decrease"
};

static const unsigned int STENCIL_OPERATION_VALUES[] = {
	GL_ZERO,
	GL_KEEP,
	GL_ZERO,
	GL_REPLACE,
	GL_INCR_WRAP,
	GL_DECR_WRAP,
	GL_INVERT,
	GL_INCR,
	GL_DECR
};

static const char* const TEXTURE_MIN_FILTER_NAMES[] = {
	"Nearest",
	"Linear",
	"Nearest_MipmapNearest",
	"Linear_MipmapNearest",
	"Nearest_MipmapLinear",
	"Linear_MipmapLinear"
};

static const unsigned int TEXTURE_MIN_FILTER_VALUES[] = {
	GL_NEAREST,
	GL_LINEAR,
	GL_NEAREST_MIPMAP_NEAREST,
	GL_LINEAR_MIPMAP_NEAREST,
	GL_NEAREST_MIPMAP_LINEAR,
	GL_LINEAR_MIPMAP_LINEAR
};

static const char* const TEXTURE_MAG_FILTER_NAMES[] = {
	"Nearest",
	"Linear"
};

static const unsigned int TEXTURE_MAG_FILTER_VALUES[] = {
	GL_NEAREST,
	GL_LINEAR,
};

static const char* const TEXTURE_WRAP_NAMES[] = {
	"ClampToEdge",
	"Repeat",
	"MirroredRepeat"
};

static const unsigned int TEXTURE_WRAP_VALUES[] = {
	GL_CLAMP_TO_EDGE,
	GL_REPEAT,
	GL_MIRRORED_REPEAT
};

static const char* const FORMAT_NAMES[] = {
	"UNKNOWN",
	"RGBA",
	"RGB",
	"RG",
	"R",
	"R8",
	"R8_SNORM",
	"R16",
	"R16_SNORM",
	"R8G8",
	"R8G8_SNORM",
	"R16G16",
	"R16G16_SNORM",
	"R3G3B2",
	"R4G4B4",
	"R5G5B5",
	"R8G8B8",
	"R8G8B8_SNORM",
	"R10G10B10",
	"R12G12B12",
	"R16G16B16_SNORM",
	"R2G2B2A2",
	"R4G4B4A4",
	"R5G5B5A1",
	"R8G8B8A8",
	"R8G8B8A8_SNORM",
	"R10G10B10A2",
	"R10G10B10A2_UINT",
	"R12G12B12A12",
	"R16G16B16A16",
	"SR8G8B8",
	"SR8G8B8A8",
	"R16_FLOAT",
	"R16G16_FLOAT",
	"R16G16B16_FLOAT",
	"R16G16B16A16_FLOAT",
	"R32_FLOAT",
	"R32G32_FLOAT",
	"R32G32B32_FLOAT",
	"R32G32B32A32_FLOAT",
	"R11G11B10_FLOAT",
	"R9G9B9_E5",
	"R8_INT",
	"R8_UINT",
	"R16_INT",
	"R16_UINT",
	"R32_INT",
	"R32_UINT",
	"R8G8_INT",
	"R8G8_UINT",
	"R16G16_INT",
	"R16G16_UINT",
	"R32G32_INT",
	"R32G32_UINT",
	"R8G8B8_INT",
	"R8G8B8_UINT",
	"R16G16B16_INT",
	"R16G16B16_UINT",
	"R32G32B32_INT",
	"R32G32B32_UINT",
	"R8G8B8A8_INT",
	"R8G8B8A8_UINT",
	"R16G16B16A16_INT",
	"R16G16B16A16_UINT",
	"R32G32B32A32_INT",
	"R32G32B32A32_UINT"
};

static const unsigned int FORMAT_VALUES[] = {
	0xFFFFFFFFu,
	GL_RGBA,
	GL_RGB,
	GL_RG,
	GL_RED,
	GL_R8,
	GL_R8_SNORM,
	GL_R16,
	GL_R16_SNORM,
	GL_RG8,
	GL_RG8_SNORM,
	GL_RG16,
	GL_RG16_SNORM,
	GL_R3_G3_B2,
	GL_RGB4,
	GL_RGB5,
	GL_RGB8,
	GL_RGB8_SNORM,
	GL_RGB10,
	GL_RGB12,
	GL_RGB16_SNORM,
	GL_RGBA2,
	GL_RGBA4,
	GL_RGB5_A1,
	GL_RGBA8,
	GL_RGBA8_SNORM,
	GL_RGB10_A2,
	GL_RGB10_A2UI,
	GL_RGBA12,
	GL_RGBA16,
	GL_SRGB8,
	GL_SRGB8_ALPHA8,
	GL_R16F,
	GL_RG16F,
	GL_RGB16F,
	GL_RGBA16F,
	GL_R32F,
	GL_RG32F,
	GL_RGB32F,
	GL_RGBA32F,
	GL_R11F_G11F_B10F,
	GL_RGB9_E5,
	GL_R8I,
	GL_R8UI,
	GL_R16I,
	GL_R16UI,
	GL_R32I,
	GL_R32UI,
	GL_RG8I,
	GL_RG8UI,
	GL_RG16I,
	GL_RG16UI,
	GL_RG32I,
	GL_RG32UI,
	GL_RGB8I,
	GL_RGB8UI,
	GL_RGB16I,
	GL_RGB16UI,
	GL_RGB32I,
	GL_RGB32UI,
	GL_RGBA8I,
	GL_RGBA8UI,
	GL_RGBA16I,
	GL_RGBA16UI,
	GL_RGBA32I,
	GL_RGBA32UI
};

static const char* const ATTRIBUTE_VALUE_NAMES[] = {
	"Position",
	"Normal",
	"Texcoord",
	"Tangent",
	"Bitangent",
	"Color",
	"BufferFloat",
	"BufferFloat2",
	"BufferFloat3",
	"BufferFloat4",
	"BufferInt",
	"BufferInt2",
	"BufferInt3",
	"BufferInt4"
};

static const char* const SYSTEM_VARIABLE_NAMES[] = {
	"--NONE--",
	"Time",
	"TimeDelta",
	"FrameIndex",
	"ViewportSize",
	"MousePosition",
	"View",
	"Projection",
	"ViewProjection",
	"Orthographic",
	"ViewOrthographic",
	"GeometryTransform",
	"IsPicked",
	"IsSavingToFile",
	"CameraPosition",
	"CameraPosition3",
	"CameraDirection3",
	"KeysWASD",
	"Mouse",
	"MouseButton",
	"PickPosition",
	"PluginVariable"
};

static const char* const VARIABLE_TYPE_NAMES[] = {
	"bool",
	"bool2",
	"bool3",
	"bool4",
	"int",
	"int2",
	"int3",
	"int4",
	"float",
	"float2",
	"float3",
	"float4",
	"float2x2",
	"float3x3",
	"float4x4"
};

static const char* const FUNCTION_NAMES[] = {
	"None",
	"Pointer",
	"CameraSnapshot",
	"ObjectProperty",
	"MatrixIdentity",
	"MatrixLookAtLH",
	"MatrixLookToLH",
	"MatrixOrthographicLH",
	"MatrixPerspectiveFovLH",
	"MatrixPerspectiveLH",
	"MatrixRotationAxis",
	"MatrixRotationNormal",
	"MatrixRotationRollPitchYaw",
	"MatrixRotationX",
	"MatrixRotationY",
	"MatrixRotationZ",
	"MatrixScaling",
	"MatrixTranslation",
	"ScalarCos",
	"ScalarSin",
	"VectorNormalize",
	"PluginFunction"
};

static const char* const GEOMETRY_NAMES[] = {
	"Cube",
	"ScreenQuad",
	"Circle",
	"Triangle",
	"Sphere",
	"Plane",
	"ScreenQuadNDC"
};
