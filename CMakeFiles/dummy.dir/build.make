# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.5

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/undergrats/dio/starpu/smartCoopScheduler

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/undergrats/dio/starpu/smartCoopScheduler

# Include any dependencies generated for this target.
include CMakeFiles/dummy.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/dummy.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/dummy.dir/flags.make

CMakeFiles/dummy.dir/dummy_sched.c.o: CMakeFiles/dummy.dir/flags.make
CMakeFiles/dummy.dir/dummy_sched.c.o: dummy_sched.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/undergrats/dio/starpu/smartCoopScheduler/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/dummy.dir/dummy_sched.c.o"
	/usr/bin/cc  $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/dummy.dir/dummy_sched.c.o   -c /home/undergrats/dio/starpu/smartCoopScheduler/dummy_sched.c

CMakeFiles/dummy.dir/dummy_sched.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/dummy.dir/dummy_sched.c.i"
	/usr/bin/cc  $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/undergrats/dio/starpu/smartCoopScheduler/dummy_sched.c > CMakeFiles/dummy.dir/dummy_sched.c.i

CMakeFiles/dummy.dir/dummy_sched.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/dummy.dir/dummy_sched.c.s"
	/usr/bin/cc  $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/undergrats/dio/starpu/smartCoopScheduler/dummy_sched.c -o CMakeFiles/dummy.dir/dummy_sched.c.s

CMakeFiles/dummy.dir/dummy_sched.c.o.requires:

.PHONY : CMakeFiles/dummy.dir/dummy_sched.c.o.requires

CMakeFiles/dummy.dir/dummy_sched.c.o.provides: CMakeFiles/dummy.dir/dummy_sched.c.o.requires
	$(MAKE) -f CMakeFiles/dummy.dir/build.make CMakeFiles/dummy.dir/dummy_sched.c.o.provides.build
.PHONY : CMakeFiles/dummy.dir/dummy_sched.c.o.provides

CMakeFiles/dummy.dir/dummy_sched.c.o.provides.build: CMakeFiles/dummy.dir/dummy_sched.c.o


# Object files for target dummy
dummy_OBJECTS = \
"CMakeFiles/dummy.dir/dummy_sched.c.o"

# External object files for target dummy
dummy_EXTERNAL_OBJECTS =

dummy: CMakeFiles/dummy.dir/dummy_sched.c.o
dummy: CMakeFiles/dummy.dir/build.make
dummy: CMakeFiles/dummy.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/undergrats/dio/starpu/smartCoopScheduler/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable dummy"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/dummy.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/dummy.dir/build: dummy

.PHONY : CMakeFiles/dummy.dir/build

CMakeFiles/dummy.dir/requires: CMakeFiles/dummy.dir/dummy_sched.c.o.requires

.PHONY : CMakeFiles/dummy.dir/requires

CMakeFiles/dummy.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/dummy.dir/cmake_clean.cmake
.PHONY : CMakeFiles/dummy.dir/clean

CMakeFiles/dummy.dir/depend:
	cd /home/undergrats/dio/starpu/smartCoopScheduler && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/undergrats/dio/starpu/smartCoopScheduler /home/undergrats/dio/starpu/smartCoopScheduler /home/undergrats/dio/starpu/smartCoopScheduler /home/undergrats/dio/starpu/smartCoopScheduler /home/undergrats/dio/starpu/smartCoopScheduler/CMakeFiles/dummy.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/dummy.dir/depend
