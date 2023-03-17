#pragma once

#include <QObject>
#include <QtPlugin>
#include "PlotJuggler/dataloader_base.h"

using namespace PJ;

class DataLoadMETASYS : public DataLoader
{
  Q_OBJECT
  Q_PLUGIN_METADATA(IID "facontidavide.PlotJuggler3.DataLoader")
  Q_INTERFACES(PJ::DataLoader)

public:
  DataLoadMETASYS();
  virtual const std::vector<const char*>& compatibleFileExtensions() const override;

  bool readDataFromFile(PJ::FileLoadInfo* fileload_info,
                        PlotDataMapRef& destination) override;

  ~DataLoadMETASYS() override = default;

  virtual const char* name() const override
  {
    return "METASYS";
  }


protected:
  QSize parseHeader(QFile* file, std::vector<std::string>& ordered_names);

private:
  std::vector<const char*> _extensions;

  std::string _default_time_axis;
};


