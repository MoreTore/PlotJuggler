
include_directories(  ../ )

add_definitions(${QT_DEFINITIONS})
add_definitions(-DQT_PLUGIN)

SET( SRC
    dataload_metasys.cpp
    )

add_library(DataLoadMETASYS SHARED ${SRC} ${UI_SRC}  )
target_link_libraries(DataLoadMETASYS
    ${Qt5Widgets_LIBRARIES}
    ${Qt5Xml_LIBRARIES}
    plotjuggler_base
    QCodeEditor)


install(TARGETS DataLoadMETASYS DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY}  )

