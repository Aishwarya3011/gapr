/* trace/evaluator.hh
 *
 * Copyright (C) 2020 GOU Lingfeng
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <memory>
#include <valarray>
#include <functional>
#include <filesystem>

#include "gapr/cube.hh"

class Evaluator {
	public:
		virtual ~Evaluator() { }
		Evaluator(const Evaluator&) =delete;
		Evaluator& operator=(const Evaluator&) =delete;
		static std::shared_ptr<Evaluator> create();
		static std::shared_ptr<Evaluator> create_stage1();
		static std::shared_ptr<Evaluator> create_stage2();

		using BatchGenerator=std::function<std::valarray<float>()>;
		virtual void train(BatchGenerator gen, const std::filesystem::path& fn_params) =0;
		virtual std::valarray<float> predict(const std::valarray<float>& input) =0;

		virtual void load(std::istream& str) =0;

	protected:
		constexpr Evaluator() noexcept { }
	private:
};

class Detector {
	public:
		virtual ~Detector() { }
		Detector(const Detector&) =delete;
		Detector& operator=(const Detector&) =delete;
		static std::shared_ptr<Detector> create();

		virtual std::valarray<uint16_t> predict(gapr::cube_view<const void> input) =0;

		virtual void load(std::istream& str) =0;

		struct Batch {
			std::vector<float> input;
			std::vector<float> target;
			unsigned int n_bat;
		};
		using BatchGenerator=std::function<Batch()>;
		virtual void train(unsigned int seed, BatchGenerator gen, const std::filesystem::path& fn_params) =0;

	protected:
		constexpr Detector() noexcept { }
	private:
};

constexpr unsigned int prober_wh=192+1;
constexpr unsigned int prober_d=48-1;
//constexpr std::array<unsigned int, 3> sliding_cube_sizes{256+1, 64-1};
constexpr unsigned int prober_d2=2*prober_d+1;
