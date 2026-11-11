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

# ---- code commun
set(STFX_SOURCES
    src/Util.cpp src/License.cpp src/Net.cpp src/ShaderDoc.cpp src/Params.cpp src/ShadertoyApi.cpp src/GlslGen.cpp
    src/Renderer.cpp src/Instance.cpp src/Panel.cpp src/Plugin.cpp src/StbImpl.cpp)

function(stfx_plugin target outname)
    add_library(${target} SHARED ${STFX_SOURCES} src/ShaderToyFX.def)
    target_include_directories(${target} PRIVATE src ${TP}/nlohmann/.. ${TP}/stb ${TP})
    target_link_libraries(${target} PRIVATE ffgl imgui winhttp comdlg32 shell32 ole32 uuid)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${outname} PREFIX "")
    if(MINGW)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++ -s)
    endif()
endfunction()

stfx_plugin(ShaderToyFX_Effect ShaderToyFX)
stfx_plugin(ShaderToyFX_Source ShaderToyFX_Source)
target_compile_definitions(ShaderToyFX_Source PRIVATE STFX_SOURCE)
