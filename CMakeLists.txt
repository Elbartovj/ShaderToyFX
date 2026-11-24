cmake_minimum_required(VERSION 3.16)
project(ShaderToyFX VERSION 1.0 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ShaderToyFX cible Windows (Resolume FFGL 64 bits).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    # runtime statique : la DLL n'a besoin d'aucun redistribuable VC++
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /MP /W3 /wd4996)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
add_compile_definitions(NOMINMAX UNICODE _UNICODE)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ---- FFGL SDK + GLEW (statique)
file(GLOB FFGL_SOURCES ${TP}/ffgl/ffgl/*.cpp ${TP}/ffgl/ffglex/*.cpp)
add_library(ffgl STATIC ${FFGL_SOURCES} ${TP}/glew/src/glew.c)
target_include_directories(ffgl PUBLIC ${TP}/ffgl ${TP}/glew/include)
target_compile_definitions(ffgl PUBLIC GLEW_STATIC)
target_link_libraries(ffgl PUBLIC opengl32)

# ---- Dear ImGui (Win32 + DX11)
add_library(imgui STATIC
    ${TP}/imgui/imgui.cpp ${TP}/imgui/imgui_draw.cpp ${TP}/imgui/imgui_tables.cpp
    ${TP}/imgui/imgui_widgets.cpp ${TP}/imgui/imgui_stdlib.cpp
    ${TP}/imgui/backends/imgui_impl_win32.cpp ${TP}/imgui/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC ${TP}/imgui)
target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi imm32 gdi32 user32)

# ---- Projets SHADERed : glslang (GLSL/HLSL -> SPIR-V), SPIRV-Cross (SPIR-V -> GLSL), pugixml, miniz, glm
# Téléchargés à la configuration (FetchContent). Pour une compilation hors ligne, pointer
# FETCHCONTENT_SOURCE_DIR_<NOM> vers des copies locales.
# dépendances anciennes (cmake_minimum_required < 3.5) : acceptées par CMake 4
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
FetchContent_Declare(glslang GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git GIT_TAG 16.6.0 GIT_SHALLOW ON)
FetchContent_Declare(spirv_cross GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git GIT_TAG vulkan-sdk-1.4.363.0 GIT_SHALLOW ON)
FetchContent_Declare(pugixml GIT_REPOSITORY https://github.com/zeux/pugixml.git GIT_TAG v1.15 GIT_SHALLOW ON)
FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git GIT_TAG 3.1.0 GIT_SHALLOW ON)
FetchContent_Declare(glm GIT_REPOSITORY https://github.com/g-truc/glm.git GIT_TAG 1.0.2 GIT_SHALLOW ON)

set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
foreach(o CLI ENABLE_TESTS ENABLE_HLSL ENABLE_MSL ENABLE_CPP ENABLE_REFLECT ENABLE_C_API)
    set(SPIRV_CROSS_${o} OFF CACHE BOOL "" FORCE)
endforeach()
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL ON CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glslang spirv_cross pugixml miniz)
FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
    FetchContent_Populate(glm)
endif()

add_library(godot_shader STATIC
    ${TP}/godot_shader/ShaderTranscompiler.cpp ${TP}/godot_shader/Godot/misc.cpp
    ${TP}/godot_shader/Godot/shader_compiler_gles3.cpp ${TP}/godot_shader/Godot/shader_language.cpp
    ${TP}/godot_shader/Godot/shader_types.cpp)
target_include_directories(godot_shader PUBLIC ${TP}/godot_shader)
if(MSVC)
    target_compile_options(godot_shader PRIVATE /w)
else()
    target_compile_options(godot_shader PRIVATE -w)
endif()

# ---- code commun
set(STFX_SOURCES
    src/Util.cpp src/License.cpp src/Net.cpp src/ShaderDoc.cpp src/Params.cpp src/ShadertoyApi.cpp src/GlslGen.cpp
    src/Renderer.cpp src/Sites.cpp src/SitesWeb.cpp src/Instance.cpp src/Panel.cpp src/Preview.cpp src/Plugin.cpp src/StbImpl.cpp
    src/sed/SedProject.cpp src/sed/SedCompiler.cpp src/sed/SedGeometry.cpp src/sed/SedRenderer.cpp src/sed/SedGodot.cpp
    src/sed/SedArchive.cpp)

function(stfx_plugin target outname)
    add_library(${target} SHARED ${STFX_SOURCES} src/ShaderToyFX.def)
    target_include_directories(${target} PRIVATE src ${TP}/nlohmann/.. ${TP}/stb ${TP} ${glm_SOURCE_DIR})
    target_compile_definitions(${target} PRIVATE GLM_ENABLE_EXPERIMENTAL)
    target_link_libraries(${target} PRIVATE ffgl imgui winhttp comdlg32 shell32 ole32 uuid
        glslang glslang-default-resource-limits SPIRV spirv-cross-glsl spirv-cross-util pugixml miniz godot_shader)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${outname} PREFIX "")
    if(MINGW)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++ -s)
    endif()
endfunction()

stfx_plugin(ShaderToyFX_Effect ShaderLoader)
stfx_plugin(ShaderToyFX_Source ShaderLoader_Source)
target_compile_definitions(ShaderToyFX_Source PRIVATE STFX_SOURCE)
