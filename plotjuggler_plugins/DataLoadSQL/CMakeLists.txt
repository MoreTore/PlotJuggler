
include_directories(  ../ )

add_definitions(${QT_DEFINITIONS})
add_definitions(-DQT_PLUGIN)

SET( SRC
    dataload_sql.cpp
    )

add_library(DataLoadSQL SHARED ${SRC} ${UI_SRC}  )
find_package(Qt5 COMPONENTS Sql REQUIRED)
target_link_libraries(DataLoadSQL
    ${Qt5Widgets_LIBRARIES}
    ${Qt5Xml_LIBRARIES}
    plotjuggler_base
    Qt5::Sql
    QCodeEditor)


install(TARGETS DataLoadSQL DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY}  )

