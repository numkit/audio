# numkit/audio

[![License: 0BSD](https://img.shields.io/badge/License-0BSD-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)

The **Audio Toolbox** for **[NumKit](https://github.com/numkit/numkit)**.

High-performance audio feature extraction, spectral shape descriptors, frequency scale conversions, and zero-dependency audio/MIDI I/O with MATLAB R2025b parity.

---

## Features

- **Frequency & Loudness Scales**: `hz2mel`, `mel2hz`, `hz2bark`, `bark2hz`, `hz2erb`, `erb2hz`, `phon2sone`, `sone2phon` (ISO 532-1 & ISO 532-2).
- **Spectral Descriptors**: `spectralCentroid`, `spectralSpread`, `spectralRolloffPoint`, `spectralDecrease`, `spectralSlope`, `spectralFlux`, `spectralCrest`, `spectralEntropy`, `spectralFlatness`, `spectralKurtosis`, `spectralSkewness`.
- **Mel & Cepstral Features**: `melSpectrogram`, `audioDelta`, `cepstralCoefficients`, `mfcc`, `gtcc`.
- **Pitch & Harmonics**: `pitch` (NCF, CEP, PEF, LHS, SRH), `harmonicRatio`.
- **Audio & MIDI I/O**: `audioread`, `audiowrite`, `audioinfo` (WAV, FLAC, MP3, AIFF, AU), `midiread`, `midiwrite`, `midiinfo`.

---

## Building as a Dynamic Module

```bash
cmake -B build -S .
cmake --build build --config Release
```

Produces `numkit_audio.dll` (Windows) or `libnumkit_audio.so` (Linux/macOS).

---

## Usage in NumKit

Install via the NumKit package manager:

```bash
nk pkg install audio
```

Or load dynamically in a script:

```matlab
% Loaded automatically on first call to any audio function:
m = hz2mel(1000);
f = mel2hz(m);
```

---

## License

This software is released under the **[BSD Zero Clause License (0BSD)](LICENSE)**.
