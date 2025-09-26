# ARM64 Cross-compilation toolchain file

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Specify cross-compiler
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Where to find target environment
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# ARM-specific compiler flags
set(CMAKE_C_FLAGS "-march=armv8-a")
set(CMAKE_CXX_FLAGS "-march=armv8-a")

# Define ARM64 architecture
add_definitions(-D__aarch64__)

# Disable features not available in cross-compilation
set(CMAKE_CROSSCOMPILING_EMULATOR "")