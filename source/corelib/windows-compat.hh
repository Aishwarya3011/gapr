#ifndef _GAPR_WINDOWS_COMPAT_
#define _GAPR_WINDOWS_COMPAT_

#if defined(__WIN32__)
#include <io.h>
[[maybe_unused]] static inline auto fdatasync=::_commit;
#elif defined(__APPLE__)
#include <unistd.h>
[[maybe_unused]] static inline auto fdatasync=::fsync;
#endif

#ifdef __WIN32__

#include <array>
#include <cassert>
#include <wchar.h>
static inline FILE* fopen(const wchar_t* fn, const char* mode) {
	std::array<wchar_t, 4> wmode;
	unsigned int i=0;
	do {
		assert(i<wmode.size());
		wmode[i]=*(mode++);
	} while(wmode[i++]);
	return ::_wfopen(fn, &wmode[0]);
}

#include <sys/locking.h>
#include <limits>
static inline int lock_fd_impl(int fd, int what) {
	return ::_locking(fd, what, std::numeric_limits<long>::max());
}
static inline int lock_fd(int fd) {
	return lock_fd_impl(fd, _LK_NBLCK);
}
static inline int unlock_fd(int fd) {
	return lock_fd_impl(fd, _LK_UNLCK);
}

#ifdef TIFFLIB_VERSION
static inline TIFF* TIFFOpen(const wchar_t* fn, const char* mode) {
	return ::TIFFOpenW(fn, mode);
}
#endif

#else

#include <unistd.h>

#include <fcntl.h>
static inline int lock_fd_impl(int fd, int type) {
	struct ::flock lck{};
	lck.l_type=type;
	lck.l_whence=SEEK_SET;
	lck.l_start=0;
	lck.l_len=0;
	return ::fcntl(fd, F_SETLK, &lck);
}
static inline int lock_fd(int fd) {
	return lock_fd_impl(fd, F_WRLCK);
}
static inline int unlock_fd(int fd) {
	return lock_fd_impl(fd, F_UNLCK);
}

#endif

#if defined(__APPLE__) || defined(__WIN32__)

#ifdef TIFFLIB_VERSION
static void hint_sequential_read(TIFF* tif) {
	// XXX
}
static void hint_discard_cache(TIFF* tif) {
	// XXX
}
#endif
static void hint_sequential_read(FILE* file) {
	// XXX
}
static void hint_discard_cache(FILE* file) {
	// XXX
}

#else

#ifdef TIFFLIB_VERSION
static void hint_sequential_read(TIFF* tif) {
	auto fd=TIFFFileno(tif);
	::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
}
static void hint_discard_cache(TIFF* tif) {
	auto fd=TIFFFileno(tif);
	::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
}
#endif
[[maybe_unused]] inline static void hint_sequential_read(FILE* file) {
	auto fd=::fileno(file);
	::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
}
[[maybe_unused]] inline static void hint_discard_cache(FILE* file) {
	auto fd=::fileno(file);
	::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
}

#endif

#endif

