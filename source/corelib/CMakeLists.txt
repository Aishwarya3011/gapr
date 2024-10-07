add_library(core-lib SHARED)

set_target_properties(core-lib PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ..
	OUTPUT_NAME "${PROJECT_NAME}Core")

install(TARGETS core-lib
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	COMPONENT core
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT core
	)
# XXX install dirs/licenses...

target_sources(core-lib PRIVATE
	"acceptor.cc"
	"archive.cc"
	"buffer.cc"
	"connection.cc"
	"cube-cache.cc"
	"cube.cc"
	"cube-loader.cc"
	"cube-builder.cc"
	"downloader.cc"
	"fiber.cc"
	"filter.cc"
	"mem-file-cache.cc"
	"model.cc"
	"options.cc"
	"parser.cc"
	"plugin-loader.cc"
	"program.cc"
	"promise.cc"
	"streambuf.cc"
	"trace-api.cc"
	"utility.cc"
	"swc-helper.cc"
	"detail/asio-misc.cc"
	"detail/cube-loader-nrrd.cc"
	"detail/cube-loader-tiff.cc"
	"detail/cube-loader-v3d.cc"
	)

add_library(core-lib-asio STATIC "asio-src.cc")
set_target_properties(core-lib-asio PROPERTIES
	POSITION_INDEPENDENT_CODE TRUE)
target_link_libraries(core-lib-asio PRIVATE
	Boost::boost
	OpenSSL::SSL
	)
target_link_libraries(core-lib PRIVATE core-lib-asio)
target_compile_definitions(core-lib PRIVATE BOOST_ASIO_SOURCE)

target_link_libraries(core-lib PRIVATE PkgConfig::LMDB)
target_link_libraries(core-lib PUBLIC
	Threads::Threads
	ZLIB::ZLIB
	TIFF::TIFF
	Boost::boost
	Boost::context
	OpenSSL::SSL
	CURL::libcurl
	)

if(ENABLE_VP9)
	target_sources(core-lib PRIVATE "detail/cube-loader-vp9.cc")
	target_compile_definitions(core-lib PRIVATE WITH_VP9)
	target_link_libraries(core-lib PRIVATE
		WEBM::WEBM
		VPX::VPX
		)
endif()
if(ENABLE_HEVC)
	target_sources(core-lib PRIVATE "detail/cube-loader-hevc.cc")
	target_compile_definitions(core-lib PRIVATE WITH_HEVC)
	target_link_libraries(core-lib PRIVATE
		jctvc-hm
		)
endif()

if(WIN32)
	target_link_libraries(core-lib PRIVATE mswsock ws2_32)

	add_library(core-ver OBJECT)
	configure_file("version.rc.in" "version.rc" @ONLY)
	target_sources(core-ver PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
else()
	target_link_libraries(core-lib PRIVATE "${CMAKE_DL_LIBS}")
endif()
if(ANDROID)
	target_link_libraries(core-lib PRIVATE "-llog")
endif()

if(OpenMP_FOUND)
	if(ENABLE_VP9 OR ENABLE_HEVC)
		target_compile_definitions(core-lib PRIVATE HAVE_OPENMP)
		target_link_libraries(core-lib PRIVATE OpenMP::OpenMP_CXX)
	endif()
endif()
if(NOT HAVE_STD_FILESYSTEM)
	target_compile_definitions(core-lib PRIVATE STD_FILESYSTEM_MISSING)
	target_link_libraries(core-lib PRIVATE Boost::filesystem)
endif()
if(NOT HAVE_STD_CHARCONV)
	target_compile_definitions(core-lib PRIVATE STD_CHARCONV_MISSING)
endif()
