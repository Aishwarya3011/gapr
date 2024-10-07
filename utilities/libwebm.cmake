include(ExternalProject)

set(LIBWEBM_VERSION "1.0.0.27")
set(LIBWEBM_SHA1SUM "1471d5b90ebf747447f5845ebead2f7baadd0201")

if(ANDROID)
	set(LIBWEBM_PATCH_CMD "sed" "-i" "/^#include *<fcntl.h>/i#include <unistd.h>" "<SOURCE_DIR>/mkvmuxerutil.cpp")
	set(LIBWEBM_EXTRA_CFG "-DCMAKE_CXX_COMPILER_TARGET=${CMAKE_CXX_COMPILER_TARGET}")
endif()

ExternalProject_Add(libwebm
	PREFIX "utilities"
	DOWNLOAD_DIR "${CMAKE_CURRENT_LIST_DIR}/../subprojects/packagecache"
	URL "https://github.com/webmproject/libwebm/archive/libwebm-${LIBWEBM_VERSION}.tar.gz"
	URL_HASH "SHA1=${LIBWEBM_SHA1SUM}"
	PATCH_COMMAND ${LIBWEBM_PATCH_CMD}
	CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCMAKE_CXX_FLAGS=-fPIC ${CMAKE_CXX_FLAGS}" ${LIBWEBM_EXTRA_CFG}
	BUILD_COMMAND "${CMAKE_MAKE_PROGRAM}" "webm"
	BUILD_BYPRODUCTS "<BINARY_DIR>/libwebm.a"
	INSTALL_COMMAND ""
	)

if(NOT TARGET WEBM::WEBM)
	ExternalProject_Get_Property(libwebm binary_dir source_dir)
	add_library(libwebm-webm INTERFACE)
	target_link_libraries(libwebm-webm INTERFACE 
		"${binary_dir}/libwebm.a")
	target_include_directories(libwebm-webm SYSTEM INTERFACE
		"${source_dir}")
	add_dependencies(libwebm-webm libwebm)
	add_library(WEBM::WEBM ALIAS libwebm-webm)
endif()

