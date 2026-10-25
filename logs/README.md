# Logs reais

Saídas gravadas direto dos binários, sem edição (compilados em Release):

| arquivo | comando | o que mostra |
|---|---|---|
| `eva2_demo.txt` | `./build/eva2 --demo` | Eva 2: três dias de passeio apontando e nomeando, teste de sentido, imaginação, compreensão ("vamos para a praia"), sozinha na praia, fome |
| `ground.txt` | `APONTA=1 ./ground N 8` e `./ground 4 8` | medida de sentido com olhos: nomear desenhos novos, imaginar, entender (tabela do README) |
| `ground_detalhe.txt` | `APONTA=1 V=1 ./ground 4 1` | o que ela diz vendo cada coisa e o que imagina de cada palavra, uma semente |
| `bench2.txt` | `./bench2` | 20 frases × 2 exposições, 8 sementes: palavras e frases inteiras certas |
| `bench2_detalhe.txt` | `V=1 ./bench2 2` | as completações frase a frase da primeira semente |
| `vision_test.txt` | `./vision_test` | só o córtex visual: erro de V1, erro de imaginação, sobreposição de códigos |
| `eva1_demo.txt` | `./build/eva` | Eva 1 (neurônios de pulsos): diálogo que nasce do estado interno |

Como compilar os testes está em `v2/tests/README.md` e `tests/README.md`.
