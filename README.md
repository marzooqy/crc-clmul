### Hardware acceleration for all CRC parameters

Much hardware acceleration effort is dedicated to CRC32 and CRC32C (and to a lesser extent CRC64). This repository is an attempt to provide a single unified hardware-accelerated algorithm that could be used to compute the CRC for any CRC parameters, with a maximum polynomial width of 64. This is likely the first time this was achieved, though others have gotten close to doing it in the past.

The algorithm is based on the Intel paper "Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction". It takes into account both reflections and different polynomial width.

### Installation

GCC expects the library to be compiled with the `-mssse3` and `-mpclmul` flags.

### Benchmark

| Length | Reflected | Non-Reflected |
| --- | :-: | :-: |
| 100 B | 674.93 MiB/s | 689.57 MiB/s |
| 1 KB | 5.14 GiB/s | 4.65 GiB/s |
| 10 KB | 23.48 GiB/s | 23.07 GiB/s |
| 100 KB | 31.52 GiB/s | 31.48 GiB/s |
| 1 MB | 33.61 GiB/s | 33.68 GiB/s |
| 10 MB | 30.88 GiB/s | 30.65 GiB/s |
| 100 MB | 16.08 GiB/s | 16.27 GiB/s |