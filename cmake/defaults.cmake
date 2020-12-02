# Default configuration values
#
# You can change these through CMake by adding -D<variable>=<value> when
# configuring the build.

option(BUILD_BINLOG "Build binlog router" ON)
option(BUILD_CDC "Build Avro router" ON)
option(BUILD_FILTERS "Build filter modules" ON)
option(BUILD_GUI "Build MaxScale admin GUI" ON)
option(BUILD_KAFKACDC "Build MariaDB-to-Kafka CDC module" ON)
option(BUILD_LUAFILTER "Build Luafilter" ON)
option(BUILD_MAXCTRL "Build MaxCtrl" ON)
option(BUILD_MIRROR "Build Mirror router" ON)
option(BUILD_RABBITMQ "Build RabbitMQ components" ON)
option(BUILD_STORAGE_MEMCACHED "Build Memcached-based storage for Cache" ON)
option(BUILD_STORAGE_REDIS "Build Redis-based storage for Cache" ON)
option(BUILD_GSSAPI "Build GSSAPI authenticator" ON)
option(BUILD_SYSTEM_TESTS "Build system tests" OFF)
option(BUILD_TESTS "Build tests" ON)
option(BUILD_TOOLS "Build extra utility tools" ON)
option(BUILD_MONGO "Build MongoDB protocol" OFF)
option(GCOV "Use gcov build flags" OFF)
option(INSTALL_EXPERIMENTAL "Install experimental modules" OK)
option(PACKAGE "Enable package building (this disables local installation of system files)" OFF)
option(PROFILE "Profiling (gprof)" OFF)
option(STATIC_EMBEDDED "Use static version of libmysqld" ON)
option(WITH_ASAN "Enable AddressSanitizer" OFF)
option(WITH_JEMALLOC "Use jemalloc as the memory allocator" OFF)
option(WITH_MAXSCALE_CNF "Install the template maxscale.cnf file" ON)
option(WITH_SCRIPTS "Install init.d scripts and ldconf configuration files" ON)
option(WITH_TCMALLOC "Use tcmalloc as the memory allocator" OFF)
option(WITH_TSAN "Enable ThreadSanitizer" OFF)

# Default package name
set(PACKAGE_NAME "maxscale" CACHE STRING "Name of the generated package")

# Which component to build (core, experimental, devel, cdc-connector, all)
set(TARGET_COMPONENT "core" CACHE STRING "Which component to build (core, experimental, devel, cdc-connector, all)")

# Default user for the administrative interface
set(DEFAULT_ADMIN_USER "root" CACHE STRING "Default user for the administrative interface")
