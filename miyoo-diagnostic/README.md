# Miyoo Mini / Mini Plus — SDL2 diagnostic (fase 16)

Questo pacchetto contiene **il sorgente del test**, non un eseguibile ARM già compilato.

## Compilazione

Usare il compilatore della union-miyoomini-toolchain e `sdl2-config` della **stessa build SDL2 modificata** (fase 16). Ad esempio, dopo aver configurato il toolchain e installato SDL2 nel sysroot:

```sh
export CC=/percorso/toolchain/bin/arm-linux-gnueabihf-gcc
export SDL2_CONFIG=/percorso/sysroot/usr/bin/sdl2-config
sh build.sh
file miyoo_sdl2_diagnostic
```

I percorsi sono indicativi: adattarli alla toolchain installata. Se `sdl2-config` restituisce percorsi host anziché target, correggere le opzioni di compilazione e linking.

## Esecuzione

1. Copiare `miyoo_sdl2_diagnostic`, `run.sh` e la libreria SDL2 modificata nella directory sulla SD. Mettere la libreria in `lib/` **solo se necessario**; verificare che il binario usi la versione modificata e non quella di sistema.
2. Avviare `run.sh` dal launcher o da shell sulla console. Non lanciare il test mentre un altro programma usa il framebuffer.
3. Vengono mostrate 7 scene per circa 1,8 secondi ciascuna: barre RGB, blending rettangoli, linee diagonali, texture/scaling/clipping, modulazione, flip, viewport/clipping. Fotografare o annotare anomalie; `ESC` interrompe.
4. Il test esegue poi 3 benchmark di 120 frame ciascuno (fill, copie texture, alpha fill).
5. Restituire `miyoo-results.csv`, `miyoo-stdout.log`, modello Miyoo (Mini/Mini Plus), firmware, versione della SDL2 e foto delle scene anomale.

## Interpretazione

`PASS` significa solo che la chiamata SDL2 ha restituito successo: **non** garantisce pixel corretti. FPS sono indicativi e dipendono da present/vsync e carico. Il programma non legge i pixel dal framebuffer e non esegue confronto automatico con il renderer software. L'esecuzione non è stata verificata su hardware.

Attenzione: questo programma interagisce direttamente con il driver video custom; testare con SD e dati salvati al sicuro.
