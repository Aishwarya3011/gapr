include(ExternalProject)
ExternalProject_Add(ext-neurolabi
	PREFIX "neurolabi"
	URL "https://github.com/tingzhao/neutube/archive/neutube.zip"
	URL_HASH "SHA1=ec16ce2ce26ccc9d4613bbc814ed0a121536b7c6"
	#GIT_REPOSITORY "https://github.com/tingzhao/neutube.git"
	UPDATE_COMMAND ""
	PATCH_COMMAND sed -i "/z >= 0/s/z < tw->trace_mask->width/z < tw->trace_mask->depth/" "<SOURCE_DIR>/neurolabi/c/tz_trace_utils.c"
	SOURCE_SUBDIR "neurolabi/c"
	CMAKE_ARGS "-DCMAKE_BUILD_TYPE=Release" "-DUSE_LIBJANSSON=OFF" "-DUSE_LIBXML2=OFF" "-DCMAKE_C_FLAGS=-fPIC" "-DUSE_LIBFFTWF=OFF" "-DUSE_LIBFFTW=OFF"
	#CMAKE_ARGS " "-DCMAKE_CXX_FLAGS=-fPIC"
	BUILD_COMMAND "${CMAKE_MAKE_PROGRAM}"
	BUILD_BYPRODUCTS "<BINARY_DIR>/libneurolabi.a"
	INSTALL_COMMAND ""
	)

if(NOT TARGET neurolabi::c)
	ExternalProject_Get_Property(ext-neurolabi binary_dir source_dir)
	set(neurolabi_c_incs "${source_dir}/neurolabi/c"
		"${source_dir}/neurolabi/c/include"
		"${source_dir}/neurolabi/lib/genelib/src")
	add_library(neurolabi-c INTERFACE)
	target_link_libraries(neurolabi-c INTERFACE 
		"${binary_dir}/libneurolabi.a")
	target_include_directories(neurolabi-c SYSTEM INTERFACE
		"${neurolabi_c_incs}")
	add_dependencies(neurolabi-c ext-neurolabi)
	add_library(neurolabi::c ALIAS neurolabi-c)
endif()

add_executable(trace
	"compute.cc"
	"main.cc"
	"evaluator-train.cc"
	"reset-proofread.cc"
	"utils.cc"
	)

set_target_properties(trace PROPERTIES
	#PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY ..
	RUNTIME_OUTPUT_NAME "gapr-trace"
	)

add_library(trace-evaluator STATIC)
set(USE_TORCH FALSE)
if(USE_TORCH)
	find_package(Torch REQUIRED)
	target_sources(trace-evaluator PRIVATE "evaluator-torch-resnet.cc")
	target_link_libraries(trace-evaluator PRIVATE torch)
else()
	target_sources(trace-evaluator PRIVATE "evaluator-dumb.cc")
endif()

find_package(GSL REQUIRED)
target_link_libraries(trace PRIVATE
	core-lib
	neurolabi::c
	trace-evaluator
	Threads::Threads
	GSL::gsl
	)

install(TARGETS trace
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT trace
	)

if(WIN32)
	target_link_libraries(trace PRIVATE core-ver)
	target_link_libraries(trace PRIVATE
		#ws2_32 # XXX cache.thread.cc __imp_select
		pcre2-posix # XXX neutube __imp_pcre2_regcomp
		)
endif()

