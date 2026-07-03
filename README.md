# Mix Checker — Alphastudio

Wtyczka VST3 na koniec łańcucha masteringowego: odsłuch pasm, stały korektor liniowofazowy ±2 dB i monitoring Mid/Side. Cały tor linear phase + oversampling x8.

## Funkcje

- **Odsłuch pasma (solo)** — kliknij przycisk pasma; ponowne kliknięcie wyłącza solo i gra całe spektrum.
- **Korektor 8-pasmowy** — przyciski −2 / +2 nad każdym pasmem dodają/ucinają 2 dB (zakres ±12 dB). Działa zawsze, także bez aktywnego solo. Crossovery komplementarne — przy 0 dB odpowiedź jest matematycznie idealnie płaska.
- **Monitoring** — STEREO / MID / SIDE (dotyczy toru odsłuchu; bypass zawsze gra oryginał).
- **Bypass** — bez zmiany latencji (uczciwe A/B).

## Pasma

| Przycisk | Zakres odsłuchu |
|---|---|
| Low End | 16–60 Hz |
| Bass | 50–220 Hz |
| Low Mid | 185–460 Hz |
| Mid | 430–1000 Hz |
| High Mid | 950–2000 Hz |
| Low High | 2000–4300 Hz |
| High | 3800–6800 Hz |
| Highend | 6800 Hz – Nyquist |

Crossovery EQ leżą w środku geometrycznym nakładek pasm: ~55, 202, 445, 975, 2000, 4042, 6800 Hz.

## Budowanie (macOS)

Wymagania: Xcode, CMake (`/Applications/CMake.app`), lokalna kopia JUCE w folderze `JUCE/`.

```bash
export PATH="/Applications/CMake.app/Contents/bin:$PATH"
cd ~/Desktop/Alphastudio\ Mix\ Checker
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Gotowy `Mix Checker.vst3` ląduje w `build/MixChecker_artefacts/Release/VST3/` i jest kopiowany do systemowego folderu VST3.

## Uwagi techniczne

- Filtry to jeden kompozytowy, symetryczny FIR (windowed-sinc, Kaiser β=9) liczony splotem FFT — zerowe zniekształcenia fazowe.
- Oversampling x8 na filtrach półpasmowych FIR equiripple (max quality) — cały tor linear phase.
- Latencja stała (~75 ms przy 48 kHz), raportowana hostowi.
- Zmiana pasma/gainu przebudowuje odpowiedź impulsową poza wątkiem audio; JUCE płynnie crossfade'uje między IR (bez trzasków).
