# Medição da Eva 2

    g++ -O2 -std=c++17 -Iv2 -Isrc v2/eva2.cpp v2/temporal_memory.cpp v2/vision.cpp v2/tests/bench.cpp -o bench2
    ./bench2 2        # 20 frases, 2 exposições, 8 sementes
    V=1 ./bench2 2    # mostra as completações da primeira semente

    g++ -O2 -std=c++17 -Iv2 -Isrc v2/eva2.cpp v2/temporal_memory.cpp v2/vision.cpp v2/tests/ground.cpp -o ground
    APONTA=1 ./ground 4 8       # 4 voltas apontando, 8 sementes: nomeia desenhos novos (72%), imagina (94%), entende (8/8)
    APONTA=1 V=1 ./ground 4 1   # mostra o que ela diz e imagina na primeira semente; escreve ground_imagina.ppm
    ./ground 4 8                # sem apontar: 21% / 40%

    g++ -O2 -std=c++17 -Iv2 v2/vision.cpp v2/tests/vision_test.cpp -o vision_test
    ./vision_test               # só o córtex visual: erro de V1, erro de imaginação, imagens ve/v1/imagina/mistura.ppm

As imagens saem em PPM; para PNG: python3 -c "from PIL import Image; Image.open('x.ppm').save('x.png')"
