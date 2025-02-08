cmake_minimum_required(VERSION 3.28)
project(DroneGame C)

set(CMAKE_C_STANDARD 17)

# * Include packages
find_package(Curses REQUIRED)

# * Include directories
include_directories(${CURSES_INCLUDE_DIR})
include_directories(include)

# * Add the executables
add_executable(DroneGame main.c)
add_executable(blackboard src/blackboard.c)
add_executable(keyboard_manager src/keyboard_manager.c)
add_executable(obstacles src/obstacles.c)
add_executable(targets_generator src/targets_generator.c)
add_executable(drone_dynamics src/drone_dynamics.c)
add_executable(watchdog src/watchdog.c)
add_executable(inspector src/inspector_window.c)

# * Put all executables in the same folder
set_target_properties(
        DroneGame blackboard keyboard_manager obstacles targets_generator drone_dynamics watchdog
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

# * Link ncurses with the blackboard script
target_link_libraries(blackboard PRIVATE ${CURSES_LIBRARIES})
target_link_libraries(keyboard_manager PRIVATE ${CURSES_LIBRARIES})
target_link_libraries(inspector PRIVATE ${CURSES_LIBRARIES})
target_link_libraries(drone_dynamics m)