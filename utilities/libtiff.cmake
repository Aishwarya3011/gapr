include(ExternalProject)

set(LIBTIFF_VERSION "4.1.0")
set(LIBTIFF_SHA1SUM "7a882f8d55fd0620cbf89c47994d2d1d3b975452")

find_program(PROG_SHELL NAMES sh bash)
find_program(PROG_MAKE NAMES make gmake)

ExternalProject_Add(libtiff
	PREFIX "utilities"
	DOWNLOAD_DIR "${CMAKE_CURRENT_LIST_DIR}/../subprojects/packagecache"
	URL "http://download.osgeo.org/libtiff/tiff-${LIBTIFF_VERSION}.tar.gz"
	URL_HASH "SHA1=${LIBTIFF_SHA1SUM}"
	CONFIGURE_COMMAND "${PROG_SHELL}" "<SOURCE_DIR>/configure"
	"--host=${ANDROID_LLVM_TRIPLE}" "--with-sysroot=${CMAKE_SYSROOT}"
	"--enable-shared" "--disable-static"
	"--disable-lzma" "--disable-jpeg" "--disable-cxx"
	"CC=${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET}"
	#"CFLAGS=${CMAKE_C_FLAGS_RELEASE}"
	BUILD_COMMAND "${PROG_MAKE}" "-j"
	BUILD_BYPRODUCTS "<BINARY_DIR>/libtiff/.libs/libtiff.so"
	INSTALL_COMMAND ""
	)

if(NOT TARGET TIFF::TIFF)
	ExternalProject_Get_Property(libtiff binary_dir source_dir)
	add_library(libtiff-tiff INTERFACE)
	target_link_libraries(libtiff-tiff INTERFACE 
		"${binary_dir}/libtiff/.libs/libtiff.so")
	target_include_directories(libtiff-tiff SYSTEM INTERFACE
		"${source_dir}/libtiff" "${binary_dir}/libtiff")
	add_dependencies(libtiff-tiff libtiff)
	add_library(TIFF::TIFF ALIAS libtiff-tiff)
endif()

