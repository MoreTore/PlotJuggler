#include "dataload_sql.h"
#include <QTextStream>
#include <QFile>
#include <QMessageBox>
#include <QDebug>
#include <QSettings>
#include <QProgressDialog>
#include <QDateTime>
#include <QInputDialog>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QtSql>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlDriver>
#include <QSqlField>
#include <QSqlResult>
#include <QSqlTableModel>
#include <QSqlQueryModel>
#include <QSqlRelationalTableModel>
#include <QSqlRelationalDelegate>
#include <QSqlRelation>
#include <QSqlIndex>
#include <QThread>

DataLoadSQL::DataLoadSQL()
{
  _extensions.push_back("sqlph");
}

const QRegExp metasys_regx = QRegExp("[. /:]"); // regex for splitting metasys point names

const std::vector<const char*>& DataLoadSQL::compatibleFileExtensions() const
{
  return _extensions;
}

bool DataLoadSQL::readDataFromFile(FileLoadInfo* info, PlotDataMapRef& plot_data)
{ 
  // Open the database
  QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");
  db.setHostName("localhost");
  db.setDatabaseName("sys");
  db.setUserName("ryley");
  db.setPassword("12345678");
  if (!db.open()) {
    qDebug() << "Database error:" << db.lastError().text();
    return false;
  } else {
    qDebug() << "Database connection established";
    qDebug() << "Database name:" << db.databaseName();
  }
  // promt user for query text
  QString query_text = QInputDialog::getText(nullptr, "Query", "Enter query text");
  // Prepare the query
  QSqlQuery query("SELECT COUNT(*) FROM PointData", db);
  // Execute the query and iterate over the results line by line
  int query_count = 0;
  int linecount = 0;
  // create a vector of timeseries
  std::vector<PlotData*> plots_vector;

  if (query.exec() && query.next()) {
    query_count = query.value(0).toInt();
    // reset the query
    query.finish(); // finish the query
    qDebug() << "Number of entries:" << query_count;
    QProgressDialog progress_dialog;
    progress_dialog.setLabelText("Loading... please wait");
    progress_dialog.setWindowModality(Qt::ApplicationModal);
    progress_dialog.setRange(0, query_count - 1);
    progress_dialog.setAutoClose(true);
    progress_dialog.setAutoReset(true);
    progress_dialog.setValue(linecount);
    progress_dialog.show();
    progress_dialog.setValue(linecount);
    QApplication::processEvents();

    qDebug() << "Loading query";
    // exec a new query "SELECT * FROM PointData"
    query = QSqlQuery("SELECT * FROM PointData", db);
    
    if (query.exec()) { // This hangs the program 
      qDebug() << "Query executed successfully";
    } else {
      qDebug() << "Query error:" << query.lastError().text() << "\n\rQuary text:" << query.lastQuery();
      return false;
    }

    while (query.next()) {
      // check if there are the correct number of columns
      if (query.record().count() != 5) {
        qDebug() << "Incorrect number of columns at query line" << linecount;
        return false;
      }
      QString pointName = query.value(0).toString();
      // split the string into a vector of strings
      QStringList pointNameParts = pointName.split(metasys_regx, QString::SplitBehavior::SkipEmptyParts);
      // remove the second last element
      pointNameParts.removeAt(pointNameParts.size() - 1);
      // join the vector of strings into a single string but with a '/' between each part
      //qDebug() << "Point Name Before:" << pointName << "at query line" << linecount;
      pointName = pointNameParts.join("/");
      //qDebug() << "Point Name After:" << pointName;
      //int pointId = query.value(1).toInt();
      //int pointSliceId = query.value(2).toInt();
      QDateTime utcDateTime = query.value(3).toDateTime();
      bool is_number;
      double actualValue = query.value(4).toDouble(&is_number);
      //Do something with the data
      //qDebug() << "Point Name:" << pointName
      //         << ", UTC DateTime:" << utcDateTime.toString()
      //         << ", Actual Value:" << actualValue;

      double t = utcDateTime.toMSecsSinceEpoch()/1000.0;
      if (!utcDateTime.isValid())
      {
        //skip this point
        linecount++;
        continue;
      }
      // add the name to the vector
      auto it = plot_data.addNumeric(pointName.toStdString());
      plots_vector.push_back(&(it->second));
      
      if (is_number)
      {
        PlotData::Point point(t, actualValue);
        plots_vector.back()->pushBack(point);
      } else {
        qDebug() << "Point " << pointName << " actualValue" << actualValue << "is not a number" << "at query line" << linecount;
      }
      if (linecount % 1000 == 0)
      {
        progress_dialog.setValue(linecount);
        QApplication::processEvents();
      }
      linecount++;
    }
  } else {
    qDebug() << "Query error:" << query.lastError().text();
    return false;
  }
  // Close the database
  db.close();
  return true;
}






