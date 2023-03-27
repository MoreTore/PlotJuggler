/*DataStreamServer PlotJuggler  Plugin license(Faircode)

Copyright(C) 2018 Philippe Gauthier - ISIR - UPMC
Permission is hereby granted to any person obtaining a copy of this software and
associated documentation files(the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and / or sell copies("Use") of the Software, and to permit persons
to whom the Software is furnished to do so. The above copyright notice and this permission
notice shall be included in all copies or substantial portions of the Software. THE
SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#pragma once

#include <QUdpSocket>
#include <QtPlugin>
#include <thread>
#include "PlotJuggler/datastreamer_base.h"
#include "PlotJuggler/messageparser_base.h"
#include <QSqlTableModel>
#include <QTimer>


using namespace PJ;

struct ColumnSelection {
    QString nameColumn;
    QString utcdatetimeColumn;
    QString valueColumn;
};

class SQLServer : public PJ::DataStreamer
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "facontidavide.PlotJuggler3.DataStreamer")
  Q_INTERFACES(PJ::DataStreamer)

public:
  SQLServer();

  virtual bool start(QStringList*) override;

  virtual void shutdown() override;

  virtual bool isRunning() const override
  {
    return _running;
  }

  virtual ~SQLServer() override;

  virtual const char* name() const override
  {
    return "SQL Streamer";
  }

  virtual bool isDebugPlugin() override
  {
    return false;
  }
  

  signals:
    void tableUpdated();

private:
  int _limit;
  int _offset;
  QSqlDatabase _db;
  std::thread _thread;
  QString selectTable();
  QString selectDatabase();
  ColumnSelection selectColumns(const QStringList &availableColumns);
  bool _running;
  QSqlTableModel* _model;
  PJ::MessageParserPtr _parser;
  QTimer _checkNewRowsTimer; // Add this new QTimer
  int _previousRowCount;
  int _row = 0;
  //std::map<std::string, Parameters> _parameters;

private slots:

  void processData();
  void loop();
  void checkForNewRows();
  
};
