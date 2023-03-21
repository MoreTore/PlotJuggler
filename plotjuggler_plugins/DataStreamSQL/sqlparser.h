#include <QSqlTableModel>
#include <QString>
#include <string>
#include <functional>
#include <map>

class SQLParser
{
public:
  SQLParser(QSqlTableModel* model)
      : _model(model)
  {
  }

  bool parseMessage(double& timestamp)
  {
    // Define a recursive function to parse the data
    std::function<void(int row, const QString& prefix)> parseImpl;

    parseImpl = [&](int row, const QString& prefix) {
      int columnCount = _model->columnCount();

      for (int column = 0; column < columnCount; column++)
      {
        QString key = prefix.isEmpty()
                          ? _model->headerData(column, Qt::Horizontal).toString()
                          : prefix + "/" + _model->headerData(column, Qt::Horizontal).toString();

        QVariant data = _model->data(_model->index(row, column));

        if (data.isNull())
        {
          continue;
        }

        if (data.type() == QVariant::Double || data.type() == QVariant::Int || data.type() == QVariant::LongLong || data.type() == QVariant::UInt || data.type() == QVariant::ULongLong)
        {
          double value = data.toDouble();
          // Store the data in your data structure
          // For example, you can use a similar approach to the one used in ProtobufParser
          auto& series = this->getSeries(key.toStdString());
          series.pushBack({timestamp, value});
        }
        else if (data.type() == QVariant::String)
        {
          QString value = data.toString();
          // Store the data in your data structure
          // For example, you can use a similar approach to the one used in ProtobufParser
          auto& series = this->getStringSeries(key.toStdString());
          series.pushBack({timestamp, value.toStdString()});
        }
        // Add more data types as needed
      }
    };

    // Iterate through all the rows in the table and call parseImpl for each row
    int rowCount = _model->rowCount();
    for (int row = 0; row < rowCount; row++)
    {
      // Retrieve the timestamp from a specific column (e.g., column 0)
      timestamp = _model->data(_model->index(row, 0)).toDouble();
      parseImpl(row, "");
    }

    return true;
  }

private:
  QSqlTableModel* _model;

  // Add methods and data structures similar to those in ProtobufParser
  // to store the parsed data
};
