cmake_minimum_required(VERSION 2.8.7)
project(fato_cuda)

find_package(catkin REQUIRED COMPONENTS
  cmake_modules
  roscpp
  resource_retriever
)

if(TRACKER_VERBOSE_LOGGING)
  add_definitions(-DVERBOSE_LOGGING)
endif()

find_package(OpenCV REQUIRED)
find_package(OpenGL REQUIRED)
find_package(CUDA 6.5 REQUIRED)
find_package(Eigen REQUIRED)
add_definitions(${EIGEN_DEFINITIONS})
find_package(urdfdom REQUIRED)

set(GENCODE_SM20 -gencode=arch=compute_20,code=sm_20)
set(GENCODE_SM30 -gencode=arch=compute_30,code=sm_30)
set(GENCODE_SM35 -gencode=arch=compute_35,code=sm_35)
set(GENCODE_SM50 -gencode=arch=compute_50,code=sm_50)
set(GENCODE_SM52 -gencode=arch=compute_52,code=sm_52)
set(CUDA_NVCC_FLAGS -O3;--compiler-options;-fPIC;-ftz=true;-prec-sqrt=false;-prec-div=false)
set(CUDA_PROPAGATE_HOST_FLAGS OFF) # don't propagate c++11 options to nvcc
if (${CUDA_VERSION_MAJOR} LESS 7)
    set(CUDA_OPTIONS ${GENCODE_SM20} ${GENCODE_SM30} ${GENCODE_SM35} ${VERBOSE_PTXAS})
else()
    set(CUDA_OPTIONS ${GENCODE_SM20} ${GENCODE_SM30} ${GENCODE_SM35} ${GENCODE_SM50} ${GENCODE_SM52} ${VERBOSE_PTXAS})
endif()

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

pkg_check_modules(GLEW REQUIRED glew)


# OGRE SETUP DEFINED BY KARL
pkg_check_modules(OGRE_OV OGRE OGRE-Overlay)
# Old versions of OGRE (pre 1.9) included OGRE-Overlay in the main package
# (i.e. there was no OGRE-Overlay component).  So if the above
# pkg_check_modules() failed, try looking for just OGRE.
if(NOT OGRE_OV_FOUND)
  pkg_check_modules(OGRE_OV REQUIRED OGRE)
endif(NOT OGRE_OV_FOUND)
## Find OGRE Plugin path (not necessarily platform-independent, I guess)
execute_process(COMMAND
  pkg-config --variable=plugindir OGRE
  OUTPUT_VARIABLE OGRE_PLUGIN_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )


# Build the version number and other build-time constants into the
# source for access at run-time.
set(ENV_CONFIG_FILE ${CMAKE_CURRENT_BINARY_DIR}/env_config.cpp)
#set(RVIZ_VERSION ${rviz_VERSION})
#message("making version ${RVIZ_VERSION}.")
set(ROS_DISTRO $ENV{ROS_DISTRO})


# find absolute path of ogre libraries.
# This is stored in the cache to save time on cmake re-run.
# Using absolute paths is necessary if pkg-config finds Ogre in a different
# location than the default.  This can happen when Ogre is built from source,
# or when 2 versions of Ogre are installed.  Using absolute paths ensures that
# components that link against Ogre (rviz and any packages depending on rviz)
# all find the same Ogre shared library.
if(NOT DEFINED OGRE_OV_LIBRARIES_ABS)
  unset(OGRE_OV_LIBRARIES_ABS_TMP)
  foreach(_lib ${OGRE_OV_LIBRARIES})
    set(OGRE_OV_LIB_TAG "OGRE_OV_RVIZ_LIB_${_lib}")

    find_library(${OGRE_OV_LIB_TAG}
            NAMES ${_lib}
            HINTS ${OGRE_OV_LIBRARY_DIRS}
            PATHS ${OGRE_OV_LIBRARY_DIRS}
    )

    set(OGRE_OV_LIBRARIES_ABS_TMP ${OGRE_OV_LIBRARIES_ABS_TMP} ${${OGRE_OV_LIB_TAG}})
  endforeach(_lib)

  set(OGRE_OV_LIBRARIES_ABS ${OGRE_OV_LIBRARIES_ABS_TMP}
        CACHE FILEPATH "Pathname of library ${_lib}")
endif(NOT DEFINED OGRE_OV_LIBRARIES_ABS)
# add gl rendersystem
list(APPEND OGRE_OV_LIBRARIES_ABS
  ${OGRE_PLUGIN_PATH}/RenderSystem_GL.so
)
message(STATUS "OGRE_OV_LIBRARIES_ABS=${OGRE_OV_LIBRARIES_ABS}")

# OGRE SETUP

find_package(ASSIMP QUIET)
if (NOT ASSIMP_FOUND)
  pkg_check_modules(ASSIMP assimp)
endif()
if (ASSIMP_FOUND)
  if( NOT ${ASSIMP_VERSION} VERSION_LESS "2.0.1150" )
    add_definitions(-DASSIMP_UNIFIED_HEADER_NAMES)
    message(STATUS "Assimp version has unified headers")
  else()
    message(STATUS "Assimp version does not have unified headers")
  endif()
  include_directories(${ASSIMP_INCLUDE_DIRS})
  link_directories(${ASSIMP_LIBRARY_DIRS})
else()
  message(STATUS "could not find assimp (perhaps available through ROS package?), so assimping assimp v2")
  set(ASSIMP_LIBRARIES assimp)
  set(ASSIMP_LIBRARY_DIRS)
  set(ASSIMP_CXX_FLAGS)
  set(ASSIMP_CFLAGS_OTHER)
  set(ASSIMP_LINK_FLAGS)
  set(ASSIMP_INCLUDE_DIRS)
endif()



catkin_package(
  INCLUDE_DIRS
    include
    ${OGRE_OV_INCLUDE_DIRS}
    ${CUDA_INCLUDE_DIRS}
  DEPENDS
    libx11-dev
    opengl
    libglew-dev
  LIBRARIES
    ${PROJECT_NAME}
    ${OGRE_OV_LIBRARIES_ABS}
  CATKIN_DEPENDS
)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -march=native")

# show additional files in qtcreator
execute_process(COMMAND find ${PROJECT_SOURCE_DIR} -type f
  OUTPUT_VARIABLE FILES_STRING)
string(REPLACE "\n" ";" FILES_LIST ${FILES_STRING})
add_custom_target(additional_files_${PROJECT_NAME} SOURCES
  ${FILES_LIST}
  ${PROJECT_SOURCE_DIR}/../README.md
)

include_directories(include
  ${catkin_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS}
  ${EIGEN_INCLUDE_DIR}
  ${OGRE_OV_INCLUDE_DIRS}
  ${urdfdom_INCLUDE_DIRS}
)

cuda_add_library(${PROJECT_NAME}
  src/utility_kernels_pose.cu
  src/utility_kernels.cu
  OPTIONS ${CUDA_OPTIONS}
)

target_link_libraries(${PROJECT_NAME}
  ${OpenCV_LIBS}
  ${catkin_LIBRARIES}
  ${OGRE_OV_LIBRARIES_ABS}
  ${CUDA_LIBRARIES}
  ${urdfdom_LIBRARIES}
  assimp
)