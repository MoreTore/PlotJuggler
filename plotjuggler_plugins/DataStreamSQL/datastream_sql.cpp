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

bool SQLServer::addPoints(QSqlDatabase* database, QSettings* settings)
{
    QString pointDefsSource = selectPointDefsSource(database);

    QSqlRecord tableRecord = _db.record(pointDefsSource);
    QStringList availableColumns;
    for (int i = 0; i < tableRecord.count(); i++) {
        availableColumns << tableRecord.fieldName(i);
    }

    // Let the user select the required columns
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
    int count = 0;
    QSqlQuery query("SELECT " + selectedColumns.pointIDColumn + "," + selectedColumns.nameColumn + " FROM " + pointDefsSource, *database);
    while (query.next())
    {
      count++;
      int pointId = query.value(0).toInt();
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
    return true;
}

bool SQLServer::start(QStringList*)
{
  if (_running)
  {
    return _running;
  }

  if (!displaySignInDialog(&_settings)) return false;


  _model = new QSqlQuery(_db);
  _model->setForwardOnly(true);

    // PointID,UTCDateTime,ActualValue
  _model->prepare("SELECT * FROM " + _selectedTable);

  // Use the SELECT COUNT(*) from table to get the number of rows in the table
  _previousRowCount = countRowsInTable(&_db, &_selectedTable);
  if (!(_previousRowCount > 0) ) {
#ifdef DEBUG
      qDebug() << "No rows in selected table!";
#endif
      return false;
  }
  if(!addPoints(&_db, &_settings)){
#ifdef DEBUG
      qDebug() << "No point definitions";
#endif
      return false;
  }
  
  _thread = std::thread([this]() { this->loop(); });
  if(!_model->exec()){
      qDebug() << "Failed";
  }

  _running = true;

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

void SQLServer::processData()
{
  if (!_model->next() || !_running) return;

  if (_row > _previousRowCount)
  {
    if (!_checkNewRowsTimer.isActive()) {
      _checkNewRowsTimer.start(60000); // Check for new rows every 60 seconds
    }
    return;
  }

  QDateTime utcDateTime = _model->value(3).toDateTime();
  double timestamp = utcDateTime.toMSecsSinceEpoch() / 1000.0;

  if (!utcDateTime.isValid())
  {
    _row++;
    return;
  }

  double actualValue = _model->value(4).toDouble();
  _row++;

  try
  {
    std::lock_guard<std::mutex> lock(mutex());
    int pointId = _model->value(1).toInt();
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

void SQLServer::loop()
{
  _running = true;
  qDebug() << "SQLServer::loop()";
  QElapsedTimer timer; // start a timer to profile the loop
  timer.start();
  // create a timer to check for new rows evry interval of time
  _checkNewRowsTimer.setInterval(60000); // Check for new rows every 60 seconds
  connect(&_checkNewRowsTimer, &QTimer::timeout, this, &SQLServer::checkForNewRows);
  while (_running)
  {
    processData();
    if (_row % 100000 == 0) // print elapsed time every 100000 messages
    {
      qDebug() << "SQLServer::loop() processed" << _row << "messages in" << timer.elapsed() << "ms";
    }
    if ((_row > _previousRowCount) && (!_checkNewRowsTimer.isActive()))
    {
      _checkNewRowsTimer.start(60000); // Check for new rows every 60 seconds
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
  int rowCount = countRowsInTable(&_db, &_selectedTable);
  if (rowCount > _previousRowCount)
  {
    _previousRowCount = rowCount;
    _checkNewRowsTimer.stop();
    _row = 0;
  }
}


QString SQLServer::selectModelFromList(QSqlDatabase* database, QDialog* tableDialog)
{
    QSqlQueryModel model;
    model.setQuery(QSqlQuery("SELECT TABLE_NAME FROM INFORMATION_SCHEMA.TABLES UNION SELECT TABLE_NAME FROM INFORMATION_SCHEMA.VIEWS;", *database));

    QVBoxLayout layout;
    QListWidget listWidget;

    for (int i = 0; i < model.rowCount(); ++i) {
        listWidget.addItem(model.record(i).value(0).toString());
    }

    QPushButton selectButton("Select");
    QPushButton cancelButton("Cancel");

    layout.addWidget(&listWidget);
    layout.addWidget(&selectButton);
    layout.addWidget(&cancelButton);
    tableDialog->setLayout(&layout);

    QString selectedView;

    QObject::connect(&selectButton, &QPushButton::clicked, [&]() {
        QList<QListWidgetItem*> selectedItems = listWidget.selectedItems();
        if (!selectedItems.isEmpty()) {
            selectedView = selectedItems.first()->text();
            tableDialog->accept();
        }
        else {
            QMessageBox::warning(tableDialog, "No source selected", "Please select a source");
        }
    });
    QObject::connect(&cancelButton, &QPushButton::clicked, [&]() {
        tableDialog->reject();
    });

    tableDialog->exec();
    return selectedView;
}

QString SQLServer::selectPointDataTable(QSqlDatabase* database)
{
    QDialog tableDialog;
    tableDialog.setWindowTitle("Select Point Data Source");
    return selectModelFromList(database, &tableDialog);
}

QString SQLServer::selectPointDefsSource(QSqlDatabase* database)
{
    QDialog tableDialog;
    tableDialog.setWindowTitle("Select Point Definitions Source");
    return selectModelFromList(database, &tableDialog);
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

    QComboBox nameColumnComboBox;
    QComboBox utcdatetimeColumnComboBox;
    QComboBox valueColumnComboBox;

    nameColumnComboBox.addItems(availableColumns);
    utcdatetimeColumnComboBox.addItems(availableColumns);
    valueColumnComboBox.addItems(availableColumns);

    // Read last selected columns from QSettings
    QString lastName = settings->value("lastSelectedColumns/name", availableColumns).toString();
    QString lastUtcDatetime = settings->value("lastSelectedColumns/utcdatetime", availableColumns).toString();
    QString lastValue = settings->value("lastSelectedColumns/value", availableColumns).toString();

    // Set combo box current text if saved settings are available in the list
    if (availableColumns.contains(lastName)) {
        nameColumnComboBox.setCurrentText(lastName);
    }
    if (availableColumns.contains(lastUtcDatetime)) {
        utcdatetimeColumnComboBox.setCurrentText(lastUtcDatetime);
    }
    if (availableColumns.contains(lastValue)) {
        valueColumnComboBox.setCurrentText(lastValue);
    }

    form.addRow("Name Column:", &nameColumnComboBox);
    form.addRow("UTCDatetime Column:", &utcdatetimeColumnComboBox);
    form.addRow("Value Column:", &valueColumnComboBox);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           Qt::Horizontal, &columnDialog);
    form.addRow(&buttonBox);

    QObject::connect(&buttonBox, SIGNAL(accepted()), &columnDialog, SLOT(accept()));
    QObject::connect(&buttonBox, SIGNAL(rejected()), &columnDialog, SLOT(reject()));

    ColumnSelection selectedColumns;
    if (columnDialog.exec() == QDialog::Accepted) {
        selectedColumns.nameColumn = nameColumnComboBox.currentText();
        selectedColumns.utcdatetimeColumn = utcdatetimeColumnComboBox.currentText();
        selectedColumns.valueColumn = valueColumnComboBox.currentText();

        // Save last selected columns to QSettings
        settings->setValue("lastSelectedColumns/name", selectedColumns.nameColumn);
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

bool SQLServer::displaySignInDialog(QSettings* settings)
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


  QString connectionString;
  if (useTrustedConnection) {
    connectionString = QString("Driver={%1};Server=%2;Database=%3;Trusted_Connection=Yes;TrustServerCertificate=yes;")
                          .arg(driverName)
                            .arg(hostName)
                          .arg(dbName);
  } else {
    connectionString = QString("Driver={SQL Server};Server=%1;Database=%2;")
                          .arg(hostName)
                          .arg(dbName);
  }

  _db.setDatabaseName(connectionString);

  _db.setConnectOptions("SQL_ATTR_ACCESS_MODE=SQL_MODE_READ_ONLY;");
  if (!_db.open()) {
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
  qDebug() << _db.lastError().text();

  QString selectedDatabase = selectDatabase();
  if (selectedDatabase.isEmpty()) {
    qDebug() << "No database selected";
    return false;
  } else {
    // Set the database name and reconnect
    connectionString = QString("Driver={%1};Server=%2;Database=%3;Trusted_Connection=Yes;TrustServerCertificate=yes;")
                            .arg(driverName)
                            .arg(hostName)
                            .arg(dbName);
    _db.setDatabaseName(connectionString);
    _db.setConnectOptions("SQL_ATTR_ACCESS_MODE=SQL_MODE_READ_ONLY;");
    if (!_db.open()) {
      qDebug() << "Failed to connect to the selected database:" << _db.lastError().text();
      return false;
    }
    qDebug() << "Connected to the selected database:" << selectedDatabase;
  }

  _selectedTable = selectPointDataTable(&_db);
  if (_selectedTable.isEmpty()) {
    qDebug() << "No table selected";
    return false;
  } else {
    QSqlRecord tableRecord = _db.record(_selectedTable);
    QStringList availableColumns;
    for (int i = 0; i < tableRecord.count(); i++) {
        availableColumns << tableRecord.fieldName(i);
    }

    // Let the user select the required columns
    ColumnSelection selectedColumns = selectPointDataColumns(availableColumns, settings);

    if (selectedColumns.nameColumn.isEmpty() ||
      selectedColumns.utcdatetimeColumn.isEmpty() ||
      selectedColumns.valueColumn.isEmpty()) {
      qDebug() << "Required columns not selected";
      return false;
    } else {
      // Perform the parsing with the selected columns
      qDebug() << "Selected Columns: Name =" << selectedColumns.nameColumn
                << ", UTCDatetime =" << selectedColumns.utcdatetimeColumn
                << ", Value =" << selectedColumns.valueColumn;
    }
  }
  return true;
}
