configure_file("config.hh.in" "config.hh" @ONLY)
configure_file("corelib/gapr/version.hh.in" "corelib/gapr/version.hh" @ONLY)

configure_file("dir.in" "dir" @ONLY)
add_subdirectory(corelib)
if(ENABLE_GUI)
	add_subdirectory(guilib)
endif()

#add_executable(cli-prog "main.cc")
#set_target_properties(cli-prog PROPERTIES
#RUNTIME_OUTPUT_NAME "gapr")
#install(TARGETS cli-prog
#RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
#COMPONENT core
#)
#target_link_libraries(cli-prog PRIVATE core-lib)

if(NOT ENABLE_GUI)
	return()
endif()

add_subdirectory(gather)
add_subdirectory(export)
add_subdirectory(trace)

if(ENABLE_GUI)
	add_subdirectory(start)
	add_subdirectory(fix)
	add_subdirectory(proofread)
	add_subdirectory(show)
endif()

# # Other configurations (for config.hh, version.hh, etc.)
# configure_file("config.hh.in" "config.hh" @ONLY)
# configure_file("corelib/gapr/version.hh.in" "corelib/gapr/version.hh" @ONLY)

# # Add core libraries and other subdirectories
# add_subdirectory(corelib)

# if(ENABLE_GUI)
#     add_subdirectory(guilib)
# endif()

# # Create a logger library from logger.cc in the source folder
# add_library(logger STATIC
#     source/logger.cc  
# )

# # Ensure logger.h is accessible by other components
# target_include_directories(logger PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}/source  
# )

# # Add other subdirectories for components
# add_subdirectory(gather)
# add_subdirectory(export)
# add_subdirectory(trace)

# # Add convert executable using files from convert folder
# add_executable(convert
#     convert/main.cc
#     convert/dshelper.cc
#     convert/nethelper.cc
#     convert/savewebm.cc
#     convert/loadtiff.cc
#     convert/tifftiled.cc
#     convert/split.cc  
# )

# # Set properties for the convert executable
# set_target_properties(convert PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ..
#     RUNTIME_OUTPUT_NAME "gapr-convert"
# )

# # Link libraries for convert, including logger
# target_link_libraries(convert PRIVATE
#     core-lib
#     logger  
#     Threads::Threads
# )

# # Install rules for convert
# install(TARGETS convert
#     RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
#     COMPONENT convert
# )

# # Other subdirectories for GUI components
# if(ENABLE_GUI)
#     add_subdirectory(start)
#     add_subdirectory(fix)
#     add_subdirectory(proofread)
#     add_subdirectory(show)
# endif()
