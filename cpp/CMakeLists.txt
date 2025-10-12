cmake_minimum_required(VERSION 4.1.1)
project("ComputerVision")

#make sure it doesnt cache stuff that could screw build up between repo and prebuilt
get_filename_component(CACHE_DIR ${CMAKE_BINARY_DIR}/.. ABSOLUTE)
file(REMOVE_RECURSE ${CACHE_DIR})
#change here repo for testing or prebuilt for production
#(repo: cd main/cpp/; git clone https://github.com/Tencent/ncnn.git; cd ncnn; git submodule update --init)
set(NCNN_PREBUILT ON CACHE BOOL "Use prebuilt NCNN libraries (ON) or build from source (OFF)" FORCE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
set(CMAKE_ANDROID_STL_TYPE c++_shared CACHE STRING "Android STL type" FORCE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_link_options(
        -s
        -Wl,--gc-sections
        -Wl,--no-undefined
)
add_compile_options(
        -O3
        -fomit-frame-pointer
        -ffunction-sections -fdata-sections
)
include_directories(include/internal)

if(NCNN_PREBUILT)
    file(GLOB ALL_WITH_NCNN LIST_DIRECTORIES true "include/external/*")
    include_directories(
            ${ALL_WITH_NCNN}
            ABIs/${ANDROID_ABI}
    )
    file(GLOB_RECURSE LIBS ABIs/${ANDROID_ABI}/*.so)
else()
    file(GLOB ALL_BUT_NCNN LIST_DIRECTORIES true "include/external/*")
    list(FILTER ALL_BUT_NCNN EXCLUDE REGEX ".*/ncnn$")
    include_directories(${ALL_BUT_NCNN})

    set(ANDROID_USE_LEGACY_TOOLCHAIN_FILE OFF CACHE BOOL "Use legacy toolchain file" FORCE)
    set(ANDROID_STL ${CMAKE_ANDROID_STL_TYPE} CACHE STRING "Android STL to use" FORCE)
    set(NCNN_VULKAN ON CACHE BOOL "Vulkan support" FORCE)
    set(NCNN_OPENMP OFF CACHE BOOL "OpenMP support" FORCE)
    set(NCNN_SYSTEM_GLSLANG OFF CACHE BOOL "Use system glslang library" FORCE)
    set(NCNN_PIXEL_ROTATE OFF CACHE BOOL "Pixel rotation" FORCE)
    set(NCNN_PIXEL_AFFINE OFF CACHE BOOL "Pixel affine" FORCE)
    set(NCNN_PIXEL_DRAWING OFF CACHE BOOL "Pixel drawing" FORCE)
    set(NCNN_BUILD_TESTS OFF CACHE BOOL "NCNN tests build" FORCE)
    set(NCNN_BUILD_TOOLS OFF CACHE BOOL "NCNN tools build" FORCE)
    set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "NCNN examples build" FORCE)
    set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "NCNN benchmark build" FORCE)
    set(NCNN_COMPILER_SUPPORT_GNU_INLINE_ASM OFF CACHE BOOL "GNU inline assembly" FORCE)
    set(NCNN_COMPILER_SUPPORT_ARM_VFPV4 OFF CACHE BOOL "ARM VFPv4 support")
    set(NCNN_COMPILER_SUPPORT_ARM82_FP16 OFF CACHE BOOL "ARMv8.2 FP16 support")
    set(NCNN_COMPILER_SUPPORT_ARM82_DOTPROD OFF CACHE BOOL "ARMv8.2 Dot Product support")
    set(NCNN_COMPILER_SUPPORT_ARM82_FP16FML OFF CACHE BOOL "ARMv8.2 FP16 FML support")
    set(NCNN_COMPILER_SUPPORT_ARM84_BF16 OFF CACHE BOOL "ARMv8.4 BF16 support")
    set(NCNN_COMPILER_SUPPORT_ARM84_I8MM OFF CACHE BOOL "ARMv8.4 I8MM support")
    set(NCNN_COMPILER_SUPPORT_ARM86_SVE OFF CACHE BOOL "ARMv8.6 SVE support")
    set(NCNN_COMPILER_SUPPORT_ARM86_SVE2 OFF CACHE BOOL "ARMv8.6 SVE2 support")
    set(NCNN_COMPILER_SUPPORT_ARM86_SVEBF16 OFF CACHE BOOL "ARMv8.6 SVE BF16 support")
    set(NCNN_COMPILER_SUPPORT_ARM86_SVEI8MM OFF CACHE BOOL "ARMv8.6 SVE I8MM support")
    set(NCNN_COMPILER_SUPPORT_ARM86_SVEF32MM OFF CACHE BOOL "ARMv8.6 SVE F32MM support")
    set(CMAKE_HAVE_LIBC_PTHREAD ON CACHE BOOL "pthread init" FORCE)
    set(CMAKE_HAVE_PTHREAD_CREATE ON CACHE BOOL "pthread init" FORCE)
    set(CMAKE_HAVE_PTHREADS_CREATE ON CACHE BOOL "pthread init" FORCE)
    set(THREADS_HAVE_PTHREAD_ARG ON CACHE BOOL "Use pthread_arg" FORCE)
    set(COMPILER_HAS_HIDDEN_VISIBILITY OFF CACHE BOOL "hidden visibility" FORCE)
    set(COMPILER_HAS_HIDDEN_INLINE_VISIBILITY OFF CACHE BOOL "hidden inline visibility" FORCE)
    set(COMPILER_HAS_DEPRECATED_ATTR OFF CACHE BOOL "deprecated attribute" FORCE)
    set(COMPILER_HAS_DEPRECATED OFF CACHE BOOL "deprecated" FORCE)
    set(OpenMP_COMPILE_RESULT_C_fopenmplibomp OFF CACHE BOOL "Use fopenmplibomp" FORCE)

    add_subdirectory(ncnn)
    set(LIBS ncnn)
endif()

file(GLOB_RECURSE SRC src/*.cpp src/*.c)
add_library("ComputerVision" SHARED ${SRC})
target_link_libraries("ComputerVision" PRIVATE
        android
        log
        ${LIBS}
)