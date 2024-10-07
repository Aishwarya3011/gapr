#ifdef __cplusplus
#if defined(_WIN32) || defined(__CYGWIN__)
/*! enable ex.running_in_this_thread for Windows */
namespace boost::asio::detail {
	template<typename,typename> class __declspec(dllimport) call_stack;
}
namespace gapr::detail {
	template<typename> class __declspec(dllimport) template_class;
}
#endif
#endif
