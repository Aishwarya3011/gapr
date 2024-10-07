#!/bin/bash

set -e
set -x

if [ ! -d meson-info ]; then
	echo "Run this in a configured meson build directory." >&2
	exit -1
fi

build_package() {
	dist_type=$1
	meson configure -Dinstall-type=dist-$dist_type .

	proj_info=$(meson introspect --projectinfo | sed 's,"subprojects":.*,,')
	proj_name=$(echo "$proj_info" | sed 's,.*"descriptive_name": "\([a-zA-Z0-9\.]*\)".*,\1,')
	proj_ver=$(echo "$proj_info" | sed 's,.*"version": "\([a-zA-Z0-9\.]*\)".*,\1,')
	pkg_name="$proj_name-$proj_ver"
	package_root="$(pwd)/packaging/package-ROOT"
	pkg_file="$(pwd)/packaging/$pkg_name-Windows-$dist_type.exe"

	meson compile
	rm -rf "$package_root"
	DESTDIR="$package_root" meson install --no-rebuild

	rm -f "$pkg_file"
	( cd "$(pwd)/packaging"; makensis -INPUTCHARSET UTF8 installer.nsi; )
}

# merge scripts
case x"$1" in
	xlite|xfull)
		build_package "$1"
		;;
	x)
		build_package lite
		build_package full
		;;
esac

exit 0

