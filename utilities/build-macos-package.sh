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
	dmg_name="$proj_name-$proj_ver"
	package_dir="$proj_name.app"
	package_root="$(pwd)/packaging/package-ROOT"
	pkg_path="$(pwd)/packaging/$dmg_name-macOS-$dist_type.pkg"

	meson compile
	rm -rf "$package_root" "$package_root".prev
	DESTDIR="$package_root/$package_dir" meson install

	rm -f "$pkg_path"
	productbuild --component "$package_root/$package_dir" /Applications "$pkg_path"
	mv "$package_root" "$package_root".prev
}

build_package lite
build_package full

exit 0

