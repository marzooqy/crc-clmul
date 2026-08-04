### Hardware acceleration for all CRC parameters

Much hardware acceleration effort is dedicated to CRC32 and CRC32C (and to a lesser extent CRC64). This repository is an attempt to provide a single unified hardware-accelerated algorithm that could be used to compute the CRC for any CRC parameters in the [RevEng CRC catalogue](https://reveng.sourceforge.io/crc-catalogue), with a maximum polynomial width of 64. This is likely the first time this was achieved, though others have gotten close to doing it in the past.

The algorithm is based on the Intel paper "Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction". It takes into account both reflections and different polynomial widths.

A 64-bit system is expected. A slow software version of the library can be used by defining `DISABLE_SIMD`.

### Benchmark

Measured in GiB/s.

| Length | Reflected | Non-Reflected |
| --- | :-: | :-: |
| 100 B | 13.6 | 11.8 |
| 1 KB | 32.4 | 31.0 |
| 10 KB | 34.4 | 34.1 |
| 100 KB | 34.4 | 34.4 |
| 1 MB | 34.6 | 34.5 |
| 10 MB | 32.9 | 31.9 |
| 100 MB | 16.3 | 16.6 |

Tested on a 12th generation Intel i7 processor.