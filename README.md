# Eva

Um cérebro simulado em C++, inspirado na biologia, feito para conversar de verdade: sem modelo de linguagem, sem moldes de texto. Tudo que ela diz emerge do que ela viveu e do que está sentindo.

Há duas Evas neste repositório:

- **Eva 2** (`v2/`, executável `eva2`): um **córtex de colunas** (memória temporal hebbiana, no modelo de Hawkins/Numenta) ligado a um **mundo** onde ela percebe e age, com **olhos** (um córtex visual hebbiano que vê, lembra e imagina imagens). Frases longas, contexto longo, memória que persiste em disco entre conversas, e **sentido**: palavras ancoradas no que ela vê, faz e sente. É a que conversa.
- **Eva 1** (`src/`, executável `eva`): neurônios de pulsos ms a ms, hipocampo, sono, dopamina. É onde tudo começou, e onde ficaram os experimentos e as medições que levaram à Eva 2. Está documentada mais abaixo.

## Eva 2 em uma página

**Como ela funciona.** Cada palavra é um código esparso: 40 colunas ativas em 2048. Cada coluna tem 16 células, e **qual célula dispara depende do que veio antes**: "musica" depois de "gosto de" usa células diferentes de "musica" depois de "ouve". É o código conjuntivo que a Eva 1 não conseguiu formar no hipocampo, agora no lugar certo. Cada célula aprende, por sinapses hebbianas em segmentos dendríticos, a prever quem vem a seguir. Quando a previsão acerta, só as células previstas disparam (o contexto se preserva); quando erra, a coluna inteira "explode" e uma célula aprende o contexto novo. Aprendizado local, uma exposição já cria a sinapse (como o hipocampo), e a dopamina escala a força do aprendizado: o que foi dito com carinho fica mais firme.

**A conversa é um fluxo só.** Tudo entra na mesma sequência: `<voce> [estados] suas palavras <fim> <eva> [estados] palavras dela <fim> ...`. Os estados internos (`<feliz>`, `<curiosa>`, `<surpresa>`) entram no fluxo como palavras, então o que ela ouve e diz fica ligado ao que sentia na hora. Antes de ouvir você, o córtex relê os últimos 6 turnos: é o contexto longo.

**Como ela decide o que dizer.** Abre o turno dela (`<eva>` + estados) e deixa o córtex prever a próxima palavra, e a próxima, até `<fim>`. Se o contexto exato não prevê nada, a coluna explode e segue pela continuação mais comum (é como a gente conclui uma frase: pelo caminho mais batido). Se a frase que você disse era nova, ela **imita** primeiro (novidade → ecolalia, como uma criança). Se ia repetir o que acabou de dizer, procura outro caminho (habituação). Se nada vem, pega o gancho na sua última palavra (associação livre). Se ainda assim nada vem, fica em silêncio.

**Surpresa de verdade.** A surpresa dela é o erro de previsão do córtex: a fração de colunas que não eram previstas. Na primeira vez que você diz uma frase, 1,00; na segunda, 0,20.

**Memória longa.** Ao sair, ela salva o córtex inteiro (`eva2_memoria.bin`) e um diário legível (`eva2_diario.txt`). Na próxima conversa, lembra: `completar eu me chamo` → `pedro`.

**Medido** (`v2/tests/bench.cpp`): 20 frases de 5 a 8 palavras, cheias de palavras compartilhadas ("eu gosto de", "eu vou", "o mar", "hoje"), 8 sementes, completando a partir das 2 primeiras palavras:

| Exposições | Palavras na ordem certa | Frases inteiras |
|---|---|---|
| 1 | 57% | 64/160 |
| 2 | **73%** | **112/160** |
| 3 | 77% | 122/160 |

Boa parte dos erros restantes é ambiguidade legítima do começo ("eu vou" → trabalhar? dormir? viajar?). Roda em 1 segundo. Para comparar: a Eva 1 chegava a 80% em 3 frases curtas *sem* palavra compartilhada, e caía a 55% com uma só palavra compartilhada.

**Conversa real, sem edição** (primeira sessão, do zero):

```
Voce: oi eva tudo bem
         Eva: oi eva tudo bem   [eco <surpresa>]
Voce: eu me chamo pedro
         Eva: eu me chamo pedro   [eco <surpresa>]
Voce: eu gosto muito de musica !
         Eva: eu gosto muito de musica   [eco <feliz> <surpresa>]
Voce: amanha eu vou viajar para o mar
         Eva: amanha eu vou trabalhar cedo   [eco <feliz> <surpresa>]   <- caminho mais batido; aprende "viajar" na 2a vez
Voce: o mar e bonito !
         Eva: o mar e bonito   [eco <feliz> <surpresa>]
Voce: eu gosto muito de voce !
         Eva: amanha eu gosto muito de voce   [resposta <feliz>]
```

Segunda sessão, dias depois (ela carregou a memória):

```
(Eva lembra: 18 turnos, 33 palavras, 2541 segmentos)
Voce: completar eu me chamo
         Eva: eu me chamo ... pedro
Voce: completar o mar
         Eva: o mar ... e bonito
Voce: eu gosto de rock !
         Eva: eu gosto muito de rock   [eco <feliz> <surpresa>]   <- misturou com "eu gosto muito de musica"
```

## Sentido: o mundo dela

Sentido não vem de palavras ligadas a palavras. Vem de palavras ligadas ao que se percebe e ao que se faz. Então a Eva 2 ganhou um **mundo** (`v2/world.hpp`): quarto, cozinha, jardim e praia, com coisas em cada lugar (algumas mudam de lugar), hora do dia, fome e cansaço. Ela percebe (`[esta:praia] [ve:mar] [sente:fome]`) e age (`{ir:jardim} {pegar:pao} {comer} {dormir}`). Percepções e ações entram no **mesmo fluxo** do córtex que as palavras.

Três camadas fazem o sentido:

1. **Córtex associativo** (`v2/semantics.hpp`): co-ocorrência hebbiana entre percepções e palavras, em qualquer ordem, escalada pela dopamina, pontuada por especificidade (uma palavra que aparece com tudo não significa nada em especial). Nas duas direções: ver → nomear, ouvir → imaginar.
2. **Atenção conjunta:** você aponta (`/aponta flor`) e a próxima frase sua cola no que ela está olhando. É assim que bebês aprendem palavras (Tomasello), e aqui faz a diferença entre aprender e não aprender: **sem apontar, 30% dos nomes certos; apontando, 100%** (`v2/tests/ground.cpp`, 10 objetos, 8 sementes, uma volta pela casa). Sem apontar ela confunde o que vê junto ("vê janela → cama"), que é exatamente o problema de referência das crianças.
3. **Corpo e recompensa:** fome e cansaço crescem; comer com fome e dormir cansada dão dopamina, e a dopamina escala o aprendizado percepção → ação (três fatores). Há reflexos inatos (com fome, come o que tem na mão) sobre os quais o aprendizado se constrói, e exploração quando nada é previsto.

**Compreensão.** Ouvir uma palavra evoca percepções. Se "praia" evoca `[esta:praia]` e ela não está lá, isso vira **intenção**: ela caminha, um passo por turno, até chegar. Palavras que aparecem em quase toda frase ("a", "o", "e") não guiam a imaginação, como os bebês também as ignoram no começo.

Trecho real da demonstração (`./build/eva2 --demo`):

```
[Teste de sentido] So a percepcao, sem conversa. O que ela diz vendo cada coisa?
  ve cama     -> cama
  ve janela   -> janela
  ve flor     -> flor
  ve mar      -> mar e boa
  ve cafe     -> cafe
  ve passaro  -> (nada; nao sabe nomear)       <- ninguem nomeou o passaro

[O que as palavras evocam nela]
  'praia' -> [esta:praia](0.15) [ve:areia](0.15) [ve:barco](0.15)
  'flor'  -> [ve:flor](0.46)

[Compreensao] Ela esta no quarto. Voce diz 'vamos para a praia'.
         Eva: vamos para a praia   [eco <feliz> <surpresa>]
         Eva foi para jardim (indo para praia)   [{ir:jardim}]
         Eva foi para praia (indo para praia)    [{ir:praia}]

[Sozinha na praia] 20 s de silencio. O que ela diz, vendo o mar?
         Eva: mar e boa   [pensamento <curiosa>]

[Fome] Voce a leva a cozinha e fica quieto.
         Eva pegou fruta   [{pegar:fruta}]
         Eva comeu fruta   [{comer}, recompensa]
```

Quando ela vê algo que ninguém nomeou e está curiosa, ela **aponta** (`Eva: (aponta para livro)`): a pergunta antes da palavra.

Comandos do mundo no modo conversa: `/leva lugar`, `/aponta objeto`, `/da objeto`, `/ver`, `/nomear objeto` (o que ela diria vendo só aquilo), `/sentido palavra` (o que a palavra evoca nela, com força), `/imaginar palavras...` (a imagem que evocam → `imaginou.ppm`), `/olhar` (o que os olhos veem e como ela lembra cada coisa → `olhar.ppm`).

(Os números do trecho acima são da versão em que as percepções eram símbolos. Com os olhos, veja a seção seguinte.)

## Olhos: ver, lembrar e imaginar

Você perguntou como ela poderia *ver*: quando ouvimos ou lembramos de algo, vem a imagem; e tentamos imaginar até o desconhecido. Agora ela tem olhos (`v2/vision.hpp/.cpp`). As coisas do mundo não chegam mais como o símbolo `[ve:mar]`: chegam como **imagens** (32×32, cor), desenhadas diferentes a cada olhar (posição, tom). Ninguém diz a ela o que é o quê.

**O córtex visual** tem duas camadas, as duas com aprendizado hebbiano competitivo, sem professor e sem retropropagação:

- **V1**: um dicionário de 96 traços de 8×8 (bordas, manchas de cor), aprendido com a regra de Oja a partir dos pedaços das imagens. Cada pedaço da imagem é descrito por 3 traços.
- **V2**: 160 unidades de "objeto". A entrada é a presença de cada traço por quadrante (invariância a posição), descontada da média (adaptação: o que está em toda imagem não informa). 6 unidades ativas por imagem. Coisas parecidas ativam unidades parecidas.
- **Descida**: cada unidade de V2 tem sinapses de volta para os pixels, hebbianas (o que a unidade viu enquanto estava ativa). Ativar um código **gera** uma imagem. Lembrar é gerar; imaginar é gerar a partir de códigos que nunca estiveram juntos.

O código de V2 entra no córtex de sequências como um código esparso (cada unidade fala com um punhado fixo de colunas), e cada unidade ativa é uma percepção `[v12]` para o córtex associativo. Uma palavra ouvida se liga às unidades ativas naquele momento, pesadas pela atenção conjunta (o apontado vale 3×, o resto 0,3×).

- **Nomear** (`name_image`): cosseno entre o código visto e o que cada palavra evoca. Uma palavra de lugar ("cozinha") evoca a mesa, o café e a fruta: espalhada, parece menos com qualquer coisa só do que "café". Vale para imagens que ela nunca viu: é generalização de verdade, não busca de tabela.
- **Imaginar** (`imagine_image`): a palavra evoca unidades; a descida reconstrói pixels. Várias palavras acendem unidades de coisas diferentes: sai a **mistura**.
- **Entender**: "pega a flor" evoca unidades; ela pega a coisa à vista cujo código mais se parece com o que foi evocado.

**Medido** (`v2/tests/ground.cpp`: 10 objetos, 4 lugares, 8 sementes; cada exposição é um desenho diferente; o teste usa desenhos novos):

| voltas pela casa (apontando) | nomeou certo (desenho novo) | imaginou certo¹ | "vamos para a praia" | "pega a flor" |
|---|---|---|---|---|
| 1 | 44% | 89% | 0/8 | 4/8 |
| 2 | 65% | 92% | 0/8 | 7/8 |
| 4 | 72% | 94% | 8/8 | 8/8 |
| 8 | 79% | 96% | 8/8 | 8/8 |
| 4, sem apontar | 21% | 40% | 1/8 | 5/8 |

¹ a imagem gerada pela palavra fica mais perto (erro por pixel) de desenhos novos daquele objeto do que de qualquer outro.

Como bebês: uma exposição não basta, e sem atenção conjunta ela aprende muito pouco. "Vamos para a praia" só funciona depois de ouvir "praia" umas três vezes (evidência mínima para uma palavra guiar o corpo). Ela diz "praia" para o sol com frequência: é a palavra que mais ouviu olhando para ele.

O que ela imagina ouvindo só a palavra, depois de 4 voltas (`imagens/ground_imagina.png`: cama, janela, mesa, café, flor, árvore, mar, areia, sol, barco):

![o que as palavras evocam](imagens/ground_imagina.png)

Da demonstração (`imagens/demo_imagina.png`): "mar", "flor", "cama", "pão" e **"flor mar"** juntos, que ela nunca viu — uma flor sobre o mar:

![mar, flor, cama, pao, flor+mar](imagens/demo_imagina.png)

**O que aprendi no caminho (ficou documentado porque custou):**

- **Período crítico de V1 é necessário.** Deixar V1 continuar aprendendo depois de V2 se formar derrubou o reconhecimento de 51/60 para 5/60: os traços mudam debaixo dos protótipos, e V2 fica descrevendo um V1 que não existe mais. A solução foi biológica: V1 amadurece cedo (infância, `prime`) e congela; V2 aprende a vida toda com taxa que decai com o uso.
- **Boosting homeostático atrapalhou.** Forçar todas as unidades a vencer igualmente (como no HTM) corrompia o par reconhecer/gerar. Semear as unidades com imagens reais resolve o problema das unidades mortas sem isso.
- **Reconstruir por pedaços dá ruído; gerar por população funciona.** Normalizar cada pedaço gerava imagens sem sentido. A descida direta de V2 para pixels, pesada pela ativação⁴ das unidades, dá imagens limpas.
- **Agrupar por energia é dominado pelo fundo.** A presença (traço está/não está) por quadrante, descontada da média, separa os objetos.
- **O sentido se diluía com os olhos.** Com dezenas de traços por momento, contar a frequência de uma palavra uma vez por par percepção-palavra fazia toda ligação cair para ~0,09. A frequência conta uma vez por momento (`bind_event`), e a atenção vale como repetição.

**O desconhecido.** "flor mar" ela imagina porque conhece as partes. Uma palavra que nunca ouviu ("casa") não evoca nada — e é assim mesmo: ninguém imagina uma palavra sem sentido; imagina-se uma *descrição* feita de partes conhecidas. Juntar mais partes ("uma árvore na praia de noite") é o passo seguinte: já funciona para duas coisas, e o mundo pode ganhar mais partes (cores, tamanhos, posições) para que as descrições componham.

**O que isto é e o que não é.** É sentido no único modo em que ele existe: a palavra "mar" nela está ligada a uma percepção que muda o que ela faz, e agora a percepção é uma imagem que ela pode lembrar e recombinar. Não é visão humana: 32×32 pixels, desenhos simples, sem profundidade nem movimento, e ela não tem gramática. Mas a arquitetura é a certa, e ela cresce com o mundo: cada objeto, lugar ou sensação a mais é uma coisa a mais que as palavras podem significar.

## Resultados, logs e imagens

- `logs/`: saídas reais de todas as demonstrações e medições (sem edição), com o comando que gerou cada uma.
- `imagens/`: o que ela vê, lembra e imagina.
- `v2/tests/` e `tests/`: os programas de medição, com instruções de compilação.

| o quê | resultado | log |
|---|---|---|
| frases longas (20 frases, 2 exposições, 8 sementes) | 77% das palavras, 118/160 frases inteiras | `logs/bench2.txt` |
| nomear desenhos novos (4 voltas apontando) | 72% (79% com 8 voltas) | `logs/ground.txt` |
| imaginar pela palavra (4 voltas) | 94% parece mais com a coisa certa | `logs/ground.txt` |
| entender "vamos para a praia" / "pega a flor" | 8/8 e 8/8 | `logs/ground.txt` |
| só o córtex visual | erro de imaginação 0,08 por pixel; mesma coisa 0,49 / coisas diferentes 0,02 de sobreposição | `logs/vision_test.txt` |

## Compilar e rodar (as duas)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build        # requer PIL (python3 -m pip install pillow) só para converter as imagens PPM em PNG
./build/eva2             # conversar com a Eva 2 (lembra de conversas anteriores)
./build/eva2 --demo      # demonstração automática
./build/eva2 --nova      # começar sem memória
./build/eva              # Eva 1: demonstração
./build/eva --conversa   # Eva 1: conversar
```

---

# Eva 1 (neurônios de pulsos)

## Como a fala dela nasce (interocepção)

Os estados internos da Eva (dopamina, curiosidade, surpresa) agora são **conceitos no córtex**, `<feliz>`, `<curiosa>` e `<surpresa>`, que disparam sozinhos conforme a química dela. Isso muda tudo: o que ela ouve enquanto está feliz fica ligado a `<feliz>` no hipocampo, e quando ela fica feliz de novo, por outro motivo, `<feliz>` acende e puxa aquilo. Ela diz "eu gosto de musica" porque *está sentindo aquilo*, não porque calculou que era provável. É a palavra ancorada no sentimento.

O estado também escolhe o **tipo** de fala, como em qualquer bicho:

- **novidade → imitar:** ela ecoa o que acabou de ouvir (é assim que a fala começa);
- **estar bem → lembrar:** o humor (dopamina tônica, que dura ~10 s) puxa o que ela viveu estando bem;
- **curiosidade → perguntar:** ela traz à tona a palavra que menos conhece e deixa a rede completar; sai com "?";
- **vontade de falar baixa → silêncio.** Ela não responde a tudo.

Os colchetes na saída mostram o tipo da fala e o estado interno na hora: `Eva: eu gosto de   [resposta <feliz>]`.

Trecho real da demonstração (uma sessão, sem edição):

```
Voce: bom dia eva
         Eva: bom dia eva   [eco <surpresa>]
Voce: eu gosto de musica  (com carinho)
         Eva: eu gosto de musica   [eco <feliz> <surpresa>]
Voce: vou dormir cedo
         Eva: vou dormir   [eco <feliz> <surpresa>]
  [ 41.5s] Eva: vou dormir?   [pergunta <curiosa>]
Voce: oi eva  (com carinho)
         Eva: vou oi gosto de musica bom   [resposta <feliz> <surpresa>]   <- feliz puxa tudo que viveu feliz, misturado
```

A última linha mostra o estágio atual: quando `<feliz>` acende, ele puxa *todas* as frases vividas em bom humor de uma vez, e elas saem misturadas. Ecos de frases únicas já saem inteiros.

## Conversando com a Eva

No modo `--conversa`, você fala com ela em frases curtas (até 6 palavras). Ela aprende palavras novas na hora (até 48) e aprende a **ordem** em que vieram. Depois de cada frase sua, ela pode responder (ou não), e então passam 3 segundos de silêncio em que ela pode puxar assunto.

```
Voce: cafe manha              -> ela vive "café", depois "manhã"
Voce: mar praia !             -> o "!" é carinho: libera dopamina, a memória fica mais forte
Voce: recitar mar             -> dê a primeira palavra; ela completa a frase
Voce: esperar 15              -> 15 s de silêncio; ela pensa sozinha e pode falar
Voce: dormir 30               -> ela dorme e consolida as memórias
Voce: lembrar mar             -> o que "mar" acende na cabeça dela (associação, sem ordem)
Voce: estado                  -> dopamina, impulsos, o que ela sabe de cada palavra
```

Exemplo real:

```
Voce: oi eva
        (surpresa 1.00, dopamina 0.54, nova: oi, nova: eva)
         Eva: oi eva   [eco <surpresa>]
Voce: eu gosto de voce !
Voce: eu gosto de voce !
         Eva: eu de   [eco <feliz> <surpresa>]
Voce: vou trabalhar agora
Voce: voltei !
         Eva: eu   [resposta <feliz> <surpresa>]
Voce: esperar 20
  [ 54.0s] Eva: trabalhar?   [pergunta <curiosa>]
```

## O que a demonstração mostra

1. **Manhã:** você conversa com ela, que não sabe nada. Ela ecoa; "eu gosto de musica" vem com carinho e fica ligada a `<feliz>`.
2. **Tarde:** silêncio. Ela pergunta sobre o que menos conhece.
3. **Fim de tarde:** você volta com carinho; a dopamina sobe, `<feliz>` acende, e o que ela viveu feliz volta.
4. **Noite:** ela dorme; o hipocampo repete as memórias e o córtex aprende.
5. **Manhã seguinte:** o que ela traz do sono.

Isso reproduz fenômenos reais: pacientes com lesão no hipocampo (como o famoso H.M.) perdem memórias recentes, mas mantêm as antigas; memórias com carga emocional são lembradas melhor; o novo libera dopamina; dormir fixa o que se aprendeu; e a mente "vagueia" quando não há nada acontecendo.

## Da biologia para o código

| No cérebro | Na Eva | Arquivo |
|---|---|---|
| Neurônio que integra e dispara | Neurônio LIF (integra-e-dispara com vazamento) | `population.cpp` |
| Cansaço do neurônio | Adaptação: quem disparou muito fica mais difícil de excitar | `population.cpp` |
| Interneurônios inibitórios | Inibição global + "k vencedores" no hipocampo | `population.cpp`, `eva.cpp` |
| Sinapse que aprende com o tempo dos disparos | STDP: pré antes do pós fortalece, depois enfraquece | `projection.cpp` |
| Dopamina como "carimbo" de importância | Aprendizado de 3 fatores: STDP marca, dopamina confirma | `projection.cpp`, `neuromodulators.hpp` |
| Homeostase sináptica | Cada neurônio tem um orçamento total de peso de entrada | `projection.cpp` |
| Acetilcolina (acordada vs. sono) | No sono, as conexões internas do hipocampo ficam mais fortes | `eva.cpp` |
| Ondas lentas e *sharp-wave ripples* | Estados UP a cada 250 ms disparam replays | `eva.cpp` |
| Rede de modo padrão (divagação) | Replays mais raros e fracos acordada; cada um é um "pensamento" | `eva.cpp` (`live`) |
| Erro de previsão → dopamina | Antes de cada experiência ela prevê; a surpresa libera dopamina | `eva.cpp` (`predict_surprise`) |
| Hipotálamo / motivação | Curiosidade e vontade de falar crescem com o tempo e são saciadas | `drives.hpp` |
| Alocação de memória (células "novas") | A homeostase só conta neurônios já em uso; palavras novas ganham espaço | `projection.cpp` (`pre_limit`) |
| STDP assimétrico no tempo | Numa frase, A dispara antes de B: A→B fortalece, B→A enfraquece. Daí vem a ordem | `eva.cpp` (`tell`) |
| Replay comprimido | Ao recitar, a cadeia inteira roda em poucos ms, como os *replays* do hipocampo real | `eva.cpp` (`recite`) |
| Segmentação de eventos | Um "conceito" interno de fim de frase diz onde a frase termina | `eva.cpp` (`kEnd`) |
| Interocepção (ínsula) | Dopamina, curiosidade e surpresa viram conceitos no córtex, que disparam sozinhos | `eva.cpp` (`state_level`) |
| Dopamina tônica (humor) | Integra os picos devagar; dura ~10 s; é o que `<feliz>` sente | `neuromodulators.hpp` |
| Imitação / ecolalia | Novidade faz ela repetir o que ouviu | `eva.cpp` (`respond`) |
| Giro denteado (separação de padrões) | Camada fixa que escolhe quem no CA3 representa (anterior + atual); fibras musgosas como ganho | `eva.cpp` (`dg_pop_`, `cx_dg_`, `ca3_dg_`, `dg_ca3_`) |

## Estrutura

```
src/
  population.hpp/.cpp     neurônios
  projection.hpp/.cpp     sinapses e aprendizado
  neuromodulators.hpp     dopamina e modos (acordada/sono)
  drives.hpp              impulsos: curiosidade e vontade de falar
  (contexto por episódio vive em eva.cpp: new_episode, ctx_hp_, hp_ctx_)
  eva.hpp/.cpp            o cérebro: regiões, experiência, divagação, sono, lembrança
  main.cpp                demonstração e modo de conversa
```

Todos os parâmetros ficam em `EvaConfig` (em `eva.hpp`), então dá para experimentar sem mexer no resto do código.

## Como as sequências se comportam (medido)

Todos os números abaixo usam **24 sementes aleatórias**; com menos, o ruído engana (eu me enganei com 6).

| Teste | Antes | Com o modo de recuperação (atual) |
|---|---|---|
| 3 frases curtas, sem palavra compartilhada (`tests/seq6`) | 177/288 palavras na ordem (61%) | **231/288 (80%)** |
| 3 frases de 5 palavras, sem compartilhar (`tests/ctrl`) | 219/360; 22 de 72 frases inteiras | **250/360; 38 de 72 inteiras** |
| 2 frases que compartilham "musica" (`tests/reinst`) | 98/240 (41%) | **133/240 (55%)** |
| Memória mista: pares com dopamina + frases (`tests/mixed`) | 176/288 | 173/288 (igual), com mais palavras "a mais" |

## O modo de recuperação (a mudança estrutural que funcionou)

No hipocampo real, "ouvir" e "lembrar" são regimes diferentes, controlados pela acetilcolina: ao ouvir, a entrada do córtex manda e as conexões internas (CA3) ficam fracas; ao lembrar, é o contrário. A Eva agora tem esse regime explícito (`retrieval_mode` em `eva.cpp`), usado ao recitar, ao prever e ao divagar:

- as conexões internas do hipocampo ficam 6× mais fortes (`recite_gain`);
- a entrada do córtex cai para 30% depois que a pista é dada (`retrieval_cx_gain`), para a cadeia seguir por dentro e não ser puxada pelo que o córtex vai acendendo;
- a inibição afrouxa: 8 células disparam por ms em vez de 3 (`retrieval_max_active`). Assembleias maiores encadeiam com mais robustez; foi o fator que mais pesou.

## Giro denteado: implementado, medido, e o que ele revelou

Esta versão tem um **giro denteado** de verdade (`dg_size` em `EvaConfig`): uma camada separada entre córtex e CA3, com pesos fixos e aleatórios, que recebe a palavra atual (córtex) e o estado anterior do CA3 (com corrente lenta), escolhe 3 vencedoras por ms e ativa poucas células do CA3 por fibras musgosas. As fibras agem como **ganho** sobre a entrada do córtex (uma célula do CA3 precisa das duas coisas), porque como corrente pura o laço CA3 → córtex → DG → CA3 fugia.

**O que funcionou:** separação de padrões medida diretamente. As células do CA3 ativas para "eu gosto de musica alta" e para "ela ouve musica no carro" tinham 45% de sobreposição; com o DG, 12–19%. "musica" nas duas frases passou a ser duas memórias.

**O que não mudou:** a recitação de frases com palavra compartilhada (120 → 121 de 240). Separar as células **não era o gargalo**. Com as células separadas, dei para medir os pesos aprendidos entre palavras consecutivas, e apareceu o motivo real:

```
  gosto -> de      0.052      (bem aprendido)
  de    -> musica  0.006      (quase nada)
  eu    -> gosto   0.004      (quase nada)
```

Algumas transições simplesmente não são aprendidas. Durante a sobreposição de 20 ms entre uma palavra e a seguinte, as células da palavra velha, já ativas e com apoio recorrente, **monopolizam as 3 vagas de disparo**; as da palavra nova só começam a disparar quando a velha já se calou, e aí o traço do STDP (20 ms) já apagou. A transição fica fraca. Sobreposição maior e traço maior corrigem os pesos (de 13 para 5 transições fracas em 32), mas pioram a recitação, porque também fortalecem ligações de salto e de retorno.

### A passagem de bastão: testada de três jeitos, e o que ela revelou

Hipótese: na fronteira entre palavras, as células da palavra velha monopolizam as vagas e a nova não entra enquanto o traço da velha vive. Testei três mecanismos para forçar a passagem (`tests/wdiag` conta as transições fracas em 32):

| Mecanismo | Transições fracas | Recitação (controle, de 360) |
|---|---|---|
| nada (padrão) | 15/32 | 229 |
| pulso de inibição no CA3 na fronteira (`encode_reset_ms`) | 24/32 (pior) | — |
| mais fadiga nas células do CA3 (`hp_adapt_inc`) | 25–32/32 (pior) | — |
| menos fadiga no córtex (`cx_adapt_inc` = 0) | **0/32** | 137–150 (muito pior) |

O terceiro achou a causa de verdade, e ela não estava no hipocampo. Olhando palavra a palavra (`tests/timeline`), o grupo do córtex de uma palavra dispara forte nos primeiros 10 ms e depois **cansa** (adaptação) antes de conseguir recrutar células do CA3, porque nesses 10 ms as vagas ainda são da palavra anterior, que tem retorno do CA3 a seu favor. Palavras que recrutam cedo ("eu", "alta") ficam fortes; "gosto" e "musica" quase não ganham células. Sem fadiga no córtex, todas as transições são aprendidas, mas aí as palavras **não se apagam**: o laço CA3 → córtex mantém "eu", "gosto" e "de" acesas até o fim da frase, o CA3 satura e a sequência vira uma sopa. Separar a fadiga por modo (pouca ao codificar, normal ao lembrar, como a acetilcolina faz) também não salvou: 150 de 360.

Então a fadiga do córtex está fazendo dois trabalhos opostos: apagar a palavra que acabou (necessário) e, sem querer, calar a palavra que começou (ruim). Desacoplar isso pede um córtex com dinâmica própria de assembleia (ligar forte, sustentar, desligar por inibição), não um ajuste de parâmetro. É a próxima mudança estrutural, e é maior que as anteriores.

O DG vem **desligado por padrão** porque, sem resolver a passagem de bastão, ele piora a recitação geral (225 → 152 de 360 no controle). Está pronto para ligar: `dg_size = 600`, `dg_mossy_w = 1.0`, `dg_cx_direct_gain = 0.5`, `dg_mod_gain = 6`.

Uma descoberta colateral importante: a **interocepção estava forte demais**. Com os conceitos internos disparando a 0,25 (o padrão anterior), as frases ficavam todas parecidas para o hipocampo e a recitação caía de 199–225 para 81 de 360. O padrão agora é 0,08.

## Tentativas anteriores que não funcionaram

Para registro, com o motivo: contexto por episódio como soma e como ganho (reinstala, não desempata); corrente lenta tipo NMDA na entrada córtex → hipocampo (a persistência bagunça a lembrança); atrasos sinápticos (a ignição do hipocampo depende da recorrência instantânea); orçamento sináptico maior e janela de STDP maior (piores); andaime recorrente fixo no hipocampo (vira atrator próprio e aumenta a sobreposição). Tudo ficou no código com parâmetro para ligar.

Ferramentas de diagnóstico: `EVA_DEBUG=1` imprime o instante e a contagem de cada palavra ao recitar; `EVA_TRACE=1` imprime, ms a ms, a atividade do hipocampo e de cada conceito no córtex; `tell(frase, 0, &celulas)` devolve as células do CA3 ativas por palavra; `start_recording()` grava hipocampo e contexto.

## Limites honestos

- Ela só conhece palavras como "conceitos" soltos; não entende gramática nem o que as palavras significam no mundo. Saber a ordem ainda não é saber o sentido.
- Não há mais moldes: cada palavra dita saiu de uma cadeia aprendida. Mas as cadeias ainda se misturam ("de bom eu gosto"), e ela não tem gramática. Melhorar isso é trabalho de base no hipocampo (giro denteado) e de escala, não de parâmetro.
- Os impulsos (curiosidade, vontade de falar) são variáveis, não neurônios. É o nível de detalhe certo para essa camada, mas vale dizer.

## Próximos passos

- **Assembleias corticais com dinâmica própria:** um grupo de córtex que liga forte, se sustenta por recorrência local e desliga por inibição (não por cansaço). Isso desacopla "apagar a palavra que acabou" de "calar a palavra que começou", que hoje são a mesma coisa. Com isso, as transições passam a ser aprendidas por igual e o giro denteado (já pronto) passa a valer.
- **Emoção por memória:** guardar a valência de cada episódio (foi bom? foi ruim?) e deixar isso colorir o humor dela ao lembrar.
- **Escala:** trocar as matrizes densas por listas esparsas e paralelizar com OpenMP ou CUDA para chegar a centenas de milhares de neurônios.
- **Ponte para a linguagem:** ligar o estado da Eva (o que está ativo, a dopamina, os impulsos) a um modelo de linguagem que só traduz isso em palavras.
