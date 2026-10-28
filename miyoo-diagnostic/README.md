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
4. Il test esegue poi 15 benchmark di 120 frame ciascuno: fill e copie con carichi crescenti, texture grandi, flip, modulazione e aggiornamenti streaming.
5. Restituire `miyoo-results.csv`, `miyoo-stdout.log`, modello Miyoo (Mini/Mini Plus), firmware, versione della SDL2 e foto delle scene anomale.

## Interpretazione

Il benchmark versione 2 mantiene i casi `opaque_fill`, `texture_copy` e
`alpha_fill` con 24 operazioni per frame. Aggiunge copie con 96 e 384 sprite,
fill opachi e alpha con 384 rettangoli, flip orizzontale/verticale e modulazione
colore/alpha con 96 sprite, aggiornamenti tramite `SDL_UpdateTexture` e
lock/unlock, e una texture 256×256 disegnata a 128×128 con 24 e 96 sprite.
Posizioni e colori dei benchmark sono cambiati: per confrontare due versioni
della libreria usare lo stesso eseguibile diagnostico versione 2.

Ogni riga `benchmark` registra anche:

- `submit_ms`: tempo totale di preparazione e invio dei comandi, inclusi gli
  aggiornamenti streaming. Può comprendere attese dovute a flush automatici.
- `flush_ms`: tempo totale nelle chiamate `SDL_RenderFlush`, inclusa l'attesa
  delle operazioni hardware accodate nel backend MINI.
- `present_ms`: tempo totale nelle chiamate `SDL_RenderPresent`.
- `min_frame_ms` e `max_frame_ms`: durata minima e massima osservata per frame.
- `errors`: errori rilevati dalle chiamate SDL controllate nel caso corrente.

I tre tempi sono totali sull'intero caso, non medie per frame né timestamp GPU.
`SDL_RenderPresent` non restituisce un codice di errore: controllare anche il log
e l'output visivo. Un caso con errori non è valido per confrontare le prestazioni.

Per una prova rapida o più lunga, impostare da shell sulla console:

```sh
MIYOO_BENCH_FRAMES=30 sh run.sh
```

Il valore predefinito è 120; sono accettati valori da 1 a 10000. Il diagnostico
aggiunge circa 528 KiB di dati al picco: texture grande da 256 KiB, buffer CPU
temporaneo da 256 KiB e buffer streaming da 16 KiB. Il buffer temporaneo viene
liberato prima dei benchmark della texture grande. L'accodamento nel backend
non richiede nuovi buffer pixel: riusa le texture DMA residenti e limita a otto
le operazioni in volo, aspettandole prima di modifiche, rilascio e presentazione.

`PASS` significa solo che la chiamata SDL2 ha restituito successo: **non** garantisce pixel corretti. FPS sono indicativi e dipendono da present/vsync e carico. Il programma non legge i pixel dal framebuffer e non esegue confronto automatico con il renderer software. L'esecuzione non è stata verificata su hardware.

Attenzione: questo programma interagisce direttamente con il driver video custom; testare con SD e dati salvati al sicuro.
