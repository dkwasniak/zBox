# Audio NS stutter — status diagnozy i plan

Data: 2026-06-29

## Symptom

Na ścieżce NS (lokalny I2S / głośnik) audio wyraźnie przerywa. Problem jest
łatwy do odtworzenia po szybkim przełączaniu utworów, szczególnie w okolicy:

```text
/music/9383471d_babajaga.mp3
```

Opis odsłuchowy: przerywanie / efekt jak cofnięta płyta.

## Co już wiemy z logów

### Wykluczone lub mocno osłabione hipotezy

1. **To nie wygląda na wyciek pamięci.**
   Heap i largest block są stabilne w trakcie testów.

2. **LED/FastLED nie jest główną przyczyną.**
   Test z `ENABLE_LEDS=false` nadal przerywa. W logach:

   ```text
   [DIAG] HWM loop=5924 led=0 audio=5460 nfc=0
   ```

   Czyli task LED nie działa, a symptom zostaje.

3. **Prosty underrun wejścia MP3/SD nie tłumaczy aktualnego objawu.**
   Po wyłączeniu LED:

   ```text
   [AUDIO_TEL] SD=8071-9203 B/s dec_in=8071-9203 B/s drops=0 max_read=21-26
   ```

   Nie ma `drops`, `max_read` jest umiarkowany, heap stabilny.

4. **Eksperyment z dynamicznym przekazywaniem `AudioInfo` do I2S był błędny.**
   Spowodował panic:

   ```text
   assert failed ... I2SDriverESP32V1::writeBytes ... (tx_chan != nullptr)
   ```

   Przyczyna: race między drain taskiem lokalnego bufora PCM a rekonfiguracją I2S.
   Ta ścieżka została wycofana i nie powinna wracać bez synchronizacji.

### Co nadal jest podejrzane

1. **Reset dekodera / resztki PCM przy szybkim przełączaniu.**
   Aktualna sekwencja robi:

   ```cpp
   clearActiveTransport();
   decoderStream.end();
   writeSilenceGap(...);
   decoderStream.begin();
   ```

   Problem: `decoderStream.end()` może jeszcze wypchnąć końcówkę starego dekodera
   do aktualnego sinka. Przy bezpośrednim I2S mogło to być mniej widoczne, ale przy
   lokalnym buforze PCM takie bajty mogą zostać zakolejkowane przed następnym utworem.

2. **Konkretny plik MP3 może mieć cechę, której Helix/audio-tools nie obsługuje dobrze.**
   Do sprawdzenia lokalnie na pliku z SD:

   ```text
   /music/9383471d_babajaga.mp3
   ```

   Szczególnie: VBR, Xing/LAME header, uszkodzone ramki, zmiana sample rate,
   nietypowy encoder, bardzo niski bitrate.

3. **Lokalny bufor PCM może maskować telemetrię, ale nie usuwać artefaktu.**
   `drops=0` oznacza tylko, że decoder przyjął wejściowe bajty MP3. Nie mówi jeszcze,
   czy PCM wychodzący z dekodera jest poprawny i czy reset nie miesza ramek między
   utworami.

## Aktualne zmiany testowe w firmware

### `audio.cpp`

- lokalny bufor PCM dla NS: `LOCAL_PCM_BUFFER_SIZE = 96 KB`
- I2S DMA: `buffer_count=32`, `buffer_size=512`
- odczyt MP3 dla NS: `LOCAL_AUDIO_READ_CHUNK_SIZE = 512`
- telemetry: osobne `read_ms`, `decode_write_ms`, `sched_gap_ms`
- próg utrzymywania lokalnego bufora PCM podniesiony testowo do `80 KB`

### `zbox_config.h`

Testowo:

```cpp
#define ENABLE_LEDS false
```

To jest wyłącznie test diagnostyczny. Po zakończeniu diagnozy trzeba przywrócić LED-y.

## Następny plan

### Krok 1 — sprawdzić plik MP3

Skopiować z karty SD problematyczny plik i sprawdzić:

```bash
ffprobe -hide_banner -show_format -show_streams 9383471d_babajaga.mp3
ffmpeg -v warning -i 9383471d_babajaga.mp3 -f null -
```

Jeśli `ffmpeg` pokaże błędy ramek albo nietypowe parametry, zrobić testową konwersję:

```bash
ffmpeg -i 9383471d_babajaga.mp3 -ar 44100 -ac 2 -b:a 128k babajaga_fixed.mp3
```

I sprawdzić, czy `babajaga_fixed.mp3` nadal przerywa na urządzeniu.

### Krok 2 — instrumentacja lokalnego PCM

Dodać krótką telemetrię bufora PCM, ale bez spamowania UART:

```text
[PCM] active=1 used=xxxxx min=xxxxx max=xxxxx underruns=n
```

Cel: zobaczyć, czy podczas słyszalnego przerywania `used` spada do zera albo czy
artefakt występuje mimo pełnego bufora.

### Krok 3 — bezpieczny test resetu dekodera

Jeżeli plik MP3 jest poprawny, przetestować zmianę kolejności resetu:

```cpp
clearActiveTransport();
decoderStream.end();
clearActiveTransport();  // drop tail emitted by end()
writeSilenceGap(...);
decoderStream.begin();
```

To musi być osobny test, bo zmienia zachowanie przejścia między utworami.

### Krok 4 — jeśli reset nie pomaga

Rozważyć obejście dla NS:

- na zmianie utworu zatrzymać drain lokalnego PCM,
- wyczyścić bufor,
- zrestartować dekoder,
- dopiero po prefillu nowego utworu wznowić drain.

To wymaga ostrożnej synchronizacji, żeby nie wrócić do błędu `tx_chan != nullptr`.

## Ważna decyzja

Nie traktować już “za mały I2S DMA” jako potwierdzonego root cause. Była to sensowna
pierwsza hipoteza, ale aktualne testy z lokalnym PCM, większym buforem i wyłączonymi
LED-ami wskazują, że główny problem jest raczej w:

- konkretnym pliku MP3,
- granicy Helix/audio-tools,
- albo resetowaniu / mieszaniu PCM przy szybkiej zmianie utworów.
