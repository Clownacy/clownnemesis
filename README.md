# clownnemesis

A compressor and decompressor for the Nemesis format.

A description of the Nemesis format can be found here:
https://segaretro.org/Nemesis_compression.

The compressor currently implements three prefix-code generation algorithms:
Shannon, Fano, and Huffman. Currently the Fano algorithm produces the smallest
data as the Huffman implementation is naive.

Both an executable and library are provided. Both are written in ANSI C (C89).

To build this, use CMake.
