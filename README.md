# Shader Loader : shaders Shadertoy, SHADERed, ShaderKit, FragCoord, Shaderoo et NEORT dans Resolume (FFGL)

*Anciennement ShaderToyFX.*

[![Build](../../actions/workflows/build.yml/badge.svg)](../../actions/workflows/build.yml)
[![Licence : GPL v3](https://img.shields.io/badge/licence-GPLv3-blue.svg)](LICENSE)

**Télécharger :** la dernière version (DLL prêtes à l'emploi) se trouve dans l'onglet [Releases](../../releases).

Shader Loader est un plugin FFGL 2 pour Resolume Arena/Avenue 7.4+ (Windows 64 bits). Il exécute directement des shaders Shadertoy, y compris les shaders multi-passes (Buffer A à D, Common), sans conversion vers ISF. Il lit aussi les **projets [SHADERed](https://shadered.org)** (GLSL, GLSL ES, HLSL, GLSL Vulkan, Godot) et les **shaders Godot** `canvas_item`.

Deux DLL sont produites :

| DLL | Type Resolume | Usage |
|---|---|---|
| `ShaderLoader.dll` | **Effet** | S'applique sur un clip ou une couche. L'image du dessous arrive dans les `iChannel` libres. |
| `ShaderLoader_Source.dll` | **Source** | Générateur autonome. Une source ne reçoit **aucune image de Resolume** : pour qu'un shader utilise ta vidéo ou les couches du dessous, utilise l'**effet** (sur le clip, la couche, un groupe, ou un clip *Video Router*). |

## Installation

1. Copier `ShaderLoader.dll` et/ou `ShaderLoader_Source.dll` dans un dossier de plugins.
   Par défaut : `Documents\Resolume Arena\Extra Effects`. On peut aussi ajouter un dossier dans *Preferences → Video → FFGL Plugins*.
   **Mise à jour depuis ShaderToyFX** : supprime `ShaderToyFX.dll` et `ShaderToyFX_Source.dll` avant de copier les nouvelles DLL. Les identifiants du plugin ne changent pas : tes compositions, ta bibliothèque, tes favoris et ta clé API sont conservés (les dossiers restent `Documents\ShaderToyFX` et `%APPDATA%\ShaderToyFX`).
2. Redémarrer Resolume. L'effet apparaît sous le nom **Shader Loader**, la source sous **Shader Loader Src** (nom limité à 16 caractères par FFGL).
3. Pour récupérer des shaders, deux modes sont possibles :
   - **sans clé API** (recommandé si la création de clé t'est refusée) : passe par le bookmarklet, voir ci-dessous ;
   - **avec une clé API** : <https://www.shadertoy.com/myapps>, à coller dans l'onglet **PRÉFÉRENCES**. Elle active la recherche intégrée et le Random en ligne.

## Mode sans clé : le bookmarklet

Un bookmarklet est un favori qui exécute un petit script sur la page ouverte.

1. Dans le panneau, clique **Copier le bookmarklet** (onglet SHADERTOY ou PRÉFÉRENCES). Le même code se trouve dans `bookmarklet.txt`.
2. Dans ton navigateur, crée un nouveau favori (Ctrl+D, puis « Modifier »), nomme-le « → Shader Loader » et colle le code comme **URL**.
3. Ouvre n'importe quel shader sur shadertoy.com, puis clique sur ce favori. Le shader complet (toutes ses passes) est téléchargé sous la forme `Titre [id].json`.
4. Shader Loader surveille ton dossier **Téléchargements** pendant que le panneau est ouvert, même s'il est en arrière-plan. Il déplace le fichier dans `Documents\ShaderToyFX\Shadertoy` et, si l'option est cochée, le charge directement dans l'instance ciblée.

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
| Inputs | **iChannel0 à 3** (+ *Image*) | Choisit ce qui entre dans chaque iChannel : *Shader (défaut)*, *Entrée Resolume* (le clip/la couche sous l'effet), *Image (fichier)* (un sélecteur d'image apparaît), *Audio FFT* ou *Noir*. Seuls les canaux utilisés par le shader sont affichés, avec leur contenu d'origine, par ex. « iChannel0 (texture) ». Tout canal 2D peut être remplacé, y compris un buffer : choisis *Audio FFT* pour y envoyer le spectre de Resolume. |
| Shader Params | **Save as Default** | Écrit dans le fichier du shader les valeurs actuelles des sliders comme nouvelles valeurs par défaut (aussi : bouton *Réglages actuels → valeurs par défaut* de l'onglet ÉDITEUR). Les sliders reviennent au milieu, qui vaut désormais ces valeurs. Un shader hors de la bibliothèque est enregistré dans `Local`. Pas pour les projets SHADERed. |
| Models | **Model 1, Model 2**, **Model Fit** | Projets SHADERed avec des modèles `.obj` : remplace chaque modèle par un `.obj` de ton choix. *Model Fit* (activé par défaut) le recale sur la taille et la position du modèle d'origine. Les `.obj` sans normales reçoivent des normales plates. |
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
Documents\ShaderToyFX\ShaderKit\ (FragCoord, Shaderoo, NEORT) <- imports du favori universel
```

Un shader téléchargé est enregistré en `.json` et c'est ce fichier qui est stocké dans la composition. Le set fonctionne donc ensuite sans connexion.

## Correspondance des entrées Shadertoy

| Entrée Shadertoy | Dans Shader Loader |
|---|---|
| Buffer A à D | Buffers flottants RGBA32F en ping-pong, avec le même ordre et le même feedback que sur le site |
| Texture, Cubemap, Volume (`.bin`) | Téléchargés depuis shadertoy.com, puis mis en cache |
| Music, SoundCloud, Micro | FFT Resolume (texture 512×2 : ligne 0 = spectre, ligne 1 = forme d'onde synthétisée) |
| Video, Webcam | **Entrée Resolume** (le clip sous l'effet) |
| Keyboard | Texture vide (256×3) |
| Canal non utilisé de la passe Image | Entrée Resolume (pratique pour transformer un shader en effet) |
| Passe *Sound*, passe *Cube A* | Non supportées : le shader est ignoré ou noir, avec un avertissement |

Uniforms disponibles : `iResolution`, `iTime`, `iTimeDelta`, `iFrameRate`, `iFrame`, `iChannelTime[4]`, `iChannelResolution[4]`, `iMouse`, `iDate`, `iSampleRate`, `iChannel0..3`.

## Autres sites sans navigateur : onglet AUTRES SITES et liens directs

Pour ShaderKit, FragCoord, Shaderoo, NEORT et SHADERed, le plugin télécharge le shader lui-même, sans favori ni clé :

- **Onglet AUTRES SITES** : choisis le site, tape une recherche (ou laisse vide pour parcourir), puis double-clique un résultat ou clique **Charger le shader**. Un aperçu animé s'affiche à droite : le shader est converti en arrière-plan dans un cache (`media\web_preview`), sans l'ajouter à ta bibliothèque. La miniature du site s'affiche en attendant (FragCoord n'en fournit pas).
- **Onglet LIEN** : colle un lien `shaderkit.com/s/…`, `fragcoord.xyz/s/…`, `shaderoo.org/?shader=…`, `neort.io/art/…` ou `shadered.org/view?s=…`, puis **Charger**.

| Site | Accès utilisé | Recherche |
|---|---|---|
| FragCoord | Serveur MCP public du site (lecture sans clé). Les passes en « golf », WGSL, HLSL… sont converties en GLSL par FragCoord | Mots-clés et tags |
| NEORT | API publique `api.neort.io` | Par tag (vide = tag « glsl ») ; seules les œuvres GLSL sont listées |
| Shaderoo | Chargement de scène du site (`cmd=loaddb`) et textures intégrées (`rand*`, volumes `car64`, `dragon128`…) | Filtre sur titre et auteur ; tri tendance, récents, vues, likes |
| ShaderKit | Page publique du shader | Filtre sur titre, auteur et tags |
| SHADERed | Page de recherche du site, puis téléchargement du projet | Mots-clés ; tri tendance, récents, populaires |

Dans les listes (LOCAL, SHADERTOY, AUTRES SITES), clique sur l'en-tête d'une colonne pour trier ; reclique pour inverser le sens.

Shadertoy reste dans son onglet : recherche avec une clé API, ou favori sans clé. Si un site refuse l'accès direct, ouvre la page et utilise le favori universel ci-dessous.

## Favori universel : ShaderKit, FragCoord, Shaderoo, NEORT (et Shadertoy, SHADERed)

Un seul favori suffit pour importer depuis six sites. Pas besoin d'extension Firefox : c'est un favori ordinaire, qui marche dans Firefox, Chrome et Edge.

1. Le plus simple : ouvre `Installer le favori Shader Loader.html` (dans le zip) et fais glisser le bouton bleu dans ta barre de favoris, puis passe à l'étape 3. Sinon, dans le panneau, onglet LIEN ou PRÉFÉRENCES, clique **Copier le favori universel** (le même code est dans `bookmarklet-universel.txt`).
2. Dans ton navigateur, crée un favori (Ctrl+D, puis « Modifier »), nomme-le « → Shader Loader » et colle le texte comme **adresse (URL)**. Affiche la barre personnelle (Ctrl+Maj+B) pour l'avoir sous la main.
3. Ouvre la page d'un shader, puis clique sur le favori. Le fichier téléchargé (`Titre [site-id].json`, `.glsl` ou `.zip`) est récupéré dans tes Téléchargements, converti, rangé dans `Documents\ShaderToyFX\<Site>\`, puis chargé.

| Site | Page à ouvrir | Prise en charge |
|---|---|---|
| shadertoy.com | `/view/ID` | Toutes les passes, comme le bookmarklet Shadertoy |
| shadered.org | `/view?s=ID` | Projet complet (voir plus bas) |
| shaderkit.com | `/s/ID` | Image et buffers (feedback compris), Common, sliders des uniforms du site avec leurs plages |
| fragcoord.xyz | `/s/ID` | Export « Shadertoy » du site. `u_audio` devient l'audio Resolume. Pour les shaders à plusieurs passes, préfère l'onglet LIEN ou AUTRES SITES |
| shaderoo.org | `?shader=ID` | Image, Buff_A..D, includes, textures intégrées (dont les volumes 3D `car64`, `dragon128`), images en ligne. `#ShaderooNumPasses N` : la passe tourne une fois par image et son résultat se construit sur N images. Non pris en charge : passes géométrie (`Geom_*`, maillages), tailles de buffer fixes (rendus à la taille de sortie) |
| neort.io | `/art/ID` | Œuvres en GLSL (style GLSLSandbox : `resolution`, `time`, `mouse`, `backbuffer`). Les œuvres en JavaScript, p5.js ou three.js ne sont pas importables |

Les fichiers `.frag` / `.glsl` avec `void main()` (style The Book of Shaders, GLSLSandbox, GLSL ES 3 avec `out vec4`) sont aussi convertis automatiquement quand tu les ouvres dans l'onglet FICHIER.

## Exporter un shader en plugin FFGL autonome

Onglet **ÉDITEUR** → **Exporter en plugin FFGL…** : crée une DLL qui contient le shader (code, images, valeurs des sliders) et apparaît dans Resolume sous son propre nom, comme n'importe quel effet ou source, sans bibliothèque ni panneau.

- **Nom dans Resolume** : 16 caractères au plus. L'identifiant FFGL (4 caractères) est tiré du nom : deux exports du même nom remplacent le même plugin.
- **Effet / Source** : proposé selon que le shader lit l'entrée Resolume ou non.
- **Réglages actuels des sliders comme valeurs par défaut** : le plugin démarre avec le rendu que tu as réglé.
- Le fichier `Shader Loader - <nom>.dll` est écrit par défaut dans le dossier de Shader Loader (que Resolume scanne déjà). Redémarre Resolume pour le voir.
- Le plugin exporté contient tout Shader Loader (~10 Mo). Il fonctionne sur un autre PC sans Shader Loader installé. Pas pour les projets SHADERed ni les shaders Godot.
- Respecte la licence du shader si tu diffuses le plugin (la plupart des shaders Shadertoy sont « non commercial »).

## Améliorer un shader avec Claude

### Avec Claude Desktop (ton abonnement Claude, sans clé API)

1. Onglet **ÉDITEUR** (ou PRÉFÉRENCES) → **Connecter à Claude Desktop**, puis quitte complètement Claude Desktop (icône près de l'horloge → *Quitter*) et rouvre-le. Le bouton copie le petit serveur Shader Loader dans `%APPDATA%\ShaderToyFX` et l'ajoute à `claude_desktop_config.json` (une copie `.bak` est gardée). Autre possibilité : double-cliquer **`Shader Loader pour Claude Desktop.mcpb`** (dans le zip) pour l'installer comme extension.
2. Dans Resolume, charge l'effet ou la source Shader Loader. Si tu en as plusieurs, sélectionne l'instance voulue dans le panneau.
3. Dans Claude Desktop, demande par exemple : « Améliore l'effet chargé dans Shader Loader : réagit aux basses, plus saturé, ajoute des sliders vitesse et couleur ». Le bouton *Copier la demande* de l'ÉDITEUR prépare ce message.

Claude utilise alors ces outils de l'extension :

| Outil | Rôle |
|---|---|
| `list_instances` | Instances Shader Loader ouvertes dans Resolume (E1, E2… effets ; S1… sources) |
| `get_shader` | Code des passes, branchement des iChannels, sliders, erreurs |
| `update_shader` | Compile le nouveau code ; s'il compile, il est appliqué en direct, sinon les erreurs reviennent à Claude |
| `capture_output` | Image du rendu actuel (le clip doit jouer), pour que Claude vérifie le résultat |
| `save_shader` | Enregistre dans la bibliothèque (ou sous un nouveau nom) |
| `revert_shader` | Recharge la version enregistrée |

Tout passe par un canal local entre Claude Desktop et Resolume sur ton ordinateur ; rien n'est enregistré sans `save_shader`. Pas pour les projets SHADERed ni les shaders Godot.

### Avec une clé API Anthropic (facturée à part)

Dans l'onglet **ÉDITEUR**, rubrique **Améliorer avec Claude** : ajoute ta clé API Anthropic dans **PRÉFÉRENCES**, écris la consigne, choisis le modèle (Sonnet : rapide ; Opus : le plus capable ; Haiku : économique), puis **Améliorer (API)**. Shader Loader vérifie que le résultat compile et le fait corriger sinon (2 fois au plus), l'applique en direct ; **Enregistrer** pour le garder, **Annuler l'amélioration** pour revenir en arrière.

## Projets SHADERed et shaders Godot

Shader Loader exécute les projets [SHADERed](https://shadered.org) avec le même moteur que SHADERed : passes de rendu et compute, géométries (cube, sphère, plan, quad plein écran…), modèles `.obj`, render textures, états de rendu (fusion, profondeur, stencil, fil de fer), instanciation, buffers, cubemaps, tessellation et geometry shaders, variables système (`Time`, `ViewportSize`, `MousePosition`, matrices de caméra…) et fonctions.

- **Favori SHADERed** (comme le bookmarklet Shadertoy) : dans l'onglet LIEN, clique *Copier le favori SHADERed*, crée un favori dans ton navigateur et colle le texte comme adresse. Sur la page d'un shader de shadered.org, ce favori télécharge le projet en `Titre [shadered-id].zip`. Shader Loader le récupère dans tes Téléchargements, le range dans la bibliothèque, supprime l'archive et charge le projet.
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

Ainsi, la plupart des shaders Shadertoy deviennent réglables sans modification. Sont volontairement exclues les valeurs qui doivent rester constantes en GLSL : taille de tableau, `#if`, `case`, initialisation d'une autre constante ou d'une variable globale, ainsi que PI/TAU et les valeurs inutilisées. Si un cas particulier empêche quand même la compilation, Shader Loader retire le paramètre fautif et l'indique en bas de la fenêtre.

**Le milieu de chaque slider correspond exactement à la valeur d'origine du shader** : à l'import, les sliders donnent donc le même rendu que sur Shadertoy, et *réinitialiser* un paramètre dans Resolume y revient. La plage va de 0 au double de cette valeur (entiers : de 1 à 2×−1).

Pour que le rendu reste identique à Shadertoy une fois les valeurs transformées en sliders, Shader Loader :
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

Shader Loader est un logiciel libre sous licence **GNU GPL v3** (voir [`LICENSE`](LICENSE)). Les composants tiers inclus et leurs licences sont listés dans [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

Ce projet n'est affilié ni à Shadertoy, ni à Resolume, ni à Headsta.sh. Les shaders que tu importes restent sous la licence de leurs auteurs.
