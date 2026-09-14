cmake_minimum_required(VERSION 4.4.2)
project(DDMEditor CXX)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

#find_program(CPPCHECK_PROGRAM cppcheck)
#if(CPPCHECK_PROGRAM)
#    set(CMAKE_CXX_CPPCHECK "${CPPCHECK_PROGRAM};--enable=warning,portability")
#endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE LIB_SOURCE
	"./main.cpp"

)
add_executable(DDMEditor ${LIB_SOURCE})

#file(GLOB_RECURSE LIB_HEADERS "include/*.h" "src/*.h")

find_package(SDL3 REQUIRED)
add_library(FinalSTATIC STATIC IMPORTED)
set_target_properties(FinalSTATIC PROPERTIES
	IMPORTED_LOCATION "/home/genios/Projects/NEELA/Final/build/Final.a"
)
target_include_directories(DDMEditor PRIVATE
	"/home/genios/Projects/NEELA/Final/include"
)
target_link_libraries(DDMEditor PRIVATE FinalSTATIC)
target_link_libraries(DDMEditor PRIVATE SDL3::SDL3)

#add_compile_definitions(NDEBUG)
