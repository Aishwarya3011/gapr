add_executable(start
	"main.cc"
	"start-window.cc"
	)

set_target_properties(start PROPERTIES
	AUTOUIC ON
	AUTOMOC ON
	AUTOUIC_SEARCH_PATHS "ui"
	WIN32_EXECUTABLE TRUE
	#PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY ..
	RUNTIME_OUTPUT_NAME "gapr"
	)

target_link_libraries(start PRIVATE gui-lib)

install(TARGETS start
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT start
	)

if(WIN32)
	target_link_libraries(start PRIVATE core-ver)
	target_sources(start PRIVATE "icon.rc")
endif()
if(MSVC)
	target_link_libraries(start PRIVATE "${GETOPT_LIBRARY}")
endif()

if(UNIX AND NOT APPLE)
	install(FILES "../gui-lib/resources/scalable/app-icon.svg"
		RENAME "${PROJECT_APPLICATION_ID}.svg"
		DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
		COMPONENT start
		)
	configure_file("appdata.xml.in" "start.appdata.xml" @ONLY)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/start.appdata.xml"
		RENAME "${PROJECT_APPLICATION_ID}.appdata.xml"
		DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo"
		COMPONENT start
		)
	configure_file("desktop.in" "start.desktop" @ONLY)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/start.desktop"
		RENAME "${PROJECT_APPLICATION_ID}.desktop"
		DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
		COMPONENT start
		)
endif()

