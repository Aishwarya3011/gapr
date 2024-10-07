#ifndef _GAPR_INCLUDE_FIX_ERROR_CODE_HH_
#define _GAPR_INCLUDE_FIX_ERROR_CODE_HH_

template<typename ErrorCode>
std::error_code to_std_error_code(ErrorCode ec) noexcept {
	// only need if macos+libstdcxx
	return ec;
}

#endif
