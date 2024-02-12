# clownnemesis

A compressor and decompressor for the Nemesis format.

A description of the Nemesis format can be found here:
https://segaretro.org/Nemesis_compression.

The compressor implements three prefix-code generation algorithms: Shannon,
Fano, and Huffman. Huffman produces the smallest files, while Fano produces the
closest results to Sega's compressor.

Both an executable and library are provided. Both are written in ANSI C (C89).

To build this, use CMake.
