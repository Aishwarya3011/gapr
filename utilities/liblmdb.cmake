include(ExternalProject)

set(LIBLMDB_VERSION "0.9.24")
set(LIBLMDB_SHA1SUM "76f4bc1827474403a78e5f8eb225233d67144286")

find_program(PROG_MAKE NAMES make gmake)

set(LIBLMDB_CC_ENV "CC=${CMAKE_C_COMPILER}")
if(ANDROID)
	set(LIBLMDB_CFLAGS "-DANDROID")
	set(LIBLMDB_CC_ENV "${LIBLMDB_CC_ENV} --target=${CMAKE_C_COMPILER_TARGET}")
endif()

ExternalProject_Add(liblmdb
	PREFIX "utilities"
	DOWNLOAD_DIR "${CMAKE_CURRENT_LIST_DIR}/../subprojects/packagecache"
	URL "https://github.com/LMDB/lmdb/archive/LMDB_${LIBLMDB_VERSION}.tar.gz"
	URL_HASH "SHA1=${LIBLMDB_SHA1SUM}"
	#PATCH_COMMAND ${LMDB_PATCH_CMD}
	CONFIGURE_COMMAND ""
	BUILD_IN_SOURCE ON
	BUILD_COMMAND "${PROG_MAKE}"
	"${LIBLMDB_CC_ENV}"
	"AR=${CMAKE_AR}"
	"XCFLAGS=-fPIC ${CMAKE_C_FLAGS_RELEASE} ${LIBLMDB_CFLAGS}"
	"LDFLAGS=-Wl,-soname=liblmdb.so"
	"-j" -C "libraries/liblmdb"
	BUILD_BYPRODUCTS "<SOURCE_DIR>/libraries/liblmdb/liblmdb.so"
	INSTALL_COMMAND ""
	)

if(NOT TARGET PkgConfig::LMDB)
	ExternalProject_Get_Property(liblmdb binary_dir source_dir)
	add_library(liblmdb-lmdb INTERFACE)
	target_include_directories(liblmdb-lmdb SYSTEM INTERFACE
		"${source_dir}/libraries/liblmdb")
	target_link_libraries(liblmdb-lmdb INTERFACE
		"${source_dir}/libraries/liblmdb/liblmdb.so")
	add_dependencies(liblmdb-lmdb liblmdb)
	add_library(PkgConfig::LMDB ALIAS liblmdb-lmdb)
endif()

