### Hardware acceleration for all CRC parameters

Much hardware acceleration effort is dedicated to CRC32 and CRC32C (and to a lesser extent CRC64). This repository is an attempt to provide a single unified hardware-accelerated algorithm that could be used to compute the CRC for any CRC parameters, with a maximum polynomial width of 64. This is likely the first time this was achieved, though others have gotten close to doing it in the past.

The algorithm is based on the Intel paper "Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction". It takes into account both reflections and different polynomial widths.

### Installation

GCC expects the library to be compiled with the `-mssse3` and `-mpclmul` flags.

### Benchmark

Measured in GiB/s.

| Length | Reflected | Non-Reflected |
| --- | :-: | :-: |
| 100 B | 0.66 | 0.67 |
| 1 KB | 5.17 | 4.68 |
| 10 KB | 23.44 | 23.07 |
| 100 KB | 31.32 | 31.42 |
| 1 MB | 33.92 | 33.80 |
| 10 MB | 31.48 | 31.58 |
| 100 MB | 16.91 | 16.43 |

Tested on a 12th generation Intel i7 processor.