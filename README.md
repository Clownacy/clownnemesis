A compressor and decompressor for the Nemesis format.

It currently implements three prefix-code generation algorithms: Shannon, Fano,
and Huffman. Currently the Fano algorithm produces the smallest data, as the
Huffman implementation is naive.

Both an executable and library are provided. Both are written in ANSI C (C89).

To build this, use CMake.
