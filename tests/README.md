# Tests (Linux, GL 4.1 core via GLFW/Mesa)

    sudo apt install libglfw3-dev libglew-dev xvfb
    S=..   # racine du projet
    g++ -std=c++17 -o gltest gltest.cpp $S/src/{Renderer,GlslGen,ShaderDoc,Util,Net,StbImpl}.cpp \
        $S/third_party/ffgl/ffglex/{FFGLScreenQuad,FFGLScopedVAOBinding,FFGLScopedBufferBinding}.cpp \
        -I$S/src -I$S/third_party -I$S/third_party/ffgl -I$S/third_party/stb -lGLEW -lglfw -lGL
    cd fixtures && xvfb-run -a ../gltest

Lance les tests depuis `tests/fixtures` (les textures sont référencées en relatif).
`minihost.cpp` est un mini hôte FFGL Windows (MinGW/Wine) qui charge la DLL et la pilote via plugMain.
