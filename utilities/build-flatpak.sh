#!/bin/bash

set -e
set -x

if [ ! -d meson-info ]; then
	echo "Run this in a configured meson build directory." >&2
	exit -1
fi

sdk_ver=23.08

build_package() {
	dist_type=$1
	cmd_start=$2
	meson configure -Dinstall-type=dist-$dist_type .

	proj_info=$(meson introspect --projectinfo | sed 's,"subprojects":.*,,')
	proj_name=$(echo "$proj_info" | sed 's,.*"descriptive_name": "\([a-zA-Z0-9\.]*\)".*,\1,')
	proj_ver=$(echo "$proj_info" | sed 's,.*"version": "\([a-zA-Z0-9\.]*\)".*,\1,')
	pkg_name="$proj_name-$proj_ver"
	package_root="$(pwd)/packaging/package-ROOT"
	repo_dir="$(pwd)/packaging/flatpak-repo"
	pkg_file="$(pwd)/packaging/$pkg_name-Linux-$dist_type.flatpak"

	ninja
	rm -rf "$package_root"
	flatpak build-init --arch=x86_64 --type=app "$package_root" cn.org.yanlab.Gapr org.freedesktop.Sdk org.freedesktop.Platform $sdk_ver
	DESTDIR="$package_root/files" meson install --no-rebuild
	flatpak build-finish --command=$cmd_start --share=network --share=ipc --socket=x11 --socket=wayland --socket=pulseaudio --device=dri --env=LD_LIBRARY_PATH=/app/lib "$package_root"
	rm -rf "$repo_dir"
	flatpak build-export "$repo_dir" "$package_root"

	rm -f "$pkg_file"
	flatpak build-bundle "$repo_dir" "$pkg_file" cn.org.yanlab.Gapr \
		--runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo
}

build_package lite gapr-proofread
build_package full gapr

exit 0
