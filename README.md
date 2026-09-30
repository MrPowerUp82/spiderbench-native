# Spiderbench Native — C++ / SDL2 / OpenGL

Port nativo do [Spiderbench](../spiderbench-remake) (Three.js/WebGL2) para **C++20 + SDL2 + OpenGL 3.3 core**
(usa um contexto 4.5+ quando disponível, para depth reversed-Z).

Este é o **primeiro corte vertical** da migração: a travessia do jogo (swing, parede, zip, poleiro, estilingue,
impulso rápido, truques aéreos) foi portada linha a linha e roda sobre um motor novo, com uma Manhattan procedural
simplificada. Os demais módulos vêm nas próximas fases (veja a tabela abaixo).

## Build (Windows, MSYS2 UCRT64)

Pré-requisitos: `mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,SDL2,zlib,jsoncpp}`,
Python 3 com Pillow (WebP) e `ffmpeg` no PATH.

```bash
python tools/convert_assets.py            # lê ../spiderbench/public/assets e gera ./assets (~80 MB)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/spiderbench.exe
```

### Build com Visual Studio Build Tools e vcpkg

No PowerShell, com Visual Studio 2022 Build Tools instalado:

```powershell
git clone https://github.com/microsoft/vcpkg.git build/vcpkg
.\build\vcpkg\bootstrap-vcpkg.bat -disableMetrics
.\build\vcpkg\vcpkg.exe install sdl2 zlib jsoncpp opengl-registry --triplet x64-windows
python tools/convert_assets.py --src ..\spiderbench-remake --out assets
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$toolchain = (Resolve-Path .\build\vcpkg\scripts\buildsystems\vcpkg.cmake).Path
& $cmake -S . -B build/msvc -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
& $cmake --build build/msvc --config Release --parallel 8
.\build\msvc\Release\spiderbench.exe
```

O executável e as DLLs necessárias ficam em `build/msvc/Release/`.

Para gerar a referência fiel da cidade a partir do JS original instalado em
`../spiderbench-remake`:

```bash
cd ../spiderbench-remake && npm ci && cd ../spiderbench-native
npm ci                                  # canvas 2D para o bake headless
cmake --build build --target bake_city       # preset "med" do remake
cmake --build build --target bake_city_low   # preset "low" (GPUs integradas)
```

Os alvos executam os bakes em sequência e validam seus resultados.
Os arquivos gerados ficam em `build/city-bake/` + `build/refshaders/` (med) e
`build/city-bake-low/` + `build/refshaders-low/` (low). Os scripts aceitam
`SB_QUALITY`, `SB_BAKE_DIR` e `SB_SHADER_DIR`.

As DLLs de runtime (SDL2, zlib, jsoncpp, libstdc++) são copiadas para `build/` automaticamente. O executável
procura `assets/` ao lado de si e, se não encontrar, na pasta do código-fonte. `RelWithDebInfo` mantém o console
(logs); `Release` gera um executável de janela.

## Controles

| Teclado / mouse | Ação |
|---|---|
| WASD · mouse | mover · câmera (clique para capturar o mouse, Esc para soltar) |
| Botão direito (segurar) | balançar na teia (soltar = release com impulso) |
| Espaço | pular (segure = carga) · durante o swing = soltar com impulso vertical · 2× no ar = truque |
| Shift | correr na parede / parkour |
| E / botão do meio | web-zip até o ponto marcado (sem alvo no ar = web-dash; na parede = zip parede acima) |
| C / Ctrl | mergulho / soltar |
| Q | impulso rápido de teia (no ar) |
| Ctrl + botão esquerdo/direito (no chão) | estilingue de teia (andar para trás estica, soltar Ctrl lança) |
| 1–5 · R | teleporte (Midtown, Central Park, Financial District, Village, telhado) · renascer |
| H · M · F11 · F12 | ajuda · música · tela cheia · screenshot |

Controle (gamepad) via SDL_GameController: LS mover, RS câmera, R2 swing/parkour, A pular, L2+R2 ou Y zip, B mergulho, L1 impulso.

## Modo de teste

```bash
build/spiderbench.exe --test 12 --scene 0 --out saida/
```

Roda input roteirizado a 60 Hz fixos, registra o estado da travessia (`[test] modo/sub-estado/posição/velocidade`),
salva screenshots e sinaliza violações de invariantes (`position jump`, `BUG: left 'swing'`). Cenas:
0 corrida + cadeia de swings + zip + poleiro · 1 corrida na parede · 2 Central Park (âncoras em árvores) · 3 Financial District · 4 agarrado na parede (cling) · 5 poleiro parado · 6 parado ao sol no Central Park (sombras do personagem).
O log inclui o nó e o clip do animador (`anim=swing#L | swingLowL`).
Variáveis de debug: `SB_GLDEBUG=1` (erros GL), `SB_DEBUGVIEW=1|2` (termo de sombra / n·l), `SB_NOREVZ=1`, `SB_LOGALL=1`.

## Mapeamento JS → C++

| Original (src/) | Nativo (src/) | Estado |
|---|---|---|
| `player/traversal/traversal.js` | `player/traversal/traversal.cpp` | portado (ground, air, swing, wall, zip, perch, sling, quick boost, truques, water bounce); falta a corda bamba (`rope`), voo do Superman e burst do Flash |
| `player/traversal/{collide,anchors,zippoints}.js` | `player/traversal/helpers.cpp` | portado (caminho por caixas AABB) |
| `player/camera.js` | `player/chase_camera.cpp` | portado integralmente |
| `player/input.js` | `player/input.cpp` | portado (teclado, mouse relativo, gamepad) |
| `player/player.js` | `player/player.cpp` | portado |
| `player/anim/animator.js` + `skeleton.js`, `builder.js`, `rigdata.js`, `clips.js`, `traversal/anim.js` | `anim/animator*.cpp`, `anim/pose.cpp`, `anim/anim_state.cpp` | portado: máquina de estados em camadas com crossfade, blend spaces (locomoção com casamento de fase e stride warp, ar, swing), IK analítico, pegada de duas mãos, pernas no swing, truques procedurais, cling / escalada na parede, corrida na parede, agachamento no poleiro, IK dos pés, olhar, respiração. Faltam o nó da corda bamba e os overlays de combate |
| `player/rig.js` | `anim/rig.cpp` + `asset/gltf.cpp` | loader GLB, mixer com crossfade, skinning, aim de ossos |
| `player/web.js` | `player/web.cpp` | comportamento portado; visual simplificado (fita iluminada) |
| `world/layout.js` | `world/layout.cpp` | grid, larguras de rua, linha de costa, Central Park; falta VMAP/FMAP e Broadway |
| `world/city.js`, `buildings.js`, `rooftops.js`, `ground.js`… | `world/world.cpp` | substituídos por geração procedural simplificada (recuos, parapeitos, caixas d'água, postes, árvores) |
| `render/pipeline.js`, `csm.js`, `lighting.js`, `sky.js`, `water.js` | `gfx/renderer.cpp` + `gfx/shaders.h` | CSM 3 cascatas, forward HDR, céu, água, bloom, blur de velocidade, ACES, FXAA |
| `ui/hud.js` | `ui/hud.cpp` | distrito, velocidade, retícula de zip, ajuda |
| `game/systems/audio.js` | `audio/audio.cpp` | mixer SDL com sprites do manifesto original, música, vento |

### Próximas fases

1. Cidade fiel: `buildings.js`/`facade.js`/`rooftops.js`/`signage.js`, Times Square, Grand Central, pontes, orla.
2. NPCs e tráfego (`world/npc/*`, `vehicles.js`, `peds.js`).
3. Combate (`game/combat/*`) e sistemas de mundo aberto (`game/systems/*`: crimes, colecionáveis, progressão, save).
4. Menus (`ui/menus/*`: trajes, mapa, configurações, modo foto) e personagens crossover.
5. Pipeline avançado: SSGI, SSR, AO, TAA, reflexos de vidro, dia/noite e chuva.

Projeto de fã, não comercial — mesma licença e aviso do projeto original.

## Referência JS para o bake

O harness em [tools/ref/README.md](tools/ref/README.md) executa `buildCity` no Node e
captura o GLSL final dos materiais com os patches globais de iluminação e CSM.
Também exporta a colisão, os prédios e os zip points finais para um arquivo
binário lido por `src/world/baked_collision.cpp`, com 512 consultas de referência
do JS. As 1.510 malhas da cena também são exportadas para um arquivo de 355 MiB,
com 7.531 buffers verificados por checksum e um leitor C++ de acesso sob demanda.
As listas completas dos 85 pools de LOD também foram exportadas e vinculadas
às malhas, cobrindo 550.880 entradas de instâncias.
O bake captura 62 texturas com pixels (incluindo o DFG LUT do Three.js) e 348 vínculos de textura a programas;
dois render targets dependem da execução e não têm pixels iniciais.
Quando o bake existe, o modo de jogo usa a cidade original: renderização com os
programas capturados e todas as consultas da travessia sobre a colisão exata
(`--procedural` volta à cidade simplificada; `--quality low` usa o bake low).
`bake_traversal.mjs` exporta o terreno analítico (`terrainHeight`) como raster em
paleta de 0,25 m (0,011% de divergência nas amostras), 27.881 árvores de ancoragem e
consultas de referência; `--validate-baked-traversal` confirma que `groundHeight`
(1.500), `raycast` (3.000) e `getZipPoints` (200) batem com o JS original.
Validações e modo de inspeção:

```powershell
.\build\msvc\Release\spiderbench.exe --validate-baked-shaders
.\build\msvc\Release\spiderbench.exe --validate-baked-pools
.\build\msvc\Release\spiderbench.exe --validate-baked-csm
.\build\msvc\Release\spiderbench.exe --validate-baked-tiles
.\build\msvc\Release\spiderbench.exe --validate-baked-environment
.\build\msvc\Release\spiderbench.exe --validate-baked-traversal
.\build\msvc\Release\spiderbench.exe --view-bake
.\build\msvc\Release\spiderbench.exe --view-bake --test 1 --out build/baked-preview
```

Os 202 programas dos passes principal, reflexo e profundidade passaram pela
compilação/link na GPU Intel UHD. O modo de inspeção desenha as malhas com texturas,
uniforms, composição dos materiais, pools por distância e três cascatas de sombra,
usando câmera livre (WASD, Q/E, mouse e Shift). A seleção dos pools coincide com
127.765 instâncias ordenadas do JS; também coincidem 24 encaixes de cascata e
7.785 estados de visibilidade/sombra dos tiles, incluindo a histerese de LOD.
O ambiente também é gerado na GPU nativa com os programas originais: ruído 3D,
LUT atmosférica, cubemap e PMREM GGX de 768 × 1024. O céu usa o passe original
em meia resolução. A composição atmosférica, seis níveis de bloom, exposição
automática, TAA e correção de cor executam 15 passes capturados de `pipeline.js`.
O TAA preserva o jitter Halton de 16 frames, reprojeção e histórico alternado.
O personagem usa os programas originais: `capture_shaders.mjs --character` roda o
`loadCharacter` do remake (material `SpiderSuit` do GLB + patch `suitfabric.js`,
skinning por `boneTexture`) e grava 4 programas e 6 texturas em `<shaders>/character`
e `<bake>/character`. O nativo desenha o traje e as lentes com a iluminação IBL/CSM
da cidade, projeta a sombra do jogador nas cascatas 0–2 e na cascata dedicada
`CSM_char` do `csm.js` (1024 px a até 25 m da câmera, só no preset med).
A cena de teste 6 deixa o personagem parado ao sol no Central Park.
Ainda faltam AO, SSGI, reflexos, shafts/flare, efeitos de câmera e a máscara de
movimento do personagem no TAA. A fidelidade de
imagem ainda precisa de comparação com capturas do navegador na mesma câmera.
`SB_PROFILE=1` mostra o tempo de CPU por quadro e `SB_PROFILE=2` o tempo de GPU
por fase (sombras, céu, cidade, pós).
