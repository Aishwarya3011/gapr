namespace {

	template<typename T>
		static bool save_impl(const T& d, std::streambuf& str) {
			gapr::Serializer<T> ser{};
			char buf[4096];
			do {
				auto r=ser.save(d, buf, sizeof(buf)-20);
				//gapr::print("save: ", static_cast<bool>(ser), ' ', r);
				auto n=str.sputn(buf, r);
				if(n<0 || static_cast<std::size_t>(n)!=r)
					gapr::report("failed to write");
			} while(ser);
			return true;
		}
	template<typename T>
		static bool load_impl(T& d, std::streambuf& str) {
			gapr::Deserializer<T> deser{};
			bool eof{false};
			char buf[4096];
			do {
				auto n=str.sgetn(buf, sizeof(buf));
				if(n!=sizeof(buf)) {
					eof=true;
				}
				auto r=deser.load(d, buf, n);
				n=r-n;
				//gapr::print("deser: ", n, '/', r);
				if(n!=0) {
					//gapr::print("seek: ", n);
					auto pos=str.pubseekoff(n, std::ios::cur, std::ios::in);
					if(pos==-1)
						//gapr::report("failed to seekg");
						throw std::logic_error{"adsf"};
				}
			} while(!eof && deser);
			//gapr::print("load: ", eof, static_cast<bool>(deser));
			return true;
			// XXX
			return !static_cast<bool>(deser);
		}

}
