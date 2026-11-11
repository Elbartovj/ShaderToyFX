# Compilation croisée Linux -> Windows avec MinGW-w64 (paquet mingw-w64)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
# le SDK FFGL inclut <Windows.h> (majuscule) : petit relais pour les systèmes de fichiers sensibles à la casse
set(CMAKE_CXX_FLAGS_INIT "-I${CMAKE_CURRENT_LIST_DIR}/cmake/mingw-compat")
