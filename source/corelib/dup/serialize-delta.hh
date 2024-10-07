namespace {

	inline bool save(const gapr::commit_history& delta, std::streambuf& str) {
		return delta.save(str);
	}
	template<typename T> static gapr::mutable_mem_file serialize(T& delta) {
		gapr::mutable_mem_file file{true};
		std::ostringstream oss;
		if(!save(delta, *oss.rdbuf()))
			gapr::report("failed to save delta");
		if(0) {
			std::ofstream fs{"/tmp/gapr.txt"};
			if(!save(delta, *fs.rdbuf()))
				gapr::report("failed to save delta");
		}
		auto str=oss.str();
		std::size_t i=0;
		while(i<str.size()) {
			auto buf=file.map_tail();
			auto n=str.size()-i;
			if(n>buf.size())
				n=buf.size();
			std::copy(&str[i], &str[i+n], buf.data());
			i+=n;
			file.add_tail(n);
		}
		return file;
	}

}
