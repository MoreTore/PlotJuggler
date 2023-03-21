#include "dataload_sql.h"
#include "Worker.h"
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
#include <QTableView>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QtConcurrent/QtConcurrent>


DataLoadSQL::DataLoadSQL()
{
  _extensions.push_back("sqlph");
}

const QRegExp metasys_regx = QRegExp("[. /:]"); // regex for splitting metasys point names

const std::vector<const char*>& DataLoadSQL::compatibleFileExtensions() const
{
  return _extensions;
}

// Add this new function
QString selectTable(QSqlDatabase &db)
{
    QDialog tableDialog;
    tableDialog.setWindowTitle("Select Table");

    QVBoxLayout layout;
    QTableView tableView;

    QSqlQueryModel model;
    model.setQuery(QSqlQuery("SHOW TABLES", db));

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

bool DataLoadSQL::readDataFromFile(FileLoadInfo* info, PlotDataMapRef& plot_data)
{ 

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
// default values loaded from settings into the lineEdits
lineEdit1->setText(settings.value("hostName", "localhost").toString());
lineEdit2->setText(settings.value("dbName", "sys").toString());
lineEdit3->setText(settings.value("userName", "ryley").toString());
lineEdit4->setText(settings.value("password", "12345678").toString());

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
} else {
    qDebug() << "Dialog was cancelled";
    return false;
}

    
QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");

//hostName = QInputDialog::getText(nullptr, "Database Host", "Enter database host (e.g., localhost):", QLineEdit::Normal, hostName);
//dbName = QInputDialog::getText(nullptr, "Database Name", "Enter database name:", QLineEdit::Normal, dbName);
//userName = QInputDialog::getText(nullptr, "User Name", "Enter database user name:", QLineEdit::Normal, userName);
//password = QInputDialog::getText(nullptr, "Password", "Enter database password:", QLineEdit::Password, password);

  hostName = lineEdit1->text();
  dbName = lineEdit2->text();
  userName = lineEdit3->text();
  password = lineEdit4->text();
  
  // Open the database
  db.setHostName(hostName);
  db.setDatabaseName(dbName);
  db.setUserName(userName);
  db.setPassword(password);
  settings.setValue("hostName", hostName);
  settings.setValue("dbName", dbName);
  settings.setValue("userName", userName);
  settings.setValue("password", password);

  if (!db.open()) {
    qDebug() << "Database error:" << db.lastError().text();
    // Warning message box 
    QMessageBox msgBox;
    msgBox.setText("Database error:" + db.lastError().text());
    msgBox.exec();
    return false;
  } else {
    qDebug() << "Database connection established";
    qDebug() << "Database name:" << db.databaseName();
  }

  QString selectedTable = selectTable(db);
  if (selectedTable.isEmpty()) {
      qDebug() << "No table selected";
      return false;
  }
    QSqlQueryModel model;
    model.setQuery(QSqlQuery("SELECT * FROM " + selectedTable + " LIMIT 200", db));
    QTableView tableView;
    tableView.setModel(&model);
    tableView.setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView.setSelectionMode(QAbstractItemView::SingleSelection);
    tableView.resizeColumnsToContents();

    // add a button to cancel the dialog
    //QPushButton cancelButton("Cancel");
    // add a button to select the table
    //QPushButton selectButton("Select");

    // add the buttons to the layout
    //QVBoxLayout layout;
    //layout.addWidget(&tableView);
    //layout.addWidget(&selectButton);
    //layout.addWidget(&cancelButton);
    //tableView.setLayout(&layout);
    //tableView.show();


  // promt user for query text
  //QString query_text = QInputDialog::getText(nullptr, "Query", "Enter query text");
  // Prepare the query

  // Execute the query and iterate over the results line by line
  int query_count = 0;
  int linecount = 0;
  // create a vector of timeseries
  std::vector<PlotData*> plots_vector;
  
  QSqlQuery query;
  query = QSqlQuery("SELECT COUNT(*) FROM " + selectedTable, db);

  if (query.exec() && query.next()) {
    query_count = query.value(0).toInt();
    // reset the query
    query.finish(); // finish the query
    qDebug() << "Number of entries:" << query_count;
  }

  QProgressDialog progress_dialog;
  progress_dialog.setLabelText("Loading... please wait");
  progress_dialog.setWindowModality(Qt::ApplicationModal);
  progress_dialog.setRange(0, query_count - 1);
  progress_dialog.setAutoClose(true);
  progress_dialog.setAutoReset(true);
  progress_dialog.setValue(1);
  progress_dialog.setMinimumDuration(0);
  progress_dialog.show();

  // please wait message box pops up 
  QMessageBox msgBox;
  msgBox.setText("Please wait...");
  //msgBox.exec();
  
  qDebug() << "Loading query.";
  // Pass the database connection name to the lambda function
  auto dbConnectionName = db.connectionName();
  
  // Wrap the query execution in a lambda function
  auto executeQuery = [selectedTable, dbConnectionName]() -> QSqlQuery {
      QSqlDatabase threadDb = QSqlDatabase::database(dbConnectionName);
      QSqlQuery query("SELECT * FROM " + selectedTable, threadDb);
      if (query.exec()) {
          qDebug() << "Query executed successfully";
      } else {
          qDebug() << "Query error:" << query.lastError().text() << "\n\rQuary text:" << query.lastQuery();
      }
      return query;
  };
  
  // Run the query in a separate thread
  QFuture<QSqlQuery> future = QtConcurrent::run(executeQuery);
  QFutureWatcher<QSqlQuery> watcher;
  QObject::connect(&watcher, &QFutureWatcher<QSqlQuery>::finished, [&]() {
      QSqlQuery query = future.result();
      if (query.isActive()) {
          // Process the results of the query
          if(query.next()){

          }
          
      } else {
          // Handle the error
          return false;
      }
  });
  
  watcher.setFuture(future);

  // exec a new query "SELECT * FROM PointData"
  //query = QSqlQuery("SELECT * FROM PointData", db);
  // Replace the table name 'PointData' with the selectedTable variable
  query = QSqlQuery("SELECT * FROM " + selectedTable, db);
  
  if (query.exec()) { // This hangs the program 
    qDebug() << "Query executed successfully";
  } else {
    qDebug() << "Query error:" << query.lastError().text() << "\n\rQuary text:" << query.lastQuery();
    return false;
  }

  bool interrupted = false;
  // close the message box
  //msgBox.close();
  
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
      // update the simple dialog box
      progress_dialog.setLabelText("Loading... please wait (" + QString::number(linecount) + "/" + QString::number(query_count) + ")");
      progress_dialog.setValue(linecount);
      QApplication::processEvents();
      if (progress_dialog.wasCanceled())
      {
        interrupted = true;
        break;
      }
    }
    linecount++;
  }

  if (interrupted)
  {
    progress_dialog.cancel();
    // promt user to load the data or clear the data
    QMessageBox msgBox;
    msgBox.setText("The data load was interrupted.");
    msgBox.setInformativeText("Do you want to load the data that was loaded so far?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    int ret = msgBox.exec();
    switch (ret) {
      case QMessageBox::Yes:
        // Yes was clicked
        break;
      case QMessageBox::No:
        // No was clicked
        plot_data.clear();
        break;
      default:
        // should never be reached
        break;
    }
    
  }
  // Close the database
  qDebug() << "Closing database connection";
  db.close();
  return true;
}
