/* fix/canvas.hh
 *
 * Copyright (C) 2019-2021 GOU Lingfeng
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

#ifndef _GAPR_FIX_CANVAS_HH_
#define _GAPR_FIX_CANVAS_HH_

#include <memory>

#include <QOpenGLWidget>

namespace gapr::fix {

	class Session;

	/*! use QOpenGLWidget
	 * QOpenGLWindow: no widget support, no shortcut support
	 * embedded QOpenGLWindow: many limitations, not portable
	 * QVulkanWindow: too complicated
	 */
	class Canvas final: public QOpenGLWidget { Q_OBJECT
		public:
			explicit Canvas(QWidget* parent=nullptr);
			void bind(Session* session);
		private:
			~Canvas() override;


			//get colors...;

			void initializeGL() override;
			void paintGL() override;
			void resizeGL(int w, int h) override;
			void wheelEvent(QWheelEvent* event) override;
			void mousePressEvent(QMouseEvent* event) override;
			void mouseReleaseEvent(QMouseEvent* event) override;
			void mouseMoveEvent(QMouseEvent* event) override;
			Q_SLOT void handle_screen_changed(QScreen* screen);

			/*const*/ std::shared_ptr<Session> _session;
	};

}

#endif
