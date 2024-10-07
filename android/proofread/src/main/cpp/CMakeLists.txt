cmake_minimum_required(VERSION 3.15.0 FATAL_ERROR)

## VERSION UPDATE
project(Gapr VERSION 1.1.0.11
	DESCRIPTION "Gapr accelerates projectome reconstruction"
	LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

add_compile_options(-Wall -Werror)

find_package(OpenMP QUIET)
find_package(Threads REQUIRED)
if(NOT TARGET ZLIB::ZLIB)
	find_library(zlib-lib libz.so)
	add_library(ZLIB::ZLIB INTERFACE IMPORTED)
	set_property(TARGET ZLIB::ZLIB PROPERTY
		INTERFACE_LINK_LIBRARIES "${zlib-lib}")
endif()
include("../../../../../utilities/libtiff.cmake")
include("../../../../../utilities/openssl.cmake")
include("../../../../../utilities/libcurl.cmake")
include("../../../../../utilities/liblmdb.cmake")
set(HAVE_STD_FILESYSTEM ON)
set(HAVE_STD_CHARCONV ON)

include("../../../../../utilities/boost.cmake")
add_compile_definitions(BOOST_ASIO_DYN_LINK BOOST_ASIO_NO_DEPRECATED)
add_compile_definitions(BOOST_ASIO_NO_TS_EXECUTORS)

set(ENABLE_VP9 ON)
include("../../../../../utilities/libvpx.cmake")
include("../../../../../utilities/libwebm.cmake")
set(ENABLE_HEVC ON)
include("../../../../../utilities/jctvc-hm.cmake")

configure_file("../../../../../source/config.hh.in" "config.hh" @ONLY)
configure_file("../../../../../source/corelib/gapr/version.hh.in"
	"gapr/version.hh" @ONLY)

include_directories("${CMAKE_SOURCE_DIR}/../../../../../source/corelib")
include_directories("${CMAKE_SOURCE_DIR}/../../../../../source/guilib")
include_directories("${CMAKE_BINARY_DIR}")

include(GNUInstallDirs)

add_subdirectory("../../../../../source/corelib" "corelib")

add_library(native-lib SHARED main.cpp
	../../../../../source/fix/compute.cc
	../../../../../source/fix/misc.cc
	../../../../../source/fix/model.cc)
set_target_properties(native-lib PROPERTIES
	OUTPUT_NAME "${PROJECT_NAME}Android")

find_library(log-lib log)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)
find_library(android-lib android)
target_link_libraries(native-lib
	${log-lib}
	${egl-lib}
	${gles-lib}
	${android-lib}
	core-lib)
target_include_directories(native-lib PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../../../../../source/fix")

add_custom_command(TARGET native-lib POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy "${OpenMP_omp_LIBRARY}" "./"
	WORKING_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
	COMMENT "copy deps"
	USES_TERMINAL)
set_property(TARGET native-lib
	PROPERTY ANDROID_NATIVE_LIB_DEPENDENCIES TRUE)

