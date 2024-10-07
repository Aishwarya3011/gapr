add_executable(proofread
	"../fix/compute.cc"
	"canvas.cc"
	"window.cc"
	"main.cc"
	"../fix/fix.qrc"
	)

set_target_properties(proofread PROPERTIES
	AUTOUIC ON
	AUTOMOC ON
	AUTORCC ON
	AUTOUIC_SEARCH_PATHS "ui"
	#PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY ..
	RUNTIME_OUTPUT_NAME "gapr-proofread"
	)

target_link_libraries(proofread PRIVATE
	gui-lib
	)

install(TARGETS proofread
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT proofread
	)

if(WIN32)
	target_link_libraries(proofread PRIVATE core-ver)
	target_sources(proofread PRIVATE "../start/icon.rc")
	target_link_libraries(proofread PRIVATE
		#ws2_32 # for cache.thread.cc::select
		)
endif()
if(MSVC)
	target_link_libraries(proofread PRIVATE "${GETOPT_LIBRARY}")
endif()

if(Qt5LinguistTools_FOUND)
	qt5_add_translation(proofread-qms translations/fix-lite.zh_CN.ts)
	add_custom_target(proofread-qms DEPENDS ${proofread-qms})
	add_dependencies(proofread proofread-qms)
	install(FILES ${proofread-qms}
		DESTINATION "${CMAKE_INSTALL_DATADIR}/gapr/translations"
		COMPONENT proofread
		)
endif()

