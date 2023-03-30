#include "sql_proc.h"
#include <QSqlQuery>

using namespace PJ;

SQLProc::SQLProc(QSqlQuery* q)
{
    _model = q;

}

SQLProc::~SQLProc()
{

}

void SQLProc::loop()
{
    _running = true;
    _model = new QSqlQuery;
    while(_running){
        processData();
    }
}
/*
void SQLProc::processData()
{
  if (!_model->next() || !_running) return;

  if (_row > _previousRowCount)
  {
    return;
  }

  QDateTime utcDateTime = _model->value(1).toDateTime();
  double timestamp = utcDateTime.toMSecsSinceEpoch() / 1000.0;

  if (!utcDateTime.isValid())
  {
    _row++;
    return;
  }

  double actualValue = _model->value(2).toDouble();
  _row++;

  try
  {
    std::lock_guard<std::mutex> lock(mutex());
    int pointId = _model->value(0).toInt();
    auto& plot = dataMap().numeric.find(_pointIdToNameMap[pointId])->second;
    plot.pushBack(PlotData::Point(timestamp, actualValue));
    emit dataReceived();

  }
  catch (std::exception& err)
  {
    QMessageBox::warning(nullptr, tr("SQL Server"),
                         tr("Problem parsing the message. SQL Server will be "
                            "stopped.\n%1")
                             .arg(err.what()),
                         QMessageBox::Ok);
    shutdown();
    emit closed();
    return;
  }
}
*/
