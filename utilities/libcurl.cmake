include(ExternalProject)

set(LIBCURL_VERSION "7.82.0")
set(LIBCURL_SHA256SUM "0aaa12d7bd04b0966254f2703ce80dd5c38dbbd76af0297d3d690cdce58a583c")

find_program(PROG_SHELL NAMES sh bash)
find_program(PROG_MAKE NAMES make gmake)

ExternalProject_Add(libcurl
	PREFIX "utilities"
	DOWNLOAD_DIR "${CMAKE_CURRENT_LIST_DIR}/../subprojects/packagecache"
	URL "https://curl.haxx.se/download/curl-${LIBCURL_VERSION}.tar.xz"
	URL_HASH "SHA256=${LIBCURL_SHA256SUM}"
	PATCH_COMMAND
	sed -i "/OPENSSL_PCDIR=/s/=.*/=USE_NO_PC/" "<SOURCE_DIR>/configure"
	CONFIGURE_COMMAND
	"${PROG_SHELL}" "<SOURCE_DIR>/configure"
	AR=${CMAKE_AR}
	"AS=${ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG}/bin/${CMAKE_ANDROID_ARCH}-linux-androideabi-as"
	"CC=${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET}"
	"CXX=${CMAKE_CXX_COMPILER} --target=${CMAKE_CXX_COMPILER_TARGET}"
	LD=${CMAKE_LINKER}
	RANLIB=${CMAKE_RANLIB}
	STRIP=${CMAKE_STRIP}
	#"CFLAGS=${CMAKE_C_FLAGS_RELEASE}"
	"--host=${ANDROID_LLVM_TRIPLE}"
	--with-pic --enable-shared --disable-static
	"--with-ssl=${OpenSSL_PREFIX}"
	"--with-ca-path=/etc/security/cacerts"
	"--with-sysroot=${CMAKE_SYSROOT}"
	BUILD_COMMAND "${PROG_MAKE}" "-j" "-C" "lib" "libcurl.la"
	BUILD_BYPRODUCTS "<BINARY_DIR>/lib/.libs/libcurl.so"
	INSTALL_COMMAND ""
	)

if(NOT TARGET CURL::CURL)
	ExternalProject_Get_Property(libcurl binary_dir source_dir)
	add_library(libcurl-curl INTERFACE)
	target_link_libraries(libcurl-curl INTERFACE 
		"${binary_dir}/lib/.libs/libcurl.so")
	target_include_directories(libcurl-curl SYSTEM INTERFACE
		"${source_dir}/include")
	if(TARGET openssl)
		add_dependencies(libcurl openssl)
	endif()
	add_dependencies(libcurl-curl libcurl)
	add_library(CURL::libcurl ALIAS libcurl-curl)
endif()

