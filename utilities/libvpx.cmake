include(ExternalProject)

set(LIBVPX_VERSION "1.9.0")
set(LIBVPX_SHA1SUM "2ab8203ad8922bdf3256e4a197d1348fa8db9a62")

find_program(YASM_OR_NASM NAMES yasm nasm)
if(NOT YASM_OR_NASM)
	message(FATAL_ERROR "Building libvpx requires yasm or nasm.")
endif()
find_program(PROG_SHELL NAMES sh bash)
find_program(PROG_MAKE NAMES make gmake)

set(LIBVPX_SETUP_ENV)
set(LIBVPX_EXTRA_CFG)
if(ANDROID)
	find_program(PROG_ENV NAMES env)
	list(APPEND LIBVPX_SETUP_ENV
		"${PROG_ENV}"
		"CC=${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET}"
		#"CFLAGS="
		"CXX=${CMAKE_CXX_COMPILER} --target=${CMAKE_CXX_COMPILER_TARGET}"
		#"CXXFLAGS="
		"LD=${CMAKE_C_COMPILER} --target=${CMAKE_C_COMPILER_TARGET}"
		"STRIP=${CMAKE_STRIP}"
		)
	if(${CMAKE_ANDROID_ARCH} MATCHES "^x86")
		list(APPEND LIBVPX_SETUP_ENV
			"AS=${CMAKE_ASM_NASM_COMPILER} ${CMAKE_ASM_NASM_COMPILER_ARG1}")
	else()
		list(APPEND LIBVPX_SETUP_ENV
			"AS=${ANDROID_TOOLCHAIN_PREFIX}as${ANDROID_TOOLCHAIN_SUFFIX}")
	endif()
	if(${CMAKE_ANDROID_ARCH} STREQUAL "arm")
		list(APPEND LIBVPX_EXTRA_CFG
			"--target=${CMAKE_ANDROID_ARCH}v7-android-gcc")
	else()
		list(APPEND LIBVPX_EXTRA_CFG
			"--target=${CMAKE_ANDROID_ARCH}-android-gcc")
	endif()
	set(LIBVPX_PATCH_CMD
		sed -i "/SONAME *=/s/=.*/= libvpx.so/" "<SOURCE_DIR>/libs.mk"
		COMMAND sed -i "s/die .*enable-shared only supported on ELF.*/\\x3a/" "<SOURCE_DIR>/configure"
		COMMAND sed -i "s/.*See build.make.Android.mk for.*/\\x3b\\x3b\\nxxx\\x29\\n/" "<SOURCE_DIR>/build/make/configure.sh"
		)
endif()

ExternalProject_Add(libvpx
	PREFIX "utilities"
	DOWNLOAD_DIR "${CMAKE_CURRENT_LIST_DIR}/../subprojects/packagecache"
	URL "https://github.com/webmproject/libvpx/archive/v${LIBVPX_VERSION}.tar.gz"
	URL_HASH "SHA1=${LIBVPX_SHA1SUM}"
	PATCH_COMMAND ${LIBVPX_PATCH_CMD}
	CONFIGURE_COMMAND
	${LIBVPX_SETUP_ENV}
	${PROG_SHELL} "<SOURCE_DIR>/configure" "--disable-install-bins"
	"--disable-install-libs" "--disable-examples" "--disable-tools"
	"--disable-docs" "--enable-vp9-highbitdepth" "--enable-vp9"
	"--disable-unit-tests" "--disable-webm-io" "--disable-libyuv"
	"--enable-shared" "--disable-static" ${LIBVPX_EXTRA_CFG}
	BUILD_COMMAND "${PROG_MAKE}" "libvpx.so"
	BUILD_BYPRODUCTS "<BINARY_DIR>/libvpx.so"
	INSTALL_COMMAND ""
	)

if(NOT TARGET VPX::VPX)
	ExternalProject_Get_Property(libvpx binary_dir source_dir)
	add_library(vpx_vpx INTERFACE)
	target_link_libraries(vpx_vpx INTERFACE 
		"${binary_dir}/libvpx.so")
	target_include_directories(vpx_vpx SYSTEM INTERFACE
		"${source_dir}")
	add_dependencies(vpx_vpx libvpx)
	add_library(VPX::VPX ALIAS vpx_vpx)
endif()

#########################
###--target=x86_64-win64-gcc --libc=/usr/x86_64-w64-mingw32/sys-root/mingw/lib \
###LD= CROSS=x86_64-w64-mingw32- AS=yasm ./configure 
#
