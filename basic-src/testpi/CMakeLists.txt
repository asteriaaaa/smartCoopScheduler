cmake_minimum_required (VERSION 3.2)
project (dummy)

INCLUDE(/usr/share/cmake-3.5/Modules/FindCUDA.cmake)

find_package(PkgConfig)
pkg_check_modules(STARPU REQUIRED starpu-1.2)
if (STARPU_FOUND)
    include_directories (${STARPU_INCLUDE_DIRS} /home/undergrats/test_starpu/starpu-1.2.7/src)
            link_directories    (${STARPU_STATIC_LIBRARY_DIRS})
                link_libraries      (${STARPU_STATIC_LIBRARIES})
            else (STARPU_FOUND)
                    message(FATAL_ERROR "StarPU not found")
                endif()

		cuda_add_executable(dummy pi.c SobolQRNG/sobol_gold.c SobolQRNG/sobol_primitives.c SobolQRNG/sobol_gpu.cu pi_kernel.cu pi_redux_kernel.cu)
