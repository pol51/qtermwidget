TEMPLATE	= app
DESTDIR 	= ..

CONFIG		+= qt debug_and_release warn_on build_all

QT += core gui

MOC_DIR 	= ../.moc

CONFIG(debug, debug|release) {
    OBJECTS_DIR = ../.objs_d
    TARGET 	= qterm
    LIBS 	+= -L.. ../libqtermwidget_d.a
} else {
    OBJECTS_DIR = ../.objs
    TARGET 	= qterm
    LIBS 	+= -L.. ../libqtermwidget.a
}

SOURCES 	= main.cpp

INCLUDEPATH 	= ../lib

#LIBS 		+= -L.. -lqtermwidget


