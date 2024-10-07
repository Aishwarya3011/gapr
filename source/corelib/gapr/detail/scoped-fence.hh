namespace gapr {
	template<std::memory_order leave> class scoped_fence {
		public:
			explicit scoped_fence(std::memory_order enter) noexcept {
				std::atomic_thread_fence(enter);
			}
			~scoped_fence() {
				std::atomic_thread_fence(leave);
			}
			scoped_fence(const scoped_fence&) =delete;
			scoped_fence& operator=(const scoped_fence&) =delete;
	};
}
