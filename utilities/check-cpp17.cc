void f() {
	static_assert(
#ifdef _MSVC_LANG
			_MSVC_LANG
#else
			__cplusplus
#endif
			>=201703L);
}
