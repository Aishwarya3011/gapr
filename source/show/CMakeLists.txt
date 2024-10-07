find_package(PkgConfig REQUIRED)
pkg_check_modules(gtk REQUIRED IMPORTED_TARGET gtk+-3.0 epoxy)

add_executable(show
	"main.cc"
	"window.cc"
	"${CMAKE_CURRENT_BINARY_DIR}/gresource.cc"
	)
set_target_properties(show PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ..
	RUNTIME_OUTPUT_NAME "gapr-show"
	)
target_link_libraries(show PRIVATE
	core-lib
	Threads::Threads
	PkgConfig::gtk
	)
install(TARGETS show
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT show)

add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gresource.cc"
	COMMAND glib-compile-resources --target "${CMAKE_CURRENT_BINARY_DIR}/gresource.cc" "gresource.xml" --generate-source
	MAIN_DEPENDENCY "gresource.xml"
	DEPENDS "ui/window.ui"
	DEPENDS "ui/menus.ui"
	BYPRODUCTS
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	COMMENT "Compile resources..."
	VERBATIM USES_TERMINAL)

if(WIN32)
	target_link_libraries(show PRIVATE core-ver)
	target_sources(show PRIVATE "../start/icon.rc")
	target_link_libraries(show PRIVATE
		#ws2_32 # for cache.thread.cc::select
		)
endif()
if(MSVC)
	target_link_libraries(show PRIVATE "${GETOPT_LIBRARY}")
endif()

