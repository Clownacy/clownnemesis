cmake_minimum_required(VERSION 3.0...3.28.3)

option(CLOWNNEMESIS_DEBUG "Enable debug prints." OFF)

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

if(CLOWNNEMESIS_DEBUG)
	target_compile_definitions(clownnemesis PRIVATE CLOWNNEMESIS_DEBUG)
endif()

add_executable(clownnemesis-tool
	"tool.c"
)

target_link_libraries(clownnemesis-tool PRIVATE clownnemesis)

add_executable(clownnemesis-test
	"test.c"
)

target_link_libraries(clownnemesis-test PRIVATE clownnemesis)
