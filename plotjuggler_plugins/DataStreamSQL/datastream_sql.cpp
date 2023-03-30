/*Wensocket PlotJuggler Plugin license(Faircode, Davide Faconti)

Copyright(C) 2018 Philippe Gauthier - ISIR - UPMC
Copyright(C) 2020 Davide Faconti
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
#include "datastream_sql.h"
#include <QTextStream>
#include <QFile>
#include <QMessageBox>
#include <QDebug>
#include <QDialog>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QTableView>
#include <mutex>
#include <QWebSocket>
#include <QIntValidator>
#include <chrono>
#include <QNetworkDatagram>
#include <QSqlTableModel>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QPushButton>
#include <QSqlRecord>
#include <QTimer>
#include <QFormLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QProgressDialog>
#include <QDateTime>
#include <QtSql>
#include <QSqlDriver>
#include <QSqlResult>
#include <QSqlQueryModel>
#include <QSqlRelationalTableModel>
#include <QSqlRelationalDelegate>
#include <QSqlRelation>
#include <QSqlIndex>
#include <QThread>
#include <QtConcurrent/QtConcurrent>
#include <QCheckBox>
#include <QListWidget>

using namespace PJ;

const QRegExp metasys_regx = QRegExp("[. /:]"); // regex for splitting metasys point names

SQLServer::SQLServer() : _running(false), _model(nullptr)
{
}

SQLServer::~SQLServer()
{
  shutdown();
  if (_model)
  {
    delete _model;
  }
}

bool SQLServer::start(QStringList*)
{
  if (_running)
  {
    return _running;
  }

  if (!displaySignInDialog(&_db, &_settings)) return false;

  if(!selectPointDataTableSources(&_db, &_selectedTables, &_columnSelection, &_settings))
  {
      qDebug() << "Failed to get table source";
      return false;
  }
  _previousRowCount = 0;
  for (int i = 0; i < _selectedTables.size(); i++) {
    _previousRowCount += countRowsInTable(&_db, &_selectedTables[0]);
    qDebug() << "Table" << i << ":" << _selectedTables.at(0);
  }
    if(!setupQuery())
    {
        return false;
    }

  if (!(_previousRowCount > 0) ) {
      qDebug() << "No rows in selected table!";
      return false;
  }
  if(!addPoints(&_db, &_settings)){
      qDebug() << "Failed to add points" << _db.lastError();
      return false;
  }
  if(!_model->exec()){
      qDebug() << "Failed to exec" << _model->lastError();
      return false;
  }
  _selectedTable = _selectedTables[0];
  _running = true;
  _thread = std::thread([this]() { this->loop(); });

  connect(&_checkNewRowsTimer, &QTimer::timeout, this, &SQLServer::checkForNewRows);
  _checkNewRowsTimer.start(6000);

  return _running;
}

void SQLServer::shutdown()
{
  qDebug() << "SQLServer::shutdown()";
  if (_running)
  {
    _running = false;
    _thread.join();
    _checkNewRowsTimer.stop();
    _row = 0;
  }
}

void SQLServer::loop()
{
  //_running = true;
  qDebug() << "SQLServer::loop()";
  QElapsedTimer timer; // start a timer to profile the loop
  timer.start();
  while (_running)
  {
    processData();
    if (_row % 100000 == 0) // print elapsed time every 100000 messages
    {
      qDebug() << "SQLServer::loop() processed" << _row << "messages in" << timer.elapsed() << "ms";
    }
    if (!(_row >= _previousRowCount) || (!_updateRowCount))
    {
        continue;
    }
    qDebug() << "Checking for new rows";
    _previousRowCount = countRowsInTable(&_db, &_selectedTable);
    _updateRowCount = false; // ack
    if (!(_previousRowCount > _row)){
        continue;
    }
    _model->finish();
    _model->clear();
    delete _model;
    if(!setupQuery()){
        _running = false;
        break;
    }
    if (!_model->exec()) {
        _running = false;
        break;
    }
  }
  qDebug() << "SQLServer::loop() finished" << "SQLServer::loop() took" << timer.elapsed() << "ms";
  QString connectionName = _db.connectionName();
  _db.close();
  _db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connectionName);
}

void SQLServer::checkForNewRows()
{
  _updateRowCount = true;
}

void SQLServer::processData()
{
  if (!_model->next() || !_running) return;

  if (_row > _previousRowCount)
  {
    return;
  }

  QDateTime utcDateTime = _model->value(1).toDateTime();



  double timestamp = utcDateTime.toMSecsSinceEpoch() / 1000.0;
  timestamp -= (3600*5); // -5 UTC

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

int SQLServer::countRowsInTable(QSqlDatabase* database, QString* selectedTableName)
{
    int count = -1;
    QSqlQuery query("SELECT COUNT(1) FROM " + *selectedTableName, *database);
    if (query.next())
    {
      count = query.value(0).toInt();
      qDebug() << "Number of rows in the table:" << count;
    } else {
      qDebug() << "Failed to get the number of rows in the table";
    }
    query.finish();
    query.clear();
    return count;
}

bool SQLServer::displaySignInDialog(QSqlDatabase* database, QSettings* settings)
{
    if (QSqlDatabase::drivers().isEmpty()) {
        QMessageBox::critical(nullptr, "Unable to load database", "This demo needs at least one Qt SQL driver");
        return false;
    }

  QDialog dialog;
  // Use a layout allowing to have a label next to each field
  QFormLayout form(&dialog);
  // Add some text above the fields
  form.addRow(new QLabel("Enter your database connection details:"));

  // Add the lineEdits with their respective labels
  QList<QLineEdit *> fields;
  QLineEdit *lineEdit1 = new QLineEdit(&dialog);
  QLineEdit *lineEdit2 = new QLineEdit(&dialog);
  QLineEdit *lineEdit3 = new QLineEdit(&dialog);
  QLineEdit *lineEdit4 = new QLineEdit(&dialog);
  QLineEdit *lineEdit5 = new QLineEdit(&dialog);
  // Add a dropdown for the driver type
  QComboBox *driverTypeComboBox = new QComboBox(&dialog);

  // Add a checkbox for Trusted Connection
  QCheckBox *trustedConnectionCheckbox = new QCheckBox("Use Trusted Connection", &dialog);
  // Set the initial checked state based on the value loaded from settings (default to false if not found)
  trustedConnectionCheckbox->setChecked(settings->value("trustedConnection", false).toBool());
  form.addRow(trustedConnectionCheckbox);
  // default values loaded from settings into the lineEdits
  lineEdit1->setText(settings->value("hostName", "localhost").toString());
  lineEdit2->setText(settings->value("dbName", "sys").toString());
  lineEdit3->setText(settings->value("userName", "ryley").toString());
  lineEdit4->setText(settings->value("password", "12345678").toString());
  lineEdit5->setText(settings->value("driverName", "ODBC Driver 17 for SQL Server").toString());
  // Add the driver type dropdown items and set the initial value based on the value loaded from settings (default to QSqlDatabase::drivers())
  driverTypeComboBox->addItems(QSqlDatabase::drivers());
  driverTypeComboBox->setCurrentText(settings->value("driverType", "QODBC3").toString());

  QString hostName = settings->value("hostName", "localhost").toString();
  form.addRow("hostName", lineEdit1);
  fields << lineEdit1;
  QString dbName = settings->value("dbName", "sys").toString();
  form.addRow("dbName", lineEdit2);
  fields << lineEdit2;
  QString userName = settings->value("userName", "ryley").toString();
  form.addRow("userName", lineEdit3);
  fields << lineEdit3;
  QString password = settings->value("password", "12345678").toString();
  form.addRow("password", lineEdit4);
  fields << lineEdit4;
  QString driverName = settings->value("driverName", "ODBC Driver 18 for SQL Server").toString();
  form.addRow("driverName", lineEdit5);
  fields << lineEdit5;
  QString driverType = settings->value("driverType", "QODBC3").toString();
  form.addRow("driverType", driverTypeComboBox);

  hostName = lineEdit1->text();
  dbName = lineEdit2->text();
  userName = lineEdit3->text();
  password = lineEdit4->text();
  driverName = lineEdit5->text();
  driverType = driverTypeComboBox->currentText();


  bool useTrustedConnection = false;
  // Add some standard buttons (Cancel/Ok) at the bottom of the dialog
  QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                            Qt::Horizontal, &dialog);
  form.addRow(&buttonBox);
  QObject::connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
  QObject::connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

  // Show the dialog as modal
  if (dialog.exec() == QDialog::Accepted) {
      // If the user didn't dismiss the dialog, do something with the fields
      foreach(QLineEdit * lineEdit, fields) {
          qDebug() << lineEdit->text();
      }

      if (driverTypeComboBox->currentText().isEmpty()) {
          QMessageBox::critical(nullptr, "No driver type selected", "Please select a driver type");
          return false;
      } else {
          _db = QSqlDatabase::addDatabase(driverType);
      }
      // Get the state of the Trusted Connection checkbox
      useTrustedConnection = trustedConnectionCheckbox->isChecked();
      qDebug() << "Trusted Connection:" << useTrustedConnection;
  } else {
    qDebug() << "Dialog was cancelled";
    return false;
  }

  hostName = lineEdit1->text();
  dbName = lineEdit2->text();
  userName = lineEdit3->text();
  password = lineEdit4->text();
  driverName = lineEdit5->text();
  driverType = driverTypeComboBox->currentText();
  settings->setValue("hostName", hostName);
  settings->setValue("dbName", dbName);
  settings->setValue("userName", userName);
  settings->setValue("password", password);
  settings->setValue("driverName", driverName);
  settings->setValue("driverType", driverType);
  settings->setValue("trustedConnection", useTrustedConnection);

  database->setDatabaseName(getConnectionString(settings));

  database->setConnectOptions("SQL_ATTR_ACCESS_MODE=SQL_MODE_READ_ONLY;");
  if (!database->open()) {
    qDebug() << "Database error:" << _db.lastError().text();
    // Warning message box
    QMessageBox msgBox;
    msgBox.setText("Database error:" + _db.lastError().text());
    msgBox.exec();
    return false;
  } else {
    qDebug() << "Database connection established";
    qDebug() << "Database name:" << _db.databaseName();
  }
  qDebug() << database->lastError().text();

  QString selectedDatabase = selectDatabase();
  if (selectedDatabase.isEmpty()) {
    qDebug() << "No database selected";
    return false;
  } else {
    // Set the database name and reconnect
    database->setDatabaseName(getConnectionString(settings));
    database->setConnectOptions("SQL_ATTR_ACCESS_MODE=SQL_MODE_READ_ONLY;");
    if (!database->open()) {
      qDebug() << "Failed to connect to the selected database:" << _db.lastError().text();
      return false;
    }
    qDebug() << "Connected to the selected database:" << selectedDatabase;
  }

  return true;
}

bool SQLServer::addPoints(QSqlDatabase* database, QSettings* settings)
{
    int count = 0;
    QSqlRecord tableRecord;
    QStringList availableColumns;
    QStringList pointDefsSourceList;
    if (!selectPointDefsTableSource(database, &pointDefsSourceList, settings))
    {
        qDebug() << "Failed to select Point Definition Table Source";
        return false;
    }

    if (pointDefsSourceList.isEmpty()) {
      qDebug() << "No point definitions table selected";
      return false;
    } else {
      qDebug() << "Selected Point Definitions Table:" << pointDefsSourceList;
      for (int i = 0; i < pointDefsSourceList.size(); i++) {
        qDebug() << "Table" << i << ":" << pointDefsSourceList.at(i);
        tableRecord = database->record(pointDefsSourceList.at(i));
        for (int j = 0; j < tableRecord.count(); j++) {
          availableColumns << tableRecord.fieldName(j);
        }
        ColumnSelection selectedColumns = selectPointDefsColumns(availableColumns, settings);
        if (selectedColumns.nameColumn.isEmpty() ||
          selectedColumns.pointIDColumn.isEmpty()) {
          qDebug() << "Required columns not selected";
          return false;
        } else {
          // Perform the parsing with the selected columns
          qDebug() << "Selected Columns: Name =" << selectedColumns.nameColumn
                    << ", PointID =" << selectedColumns.pointIDColumn;
        }
        
        QSqlQuery query("SELECT " + selectedColumns.pointIDColumn + "," + selectedColumns.nameColumn + " FROM " + pointDefsSourceList.at(i), *database);
        while (query.next())
        {
          int pointId = query.value(0).toInt();
          // check if point ID is in the std::map _pointIdToNameMap already
          if (_pointIdToNameMap.find(pointId) != _pointIdToNameMap.end()) {
            //qDebug() << "Point ID" << pointId << "already exists";
            continue;
          }
          count++;
          QStringList pointNameParts = query.value(1).toString().split(metasys_regx, QString::SplitBehavior::SkipEmptyParts);
          pointNameParts.removeAt(pointNameParts.size() - 1);
          QString pointName = pointNameParts.join("/");
          dataMap().addNumeric(pointName.toStdString());
          _pointIdToNameMap[pointId] = pointName.toStdString();
          qDebug() << "Added name" << pointName;
        }
        qDebug() << "Added" << count << " Point Definitions";
        query.finish();
        query.clear();
      }
    }
    return true;
}

bool SQLServer::selectPointDefsTableSource(QSqlDatabase* database, QStringList* selectedTables, QSettings* settings)
{
    QDialog tableDialog;
    tableDialog.setWindowTitle("Select Point Definitions Source");
    if (!selectModelsFromListWidget(database, &tableDialog, selectedTables))
    {
        qDebug() << "No tables selected";
        return false;
    }
    return true;
}

bool SQLServer::setupQuery()
{
    QString query = QString("SELECT %1, %2, %3 FROM %4 ORDER BY %2 ASC OFFSET %5 ROWS FETCH NEXT %6 ROWS ONLY")
        .arg(_columnSelection.pointIDColumn)
        .arg(_columnSelection.utcdatetimeColumn)
        .arg(_columnSelection.valueColumn)
        .arg(_selectedTables[0])
        .arg(_row)
        .arg(_previousRowCount - _row);
    qDebug() << query;
    _model = new QSqlQuery(_db);
    _model->setForwardOnly(true);
    if (!_model->prepare(query))
    {
        qDebug() << "Failed to prepare" << _model->lastError();
        return false;
    }
    return true;
}

QString SQLServer::getConnectionString(QSettings* settings)
{
    QString connectionString;
    if (settings->value("trustedConnection", true).toBool()) {
        connectionString = QString("Driver={%1};Server=%2;Database=%3;Trusted_Connection=%4;TrustServerCertificate=%5;")
                                .arg(settings->value("driverName", "").toString())
                                .arg(settings->value("hostName", "").toString())
                                .arg(settings->value("dbName", "").toString())
                                // Trusted_Connection & TrustServerCertificate is a boolean value, so we need to convert it to yes/no
                                .arg(settings->value("trustedConnection", true).toBool() ? "yes" : "no")
                                .arg(settings->value("trustedConnection", true).toBool() ? "yes" : "no");

    } else {
        connectionString = QString("Driver={%1};Server=%2;Database=%3;UID=%4;PWD=%5;")
                                .arg(settings->value("driverName", "").toString())
                                .arg(settings->value("hostName", "").toString())
                                .arg(settings->value("dbName", "").toString())
                                .arg(settings->value("userName", "").toString())
                                .arg(settings->value("password", "").toString());
    }
    qDebug() << connectionString;
    return connectionString;
}








QString SQLServer::selectDatabase()
{
    QDialog databaseDialog;
    databaseDialog.setWindowTitle("Select Database");

    QVBoxLayout layout;
    QListWidget databaseList;

    QSqlQuery query(_db);
    if (query.exec("SELECT name FROM sys.databases")) {
        while (query.next()) {
            databaseList.addItem(query.value(0).toString());
        }
    } else {
        qDebug() << "Failed to execute query:" << query.lastError().text();
    }

    QPushButton selectButton("Select");
    QPushButton cancelButton("Cancel");

    layout.addWidget(&databaseList);
    layout.addWidget(&selectButton);
    layout.addWidget(&cancelButton);
    databaseDialog.setLayout(&layout);

    QString selectedDatabase;

    QObject::connect(&selectButton, &QPushButton::clicked, [&]() {
        QListWidgetItem *selectedItem = databaseList.currentItem();
        if (selectedItem) {
            selectedDatabase = selectedItem->text();
            databaseDialog.accept();
        }
        else {
            QMessageBox::warning(&databaseDialog, "No database selected", "Please select a database");
        }
    });
    QObject::connect(&cancelButton, &QPushButton::clicked, [&]() {
        databaseDialog.reject();
    });
    query.finish();
    query.clear();

    databaseDialog.exec();
    return selectedDatabase;
}

ColumnSelection SQLServer::selectPointDataColumns(const QStringList &availableColumns, QSettings* settings)
{
    // Add QSettings instance

    QDialog columnDialog;
    columnDialog.setWindowTitle("Select Point Data Columns");

    QFormLayout form(&columnDialog);

    QComboBox pointIDColumnComboBox;
    QComboBox utcdatetimeColumnComboBox;
    QComboBox valueColumnComboBox;

    pointIDColumnComboBox.addItems(availableColumns);
    utcdatetimeColumnComboBox.addItems(availableColumns);
    valueColumnComboBox.addItems(availableColumns);

    // Read last selected columns from QSettings
    QString lastID = settings->value("lastSelectedColumns/datapointid", availableColumns).toString();
    QString lastUtcDatetime = settings->value("lastSelectedColumns/utcdatetime", availableColumns).toString();
    QString lastValue = settings->value("lastSelectedColumns/value", availableColumns).toString();

    // Set combo box current text if saved settings are available in the list
    if (availableColumns.contains(lastID)) {
        pointIDColumnComboBox.setCurrentText(lastID);
    }
    if (availableColumns.contains(lastUtcDatetime)) {
        utcdatetimeColumnComboBox.setCurrentText(lastUtcDatetime);
    }
    if (availableColumns.contains(lastValue)) {
        valueColumnComboBox.setCurrentText(lastValue);
    }

    form.addRow("PointID Column:", &pointIDColumnComboBox);
    form.addRow("UTCDatetime Column:", &utcdatetimeColumnComboBox);
    form.addRow("Value Column:", &valueColumnComboBox);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           Qt::Horizontal, &columnDialog);
    form.addRow(&buttonBox);

    QObject::connect(&buttonBox, SIGNAL(accepted()), &columnDialog, SLOT(accept()));
    QObject::connect(&buttonBox, SIGNAL(rejected()), &columnDialog, SLOT(reject()));

    ColumnSelection selectedColumns;
    if (columnDialog.exec() == QDialog::Accepted) {
        selectedColumns.pointIDColumn = pointIDColumnComboBox.currentText();
        selectedColumns.utcdatetimeColumn = utcdatetimeColumnComboBox.currentText();
        selectedColumns.valueColumn = valueColumnComboBox.currentText();

        // Save last selected columns to QSettings
        settings->setValue("lastSelectedColumns/datapointid", selectedColumns.pointIDColumn);
        settings->setValue("lastSelectedColumns/utcdatetime", selectedColumns.utcdatetimeColumn);
        settings->setValue("lastSelectedColumns/value", selectedColumns.valueColumn);
    }

    return selectedColumns;
}

ColumnSelection SQLServer::selectPointDefsColumns(const QStringList &availableColumns, QSettings* settings)
{
    QDialog columnDialog;
    columnDialog.setWindowTitle("Select Point Definition Columns");

    QFormLayout form(&columnDialog);

    QComboBox nameColumnComboBox;
    QComboBox pointIDColumnComboBox;

    nameColumnComboBox.addItems(availableColumns);
    pointIDColumnComboBox.addItems(availableColumns);

    // Read last selected columns from QSettings
    QString lastName = settings->value("lastSelectedColumns/name", "").toString();
    QString lastPointID = settings->value("lastSelectedColumns/pointID", "").toString();

    // Set combo box current text if saved settings are available in the list
    if (availableColumns.contains(lastName)) {
        nameColumnComboBox.setCurrentText(lastName);
    }
    if (availableColumns.contains(lastPointID)) {
        pointIDColumnComboBox.setCurrentText(lastPointID);
    }

    form.addRow("Name Column:", &nameColumnComboBox);
    form.addRow("PointID Column:", &pointIDColumnComboBox);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           Qt::Horizontal, &columnDialog);
    form.addRow(&buttonBox);

    QObject::connect(&buttonBox, SIGNAL(accepted()), &columnDialog, SLOT(accept()));
    QObject::connect(&buttonBox, SIGNAL(rejected()), &columnDialog, SLOT(reject()));

    ColumnSelection selectedColumns;
    if (columnDialog.exec() == QDialog::Accepted) {
        selectedColumns.nameColumn = nameColumnComboBox.currentText();
        selectedColumns.pointIDColumn = pointIDColumnComboBox.currentText();

        // Save last selected columns to QSettings
        settings->setValue("lastSelectedColumns/name", selectedColumns.nameColumn);
        settings->setValue("lastSelectedColumns/pointID", selectedColumns.pointIDColumn);
    }

    return selectedColumns;
}




bool SQLServer::selectModelsFromListWidget(QSqlDatabase* database, QDialog* tableDialog, QStringList* selectedViews)
{
    QSqlQueryModel model;
    model.setQuery(QSqlQuery("SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES UNION SELECT TABLE_NAME FROM INFORMATION_SCHEMA.VIEWS;", *database));

    QVBoxLayout layout;
    QListWidget listWidget;
    listWidget.setSelectionMode(QAbstractItemView::MultiSelection);

    for (int i = 0; i < model.rowCount(); ++i) {
        listWidget.addItem(model.record(i).value(0).toString());
    }

    QPushButton selectButton("Select");
    QPushButton cancelButton("Cancel");

    layout.addWidget(&listWidget);
    layout.addWidget(&selectButton);
    layout.addWidget(&cancelButton);
    tableDialog->setLayout(&layout);

    QObject::connect(&selectButton, &QPushButton::clicked, [&]() {
        QList<QListWidgetItem*> selectedItems = listWidget.selectedItems();
        if (!selectedItems.isEmpty()) {
            for (QListWidgetItem* item : selectedItems) {
                selectedViews->append(item->text());
            }
            tableDialog->accept();
            return true;
        }
        else {
            QMessageBox::warning(tableDialog, "No source selected", "Please select a source");
            return false;
        }
    });
    QObject::connect(&cancelButton, &QPushButton::clicked, [&]() {
        tableDialog->reject();
        return false;
    });

    tableDialog->exec();
    return true;
}

bool SQLServer::selectPointDataTableSources(QSqlDatabase* database, QStringList* selectedTables, ColumnSelection* selectedColumns, QSettings* settings)
{
  QDialog tableDialog;
  tableDialog.setWindowTitle("Select Point Data Source");
  if (!selectModelsFromListWidget(database, &tableDialog, selectedTables)) {
    qDebug() << "No tables selected";
    return false;
  }
  QSqlRecord tableRecord;
  QStringList availableColumns;
  if (selectedTables->isEmpty()) {
    qDebug() << "No tables selected";
    return false;
  } else {
    // Set the selected table
    for (int i = 0; i < selectedTables->count(); i++) {
      QString selectedTable = selectedTables->at(i);
      tableRecord = database->record(selectedTable);
      for (int i = 0; i < tableRecord.count(); i++) {
        availableColumns << tableRecord.fieldName(i);
      }
      *selectedColumns = selectPointDataColumns(availableColumns, settings);
      if (selectedColumns->pointIDColumn.isEmpty() ||
        selectedColumns->utcdatetimeColumn.isEmpty() ||
        selectedColumns->valueColumn.isEmpty()) {
        qDebug() << "Required columns not selected";
        return false;
      } else {
        // Perform the parsing with the selected columns
        qDebug() << "Selected Columns: PointID =" << selectedColumns->pointIDColumn
                  << ", UTCDatetime =" << selectedColumns->utcdatetimeColumn
                  << ", Value =" << selectedColumns->valueColumn;
      }
      qDebug() << "Selected tables:" << selectedTable;
    }
    //qDebug() << "Selected tables:" << selectedTables->join(", ");
  }
  return true;
}
