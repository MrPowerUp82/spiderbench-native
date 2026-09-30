# Referência headless do gerador JS

O código original está em `../spiderbench-remake`. Instale as dependências nele com
`npm ci` antes de executar os scripts desta pasta. Execute `npm ci` também na
raiz deste projeto para instalar o canvas headless usado pelo harness.
`SB_JS_ROOT` pode apontar para outra cópia do original.

```powershell
node tools/ref/capture_shaders.mjs
node --max-old-space-size=8192 tools/ref/capture_shaders.mjs --city
node --max-old-space-size=8192 tools/ref/bake_collision.mjs
python tools/ref/verify_collision.py
node --max-old-space-size=8192 tools/ref/bake_geometry.mjs --limit 12
node tools/ref/verify_geometry.mjs --sample
node --max-old-space-size=8192 tools/ref/bake_geometry.mjs
node tools/ref/verify_geometry.mjs
node --max-old-space-size=8192 tools/ref/bake_pools.mjs
node --max-old-space-size=4096 tools/ref/pack_pools.mjs
node --max-old-space-size=4096 tools/ref/verify_pool_data.mjs
node tools/ref/verify_csm.mjs
node --max-old-space-size=8192 tools/ref/verify_tiles.mjs
node --max-old-space-size=4096 tools/ref/verify_pools.mjs
node tools/ref/verify_bake_links.mjs
node tools/ref/verify_textures.mjs
node tools/ref/capture_environment.mjs
node tools/ref/verify_environment.mjs
```

O primeiro comando captura uma malha de prova em `build/refshaders-smoke/`. O
segundo executa `buildCity` e compila as variantes usadas por suas malhas nos
passes principal com sombras, reflexo sem sombras e profundidade. Os arquivos em `build/refshaders/`
incluem `manifest.json` e um par `.vert.glsl`/`.frag.glsl` por programa distinto.
`entries` descreve os programas e seu `pass`; `usages` relaciona as variantes de material e
geometria aos programas, inclusive quando o Three.js reutiliza um programa.
`objectPrograms` liga cada malha/material aos usos dos três passes (4.530
ligações). Cada uso inclui `uniformValues`, com os valores numéricos do material,
transformações UV e estruturas de iluminação. Matrizes de câmera e direção da
luz são atualizadas pelo C++ durante o desenho. Cada uso preserva também
`renderState`: blending, escrita/teste de profundidade, faces e polygon offset.
`shadowObjects` guarda camadas, limites de cascata, divisores dos atributos,
contagens de InstancedBufferGeometry e centros de tiles/células. Esses centros
são adicionados por hooks de importação, sem alterar os arquivos do remake.

A captura cria `lighting.js` antes de compilar a cidade. Assim, os overrides
globais de `surface.js` e `csm.js` entram nos shaders finais, junto com os patches
`onBeforeCompile` de cada material. Usa a qualidade padrão `med`, horário `day`,
depth reversed-Z, três cascatas de sombra mais a cascata do personagem, PCF com
oito taps e um placeholder CubeUV para ativar a variante IBL. O placeholder não
contém pixels; serve apenas para selecionar a mesma variante de programa. Como
no warmup do jogo, o compilador usa um render target linear de meia precisão e
os opt-outs de SSR do pipeline. O passe de reflexo é gerado para todas as
variantes da cidade, inclusive algumas que o jogo talvez não desenhe nele.

O mock de WebGL registra o GLSL entregue pelo Three.js a `shaderSource`. Ele
assume sucesso de compilação, portanto estes arquivos **não comprovam** que o
GLSL compila em uma GPU. O comando nativo abaixo faz essa verificação real:

```powershell
.\build\msvc\Release\spiderbench.exe --validate-baked-shaders
# ou: cmake --build build/msvc --config Release --target verify_baked_shaders
```

Os 202 programas compilaram e linkaram no driver Intel UHD OpenGL 4.6. O
leitor nativo troca a versão GLSL e remove a sintaxe de precisão ES, preservando
os patches dos materiais. A validação encontrou e corrigiu um placeholder CubeUV
com dimensões inválidas: o PMREM de cubemap 256 usa 768 × 1024. Os shaders de
profundidade fazem parte desse inventário. Ambiente e pós-processamento são
capturados separadamente em `build/city-bake/environment/`.
Uniforms variáveis em tempo de execução ainda precisam acompanhar todos os
sistemas do original. O gerador usa canvas 2D real e pixels das imagens originais.

## Colisão, prédios e zip points

`bake_collision.mjs` grava o arquivo versionado `build/city-bake/collision.sbcol`
e seu manifesto `collision.json`. O arquivo contém os arrays finais do
`CollisionGrid` (tipos, flags, caixas, parâmetros e grade espacial), os height
fields, as caixas de prédios, o spawn e todos os zip points validados. Os dados
são copiados como `Float32`, `Uint32` e `Uint8` em little endian. O layout está no
exportador e no leitor [baked_collision.cpp](../../src/world/baked_collision.cpp).

`queries.json` guarda 512 consultas determinísticas feitas ao JS original.
`verify_collision.py` reconstrói as consultas a partir do arquivo binário e
compara alturas, IDs de sólidos e testes de interior com a referência. O leitor
C++ implementa `topAt` e `inside`, mas ainda não está ligado ao `World`: trocar
as colisões antes de trocar a geometria desenhada criaria superfícies invisíveis.

## Malhas e materiais

`bake_geometry.mjs` grava `geometry.sbgeo` e `geometry.json` em `build/city-bake/`.
O arquivo contém buffers de vértices/índices comprimidos individualmente com
zlib; arrays compartilhados por várias malhas são gravados uma vez. O manifesto
mantém atributos (tipo, tamanho e normalização), draw ranges, grupos, matrizes,
instâncias iniciais, estado de visibilidade e parâmetros de 115 materiais. O
leitor C++ [baked_geometry.cpp](../../src/world/baked_geometry.cpp) busca e
descomprime buffers por ID sem carregar os 2,9 GiB inteiros na memória.
`verify_geometry.mjs` confere o SHA-256 de cada buffer após a descompressão.

O `materialId` e `objectOrdinal` no manifesto de shaders correspondem aos IDs
do manifesto de geometria quando os dois bakes são gerados da mesma revisão do
JS. `bake_pools.mjs` salva as listas completas dos 85 pools de LOD em
`pools.json.gz`: 550.880 entradas, inclusive as listas compartilhadas entre
LOD próximo, médio e distante. `verify_pools.mjs` confere os atributos e IDs
contra as malhas exportadas e detecta pools de LOD que ficaram de fora.
O modo `--view-bake` usa as malhas no renderer nativo, com atributos originais,
texturas, uniforms e programas capturados. `pack_pools.mjs` converte as listas
preservadas em `pool-data.json`/`.sbgeo`: 203 buffers, 15,1 MiB comprimidos.
O C++ preenche os 85 pools com as matrizes, cores e atributos originais.
`verify_pool_data.mjs` compara 249 amostras com `Pool.write` e exporta consultas
de seleção usando `Pool.update`. O verificador nativo compara 127.765 instâncias
ordenadas e seus prefixos de sombra nas seis câmeras de referência.
`verify_bake_links.mjs` confere a correspondência dos 348 usos de shader com
as malhas e os materiais exportados.

## Texturas

`capture_shaders.mjs --city` também grava `build/city-bake/textures.json` e
`texture-pixels/*.bin.z`. O manifesto associa uniforms de textura aos 348 usos
de programa. São 62 texturas com pixels preservados (499,2 MiB sem compressão)
e dois render targets dinâmicos sem pixels iniciais. `verify_textures.mjs`
descomprime os arquivos, confere os checksums e verifica as ligações com os
materiais e shaders. A amostragem no canvas headless pode diferir em detalhes
da implementação do navegador; a comparação visual com o original ainda é
necessária.

## Inspeção no executável

```powershell
.\build\msvc\Release\spiderbench.exe --view-bake
.\build\msvc\Release\spiderbench.exe --validate-baked-pools
.\build\msvc\Release\spiderbench.exe --validate-baked-csm
.\build\msvc\Release\spiderbench.exe --validate-baked-tiles
.\build\msvc\Release\spiderbench.exe --validate-baked-environment
.\build\msvc\Release\spiderbench.exe --view-bake --test 1 --out build/baked-preview
```

WASD move a câmera livre; Q/E altera a altura; Shift acelera; clique captura o
mouse; Esc fecha. `--scene 2` enquadra Central Park e `--scene 3` o Financial
District. `--bake-dir` e `--shader-dir` permitem usar outro diretório exportado.
O teste salva `baked_city.bmp` após dois frames na mesma câmera e horário.

Este modo verifica a integração visual das malhas e materiais. Usa as variantes
principais com sombras, os shaders de profundidade originais e três cascatas
2048/2048/1024 com PCF, snapping e atualizações escalonadas do JS. Mantém o
céu, ambiente PMREM e passes centrais de pós-processamento do original.
Os reflexos dinâmicos ainda recebem texturas pretas. Fachadas e telhados usam a distância à borda do tile e a
histerese 650/690 m; detalhes, decals de telhado e signage usam seus limites
originais. `verify_csm.mjs` exporta 24 encaixes de cascata e `verify_tiles.mjs`
exporta 7.785 estados chamando o código original; ambos são comparados pelo C++.
O DFG LUT do Three.js é exportado como RG16F para evitar NaNs nos materiais PBR. As
colisões do jogador continuam na cidade procedural do modo normal.

## Céu, ambiente e pós-processamento

`capture_environment.mjs` executa o renderer original com GL simulado e exporta
12 programas, buffers de geometria e uma sequência de comandos em `SBENV1`.
O C++ executa os 158 draws de inicialização: 128 camadas de ruído, LUT
atmosférica, seis faces do cubemap e filtragem GGX para PMREM de 768 × 1024.
O passe de céu roda a cada frame em meia resolução, com a câmera nativa.

A captura também executa `pipeline.js` com AO, SSR, SSGI e shafts desligados
para exportar seus 15 passes centrais e parâmetros originais. O nativo liga os
buffers reais de cor/profundidade à composição atmosférica, preserva as divisões
inteiras dos seis níveis de bloom e alterna os dois buffers de exposição.
O TAA usa o jitter Halton original, reprojeção, clipping de vizinhança e dois
buffers de histórico; redimensionamento e cortes de câmera reiniciam o histórico.
A máscara de movimento do personagem será ligada junto com o jogador nessa cena.
Correção de cor, ACES, vinheta, aberração cromática, sharpening e grain usam o
GLSL final original. Flare e efeitos de câmera ainda estão pendentes.

`verify_environment.mjs` confere os 29 buffers por checksum e as dependências
de leitura/escrita do grafo. `--validate-baked-environment` verifica a LUT/PMREM
geradas na GPU e grava `environment/native-check.json`; os pixels devem ser
finitos, não negativos e conter radiância. `SB_BAKE_TRACE=1` verifica também os
valores dos passes de pós-processamento durante a inspeção. Essas verificações
não substituem a comparação pixel a pixel com a execução no navegador.
