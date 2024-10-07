include(ExternalProject)

set(BOOST_VERSION "1.76.0")
set(BOOST_SHA256SUM "f0397ba6e982c4450f27bf32a2a83292aba035b827a5623a14636ea583318c41")

if(ANDROID)
	if(${CMAKE_ANDROID_ARCH} MATCHES "^arm")
		set(BOOST_ARCH arm)
		set(BOOST_ABI aapcs)
	elseif(${CMAKE_ANDROID_ARCH} MATCHES "^x86")
		set(BOOST_ARCH x86)
		set(BOOST_ABI sysv)
	else()
	endif()
	if(${CMAKE_ANDROID_ARCH} MATCHES "64$")
		set(BOOST_ADDRESS_MODEL "64")
	else()
		set(BOOST_ADDRESS_MODEL "32")
	endif()
	set(BOOST_EXTRA_OPTS "toolset=clang" "architecture=${BOOST_ARCH}" "address-model=${BOOST_ADDRESS_MODEL}" "binary-format=elf" "abi=${BOOST_ABI}" target-os=android)
	set(BOOST_EXTRA_PATCH "sh" "-c" "echo -e 'using clang : ${BOOST_ARCH} : ${CMAKE_CXX_COMPILER} : <compileflags>-target <compileflags>${CMAKE_CXX_COMPILER_TARGET} <linkflags>-target <linkflags>${CMAKE_CXX_COMPILER_TARGET} \\x3b ' > <BINARY_DIR>/user-config.jam")
endif()

string(REPLACE "." "_" BOOST_VERSION_UL ${BOOST_VERSION})
ExternalProject_Add(boost
	PREFIX "utilities"
	DOWNLOAD_DIR "${CMAKE_CURRENT_LIST_DIR}/../subprojects/packagecache"
	URL "https://nchc.dl.sourceforge.net/project/boost/boost/${BOOST_VERSION}/boost_${BOOST_VERSION_UL}.tar.bz2"
	URL_HASH "SHA256=${BOOST_SHA256SUM}"
	PATCH_COMMAND sed -i "/<target-os>.*in windows cygwin/s/ows cyg/ows android cyg/" "<SOURCE_DIR>/boostcpp.jam"
	CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E chdir "<SOURCE_DIR>" "./bootstrap.sh" "--with-libraries=context,system,iostreams"
	COMMAND ${BOOST_EXTRA_PATCH}
	BUILD_COMMAND "${CMAKE_COMMAND}" -E chdir "<SOURCE_DIR>"
	"./b2" variant=release link=shared threading=multi runtime-link=shared "cxxflags=-fPIC ${CMAKE_CXX_FLAGS}" ${BOOST_EXTRA_OPTS}
	"--stagedir=<BINARY_DIR>"
	"--build-dir=<BINARY_DIR>/o"
	"--user-config=<BINARY_DIR>/user-config.jam"
	BUILD_BYPRODUCTS "<BINARY_DIR>/lib/libboost_context.so"
	"<BINARY_DIR>/lib/libboost_system.so"
	"<BINARY_DIR>/lib/libboost_iostreams.so"
	INSTALL_COMMAND ""
	)

if(NOT TARGET Boost::boost)
	ExternalProject_Get_Property(boost binary_dir source_dir)
	add_library(boost_boost INTERFACE)
	target_include_directories(boost_boost SYSTEM INTERFACE
		"${source_dir}")
	add_library(boost_context INTERFACE)
	target_link_libraries(boost_context INTERFACE
		"${binary_dir}/lib/libboost_context.so"
		"${binary_dir}/lib/libboost_system.so"
		"${binary_dir}/lib/libboost_iostreams.so"
		#"${source_dir}/stage/lib/libboost_context.dll.a"
		)
	target_include_directories(boost_context SYSTEM INTERFACE
		"${source_dir}")
	add_dependencies(boost_boost boost)
	add_dependencies(boost_context boost)

	add_library(Boost::context ALIAS boost_context)
	add_library(Boost::boost ALIAS boost_boost)
	add_library(Boost::fiber ALIAS boost_boost)
endif()

# TODO if cross-compile: add tools/build/src/user-config.jam: 
#  using gcc : : x86_64-w64-mingw32-g++ : <rc>/usr/bin/x86_64-w64-mingw32-g++ ;
# and fix boost.context.jam elf->pe
