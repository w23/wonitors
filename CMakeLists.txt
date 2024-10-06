cmake_minimum_required(VERSION 3.5.0)
project(wonitors VERSION 0.1.0 LANGUAGES C)

add_executable(wonitors wonitors.c)
target_link_libraries(wonitors
    setupapi # setupapi.h, SetupDi*
    shcore # shellscalingapi.h, GetDpiForMonitor
)

# Not sure if this really gets to PerMonitorV2
#set_property(TARGET wonitors PROPERTY VS_DPI_AWARE "PerMonitor")

# Should get full PerMonitorV2 DPI-awareness
target_sources(wonitors PRIVATE dpi-aware.manifest)
