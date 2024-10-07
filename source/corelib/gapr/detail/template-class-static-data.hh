#ifndef __GAPR_DETAIL_TEMPLATE_CLASS_STATIC_DATA_
#define __GAPR_DETAIL_TEMPLATE_CLASS_STATIC_DATA_

#include "gapr/config.hh"

namespace gapr { namespace detail {
	template<typename T> class BOOST_ASIO_DECL template_class {
		public:
			static T data;
	};
	template<typename T> T template_class<T>::data;
} }

BOOST_ASIO_DECL void* template_class_static_data_1();
GAPR_CORE_DECL void* template_class_static_data_2();
void* template_class_static_data_3();

#endif
