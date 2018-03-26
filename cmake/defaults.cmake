# Default configuration values
#
# You can change these through CMake by adding -D<variable>=<value> when
# configuring the build.

# Use C99
set(USE_C99 TRUE CACHE BOOL "Use C99 standard")

# Default user for the administrative interface
set(DEFAULT_ADMIN_USER "root" CACHE STRING "Default user for the administrative interface")

# Install the template maxscale.cnf file
set(WITH_MAXSCALE_CNF TRUE CACHE BOOL "Install the template maxscale.cnf file")

# Use static version of libmysqld
set(STATIC_EMBEDDED TRUE CACHE BOOL "Use static version of libmysqld")

# Build RabbitMQ components
set(BUILD_RABBITMQ TRUE CACHE BOOL "Build RabbitMQ components")

# Build the binlog router
set(BUILD_BINLOG TRUE CACHE BOOL "Build binlog router")

# Build the Avro router
set(BUILD_CDC TRUE CACHE BOOL "Build Avro router")

# Build the multimaster monitor
set(BUILD_MMMON TRUE CACHE BOOL "Build multimaster monitor")

# Build MaxCtrl
set(BUILD_MAXCTRL TRUE CACHE BOOL "Build MaxCtrl")

# Build Luafilter
set(BUILD_LUAFILTER TRUE CACHE BOOL "Build Luafilter")

# Use gcov build flags
set(GCOV FALSE CACHE BOOL "Use gcov build flags")

# Install init.d scripts and ldconf configuration files
set(WITH_SCRIPTS TRUE CACHE BOOL "Install init.d scripts and ldconf configuration files")

# Build tests
set(BUILD_TESTS TRUE CACHE BOOL "Build tests")

# Build packages
set(PACKAGE FALSE CACHE BOOL "Enable package building (this disables local installation of system files)")

# Build extra tools
set(BUILD_TOOLS FALSE CACHE BOOL "Build extra utility tools")

# Profiling
set(PROFILE FALSE CACHE BOOL "Profiling (gprof)")

# Use tcmalloc as the memory allocator
set(WITH_TCMALLOC FALSE CACHE BOOL "Use tcmalloc as the memory allocator")

# Use jemalloc as the memory allocator
set(WITH_JEMALLOC FALSE CACHE BOOL "Use jemalloc as the memory allocator")

# Install experimental modules
set(INSTALL_EXPERIMENTAL TRUE CACHE BOOL "Install experimental modules")

# Default package name
set(PACKAGE_NAME "maxscale" CACHE STRING "Name of the generated package")

# Which component to build (core, experimental, devel, cdc-connector, all)
set(TARGET_COMPONENT "core" CACHE STRING "Which component to build (core, experimental, devel, cdc-connector, all)")

# Enable AddressSanitizer: https://github.com/google/sanitizers/wiki/AddressSanitizer
set(WITH_ASAN FALSE CACHE BOOL "Enable AddressSanitizer")
