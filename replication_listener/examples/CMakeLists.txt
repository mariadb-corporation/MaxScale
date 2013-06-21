project(examples)

link_directories(${PROJECT_BUILD_DIR}/lib)
include_directories(${PROJECT_BUILD_DIR}/include)

# Create build rules for all the simple examples that only require a
# single file.
foreach(prog basic-1 basic-2 jan_test)
  ADD_EXECUTABLE(${prog} ${prog}.cpp)
  TARGET_LINK_LIBRARIES(${prog} boost_system replication)
endforeach()

add_subdirectory(mysql2lucene EXCLUDE_FROM_ALL)
