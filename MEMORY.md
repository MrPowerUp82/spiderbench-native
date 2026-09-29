Vou portar o módulo `world/` (~20 mil linhas de JS) para C++, módulo por módulo em ordem de dependência, começando pelo layout completo, e validar a fidelidade comparando as saídas com a execução do JS original no Node. Começando agora pelo `layout.js`.

Vou montar um harness em Node para rodar o gerador original de forma headless e determinística, que servirá de referência para validar o port em C++ módulo a módulo.

Os 21 módulos da cidade carregam no Node sem navegador. Isso abre duas rotas bem diferentes para esta fase, e a escolha é sua:

Optei por essa abordagem: um passo de build executa o gerador original no Node, e a cidade em C++ carrega e renderiza o resultado com os shaders portados. Agora vou verificar quais dependências de navegador o `buildCity` exige (texturas, renderer) para criar os stubs necessários.

Vou montar o harness do bake com stubs de `Image`, `fetch`, canvas e renderer, rodar o `buildCity` inteiro e inventariar a cena (malhas, instâncias, materiais e atributos):

O gerador avança (só os telhados somam 9,3 M triângulos). Troco o renderer por um Proxy que aceita qualquer chamada:

O gerador original completou em 25s headless, gerando 32,5M triângulos estáticos, 2M instanciados, 1,1M sólidos de colisão e 117mil zip points, mas os GLBs de props e veículos falharam por um detalhe no stub de `fetch`. Vou corrigir esse problema e mapear onde estão concentrados os triângulos, já que isso vai definir o formato do bake e a estratégia de LOD.

Identifiquei 3,4 GB de geometria gerada no JS (telhados, detalhes e fachadas), com árvores e props em pools instanciados preenchidos por distância — isso definirá o formato do bake (dados quantizados/comprimidos, tiles com LOD e pools reproduzidos em C++). Agora vou puxar a lista completa de nomes de malhas para mapear tudo.

Identifiquei cerca de 200 tipos de malha organizados em estáticas por tile, instanciadas fixas, pools dinâmicos e dinâmicas futuras (excluídas do bake). Agora vou examinar a infraestrutura de suporte em `collision.js`, `pool.js` e `tilebatch.js` para definir o formato.

Vou portar as consultas de colisão (grade uniforme, caixas, cilindros, rampas e height fields) para C++, exportando os arrays no bake, e reconstruir o terreno analítico a partir das malhas de chão com verificação por amostragem contra o `terrainHeight` original. Agora sigo para os pools e os tiles.

Colisão e pools mapeados. O desafio agora são os ~40 materiais com `onBeforeCompile`; em vez de portar o GLSL à mão, vou testar rodar o `WebGLRenderer` no Node com contexto WebGL simulado para capturar o shader final exato de cada programa e compilá-lo em C++ com os uniforms no padrão do Three.js.

A captura funcionou: extraí o GLSL completo com os patches, defines e uniforms customizados. Como os shaders dependem da configuração de luz/sombra/ambiente da cena, vou verificar como `lighting.js` e `csm.js` se integram aos materiais para reproduzir os mesmos programas do jogo.