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
| `infancia_conversa.txt` | `./build/eva2 --nova --infancia 20000`, com a entrada: `/categoria gato`, `/categoria bonita`, `/categoria correm`, `/categoria brincar`, `completar o cachorro`, `completar os macacos`, `completar a girafa`, `completar as vacas`, `completar eu gosto`, `completar vamos`, `o gato é bonito`, `a vaca come a banana`, `os cachorros correm na praia`, `estado`, `sair` (a entrada não aparece no log porque vem por tubo) | ela ouve 20 mil frases da casa e depois conversa |

Os testes da Eva 2 compilam com o CMake (`eva2_bench`, `eva2_ground`, `eva2_grammar`, `eva2_categories_test`, `eva2_vision_test`); veja `v2/tests/README.md`. Os da Eva 1 estão em `tests/README.md`.
