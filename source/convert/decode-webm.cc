#include "gapr/cube.hh"
#include "gapr/streambuf.hh"
#include "gapr/cube-loader.hh"
#include "gapr/utility.hh"
#include "gapr/detail/nrrd-output.hh"

#include <fstream>

void convert_to_nrrd(const char* input, const char* output) {
	auto sb=gapr::make_streambuf(input);
	auto imageReader=gapr::make_cube_loader(input, static_cast<gapr::Streambuf&>(*sb));
	if(!imageReader)
		gapr::report("Cannot read file: ", input);

	gapr::cube_type type=imageReader->type();
	switch(type) {
	case gapr::cube_type::u8:
	case gapr::cube_type::u16:
	case gapr::cube_type::u32:
	case gapr::cube_type::i8:
	case gapr::cube_type::i16:
	case gapr::cube_type::i32:
	case gapr::cube_type::f32:
		break;
	default:
		gapr::report("Voxel type not supported");
	}
	auto [w, h, d]=imageReader->sizes();

	gapr::mutable_cube cube{type, {(unsigned int)w, (unsigned int)h, (unsigned int)d}};
	auto cube_view=cube.view<char>();
	imageReader->load(cube_view.row(0, 0), cube_view.ystride(), cube_view.zstride());

	std::ofstream fs{output, std::ios_base::binary};
	if(!fs)
		throw std::runtime_error{"Cannot open file"};
	gapr::nrrd_output nrrd{fs};
	nrrd.header();
	nrrd.finish(std::move(cube));
	fs.close();
	if(!fs)
		throw std::runtime_error{"Failed to close file."};
}
