#include "dataload_metasys.h"
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

DataLoadMETASYS::DataLoadMETASYS()
{
  _extensions.push_back("metasys");
}

const QRegExp metasys_separator("(\\,)");

const std::vector<const char*>& DataLoadMETASYS::compatibleFileExtensions() const
{
  return _extensions;
}

bool DataLoadMETASYS::readDataFromFile(FileLoadInfo* info, PlotDataMapRef& plot_data)
{ 
  QFile file(info->filename);
  if( !file.open(QFile::ReadOnly) )
  {
    // error message
    QMessageBox::warning(nullptr, "Error reading file", "Cannot open file: " + info->filename );
    return false;
  }

  QTextStream text_stream(&file);
  // count the number of lines first
  int linecount = 1;
  QDialog dialog;
  dialog.setWindowTitle("Loading... please wait");
  dialog.setWindowModality(Qt::ApplicationModal);
  QVBoxLayout layout;
  dialog.setLayout(&layout);
  QLabel label;
  label.setText("Parsed " + QString::number(linecount) +" lines");
  layout.addWidget(&label);
  dialog.show();

  // close the file and reopen it
  file.close();
  if( !file.open(QFile::ReadOnly) )
  {
    // error message
    QMessageBox::warning(nullptr, "Error reading file", "Cannot open file: " + info->filename );
    return false;
  }

  // The first line should contain the name of the columns
  QString first_line = text_stream.readLine();
  QStringList column_names = first_line.split(metasys_separator);

  // each row will be the next time stamp
  // each column will be a point in the time series
  // PointName,PointID,PointSliceID,UTCDateTime,ActualValue

  // the first column will be the PointName
  // the second column will be the PointID
  // the third column will be the PointSliceID
  // the fourth column will be the UTCDateTime
  // the fifth column will be the ActualValue
  // we only care about the name, the time, and the value

  // the points are sorted by name, then by time so we need to add values to the point until we get to a new point name

  // create a vector of timeseries
  std::vector<PlotData*> plots_vector;
  //PlotDataMapRef datamap;
  //StringSeries& str_plot = datamap.addStringSeries("str_value")->second;
  //-----------------
  // read the file line by line
  
  
  while (!text_stream.atEnd())
  {
    QString line = text_stream.readLine();
    linecount++;

    // Split using the comma separator.
    QStringList string_items = line.split(metasys_separator);
    if (string_items.size() != column_names.size())

    {
      auto err_msg = QString("The number of values at line %1 is %2,\n"
                             "but the expected number of columns is %3.\n"
                             "Aborting...")
          .arg(linecount)
          .arg(string_items.size())
          .arg(column_names.size());

      QMessageBox::warning(nullptr, "Error reading file", err_msg );
      return false;
    }

    // The first column should contain the points names.
    QString current_point_name = string_items[0];
    QString next_point_name = current_point_name;
    
    
    while (current_point_name == next_point_name)
    {
      
      
      // create a new point
      current_point_name = next_point_name;
      // the forth column will be the UTCDateTime in the format yyyy-MM-dd hh:mm:ss
      QDateTime dt = QDateTime::fromString(string_items[3], "yyyy-MM-dd hh:mm:ss");
      bool is_number;
      double t = dt.toMSecsSinceEpoch()/1000.0;
      if (!dt.isValid())
      {
        //skip this point
        line = text_stream.readLine();
        linecount++;
        string_items = line.split(metasys_separator);
        next_point_name = string_items[0];
        continue;
      }
      
      // split the current point name into parts with QRegExp using ".:/ "
      QStringList pointNameParts = current_point_name.split(QRegExp("[.:/ ]"), QString::SplitBehavior::SkipEmptyParts);
      // remove the second to last part if it is ""
      pointNameParts.removeAt(pointNameParts.size() - 1);
      // join the vector of strings into a single string but with a '/' between each part
      qDebug() << "Point Name Before:" << current_point_name << "at query line" << linecount;
      current_point_name = pointNameParts.join("/");
      qDebug() << "Point Name After:" << current_point_name;
      
      // create a new timeseries
      std::string field_name = current_point_name.toStdString();
      
      auto it = plot_data.addNumeric(current_point_name.toStdString());
      
      plots_vector.push_back(&(it->second));
      
      // the fifth column will be the ActualValue which is a floating point number
      double y = string_items[4].toFloat(&is_number); // if the value is not a number, y will be 0.0
      if (is_number)
      {
        PlotData::Point point(t, y);
        plots_vector.back()->pushBack(point);
      }
      
      // Get the next point name
      line = text_stream.readLine();
      linecount++;
      if (linecount % 10000 == 0)
        label.setText("Parsed " + QString::number(linecount) +" lines");
        QApplication::processEvents();
      string_items = line.split(metasys_separator);
      next_point_name = string_items[0];
    }
  }

  file.close();

  return true;
}






