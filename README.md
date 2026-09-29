# Spiderbench Native — C++ / SDL2 / OpenGL

Port nativo do [Spiderbench](../spiderbench) (Three.js/WebGL2) para **C++20 + SDL2 + OpenGL 3.3 core**
(usa um contexto 4.5+ quando disponível, para depth reversed-Z).

Este é o **primeiro corte vertical** da migração: a travessia do jogo (swing, parede, zip, poleiro, estilingue,
impulso rápido, truques aéreos) foi portada linha a linha e roda sobre um motor novo, com uma Manhattan procedural
simplificada. Os demais módulos vêm nas próximas fases (veja a tabela abaixo).

## Build (Windows, MSYS2 UCRT64)

Pré-requisitos (já presentes nesta máquina): `mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,SDL2,zlib,jsoncpp}`,
Python 3 com Pillow (WebP) e `ffmpeg` no PATH.

```bash
python tools/convert_assets.py            # lê ../spiderbench/public/assets e gera ./assets (~80 MB)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/spiderbench.exe
```

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
0 corrida + cadeia de swings + zip + poleiro · 1 corrida na parede · 2 Central Park (âncoras em árvores) · 3 Financial District · 4 agarrado na parede (cling) · 5 poleiro parado.
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
