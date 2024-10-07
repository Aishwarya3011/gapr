add_library(gui-lib SHARED
	"gapr/gui/application.hh"
	"gapr/gui/dialogs.hh"
	"gapr/gui/login-dialog.hh"
	"gapr/gui/range-widget.hh"
	"about-dialog.cc"
	"application.cc"
	"dialogs.cc"
	"login-dialog.cc"
	"program.cc"
	"range-widget.cc"
	"utility.cc"
	"resources/icons.qrc"
	)

set_target_properties(gui-lib PROPERTIES
	AUTOMOC ON
	AUTOUIC ON
	AUTORCC ON
	AUTOUIC_SEARCH_PATHS "ui"
	RUNTIME_OUTPUT_DIRECTORY ..
	OUTPUT_NAME "${PROJECT_NAME}Gui"
	)

install(TARGETS gui-lib
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	COMPONENT gui
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT gui
	)

target_link_libraries(gui-lib PUBLIC
	core-lib
	Threads::Threads
	Qt5::Widgets
	#TIFF::TIFF
	#OpenSSL::SSL
	)

if(Qt5LinguistTools_FOUND)
	qt5_add_translation(gui-lib-qms translations/zh_CN.ts)
	add_custom_target(gui-lib-qms DEPENDS ${gui-lib-qms})
	add_dependencies(gui-lib gui-lib-qms)
	install(FILES ${gui-lib-qms}
		DESTINATION "${CMAKE_INSTALL_DATADIR}/gapr/translations"
		COMPONENT gui
		)
endif()

