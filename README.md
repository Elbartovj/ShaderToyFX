# ShaderToyFX : shaders Shadertoy dans Resolume (FFGL)

[![Build](../../actions/workflows/build.yml/badge.svg)](../../actions/workflows/build.yml)
[![Licence : GPL v3](https://img.shields.io/badge/licence-GPLv3-blue.svg)](LICENSE)

**Télécharger :** la dernière version (DLL prêtes à l'emploi) se trouve dans l'onglet [Releases](../../releases).

ShaderToyFX est un plugin FFGL 2 pour Resolume Arena/Avenue 7.4+ (Windows 64 bits). Il exécute directement des shaders Shadertoy, y compris les shaders multi-passes (Buffer A à D, Common), sans conversion vers ISF. Il lit aussi les **projets [SHADERed](https://shadered.org)** (GLSL, GLSL ES, HLSL, GLSL Vulkan, Godot) et les **shaders Godot** `canvas_item`.

Deux DLL sont produites :

| DLL | Type Resolume | Usage |
|---|---|---|
| `ShaderToyFX.dll` | **Effet** | S'applique sur un clip ou une couche. L'image du dessous arrive dans les `iChannel` libres. |
| `ShaderToyFX_Source.dll` | **Source** | Générateur autonome. Une source ne reçoit **aucune image de Resolume** : pour qu'un shader utilise ta vidéo ou les couches du dessous, utilise l'**effet** (sur le clip, la couche, un groupe, ou un clip *Video Router*). |

## Installation

1. Copier `ShaderToyFX.dll` et/ou `ShaderToyFX_Source.dll` dans un dossier de plugins.
   Par défaut : `Documents\Resolume Arena\Extra Effects`. On peut aussi ajouter un dossier dans *Preferences → Video → FFGL Plugins*.
2. Redémarrer Resolume. L'effet apparaît sous le nom **ShaderToyFX**, la source sous **ShaderToyFX Src** (nom limité à 16 caractères par FFGL).
3. Pour récupérer des shaders, deux modes sont possibles :
   - **sans clé API** (recommandé si la création de clé t'est refusée) : passe par le bookmarklet, voir ci-dessous ;
   - **avec une clé API** : <https://www.shadertoy.com/myapps>, à coller dans l'onglet **PRÉFÉRENCES**. Elle active la recherche intégrée et le Random en ligne.

## Mode sans clé : le bookmarklet

Un bookmarklet est un favori qui exécute un petit script sur la page ouverte.

1. Dans le panneau, clique **Copier le bookmarklet** (onglet SHADERTOY ou PRÉFÉRENCES). Le même code se trouve dans `bookmarklet.txt`.
2. Dans ton navigateur, crée un nouveau favori (Ctrl+D, puis « Modifier »), nomme-le « → ShaderToyFX » et colle le code comme **URL**.
3. Ouvre n'importe quel shader sur shadertoy.com, puis clique sur ce favori. Le shader complet (toutes ses passes) est téléchargé sous la forme `Titre [id].json`.
4. ShaderToyFX surveille ton dossier **Téléchargements** pendant que le panneau est ouvert, même s'il est en arrière-plan. Il déplace le fichier dans `Documents\ShaderToyFX\Shadertoy` et, si l'option est cochée, le charge directement dans l'instance ciblée.

Sans clé, le bouton **Rechercher** ouvre la recherche dans ton navigateur, et **Aléatoire** pioche dans tes shaders déjà importés. Le bookmarklet fonctionne aussi pour les shaders publiés en « Public » seulement, puisqu'il passe par la page que tu consultes.

## Paramètres dans Resolume

| Groupe | Paramètre | Rôle |
|---|---|---|
| Load | **Shader** | Sélecteur de fichier (`.json`, `.glsl`, `.frag`, `.txt`). Le chemin est enregistré dans la composition. |
| Load | **Browse Library** | Ouvre la fenêtre *Shader Library*. |
| Load | **Random Shadertoy** | Charge un shader au hasard dans la source choisie avec *Random From*. Ne retombe jamais deux fois de suite sur le même shader. |
| Load | **Random From** | Source du tirage : *Bibliothèque (tout)*, un de **tes dossiers** (sous-dossiers compris), ou *Shadertoy en ligne* si tu as une clé API. La liste se met à jour quand tu crées un dossier, et le choix est enregistré dans la composition. |
| Load | **Restart** | Remet `iTime`/`iFrame` à zéro et vide les buffers. |
| Controls | Speed, Mouse X/Y, Mouse Click | `iTime` × vitesse (0 à 4×) et `iMouse` façon Shadertoy (`zw` positifs quand le bouton est enfoncé). |
| Controls | Shader Alpha | Désactivé par défaut : alpha = 1 comme sur Shadertoy. Activé : l'alpha du shader est conservé. |
| Audio | Audio Gain, Audio FFT | La FFT de Resolume est convertie en texture audio Shadertoy (512×2). |
| Auto Mask | **Auto Mask**, Mask Source, Mask Threshold, Mask Softness, Mask Invert | Rend transparentes les zones sombres de la sortie pour superposer le shader aux couches du dessous. *Source* : Luminance, Max RGB (garde les couleurs saturées) ou Shader Alpha × Luma. *Threshold* fixe le niveau de coupure, *Softness* adoucit le bord, *Invert* masque les zones claires à la place. |
| Inputs | **Input Fit** | Cadrage de l'entrée Resolume dans la sortie : *Étirer* (défaut), *Contenir* (image entière, bandes noires), *Remplir* (rogne les bords), *Pixel 1:1* (taille réelle, centrée). L'entrée est toujours recadrée à la résolution de sortie, donc `fragCoord / iResolution` tombe pile sur l'image, quelle que soit la résolution du clip. |
| Inputs | **Aspect Fix** | Pour les shaders qui étirent leurs coordonnées par le ratio de l'écran (`uv.x *= iResolution.x / iResolution.y`, fréquent sur Shadertoy avec des textures qui se répètent) : l'entrée Resolume ne remplit alors qu'une partie de l'écran. Coche **Aspect Fix** pour la recaler sur tout l'écran. Ne concerne que les canaux qui reçoivent l'entrée Resolume ou une image choisie. |
| Inputs | **iChannel0 à 3** (+ *Image*) | Choisit ce qui entre dans chaque iChannel : *Shader (défaut)*, *Entrée Resolume* (le clip/la couche sous l'effet), *Image (fichier)* (un sélecteur d'image apparaît), *Audio FFT* ou *Noir*. Seuls les canaux utilisés par le shader sont affichés, avec leur contenu d'origine, par ex. « iChannel0 (texture) ». Les buffers (feedback) ne sont jamais remplacés. |
| Shader Params | jusqu'à 24 sliders | Créés automatiquement à partir du shader : `uniform`, `#define` numériques et constantes globales (voir ci-dessous). Renommés au chargement, masqués s'ils ne servent pas. |

## Le panneau Shader Library

- **SHADERTOY** : recherche avec tri (populaires, récents, aimés, tendance, nom) et pagination. Chaque résultat affiche une miniature et son code. Double-clic ou *Charger le shader* pour l'utiliser.
- **LIEN** : coller `https://www.shadertoy.com/view/XXXXXX` ou l'id à 6 caractères.
- **FICHIER** : ouvrir un fichier, ou coller du code (passe Image) qui est enregistré dans `Local` puis chargé.
- **LOCAL** : ta bibliothèque, organisée en **dossiers perso**, avec la **licence** de chaque shader.
  - Colonne de gauche : tes dossiers (*Créer*, *Supprimer* un dossier vide). Glisse un shader sur un dossier pour l'y ranger, ou utilise *Déplacer vers…* ou le clic droit. **Sélection multiple** : Ctrl+clic (ajouter/retirer), Maj+clic (plage), Ctrl+A (tout) ; on glisse alors toute la sélection d'un coup.
  - Colonne **LICENCE** : *Commercial OK* (CC0, domaine public, MIT, BSD…), *Commercial (créditer)* (CC BY, CC BY-SA), *Non commercial* (CC BY-NC…, ou aucune mention sur Shadertoy, la licence par défaut étant CC BY-NC-SA 3.0), *Interdit* (« all rights reserved »…), *À vérifier* (mention ambiguë ou pas de licence dans un shader local). Survole le badge pour voir la ligne du code qui a servi à décider. Le filtre de licence permet de n'afficher que les shaders utilisables commercialement.
  - La détection est **automatique et indicative** : elle lit les commentaires du code et la description. Avant un usage commercial, relis toujours le texte de la licence. Ce n'est pas un avis juridique.
  - **Aperçu** (colonne de droite) : le shader sélectionné tourne en direct en basse résolution (160x90, 256x144 ou 384x216), avec les paramètres à leur valeur d'origine. Les effets qui lisent l'entrée Resolume sont montrés sur une image de test. Le rendu se fait dans un contexte OpenGL à part, sur son propre thread : il ne touche pas au rendu de Resolume. Décoche *Animé* pour le figer, et il s'arrête tout seul quand l'onglet ou le panneau est fermé. Double-clic sur l'aperçu pour charger le shader.
  - **Favoris** : clique l'étoile d'une ligne ou de l'aperçu, utilise le clic droit ou *Favoris ±* pour toute une sélection, ou glisse des shaders sur **Favoris** dans la colonne des dossiers. Le dossier virtuel *Favoris* liste les favoris de tous les dossiers. Un favori le reste s'il change de dossier (il est repéré par son nom de fichier).
  - Le bouton **Aléatoire dans « dossier »** charge un shader au hasard dans le dossier sélectionné, y compris *Favoris*. Dans Resolume, *Random From* propose aussi **Favoris**.
  - Un shader déplacé reste retrouvé par les compositions Resolume existantes (recherche par nom de fichier dans la bibliothèque).
- **ÉDITEUR** : édition en direct de chaque passe. *Appliquer* (Ctrl+Entrée) recompile sans perdre le shader en cours si une erreur survient. *Enregistrer* (Ctrl+S) écrit le fichier et met à jour le paramètre Shader.
- **PRÉFÉRENCES** : clé API, nombre de résultats par page, option « toujours au premier plan ».

Le bas de la fenêtre affiche les erreurs de compilation, avec la passe et la ligne concernées (par exemple `[Buffer A ligne 12]`).

Un même panneau sert toutes les instances. Le menu **Instance** en haut permet de choisir l'instance qui reçoit le shader.

## Dossiers

```
Documents\ShaderToyFX\
  Shadertoy\   <- shaders téléchargés : "Titre [id].json" (réutilisés hors ligne)
  Local\       <- tes shaders et le code collé
  media\       <- textures, cubemaps, volumes et miniatures en cache
%APPDATA%\ShaderToyFX\settings.json   <- clé API et préférences
%APPDATA%\ShaderToyFX\favorites.json  <- shaders favoris
Documents\ShaderToyFX\SHADERed\     <- projets SHADERed (un dossier par projet) et shaders Godot
```

Un shader téléchargé est enregistré en `.json` et c'est ce fichier qui est stocké dans la composition. Le set fonctionne donc ensuite sans connexion.

## Correspondance des entrées Shadertoy

| Entrée Shadertoy | Dans ShaderToyFX |
|---|---|
| Buffer A à D | Buffers flottants RGBA32F en ping-pong, avec le même ordre et le même feedback que sur le site |
| Texture, Cubemap, Volume (`.bin`) | Téléchargés depuis shadertoy.com, puis mis en cache |
| Music, SoundCloud, Micro | FFT Resolume (texture 512×2 : ligne 0 = spectre, ligne 1 = forme d'onde synthétisée) |
| Video, Webcam | **Entrée Resolume** (le clip sous l'effet) |
| Keyboard | Texture vide (256×3) |
| Canal non utilisé de la passe Image | Entrée Resolume (pratique pour transformer un shader en effet) |
| Passe *Sound*, passe *Cube A* | Non supportées : le shader est ignoré ou noir, avec un avertissement |

Uniforms disponibles : `iResolution`, `iTime`, `iTimeDelta`, `iFrameRate`, `iFrame`, `iChannelTime[4]`, `iChannelResolution[4]`, `iMouse`, `iDate`, `iSampleRate`, `iChannel0..3`.

## Projets SHADERed et shaders Godot

ShaderToyFX exécute les projets [SHADERed](https://shadered.org) avec le même moteur que SHADERed : passes de rendu et compute, géométries (cube, sphère, plan, quad plein écran…), modèles `.obj`, render textures, états de rendu (fusion, profondeur, stencil, fil de fer), instanciation, buffers, cubemaps, tessellation et geometry shaders, variables système (`Time`, `ViewportSize`, `MousePosition`, matrices de caméra…) et fonctions.

- **Favori SHADERed** (comme le bookmarklet Shadertoy) : dans l'onglet LIEN, clique *Copier le favori SHADERed*, crée un favori dans ton navigateur et colle le texte comme adresse. Sur la page d'un shader de shadered.org, ce favori télécharge le projet en `Titre [shadered-id].zip`. ShaderToyFX le récupère dans tes Téléchargements, le range dans la bibliothèque, supprime l'archive et charge le projet.
- **Charger** : colle un lien `https://shadered.org/view?s=…` dans l'onglet LIEN. Ou bien ouvre un `.sprj` ou l'archive `.zip` téléchargée sur shadered.org dans l'onglet FICHIER. Le projet est copié, avec ses textures, dans `Documents\ShaderToyFX\SHADERed\` et apparaît dans la BIBLIOTHÈQUE (aperçu, favoris, dossiers, aléatoire).
- **Langages** :

  | Langage | Prise en charge |
  |---|---|
  | GLSL | Oui, tel quel |
  | GLSL ES (version web de SHADERed) | Oui, version desktop équivalente |
  | HLSL | Oui, via glslang et SPIRV-Cross, comme SHADERed |
  | GLSL Vulkan (`.vk`) | Oui, via glslang et SPIRV-Cross, comme SHADERed |
  | Godot (`canvas_item`) | Oui, Godot 3 et syntaxes Godot 4 courantes : `source_color`, `hint_screen_texture`… |
  | Rust (rust-gpu) | Non : demande un compilateur externe. Exporte le projet en GLSL ou HLSL depuis SHADERed. |
  | C++ (Circle) | Non : le compilateur n'existe que sous Linux. Exporte le projet en GLSL ou HLSL depuis SHADERed. |
- **Sliders** : chaque variable du projet sans valeur système devient un slider Resolume (`float`, `int`, `bool`, vecteurs composante par composante). Le milieu du slider donne la valeur du projet. Pour les shaders Godot, les `uniform` deviennent des sliders, avec la plage `hint_range` si elle existe et une plage 0..1 pour les couleurs.
- **Entrée Resolume** : les paramètres *Texture 0..3* de l'effet (même menu que les iChannel) remplacent les 4 premières textures du projet par l'entrée Resolume, une image, l'audio ou du noir. Dans un shader Godot, `TEXTURE` est l'entrée Resolume, à l'endroit, comme dans Godot. `SCREEN_TEXTURE` contient ce qui est déjà dessiné.
- La souris (`MousePosition`) suit les paramètres Mouse de l'effet. Le clavier n'est pas transmis.
- Non pris en charge : passes audio, greffons SHADERed autres que GodotShaders, textures 3D depuis un fichier, modèles autres que `.obj`. Un avertissement s'affiche en bas de la fenêtre.
- **Fichier Godot seul** (`.gdshader`, ou `.shader` contenant `shader_type`) : il devient un effet plein écran, avec `TEXTURE` égale à l'entrée Resolume.

## Écrire ses propres shaders (`.glsl`)

Un fichier `.glsl` contient une passe Image avec `mainImage()`, comme sur Shadertoy. Par défaut, iChannel0, 2 et 3 reçoivent l'entrée Resolume et iChannel1 reçoit l'audio. Des annotations en commentaire permettent de changer ces affectations :

```glsl
// @iChannel0 input        (input | audio | keyboard | none | bufferA..D | chemin d'image)
// @iChannel2 textures/grain.png
```

**Paramètres Resolume** : jusqu'à 24 sliders sont créés automatiquement, à partir de :

- les `uniform float` / `uniform int` que tu déclares ;
- les **`#define` numériques** (`#define SPEED 1.5`, `#define STEPS 64`) ;
- les **constantes globales** (`const float GLOW = 0.8;`, `const int OCTAVES = 5;`, `const vec3 COLOR = vec3(1., .5, .2);` donne trois sliders `COLOR.r/.g/.b`).

Ainsi, la plupart des shaders Shadertoy deviennent réglables sans modification. Sont volontairement exclues les valeurs qui doivent rester constantes en GLSL : taille de tableau, `#if`, `case`, initialisation d'une autre constante ou d'une variable globale, ainsi que PI/TAU et les valeurs inutilisées. Si un cas particulier empêche quand même la compilation, ShaderToyFX retire le paramètre fautif et l'indique en bas de la fenêtre.

**Le milieu de chaque slider correspond exactement à la valeur d'origine du shader** : à l'import, les sliders donnent donc le même rendu que sur Shadertoy, et *réinitialiser* un paramètre dans Resolume y revient. La plage va de 0 au double de cette valeur (entiers : de 1 à 2×−1).

Pour que le rendu reste identique à Shadertoy une fois les valeurs transformées en sliders, ShaderToyFX :
- n'expose pas les réglages sans effet (utilisés seulement dans un bloc `#ifdef` désactivé ou par une fonction jamais appelée), ni les codes de touches et les vecteurs de direction ;
- initialise à zéro les variables locales déclarées sans valeur, comme le fait le navigateur sur Shadertoy ;
- calcule `pow()` d'un nombre négatif avec un exposant entier comme le compilateur de Shadertoy (au lieu de renvoyer NaN). Pour fixer une plage, ajoute `// min max défaut` en fin de ligne. L'onglet ÉDITEUR liste les paramètres détectés, sous « Paramètres réglables dans Resolume ».

```glsl
uniform float zoom;      // 0.5 8 3
uniform float mixInput;  // 0 1 0.35
```

La même syntaxe fonctionne dans les shaders JSON multi-passes et dans l'onglet Common. Le dossier `examples/` en contient deux exemples : un plasma réactif au son et des traînées de feedback multi-passes.

## Limites à connaître

- L'API Shadertoy ne donne accès qu'aux shaders publiés en **Public + API**. Le bookmarklet n'a pas cette limite. S'il échoue (changement du site), copie le code dans l'onglet FICHIER.
- Les shaders sont compilés en GLSL 4.10 desktop. La quasi-totalité des shaders WebGL2 passent tels quels. Quelques-uns, rares, reposent sur des particularités de GLSL ES et nécessitent une petite retouche dans l'ÉDITEUR.
- Les shaders lourds (raymarching) coûtent cher en GPU à la résolution de la composition. Le paramètre Speed ne change rien à ce coût.
- **Licences** : sur Shadertoy, le code est par défaut sous **CC BY-NC-SA 3.0**, sauf mention contraire de l'auteur. Pour un usage commercial (prestation VJ payée, par exemple), vérifie la licence indiquée dans le code ou demande à l'auteur.

## Compilation

Prérequis : Visual Studio 2022 (charge de travail « Développement Desktop C++ ») et CMake 3.16 ou plus. Toutes les dépendances sont incluses dans `third_party/` : FFGL SDK (Resolume), GLEW, Dear ImGui, nlohmann/json et stb_image.

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Les DLL sont générées dans `build\Release\`. Le runtime C++ est lié statiquement, donc aucun redistribuable n'est nécessaire.

La compilation croisée avec MinGW-w64 fonctionne aussi (c'est ainsi que les DLL fournies ont été générées) :

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=mingw.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

## Architecture du code

| Fichier | Rôle |
|---|---|
| `src/Plugin.*` | Classe FFGL : paramètres, horloge, souris, audio, application des shaders sur le thread GL |
| `src/Renderer.*` | Rendu OpenGL multi-passes : FBO ping-pong, samplers, textures, restauration de l'état GL pour l'hôte |
| `src/GlslGen.*` | Construction du fragment shader (en-tête des uniforms, `#line` pour des erreurs lisibles) |
| `src/ShaderDoc.*` | Lecture du JSON Shadertoy ou du GLSL, résolution des médias, paramètres `uniform float` |
| `src/ShadertoyApi.*` | API REST Shadertoy (shader, recherche, liste complète), cache disque |
| `src/Net.*` | HTTPS via WinHTTP |
| `src/Instance.*` | État partagé et thread-safe de chaque instance, chargements asynchrones, registre des instances |
| `src/sed/*` | Projets SHADERed : lecture des `.sprj` (`SedProject`), traduction GLSL/HLSL/Vulkan/GLSL ES (`SedCompiler`, glslang + SPIRV-Cross), géométries (`SedGeometry`), exécution du pipeline (`SedRenderer`), shaders Godot (`SedGodot`), import zip / shadered.org (`SedArchive`) |
| `src/Preview.*` | Aperçu animé de la bibliothèque : contexte WGL caché sur un thread dédié, rendu basse résolution relu vers le panneau |
| `src/Panel.*` | Fenêtre *Shader Library* : thread dédié, Win32, Direct3D 11 et Dear ImGui, sans toucher au contexte GL de Resolume |

Les téléchargements et le décodage d'images se font sur des threads de travail. Seule la compilation GL a lieu dans `ProcessOpenGL`. Si un shader ne compile pas, le précédent reste affiché.

## Tests

`tests/` contient un banc d'essai Linux (GLFW + Mesa) qui vérifie le feedback des buffers, les textures, l'audio, les cubemaps et volumes, le décalage de viewport, la propreté de l'état GL et les numéros de ligne des erreurs. Les DLL ont aussi été testées sous Wine avec un mini-hôte FFGL : chargement, paramètres, renommage dynamique, passe multi-buffers et ouverture du panneau depuis le bouton *Browse Library*.

## Licence

ShaderToyFX est un logiciel libre sous licence **GNU GPL v3** (voir [`LICENSE`](LICENSE)). Les composants tiers inclus et leurs licences sont listés dans [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

Ce projet n'est affilié ni à Shadertoy, ni à Resolume, ni à Headsta.sh. Les shaders que tu importes restent sous la licence de leurs auteurs.
