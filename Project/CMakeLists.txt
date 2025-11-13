# Versión mínima de CMake
cmake_minimum_required(VERSION 3.10)

# Nombre del proyecto y lenguaje (C++)
project(Loqui CXX)

# Establecer el estándar de C++ a C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- CONFIGURACIÓN UNICODE PARA WINDOWS ---
if(WIN32)
    # Habilitar Unicode
    add_compile_definitions(UNICODE _UNICODE)
    # Configurar codificación UTF-8
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
    # Para MinGW/GCC
    add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-finput-charset=UTF-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-fexec-charset=UTF-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-fwide-exec-charset=UTF-8>")
endif()

# Añadir el ejecutable del servidor
add_executable(LoquiServer server.cpp)

# Añadir el ejecutable del cliente
add_executable(LoquiClient client.cpp)

# --- Configuración Específica para Windows ---
if(WIN32)
    target_link_libraries(LoquiServer ws2_32)
    target_link_libraries(LoquiClient ws2_32)
endif()