include(ExternalProject)

set(HEVC_LIB_VERSION "16.20")

find_program(PROG_MAKE NAMES make gmake)

set(HEVC_LIB_CPP_ENV "CPP=${CMAKE_CXX_COMPILER} -fPIC")
set(HEVC_LIB_CC_ENV "CC=${CMAKE_C_COMPILER} -fPIC")
if(ANDROID)
	set(HEVC_LIB_MAKE_FLAGS "STAT_LIBS=")
	if(${CMAKE_ANDROID_ARCH} MATCHES "^x86|arm$")
		list(APPEND HEVC_LIB_MAKE_FLAGS "DEFS=-DMSYS_LINUX -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=32 -DMSYS_UNIX_LARGEFILE")
	endif()
	set(HEVC_LIB_CPP_ENV "${HEVC_LIB_CPP_ENV} --target=${CMAKE_CXX_COMPILER_TARGET}")
	set(HEVC_LIB_CC_ENV "${HEVC_LIB_CC_ENV} --target=${CMAKE_C_COMPILER_TARGET}")
endif()
if(WIN32)
	set(HEVC_LIB_MAKE_FLAGS "LIBS=")
endif()

ExternalProject_Add(hevc-lib
	PREFIX "utilities"
	SVN_REPOSITORY "https://hevc.hhi.fraunhofer.de/svn/svn_HEVCSoftware/tags/HM-${HEVC_LIB_VERSION}/"
	TLS_VERIFY FALSE
	UPDATE_COMMAND ""
	PATCH_COMMAND "patch" "-p1" "-i" "${CMAKE_CURRENT_LIST_DIR}/jctvc-hm-fix.patch"
	CONFIGURE_COMMAND ""
	BUILD_COMMAND "${PROG_MAKE}"
	${HEVC_LIB_MAKE_FLAGS}
	"${HEVC_LIB_CPP_ENV}"
	"${HEVC_LIB_CC_ENV}"
	"-C" "<SOURCE_DIR>/build/linux/" "release_highbitdepth" "-j"
	"LIB_DIR=<BINARY_DIR>"
	"BIN_DIR=<BINARY_DIR>/bin"
	"OBJ_DIR=<BINARY_DIR>/obj"
	BUILD_BYPRODUCTS "<BINARY_DIR>/libTLibDecoderHighBitDepthStatic.a" "<BINARY_DIR>/libTLibCommonHighBitDepthStatic.a"
	INSTALL_COMMAND "")

if(NOT TARGET jctvc-hm)
	ExternalProject_Get_Property(hevc-lib binary_dir source_dir)
	add_library(jctvc-hm INTERFACE)
	target_link_libraries(jctvc-hm INTERFACE 
		"${binary_dir}/libTLibDecoderHighBitDepthStatic.a"
		"${binary_dir}/libTLibCommonHighBitDepthStatic.a"
		)
	target_include_directories(jctvc-hm SYSTEM INTERFACE
		"${source_dir}/source/Lib")
	add_dependencies(jctvc-hm hevc-lib)
endif()

