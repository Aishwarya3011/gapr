#ifndef _GAPR_JAVA_HELPER_HH_
#define _GAPR_JAVA_HELPER_HH_

class JstringHelper {
	public:
		JstringHelper(JNIEnv* env, jstring str): _env{env}, _str{str} {
			_strl=env->GetStringUTFLength(str);
			_strp=env->GetStringUTFChars(str, nullptr);
		}
		~JstringHelper() {
			_env->ReleaseStringUTFChars(_str, _strp);
		}
		JstringHelper(const JstringHelper&) =delete;
		JstringHelper& operator=(const JstringHelper&) =delete;

		std::string_view view() const noexcept {
			return {_strp, _strl};
		}

	private:
		JNIEnv* _env;
		jstring _str;
		const char* _strp;
		std::size_t _strl;
};

namespace {
	template<typename T> struct JavaArrayImpl;
	template<> struct JavaArrayImpl<int32_t> {
		using java_type=jintArray;
		constexpr const static auto get_elems=&JNIEnv::GetIntArrayElements;
		constexpr const static auto rel_elems=&JNIEnv::ReleaseIntArrayElements;
	};
}
template<typename T> class JarrayHelper {
	using Impl=JavaArrayImpl<T>;
	public:
		JarrayHelper(JNIEnv* env, typename Impl::java_type arr): _env{env}, _arr{arr} {
			_arrl=env->GetArrayLength(arr);
			_arrp=(env->*Impl::get_elems)(arr, nullptr);
		}
		~JarrayHelper() {
			(_env->*Impl::rel_elems)(_arr, _arrp, 0);
		}
		JarrayHelper(const JarrayHelper&) =delete;
		JarrayHelper& operator=(const JarrayHelper&) =delete;

		const T* data() const noexcept { return _arrp; }
		std::size_t size() const noexcept { return _arrl; }

	private:
		JNIEnv* _env;
		typename Impl::java_type _arr;
		T* _arrp;
		std::size_t _arrl;
};

#endif
