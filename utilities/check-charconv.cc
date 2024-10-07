#include <charconv>
#include <cstdio>
#include <cstring>
#include <array>

int main(int argc, char* argv[]) {
	std::array<char, 20> buf;
	int v;
	std::from_chars(argv[1], argv[1]+std::strlen(argv[1]), v, 10);
	auto [eptr, ec]=std::to_chars(buf.begin(), buf.end(), v+10, 10);
	0[eptr++]='\n';
	std::fwrite(&buf[0], 1, eptr-&buf[0], stderr);
	return 0;
}
