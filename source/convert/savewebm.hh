#ifndef GAPR_CONVERT_SAVEWEBM_HH__
#define GAPR_CONVERT_SAVEWEBM_HH__

#include <vector>
#include <optional>
#include <string>

#include "gapr/mem-file.hh"

void compare(const char* input_fn, const char* output_fn);

struct cube_enc_opts {
	std::optional<unsigned int> cq_level;
	std::optional<int> cpu_used;
	std::optional<unsigned int> threads;
	std::optional<unsigned int> passes;

	std::optional<std::string> filter;
};

template<typename T>
gapr::mem_file convert_webm(T* data, unsigned int w, unsigned int h, unsigned int d, cube_enc_opts opts);

void convert_file(const char* input_fn, const char* output_fn, cube_enc_opts opts, std::vector<char>& buf, std::vector<char>& zerobuf);
void convert_to_nrrd(const char* input_fn, const char* output_fn);

#endif
