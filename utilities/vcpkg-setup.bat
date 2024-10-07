:: arg1 is vcpkg path: "vcpkg-root\installed\x64-windows"

set INCLUDE=%INCLUDE%;%~1\include
set LIB=%LIB%;%~1\lib
set CMAKE_PREFIX_PATH=%~1
set BOOST_ROOT=%~1
set Path=%Path%;%~1\tools\glib;%~1\tools\qt5\bin;%~1\bin

