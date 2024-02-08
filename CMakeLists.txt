cmake_minimum_required(VERSION 3.0...3.28.3)

project(clownnemesis LANGUAGES C)

add_library(clownnemesis-decompress STATIC
	"decompress.c"
	"decompress.h"
)

add_executable(clownnemesis-decompressor
	"decompressor.c"
)

target_link_libraries(clownnemesis-decompressor PRIVATE clownnemesis-decompress)
