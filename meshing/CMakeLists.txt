cmake_minimum_required(VERSION 2.8.3)
project(meshing)

if(CMAKE_VERSION VERSION_LESS 3.1)
    add_compile_options(-std=c++11)
else()
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS ON)
endif()

# Enable Afront Debug Information
#add_definitions(-DAFRONTDEBUG)

find_package(catkin REQUIRED cmake_modules roscpp)

find_package(VTK 7.1 REQUIRED NO_MODULE)
include(${VTK_USE_FILE})

find_package(PCL 1.8.1 EXACT REQUIRED)

find_package(Eigen3 REQUIRED)

find_package(OpenMP)
if(OPENMP_FOUND)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else(OPENMP_FOUND)
  message (STATUS "Not found OpenMP")
endif()

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES meshing
  CATKIN_DEPENDS
    roscpp
  DEPENDS
    Eigen
    PCL

)

include_directories(
    include
    ${catkin_INCLUDE_DIRS}
    ${EIGEN_INCLUDE_DIRS}
    ${PCL_INCLUDE_DIRS}
)

add_library(meshing
    src/afront_meshing.cpp
    src/mls_sampling.cpp
)

target_link_libraries(meshing
    ${catkin_LIBRARIES}
    ${PCL_LIBRARIES}
)

add_executable(afront_node src/afront_node.cpp)

target_link_libraries(afront_node
    ${catkin_LIBRARIES}
    ${PCL_LIBRARIES}
    meshing
)

catkin_add_gtest(${PROJECT_NAME}-test test/utest.cpp)
if(TARGET ${PROJECT_NAME}-test)
    target_link_libraries(${PROJECT_NAME}-test ${PROJECT_NAME})
endif()

#############
## Install ##
#############
install(
  DIRECTORY include/${PROJECT_NAME}/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION})

install(
  TARGETS
    meshing
    afront_node
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)
