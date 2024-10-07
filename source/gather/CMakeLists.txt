add_executable(gather
	"group.cc"
	"main.cc"
	"model.cc"
	"passwd.cc"
	"server.cc"
	"logger_gather.cc"
	)

set_target_properties(gather PROPERTIES
	#PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY ..
	RUNTIME_OUTPUT_NAME "gapr-gather"
	)

target_link_libraries(gather PRIVATE
	core-lib
	Threads::Threads
	)
if(NOT HAVE_STD_FILESYSTEM)
	target_compile_definitions(gather PRIVATE STD_FILESYSTEM_MISSING)
	target_link_libraries(gather PRIVATE Boost::filesystem)
endif()
if(NOT HAVE_STD_CHARCONV)
	target_compile_definitions(gather PRIVATE STD_CHARCONV_MISSING)
endif()

install(TARGETS gather
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT gather
	)
install(FILES
	"docroot/monitor.html"
	"docroot/rank.html"
	"docroot/progress.html"
	"docroot/register.html"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/gapr/docroot"
	COMPONENT gather
	)
install(FILES
	"docroot/css/bootstrap.min.css"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/gapr/docroot/css"
	COMPONENT gather
	)
install(FILES
	"docroot/js/jquery-3.2.1.slim.min.js"
	"docroot/js/bootstrap.min.js"
	"docroot/js/popper.min.js"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/gapr/docroot/js"
	COMPONENT gather
	)

if(WIN32)
	target_link_libraries(gather PRIVATE core-ver)
endif()
if(MSVC)
	target_link_libraries(gather PRIVATE "${GETOPT_LIBRARY}")
endif()

add_executable(upgrade-repo "helpers/upgrade-repo.cc")
target_link_libraries(upgrade-repo PRIVATE core-lib)
set_target_properties(upgrade-repo PROPERTIES
	RUNTIME_OUTPUT_NAME "gapr-upgrade-repo"
	)
install(TARGETS upgrade-repo
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	COMPONENT gather
	)

# add_executable(gather
# 	"group.cc"
# 	"main.cc"
# 	"model.cc"
# 	"passwd.cc"
# 	"server.cc"
# )

# set_target_properties(gather PROPERTIES
# 	RUNTIME_OUTPUT_DIRECTORY ..
# 	RUNTIME_OUTPUT_NAME "gapr-gather"
# )

# # Link the core library, logger, and threading library
# target_link_libraries(gather PRIVATE
# 	core-lib
# 	logger  # Link the logger library
# 	Threads::Threads
# )

# # Optional: Filesystem and Charconv handling
# if(NOT HAVE_STD_FILESYSTEM)
# 	target_compile_definitions(gather PRIVATE STD_FILESYSTEM_MISSING)
# 	target_link_libraries(gather PRIVATE Boost::filesystem)
# endif()
# if(NOT HAVE_STD_CHARCONV)
# 	target_compile_definitions(gather PRIVATE STD_CHARCONV_MISSING)
# endif()

# # Installation for gather
# install(TARGETS gather
# 	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
# 	COMPONENT gather
# )

# # Install necessary HTML, CSS, and JS files
# install(FILES
# 	"docroot/monitor.html"
# 	"docroot/rank.html"
# 	"docroot/progress.html"
# 	"docroot/register.html"
# 	DESTINATION "${CMAKE_INSTALL_DATADIR}/gapr/docroot"
# 	COMPONENT gather
# )
# install(FILES
# 	"docroot/css/bootstrap.min.css"
# 	DESTINATION "${CMAKE_INSTALL_DATADIR}/gapr/docroot/css"
# 	COMPONENT gather
# )
# install(FILES
# 	"docroot/js/jquery-3.2.1.slim.min.js"
# 	"docroot/js/bootstrap.min.js"
# 	"docroot/js/popper.min.js"
# 	DESTINATION "${CMAKE_INSTALL_DATADIR}/gapr/docroot/js"
# 	COMPONENT gather
# )

# # Platform-specific linking
# if(WIN32)
# 	target_link_libraries(gather PRIVATE core-ver)
# endif()
# if(MSVC)
# 	target_link_libraries(gather PRIVATE "${GETOPT_LIBRARY}")
# endif()

# # Additional executable for upgrade-repo
# add_executable(upgrade-repo "helpers/upgrade-repo.cc")
# target_link_libraries(upgrade-repo PRIVATE core-lib logger)  # Link logger here as well
# set_target_properties(upgrade-repo PROPERTIES
# 	RUNTIME_OUTPUT_NAME "gapr-upgrade-repo"
# )
# install(TARGETS upgrade-repo
# 	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
# 	COMPONENT gather
# )
