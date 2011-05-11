/*  Copyright (C) 2008 e_k (e_k@users.sourceforge.net)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/


#include <QtGui/QApplication>
#include <QtGui/QMainWindow>
#include <QtCore/QDebug>

#include "qtermwidget.h"

int main(int argc, char *argv[])
{
  QApplication app(argc, argv);
  QMainWindow *mainWindow = new QMainWindow();

  QTermWidget *console = new QTermWidget(0);

  QFont font = QApplication::font();
  font.setFamily("DejaVu sans mono");
  font.setPointSize(9);

  console->setTerminalFont(font);
  console->setHistorySize(0x00ffffff);

  console->setColorScheme(COLOR_SCHEME_WHITE_ON_BLACK);
  console->setScrollBarPosition(QTermWidget::ScrollBarRight);

  // get shell from environment
  QString Shell(getenv("SHELL"));
  if (!Shell.isEmpty()) console->setShellProgram(Shell);
  console->startShellProgram();

  mainWindow->setCentralWidget(console);
  mainWindow->resize(802, 610);

  QObject::connect(console, SIGNAL(finished()), mainWindow, SLOT(close()));

  mainWindow->setWindowTitle("QTerm");
  mainWindow->show();
  return app.exec();
}


