# Testes de medição

Compile qualquer um com:

    g++ -O2 -std=c++17 -Isrc src/population.cpp src/projection.cpp src/eva.cpp tests/NOME.cpp -o NOME

- `seq6.cpp`   frases sem palavras compartilhadas; mede palavras na ordem certa (prefixo). Ex.: `./seq6 2 3 6 4 50 16`
- `ctrl.cpp`   controle com frases de 5 palavras disjuntas; mostra até onde a cadeia chega. Ex.: `./ctrl 2`
- `mixed.cpp`  pares (um com dopamina) + frases que compartilham palavras. Ex.: `./mixed 0 2 0` (sem contexto) / `./mixed 60 2 3` (contexto como ganho 3)
- `reinst.cpp` duas frases que compartilham uma palavra; mede acerto e a reinstalação do contexto ao recitar. Ex.: `./reinst ctx=0` / `./reinst mod=3`

Todos usam 24 sementes. Com menos que isso, o ruído engana.
- `ov2.cpp`    sobreposição de células do CA3 entre duas frases que compartilham uma palavra. Ex.: `./ov2 0 1` (sem DG) / `./ov2 600 0.3 1.0 3 0.5 6` (com DG)
- `wdiag.cpp`  pesos aprendidos entre palavras consecutivas e delas para o córtex. Ex.: `./wdiag 0 1` / `./wdiag 600 1`
- Em `ctrl`, `seq6` e `mixed`, a variável de ambiente `DG=1` liga o giro denteado.
- `timeline.cpp` linha do tempo da codificação: quais células do CA3 e quais grupos do córtex disparam a cada 10 ms. Ex.: `./timeline 1 2`
