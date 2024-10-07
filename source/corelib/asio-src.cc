#include <boost/asio/impl/src.hpp>
#include <boost/asio/ssl/impl/src.hpp>

#include "gapr/detail/template-class-static-data.hh"

void* template_class_static_data_1() {
	return &gapr::detail::template_class<int>::data;
}
