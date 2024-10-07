include(ExternalProject)

set(OPENSSL_VERSION "1.1.1w")
set(OPENSSL_SHA1SUM "76fbf3ca4370e12894a408ef75718f32cdab9671")

find_program(PROG_PERL NAMES perl)
find_program(PROG_MAKE NAMES make gmake)
find_program(PROG_ENV NAMES env)

if(ANDROID)
	set(OPENSSL_PATCH_CMD
		"sed" -i -e "/^ *inherit_from/s/^/shared_extension => \"-priv.so\",shared_extension_simple=>\".so\",/"
		#-e "/which(.clang.*prebuilt/s/^\\s*if/if(1) { } elsif/"
		"<SOURCE_DIR>/Configurations/15-android.conf"
		COMMAND sed -i "/define X509_CERT_DIR/s@OPENSSLDIR.*@\"/etc/security/cacerts\"@" "<SOURCE_DIR>/include/internal/cryptlib.h"
		#COMMAND sed -i "s/X509_NAME_hash(name)/X509_NAME_hash_old(name)/" "<SOURCE_DIR>/crypto/x509/by_dir.c"
	)
endif()

ExternalProject_Add(openssl
	PREFIX "utilities"
	DOWNLOAD_DIR "${CMAKE_CURRENT_LIST_DIR}/../subprojects/packagecache"
	URL "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz"
	URL_HASH "SHA1=${OPENSSL_SHA1SUM}"
	PATCH_COMMAND ${OPENSSL_PATCH_CMD}
	CONFIGURE_COMMAND
	"${PROG_ENV}"
	"ANDROID_NDK_HOME=${ANDROID_NDK}"
	"PATH=${ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG}/bin:$ENV{PATH}"
	"${PROG_PERL}" "<SOURCE_DIR>/Configure" "-D__ANDROID_API__=${ANDROID_NATIVE_API_LEVEL}" shared android-${CMAKE_ANDROID_ARCH}
	BUILD_COMMAND
	"${PROG_ENV}"
	"ANDROID_NDK_HOME=${ANDROID_NDK}"
	"PATH=${ANDROID_NDK}/toolchains/llvm/prebuilt/${ANDROID_HOST_TAG}/bin:$ENV{PATH}"
	"${PROG_MAKE}" "-j"
	BUILD_BYPRODUCTS "<BINARY_DIR>/libcrypto-priv.so" "<BINARY_DIR>/libssl-priv.so"
	INSTALL_COMMAND "${PROG_MAKE}" install_sw "DESTDIR=<INSTALL_DIR>"
	)

if(NOT TARGET OpenSSL::SSL)
	ExternalProject_Get_Property(openssl binary_dir install_dir)
	add_library(openssl-crypto INTERFACE)
	target_include_directories(openssl-crypto SYSTEM INTERFACE
		"${install_dir}/usr/local/include")
	target_link_libraries(openssl-crypto INTERFACE
		"${binary_dir}/libcrypto-priv.so")
	add_dependencies(openssl-crypto openssl)
	add_library(openssl-ssl INTERFACE)
	target_include_directories(openssl-ssl SYSTEM INTERFACE
		"${install_dir}/usr/local/include")
	target_link_libraries(openssl-ssl INTERFACE
		"${binary_dir}/libssl-priv.so" openssl-crypto)
	add_dependencies(openssl-ssl openssl)

	add_library(OpenSSL::SSL ALIAS openssl-ssl)
	add_library(OpenSSL::CRYPTO ALIAS openssl-crypto)

	set(OpenSSL_PREFIX "<INSTALL_DIR>/usr/local")
endif()

