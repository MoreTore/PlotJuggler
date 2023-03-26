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
#include <QSettings>
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

void SQLServer::checkForNewRows()
{
    // Get the current row count
    QSqlQuery rowCountQuery("SELECT COUNT(*) FROM PointData", _db);
    int currentRowCount = 0;
    if (rowCountQuery.next()) {
        currentRowCount = rowCountQuery.value(0).toInt();
    }
    // Check if there are new rows
    if (currentRowCount > _previousRowCount) {
        // Update the model
        //_model->select();
        // Emit the tableUpdated signal
        emit tableUpdated();
    }
    // Update the previous row count
    _previousRowCount = currentRowCount;
}

bool SQLServer::start(QStringList*)
{
  _limit = 200;
  _offset = 0;
  if (_running)
  {
    return _running;
  }

  if (parserFactories() == nullptr || parserFactories()->empty())
  {
    QMessageBox::warning(nullptr, tr("SQL Server"), tr("No available MessageParsers"),
                         QMessageBox::Ok);
    _running = false;
    return false;
  }

QSettings settings("YourCompanyName", "YourApplicationName");
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

// Add a checkbox for Trusted Connection
QCheckBox *trustedConnectionCheckbox = new QCheckBox("Use Trusted Connection", &dialog);
// Set the initial checked state based on the value loaded from settings (default to false if not found)
trustedConnectionCheckbox->setChecked(settings.value("trustedConnection", false).toBool());
form.addRow(trustedConnectionCheckbox);
// default values loaded from settings into the lineEdits
lineEdit1->setText(settings.value("hostName", "localhost").toString());
lineEdit2->setText(settings.value("dbName", "sys").toString());
lineEdit3->setText(settings.value("userName", "ryley").toString());
lineEdit4->setText(settings.value("password", "12345678").toString());
lineEdit5->setText(settings.value("driverName", "ODBC Driver 17 for SQL Server").toString());

QString hostName = settings.value("hostName", "localhost").toString();
form.addRow("hostName", lineEdit1);
fields << lineEdit1;
QString dbName = settings.value("dbName", "sys").toString();
form.addRow("dbName", lineEdit2);
fields << lineEdit2;
QString userName = settings.value("userName", "ryley").toString();
form.addRow("userName", lineEdit3);
fields << lineEdit3;
QString password = settings.value("password", "12345678").toString();
form.addRow("password", lineEdit4);
fields << lineEdit4;
QString driverName = settings.value("driverName", "ODBC Driver 17 for SQL Server").toString();
form.addRow("driverName", lineEdit5);
fields << lineEdit5;

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
    // Get the state of the Trusted Connection checkbox
    useTrustedConnection = trustedConnectionCheckbox->isChecked();
    qDebug() << "Trusted Connection:" << useTrustedConnection;
} else {
    qDebug() << "Dialog was cancelled";
    return false;
}

    

// Check if there are any available database drivers
if (QSqlDatabase::drivers().isEmpty()) {
    QMessageBox::critical(nullptr, "Unable to load database", "This demo needs at least one Qt SQL driver");
    return false;
}

// Ask the user to select a database driver from the list of available drivers (e.g., QSQLITE, QMYSQL, QODBC)
QString driver = QInputDialog::getItem(nullptr, "Database Driver", "Select a database driver:", QSqlDatabase::drivers(), 0, false);
// If the user cancelled the dialog, return
if (driver.isEmpty()) {
    return false;
}

QSqlDatabase _db = QSqlDatabase::addDatabase(driver);

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

// Open the database
_db.setDatabaseName(connectionString);

//hostName = QInputDialog::getText(nullptr, "Database Host", "Enter database host (e.g., localhost):", QLineEdit::Normal, hostName);
//dbName = QInputDialog::getText(nullptr, "Database Name", "Enter database name:", QLineEdit::Normal, dbName);
//userName = QInputDialog::getText(nullptr, "User Name", "Enter database user name:", QLineEdit::Normal, userName);
//password = QInputDialog::getText(nullptr, "Password", "Enter database password:", QLineEdit::Password, password);

  hostName = lineEdit1->text();
  dbName = lineEdit2->text();
  userName = lineEdit3->text();
  password = lineEdit4->text();
/*
  // Open the database
  _db.setHostName(hostName);
  _db.setDatabaseName(dbName);
  _db.setUserName(userName);
  _db.setPassword(password);
  */
  settings.setValue("hostName", hostName);
  settings.setValue("dbName", dbName);
  settings.setValue("userName", userName);
  settings.setValue("password", password);

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
      if (!_db.open()) {
          qDebug() << "Failed to connect to the selected database:" << _db.lastError().text();
          return false;
      }
      qDebug() << "Connected to the selected database:" << selectedDatabase;
  }

  QString selectedTable = selectTable();
  if (selectedTable.isEmpty()) {
      qDebug() << "No table selected";
      return false;
  } else {
      QSqlRecord tableRecord = _db.record(selectedTable);
      QStringList availableColumns;
      for (int i = 0; i < tableRecord.count(); i++) {
          availableColumns << tableRecord.fieldName(i);
      }

      // Let the user select the required columns
      ColumnSelection selectedColumns = selectColumns(availableColumns);

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
    /*
    QSqlQueryModel model;
    model.setQuery(QSqlQuery("SELECT * FROM PointData LIMIT 200", _db));
    QTableView tableView;
    tableView.setModel(&model);
    tableView.setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView.setSelectionMode(QAbstractItemView::SingleSelection);
    tableView.resizeColumnsToContents();
    */
  

  if (!_db.open())
  {
    QMessageBox::warning(nullptr, tr("SQL Server"),
                         tr("Couldn't connect to the database: %1").arg(_db.lastError().text()),
                         QMessageBox::Ok);
    _running = false;
    return false;
  } else {
    
    //_model = new QSqlTableModel(nullptr, _db); // Instantiate the QSqlTableModel here
    qDebug() << "Connected to the database";
    _model = new QSqlQueryModel();
    //_model = new QSqlQuery(_db);

  }
  // Use the SELECT COUNT(*) from table to get the number of rows in the table
  QSqlQuery query("SELECT COUNT(*) FROM " + selectedTable, _db);
  if (query.next())
  {
    _previousRowCount = query.value(0).toInt();
    qDebug() << "Number of rows in the table:" << _previousRowCount;
  }
  else
  {
    qDebug() << "Failed to get the number of rows in the table";
    return false;
  }

  query.finish();
  query.clear();
  int cnt = _model->rowCount();
  _model->setQuery("SELECT * FROM" + selectedTable + "LIMIT 200 OFFSET 0");
  _model->setParent(this); // Set the parent to this SQLServer instance

  cnt = _model->rowCount();

  _running = true;

  // Set up the QTimer to check for new rows periodically
  connect(&_checkNewRowsTimer, &QTimer::timeout, this, &SQLServer::checkForNewRows);
  //_checkNewRowsTimer.start(2000); // Check for new rows every 2 seconds

  //processData();
  _thread = std::thread([this]() { this->loop(); });

  return _running;
}

void SQLServer::shutdown()
{
  qDebug() << "SQLServer::shutdown()";
  if (_running)
  {
    _running = false;
    _thread.join();
    // Stop the QTimer
    _checkNewRowsTimer.stop();
    _row = 0;
  }
}

void SQLServer::processData()
{

  if (!_running)
  {
    return;
  }
  
  if (_row > _previousRowCount){
    // start the timer to check for new rows. Only do this once
    if (!_checkNewRowsTimer.isActive()) {
        _checkNewRowsTimer.start(60000); // Check for new rows every 60 seconds
    }
    return;
  }
  //_model->fetchMore();

  // get the data from the column
  QString pointName = _model->record(_row).value(0).toString();
  QDateTime utcDateTime = _model->data(_model->index(_row, 3)).toDateTime();
  double timestamp = utcDateTime.toMSecsSinceEpoch()/1000.0;
  if (!utcDateTime.isValid())
  {
    _row++;
    return;
  }
  double actualValue = _model->data(_model->index(_row, 4)).toDouble();
  _row++;

  try
  {
    // important use the mutex to protect any access to the data
    std::lock_guard<std::mutex> lock(mutex());

    QStringList pointNameParts = pointName.split(metasys_regx, QString::SplitBehavior::SkipEmptyParts);
    // remove the second last element which is #85 just because thats how johnson controls made the names
    pointNameParts.removeAt(pointNameParts.size() - 1);
    // join the vector of strings into a single string but with a '/' between each part
    pointName = pointNameParts.join("/");

    auto& plotdata = dataMap().addNumeric(pointName.toStdString())->second;
    auto& plot = dataMap().numeric.find(pointName.toStdString())->second;
    plot.pushBack(PlotData::Point(timestamp, actualValue));

    
    //_parser->parseMessage(msg, timestamp);
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
    // notify the GUI
    emit closed();
    return;
  }
  return;
}

void SQLServer::loop()
{
  _running = true;
  qDebug() << "SQLServer::loop()";
  while (_running)
  {
    //auto prev = std::chrono::high_resolution_clock::now();
    processData();

    // Sleep for 100 ms
    //std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  QString connectionName = _db.connectionName();
  _db.close();
  _db = QSqlDatabase();
  QSqlDatabase::removeDatabase(connectionName);

}


// Add this new function
/*
QString SQLServer::selectTable()
{
    QDialog tableDialog;
    tableDialog.setWindowTitle("Select Table");

    QVBoxLayout layout;
    QTableView tableView;

    QSqlQueryModel model;
    model.setQuery(QSqlQuery("SELECT name FROM sys.tables", _db));

    tableView.setModel(&model);
    tableView.setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView.setSelectionMode(QAbstractItemView::SingleSelection);
    tableView.resizeColumnsToContents();

    QPushButton selectButton("Select");
    QPushButton cancelButton("Cancel");

    layout.addWidget(&tableView);
    layout.addWidget(&selectButton);
    layout.addWidget(&cancelButton);
    tableDialog.setLayout(&layout);

    QString selectedTable;

    QObject::connect(&selectButton, &QPushButton::clicked, [&]() {
        QModelIndexList selectedIndexes = tableView.selectionModel()->selectedRows();
        if (!selectedIndexes.isEmpty()) {
            QModelIndex selectedIndex = selectedIndexes.first();
            selectedTable = model.record(selectedIndex.row()).value(0).toString();
            tableDialog.accept();
        }
        else {
            QMessageBox::warning(&tableDialog, "No table selected", "Please select a table");
        }
    });
    QObject::connect(&cancelButton, &QPushButton::clicked, [&]() {
        tableDialog.reject();
    });

    tableDialog.exec();
    return selectedTable;
}
*/
QString SQLServer::selectTable()
{
    QDialog tableDialog;
    tableDialog.setWindowTitle("Select View");

    QVBoxLayout layout;
    QTableView tableView;

    QSqlQueryModel model;
    model.setQuery(QSqlQuery("SELECT TABLE_NAME FROM INFORMATION_SCHEMA.VIEWS", _db));

    tableView.setModel(&model);
    tableView.setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView.setSelectionMode(QAbstractItemView::SingleSelection);
    tableView.resizeColumnsToContents();

    QPushButton selectButton("Select");
    QPushButton cancelButton("Cancel");

    layout.addWidget(&tableView);
    layout.addWidget(&selectButton);
    layout.addWidget(&cancelButton);
    tableDialog.setLayout(&layout);

    QString selectedView;

    QObject::connect(&selectButton, &QPushButton::clicked, [&]() {
        QModelIndexList selectedIndexes = tableView.selectionModel()->selectedRows();
        if (!selectedIndexes.isEmpty()) {
            QModelIndex selectedIndex = selectedIndexes.first();
            selectedView = model.record(selectedIndex.row()).value(0).toString();
            tableDialog.accept();
        }
        else {
            QMessageBox::warning(&tableDialog, "No view selected", "Please select a view");
        }
    });
    QObject::connect(&cancelButton, &QPushButton::clicked, [&]() {
        tableDialog.reject();
    });

    tableDialog.exec();
    return selectedView;
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

    databaseDialog.exec();
    return selectedDatabase;
}



ColumnSelection SQLServer::selectColumns(const QStringList &availableColumns)
{
    QDialog columnDialog;
    columnDialog.setWindowTitle("Select Columns");

    QFormLayout form(&columnDialog);

    QComboBox nameColumnComboBox;
    QComboBox utcdatetimeColumnComboBox;
    QComboBox valueColumnComboBox;

    nameColumnComboBox.addItems(availableColumns);
    utcdatetimeColumnComboBox.addItems(availableColumns);
    valueColumnComboBox.addItems(availableColumns);

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
    }

    return selectedColumns;
}

