cmake_minimum_required (VERSION 3.2)
project (dummy)

find_package(PkgConfig)
pkg_check_modules(STARPU REQUIRED starpu-1.2)
if (STARPU_FOUND)
        include_directories (${STARPU_INCLUDE_DIRS})
            link_directories    (${STARPU_STATIC_LIBRARY_DIRS})
                link_libraries      (${STARPU_STATIC_LIBRARIES})
            else (STARPU_FOUND)
                    message(FATAL_ERROR "StarPU not found")
                endif()

add_executable(dummy dummy_sched.c)
