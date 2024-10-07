add_executable(export
	"main.cc"
	)

set_target_properties(export PROPERTIES
	#PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY ..
	RUNTIME_OUTPUT_NAME "gapr-export"
	)

target_link_libraries(export PRIVATE
	core-lib
	)

install(TARGETS export
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT export
	)

if(WIN32)
	target_link_libraries(export PRIVATE core-ver)
endif()
if(MSVC)
	target_link_libraries(export PRIVATE "${GETOPT_LIBRARY}")
endif()

