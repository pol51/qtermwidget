/*
    This file is part of Konsole, an X terminal.

    Copyright (C) 2007 Robert Knight <robertknight@gmail.com>
    Copyright (C) 1997,1998 by Lars Doelle <lars.doelle@on-line.de>
    Copyright (C) 1996 by Matthias Ettrich <ettrich@kde.org>

    Rewritten for QT4 by e_k <e_k at users.sourceforge.net>, Copyright (C)2008

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301  USA.
*/

// Own
#include "Emulation.h"

// System
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Qt
#include <QtCore/QTime>
#include <QtCore/QThread>
#include <QtCore/QHash>
#include <QtCore/QRegExp>
#include <QtCore/QTextStream>
#include <QtGui/QApplication>
#include <QtGui/QClipboard>
#include <QtGui/QKeyEvent>

// Konsole
#include "KeyboardTranslator.h"
#include "Screen.h"
#include "TerminalCharacterDecoder.h"
#include "ScreenWindow.h"

using namespace Konsole;

/* ------------------------------------------------------------------------- */
/*                                                                           */
/*                               Emulation                                  */
/*                                                                           */
/* ------------------------------------------------------------------------- */

//#define CNTL(c) ((c)-'@')

/*!
*/

Emulation::Emulation() :
  _currentScreen(0),
  _codec(0),
  _decoder(0),
  _keyTranslator(0),
  _usesMouse(false)
{

  // create screens with a default size
  _screen[0] = new Screen(40,80);
  _screen[1] = new Screen(40,80);
  _currentScreen = _screen[0];

  QObject::connect(&_bulkTimer1, SIGNAL(timeout()), this, SLOT(showBulk()) );
  QObject::connect(&_bulkTimer2, SIGNAL(timeout()), this, SLOT(showBulk()) );

  // listen for mouse status changes
  connect( this , SIGNAL(programUsesMouseChanged(bool)) ,
           SLOT(usesMouseChanged(bool)) );
}

bool Emulation::programUsesMouse() const
{
    return _usesMouse;
}

void Emulation::usesMouseChanged(bool usesMouse)
{
    _usesMouse = usesMouse;
}

ScreenWindow* Emulation::createWindow()
{
    ScreenWindow* window = new ScreenWindow();
    window->setScreen(_currentScreen);
    _windows << window;

    connect(window , SIGNAL(selectionChanged()),
            this , SLOT(bufferedUpdate()));

    connect(this , SIGNAL(outputChanged()),
            window , SLOT(notifyOutputChanged()) );
    return window;
}

/*!
*/

Emulation::~Emulation()
{
  QListIterator<ScreenWindow*> windowIter(_windows);

  while (windowIter.hasNext())
  {
    delete windowIter.next();
  }

  delete _screen[0];
  delete _screen[1];
  delete _decoder;
}

/*! change between primary and alternate _screen
*/

void Emulation::setScreen(int n)
{
  Screen *old = _currentScreen;
  _currentScreen = _screen[n&1];
  if (_currentScreen != old)
  {
     old->setBusySelecting(false);

     // tell all windows onto this emulation to switch to the newly active _screen
     QListIterator<ScreenWindow*> windowIter(_windows);
     while ( windowIter.hasNext() )
     {
         windowIter.next()->setScreen(_currentScreen);
     }
  }
}

void Emulation::clearHistory()
{
    _screen[0]->setScroll( _screen[0]->getScroll() , false );
}
void Emulation::setHistory(const HistoryType& t)
{
  _screen[0]->setScroll(t);

  showBulk();
}

const HistoryType& Emulation::history()
{
  return _screen[0]->getScroll();
}

void Emulation::setCodec(const QTextCodec * qtc)
{
  Q_ASSERT( qtc );

  _codec = qtc;
  delete _decoder;
  _decoder = _codec->makeDecoder();

  emit useUtf8Request(utf8());
}

void Emulation::setCodec(EmulationCodec codec)
{
    if ( codec == Utf8Codec )
        setCodec( QTextCodec::codecForName("utf8") );
    else if ( codec == LocaleCodec )
        setCodec( QTextCodec::codecForLocale() );
}

void Emulation::setKeyBindings(const QString& name)
{
  _keyTranslator = KeyboardTranslatorManager::instance()->findTranslator(name);
}

QString Emulation::keyBindings()
{
  return _keyTranslator->name();
}


// Interpreting Codes ---------------------------------------------------------

/*
   This section deals with decoding the incoming character stream.
   Decoding means here, that the stream is first separated into `tokens'
   which are then mapped to a `meaning' provided as operations by the
   `Screen' class.
*/

/*!
*/

void Emulation::receiveChar(int c)
// process application unicode input to terminal
// this is a trivial scanner
{
  c &= 0xff;
  switch (c)
  {
    case '\b'      : _currentScreen->BackSpace();                 break;
    case '\t'      : _currentScreen->Tabulate();                  break;
    case '\n'      : _currentScreen->NewLine();                   break;
    case '\r'      : _currentScreen->Return();                    break;
    case 0x07      : emit stateSet(NOTIFYBELL);
                     break;
    default        : _currentScreen->ShowCharacter(c);            break;
  };
}

/* ------------------------------------------------------------------------- */
/*                                                                           */
/*                             Keyboard Handling                             */
/*                                                                           */
/* ------------------------------------------------------------------------- */

/*!
*/

void Emulation::sendKeyEvent( QKeyEvent* ev )
{
  emit stateSet(NOTIFYNORMAL);

  if (!ev->text().isEmpty())
  { // A block of text
    // Note that the text is proper unicode.
    // We should do a conversion here, but since this
    // routine will never be used, we simply emit plain ascii.
    //emit sendBlock(ev->text().toAscii(),ev->text().length());
    emit sendData(ev->text().toUtf8(),ev->text().length());
  }
}

void Emulation::sendString(const char*,int)
{
    // default implementation does nothing
}

void Emulation::sendMouseEvent(int /*buttons*/, int /*column*/, int /*row*/, int /*eventType*/)
{
    // default implementation does nothing
}

// Unblocking, Byte to Unicode translation --------------------------------- --

/*
   We are doing code conversion from locale to unicode first.
TODO: Character composition from the old code.  See #96536
*/

void Emulation::receiveData(const char* text, int length)
{
  emit stateSet(NOTIFYACTIVITY);

  bufferedUpdate();

    QString unicodeText = _decoder->toUnicode(text,length);

  //send characters to terminal emulator
  for (int i=0;i<unicodeText.length();i++)
  {
    receiveChar(unicodeText[i].unicode());
  }

  //look for z-modem indicator
  //-- someone who understands more about z-modems that I do may be able to move
  //this check into the above for loop?
  for (int i=0;i<length;i++)
  {
    if (text[i] == '\030')
        {
            if ((length-i-1 > 3) && (strncmp(text+i+1, "B00", 3) == 0))
              emit zmodemDetected();
        }
  }
}

//OLDER VERSION
//This version of onRcvBlock was commented out because
//	a)  It decoded incoming characters one-by-one, which is slow in the current version of Qt (4.2 tech preview)
//	b)  It messed up decoding of non-ASCII characters, with the result that (for example) chinese characters
//	    were not printed properly.
//
//There is something about stopping the _decoder if "we get a control code halfway a multi-byte sequence" (see below)
//which hasn't been ported into the newer function (above).  Hopefully someone who understands this better
//can find an alternative way of handling the check.


/*void Emulation::onRcvBlock(const char *s, int len)
{
  emit notifySessionState(NOTIFYACTIVITY);

  bufferedUpdate();
  for (int i = 0; i < len; i++)
  {

    QString result = _decoder->toUnicode(&s[i],1);
    int reslen = result.length();

    // If we get a control code halfway a multi-byte sequence
    // we flush the _decoder and continue with the control code.
    if ((s[i] < 32) && (s[i] > 0))
    {
       // Flush _decoder
       while(!result.length())
          result = _decoder->toUnicode(&s[i],1);
       reslen = 1;
       result.resize(reslen);
       result[0] = QChar(s[i]);
    }

    for (int j = 0; j < reslen; j++)
    {
      if (result[j].characterategory() == QChar::Mark_NonSpacing)
         _currentScreen->compose(result.mid(j,1));
      else
         onRcvChar(result[j].unicode());
    }
    if (s[i] == '\030')
    {
      if ((len-i-1 > 3) && (strncmp(s+i+1, "B00", 3) == 0))
        emit zmodemDetected();
    }
  }
}*/

// Selection --------------------------------------------------------------- --

#if 0
void Emulation::onSelectionBegin(const int x, const int y, const bool columnmode) {
  if (!connected) return;
  _currentScreen->setSelectionStart( x,y,columnmode);
  showBulk();
}

void Emulation::onSelectionExtend(const int x, const int y) {
  if (!connected) return;
  _currentScreen->setSelectionEnd(x,y);
  showBulk();
}

void Emulation::setSelection(const bool preserve_line_breaks) {
  if (!connected) return;
  QString t = _currentScreen->selectedText(preserve_line_breaks);
  if (!t.isNull())
  {
    QListIterator< TerminalDisplay* > viewIter(_views);

    while (viewIter.hasNext())
        viewIter.next()->setSelection(t);
  }
}

void Emulation::testIsSelected(const int x, const int y, bool &selected)
{
  if (!connected) return;
  selected=_currentScreen->isSelected(x,y);
}

void Emulation::clearSelection() {
  if (!connected) return;
  _currentScreen->clearSelection();
  showBulk();
}

#endif

void Emulation::writeToStream( TerminalCharacterDecoder* _decoder ,
                               int startLine ,
                               int endLine)
{
  _currentScreen->writeToStream(_decoder,startLine,endLine);
}

int Emulation::lineCount()
{
    // sum number of lines currently on _screen plus number of lines in history
    return _currentScreen->getLines() + _currentScreen->getHistLines();
}

// Refreshing -------------------------------------------------------------- --

#define BULK_TIMEOUT1 10
#define BULK_TIMEOUT2 40

/*!
*/
void Emulation::showBulk()
{
    _bulkTimer1.stop();
    _bulkTimer2.stop();

    emit outputChanged();

    _currentScreen->resetScrolledLines();
    _currentScreen->resetDroppedLines();
}

void Emulation::bufferedUpdate()
{
   _bulkTimer1.setSingleShot(true);
   _bulkTimer1.start(BULK_TIMEOUT1);
   if (!_bulkTimer2.isActive())
   {
      _bulkTimer2.setSingleShot(true);
      _bulkTimer2.start(BULK_TIMEOUT2);
   }
}

char Emulation::getErase() const
{
  return '\b';
}

void Emulation::setImageSize(int lines, int columns)
{
  //kDebug() << "Resizing image to: " << lines << "by" << columns << QTime::currentTime().msec();
  Q_ASSERT( lines > 0 );
  Q_ASSERT( columns > 0 );

  _screen[0]->resizeImage(lines,columns);
  _screen[1]->resizeImage(lines,columns);

  emit imageSizeChanged(lines,columns);

  bufferedUpdate();
}

QSize Emulation::imageSize()
{
  return QSize(_currentScreen->getColumns(), _currentScreen->getLines());
}

ushort ExtendedCharTable::extendedCharHash(ushort* unicodePoints , ushort length) const
{
    ushort hash = 0;
    for ( ushort i = 0 ; i < length ; i++ )
    {
        hash = 31*hash + unicodePoints[i];
    }
    return hash;
}
bool ExtendedCharTable::extendedCharMatch(ushort hash , ushort* unicodePoints , ushort length) const
{
    ushort* entry = extendedCharTable[hash];

    // compare given length with stored sequence length ( given as the first ushort in the
    // stored buffer )
    if ( entry == 0 || entry[0] != length )
       return false;
    // if the lengths match, each character must be checked.  the stored buffer starts at
    // entry[1]
    for ( int i = 0 ; i < length ; i++ )
    {
        if ( entry[i+1] != unicodePoints[i] )
           return false;
    }
    return true;
}
ushort ExtendedCharTable::createExtendedChar(ushort* unicodePoints , ushort length)
{
    // look for this sequence of points in the table
    ushort hash = extendedCharHash(unicodePoints,length);

    // check existing entry for match
    while ( extendedCharTable.contains(hash) )
    {
        if ( extendedCharMatch(hash,unicodePoints,length) )
        {
            // this sequence already has an entry in the table,
            // return its hash
            return hash;
        }
        else
        {
            // if hash is already used by another, different sequence of unicode character
            // points then try next hash
            hash++;
        }
    }


     // add the new sequence to the table and
     // return that index
    ushort* buffer = new ushort[length+1];
    buffer[0] = length;
    for ( int i = 0 ; i < length ; i++ )
       buffer[i+1] = unicodePoints[i];

    extendedCharTable.insert(hash,buffer);

    return hash;
}

ushort* ExtendedCharTable::lookupExtendedChar(ushort hash , ushort& length) const
{
    // lookup index in table and if found, set the length
    // argument and return a pointer to the character sequence

    ushort* buffer = extendedCharTable[hash];
    if ( buffer )
    {
        length = buffer[0];
        return buffer+1;
    }
    else
    {
        length = 0;
        return 0;
    }
}

ExtendedCharTable::ExtendedCharTable()
{
}
ExtendedCharTable::~ExtendedCharTable()
{
    // free all allocated character buffers
    QHashIterator<ushort,ushort*> iter(extendedCharTable);
    while ( iter.hasNext() )
    {
        iter.next();
        delete[] iter.value();
    }
}

// global instance
ExtendedCharTable ExtendedCharTable::instance;


//#include "moc_Emulation.cpp"

