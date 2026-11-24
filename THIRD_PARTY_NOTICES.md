# Composants tiers

ShaderToyFX est distribué sous licence GPL-3.0 (voir `LICENSE`). Il inclut les composants suivants, chacun sous sa propre licence compatible, dans `third_party/` :

| Composant | Licence | Fichier |
|---|---|---|
| FFGL SDK (Resolume / FreeFrame) | BSD 3 clauses | `third_party/ffgl/LICENSE.md` |
| GLEW 2.2.0 | BSD modifiée / MIT | `third_party/glew/LICENSE.txt` |
| Dear ImGui | MIT | `third_party/imgui/LICENSE.txt` |
| nlohmann/json 3.11.3 | MIT | en-tête de `third_party/nlohmann/json.hpp` |
| stb_image | MIT ou domaine public | fin de `third_party/stb/stb_image.h` |
| GodotShaderTranscompiler (dfranx), code extrait de Godot Engine | MIT | `third_party/godot_shader/LICENSE` (Godot : MIT, © Juan Linietsky, Ariel Manzur et contributeurs) |
| glslang (Khronos), téléchargé à la compilation | BSD 3 clauses / MIT / Apache 2.0 | dépôt KhronosGroup/glslang |
| SPIRV-Cross (Khronos), téléchargé à la compilation | Apache 2.0 | dépôt KhronosGroup/SPIRV-Cross |
| pugixml, téléchargé à la compilation | MIT | dépôt zeux/pugixml |
| miniz, téléchargé à la compilation | MIT | dépôt richgel999/miniz |
| glm, téléchargé à la compilation | MIT | dépôt g-truc/glm |

Le code de `src/sed/` reprend des parties de **SHADERed** (© dfranx, licence MIT, https://github.com/dfranx/SHADERed) : lecture des projets, chaîne de compilation, géométries, variables système et boucle de rendu. Les tables de noms de `src/sed/SedNames.inc` en sont extraites telles quelles.

Les shaders téléchargés depuis Shadertoy restent la propriété de leurs auteurs et sous leur licence propre (par défaut CC BY-NC-SA 3.0). Ils ne font pas partie de ce dépôt.

ShaderToyFX n'est affilié ni à Shadertoy, ni à SHADERed, ni à Godot, ni à Resolume, ni à Headsta.sh. « Shadertoy » et « Resolume » sont des marques de leurs propriétaires respectifs.
