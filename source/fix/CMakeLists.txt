add_executable(fix)

set_target_properties(fix PROPERTIES
	AUTOUIC ON
	AUTOMOC ON
	AUTORCC ON
	AUTOUIC_SEARCH_PATHS "ui;../proofread/ui"
	#PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY ".."
	RUNTIME_OUTPUT_NAME "gapr-fix")

target_link_libraries(fix PRIVATE
	gui-lib
	)

install(TARGETS fix
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT fix
	)

target_sources(fix PRIVATE
	"model.cc"
	"misc.cc"
	"canvas.cc"
	"window.cc"
	"main.cc"
	"compute.cc"
	"neuron-list.cc"
	"fix.qrc"
	)

if(WIN32)
	target_link_libraries(fix PRIVATE core-ver)
	target_sources(fix PRIVATE "../start/icon.rc")
	target_link_libraries(fix PRIVATE
		#ws2_32 # for cache.thread.cc::select
		)
endif()
if(MSVC)
	target_link_libraries(fix PRIVATE "${GETOPT_LIBRARY}")
endif()
if(NOT HAVE_STD_CHARCONV)
	target_compile_definitions(fix PRIVATE STD_CHARCONV_MISSING)
endif()

if(Qt5LinguistTools_FOUND)
	qt5_add_translation(fix-qms translations/fix.zh_CN.ts)
	add_custom_target(fix-qms DEPENDS ${fix-qms})
	add_dependencies(fix fix-qms)
	install(FILES ${fix-qms}
		DESTINATION "${CMAKE_INSTALL_DATADIR}/gapr/translations"
		COMPONENT fix
		)
endif()

