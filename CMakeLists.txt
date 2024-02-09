cmake_minimum_required(VERSION 3.0...3.28.3)

project(clownnemesis LANGUAGES C)

add_library(clownnemesis STATIC
	"common.h"
	"common-internal.c"
	"common-internal.h"
	"compress.c"
	"compress.h"
	"decompress.c"
	"decompress.h"
)

add_executable(clownnemesis-tool
	"tool.c"
)

target_link_libraries(clownnemesis-tool PRIVATE clownnemesis)
