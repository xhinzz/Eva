# Medição da Eva 2

Todos os testes compilam junto com o resto:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

| executável | o que mede | comando de exemplo |
|---|---|---|
| `eva2_bench` | frases longas: ensina 20 frases (2 vezes) e pede para completar a partir das 2 primeiras palavras | `./build/eva2_bench` (`V=1` mostra as completações) |
| `eva2_ground` | sentido com olhos: nomear desenhos novos, imaginar pela palavra, entender "vamos para a praia" / "pega a flor" | `APONTA=1 ./build/eva2_ground 4 8` (voltas, sementes) |
| `eva2_vision_test` | só o córtex visual: erro de V1, erro de imaginação, sobreposição de códigos; escreve `ve/v1/imagina/mistura.ppm` | `./build/eva2_vision_test` |
| `eva2_categories_test` | só as categorias (segundos): o vizinho mais parecido de cada palavra é da mesma classe? | `V=1 ./build/eva2_categories_test 20000` |
| `eva2_topic` | falar sobre o assunto: depois de uma infância, responde frases novas; no assunto, gramatical, eco, com e sem o tema | `CACHE=cerebro.bin V=1 ./build/eva2_topic 10000 300` (`ECO=1` liga a imitação, `GAIN` o peso do tema, `SHARP` a competição entre intenções) |
| `eva2_scenes` | cenas: compreensão de quem faz o quê, descrição, quantidade, perguntas pela memória (presente e passado), lembranças espontâneas (com e sem cada peça) | `CACHE=cerebro.bin V=1 ./build/eva2_scenes 10000 200` (momentos da infância, testes) |
| `eva2_grammar` | gramática emergente: dias de fala de uma casa + sono; continua frases com sujeitos que nunca ouviu como sujeito | `V=1 ./build/eva2_grammar 10 2000 300 2` (dias, frases/dia, episódios de sono, sementes) |

Variáveis de ambiente úteis no `eva2_grammar`: `CAT=0` (sem categorias: nem aprende),
`FRESH=0/1` (contexto novo depois de surpresa no córtex de categorias), `MATCH`, `OVERRIDE`,
`CCOLS`, `CACT`, `MORPH`, `CTXW`, `COLS`, `CELLS`, `CCELLS`. No `eva2_scenes`: `EYES=1` (as cenas chegam como imagens pelos olhos; o relatório mostra quantos conceitos visuais ela formou e se a mesma coisa cai no mesmo conceito), `CONCEPT` (limiar de semelhança para cair num conceito visual, padrão 0,8) e `LABEL` (limiar quando a palavra ouvida já tem um conceito, padrão 0,25). No `eva2_categories_test`: `FAN`,
`ACT`, `CTXW`, `MORPH`, `MINC`. No `eva2_bench`: `CAT`, `MINC`, `DEC`, `INIT`.

O corpus (`v2/corpus.hpp`) é uma gramática do português falado com criança: ~770 palavras,
19 moldes de frase, concordância de gênero e número, restrições de sentido, frequência
zipfiana. A mesma gramática gera o que ela ouve e verifica o que ela diz. Ela nunca vê a
gramática, só as frases.

As imagens saem em PPM; para PNG: `python3 -c "from PIL import Image; Image.open('x.ppm').save('x.png')"`.

Os resultados de referência estão em `logs/` (com o comando que gerou cada um).
