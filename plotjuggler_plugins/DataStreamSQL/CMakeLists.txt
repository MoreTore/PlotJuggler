
include_directories( ../ )

add_definitions(${QT_DEFINITIONS})
add_definitions(-DQT_PLUGIN)

SET( SRC datastream_sql.cpp)

add_library(DataStreamSQL SHARED ${SRC} ${UI_SRC}  )
find_package(Qt5 COMPONENTS Sql REQUIRED)
target_link_libraries(DataStreamSQL
    ${Qt5Widgets_LIBRARIES}
    ${Qt5Xml_LIBRARIES}
    ${Qt5Sql_LIBRARIES}
    plotjuggler_base)

install(TARGETS DataStreamSQL DESTINATION ${PJ_PLUGIN_INSTALL_DIRECTORY}  )
