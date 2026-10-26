# Logs reais

Saídas gravadas direto dos binários, sem edição (compilados em Release):

| arquivo | comando | o que mostra |
|---|---|---|
| `eva2_demo.txt` | `./build/eva2 --demo` | Eva 2: três dias de passeio apontando e nomeando, teste de sentido, imaginação, compreensão ("vamos para a praia"), sozinha na praia, fome |
| `ground.txt` | `APONTA=1 ./build/eva2_ground N 8` e `./build/eva2_ground 4 8` | medida de sentido com olhos: nomear desenhos novos, imaginar, entender (tabela do README) |
| `ground_detalhe.txt` | `APONTA=1 V=1 ./build/eva2_ground 4 1` | o que ela diz vendo cada coisa e o que imagina de cada palavra, uma semente |
| `bench2.txt` | `./build/eva2_bench` | 20 frases × 2 exposições, 8 sementes: palavras e frases inteiras certas |
| `bench2_detalhe.txt` | `V=1 ./build/eva2_bench 2` | as completações frase a frase da primeira semente |
| `vision_test.txt` | `./build/eva2_vision_test` | só o córtex visual: erro de V1, erro de imaginação, sobreposição de códigos |
| `eva1_demo.txt` | `./build/eva` | Eva 1 (neurônios de pulsos): diálogo que nasce do estado interno |
| `grammar.txt` | `V=1 ./build/eva2_grammar 10 2000 300 2` | gramática emergente: 20 mil frases, 10 dias com sono, 2 cérebros; sujeitos retidos com e sem gramática, sujeitos conhecidos, categorias |
| `grammar_fresh_context.txt` | `FRESH=1 V=1 ./build/eva2_grammar 10 2000 300 2` | a variante que não funcionou ("contexto novo depois de surpresa" no córtex de categorias) |
| `categories_test.txt` | `V=1 ./build/eva2_categories_test N` | só as categorias, com 2 mil a 40 mil frases |
| `infancia_conversa.txt` | `echo sair \| ./build/eva2 --nova --infancia 10000` e depois `./build/eva2` (a mesma memória) com a entrada: `/cena` (4 vezes), `/conta`, `cadê a vovó`, `o que o lobo vê`, `o lobo viu o prato?`, `a vovó está na praia?`, `cadê o tigre`, `o urso está na praia`, `cadê o urso`, `o meu cachorro chama pingo`, `como chama o meu cachorro`, `o seu gato chama luna`, `como chama o seu gato`, `como você está`, `você está triste?`, `esperar 30`, `estado`, `sair` (a entrada não aparece no log porque vem por tubo; as cenas do `/cena` são sempre as mesmas: os primeiros sorteios da gramática) | ela vive 10 mil momentos da casa e depois conversa: sim/não, não sei, fatos contados, nomes com meu/seu, sentimento |
| `scenes.txt` | `CACHE=... V=1 ./build/eva2_scenes 10000 200` | quem faz o quê, descrição de cenas, quantidade, perguntas pela memória (presente e passado), lembranças espontâneas, sim ou não, não sei, negação, fatos contados, nomes com meu/seu, sentimentos, frases que terminam (com e sem cada peça) |
| `scenes_olhos.txt` | `EYES=1 CACHE=... V=1 ./build/eva2_scenes 10000 200` | o mesmo, com as cenas vistas pelos olhos (imagens, 3 olhares por coisa) em vez de símbolos: conceitos visuais formados e o quanto o resto sofre |
| `olhos_tracos.txt` | `./build/eva2_eyes_features` | só os olhos: reconhecer as 202 coisas com 1 e 3 olhares, só V1, V1+V4, só V4; formar conceitos sem nome com cada limiar |
| `real.txt` | `TRAIN=pt_gsd-ud-train.conllu,pt_bosque-ud-train.conllu TEST=pt_gsd-ud-dev.conllu V=1 ./build/eva2_real 8000 300` | português real (Universal Dependencies): próxima palavra em frases novas, espécies contra as classes anotadas, escala (RAM, sinapses, velocidade) |
| `topic.txt` | `CACHE=... V=1 ./build/eva2_topic 10000 300` | falar sobre o assunto: 300 frases novas, respostas com o tema ligado e desligado (imitação desligada) |
| `topic_com_imitacao.txt` | `ECO=1 CACHE=... V=1 ./build/eva2_topic 10000 300` | o mesmo, com a imitação de frases novas ligada |

Os testes da Eva 2 compilam com o CMake (`eva2_bench`, `eva2_ground`, `eva2_grammar`, `eva2_categories_test`, `eva2_vision_test`, `eva2_topic`, `eva2_scenes`, `eva2_real`, `eva2_eyes_features`); veja `v2/tests/README.md`. Os da Eva 1 estão em `tests/README.md`.

No passo "Conversar de verdade" foram gerados de novo: `scenes.txt`, `scenes_olhos.txt`, `infancia_conversa.txt`, `topic.txt`, `topic_com_imitacao.txt`, `bench2.txt`, `bench2_detalhe.txt`, `eva2_demo.txt`, e os novos `real.txt` e `olhos_tracos.txt`. Os outros (`grammar*.txt`, `categories_test.txt`, `ground*.txt`, `vision_test.txt`, `eva1_demo.txt`) são da versão anterior: o que eles medem não mudou neste passo (os números podem variar um pouco ao refazer, porque a ordem dos sorteios mudou).
