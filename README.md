### Hardware acceleration for all CRC parameters

Much hardware acceleration effort is dedicated to CRC32 and CRC32C (and to a lesser extent CRC64). This repository is an attempt to provide a single unified hardware-accelerated algorithm that could be used to compute the CRC for any CRC parameters, with a maximum polynomial width of 64. This is likely the first this was achieved. Although others have gotten close to doing it in the past.

The algorithm is based on the Intel paper "Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction". It takes into account both reflections and different polynomial width.

### Installation

GCC expects the library to be compiled with the `-mssse3` and `-mpclmul` flags.

### Benchmark

| Length | Reflected | Non-Reflected |
| --- | --- | --- |
| 100 B | 663.66 MiB/s | 686.10 MiB/s |
| 1 KB | 5289.38 MiB/s | 4725.84 MiB/s |
| 10 KB | 24088.77 MiB/s | 23483.73 MiB/s |
| 100 KB | 31767.96 MiB/s | 31884.80 MiB/s |
| 1 MB | 33710.65 MiB/s | 33182.82 MiB/s |
| 10 MB | 32682.46 MiB/s | 32851.34 MiB/s |
| 100 MB | 17736.18 MiB/s | 17739.48 MiB/s |