/* gui/program.hh
 *
 * Copyright (C) 2017 GOU Lingfeng
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


#ifndef _GAPR_INCLUDE_GUI_PROGRAM_HH_
#define _GAPR_INCLUDE_GUI_PROGRAM_HH_


#include "gapr/program.hh"

class QMainWindow;


namespace gapr {

	class GuiProgram: public Program {
		public:
			~GuiProgram() override { }

			virtual QMainWindow* window() =0;

		protected:
			GuiProgram(const char* n): Program{n} { }

		private:
			int run() override final;
	};

	int gui_launch();
	int gui_launch(gapr::Program* factory, int argc, char* argv[]);
}

#endif
