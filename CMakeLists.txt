cmake_minimum_required(VERSION 3.14)
project(COHIRADIAStreamer LANGUAGES CXX)

# Automatische Generierung der MOC-Files aktivieren (Essentiell für Signals/Slots)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Suche nach Qt6 (Fallback auf Qt5 möglich, falls gewünscht)
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Suche nach Liquid-DSP Bibliothek
find_library(LIQUID_LIB NAMES liquid REQUIRED)
include_directories(/usr/local/include) # Sicherstellung, dass Header gefunden werden

# --- ANPASSUNG: Alle neuen Dateien dem Executable hinzufügen ---
add_executable(COHIRADIAStreamer 
    main.cpp
    MainWindow.cpp
    MainWindow.h
    DspWorker.cpp
    DspWorker.h
    BinaryStructures.h
)

# Verlinken der Bibliotheken
target_link_libraries(COHIRADIAStreamer PRIVATE 
    Qt6::Widgets
    ${LIQUID_LIB}
    m  # Mathematische Bibliothek für sqrtf, sin, cos
)

# Optimierungen für die DSP-Engine (25 MSPS erfordern Power!)
if(MSVC)
    target_compile_options(COHIRADIAStreamer PRIVATE /O2 /arch:AVX2)
else()
    # -O3 und -ffast-math sind kritisch für msresamp
    target_compile_options(COHIRADIAStreamer PRIVATE -O3 -march=native -ffast-math)
endif()