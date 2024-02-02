#Checks for all the C system headers found in all the files

include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckIncludeFiles)
include(CheckCXXSourceCompiles)
include(CheckCXXSourceRuns)

# Check for libraries MaxScale depends on
find_library(HAVE_LIBSSL NAMES ssl)
if(NOT HAVE_LIBSSL)
  message(FATAL_ERROR "Could not find libssl")
endif()

find_library(HAVE_LIBCRYPT NAMES crypt)
if(NOT HAVE_LIBCRYPT)
  message(FATAL_ERROR "Could not find libcrypt")
endif()

find_library(HAVE_LIBCRYPTO NAMES crypto)
if(NOT HAVE_LIBCRYPTO)
  message(FATAL_ERROR "Could not find libcrypto")
endif()

find_library(HAVE_LIBZ NAMES z)
if(NOT HAVE_LIBZ)
  message(FATAL_ERROR "Could not find libz")
endif()

find_library(HAVE_LIBM NAMES m)
if(NOT HAVE_LIBM)
  message(FATAL_ERROR "Could not find libm")
endif()

find_library(HAVE_LIBRT NAMES rt)
if(NOT HAVE_LIBRT)
  message(FATAL_ERROR "Could not find librt")
endif()

find_library(HAVE_LIBPTHREAD NAMES pthread)
if(NOT HAVE_LIBPTHREAD)
  message(FATAL_ERROR "Could not find libpthread")
endif()

# run "ps -p 1 | grep systemd" to determine if this system uses systemd
execute_process(
    COMMAND "ps" "-p" "1"
    COMMAND "grep" "systemd"
    RESULT_VARIABLE NOT_SYSTEMD_IS_RUNNING
    OUTPUT_VARIABLE PS_OUTPUT)

find_library(HAVE_SYSTEMD NAMES systemd)
if(HAVE_SYSTEMD)
    add_definitions(-DHAVE_SYSTEMD=1)
elseif(NOT BUILD_SYSTEM_TESTS)
    # If systemd is in use, require libsystemd-dev to be installed
    if(NOT NOT_SYSTEMD_IS_RUNNING)
        message(FATAL_ERROR "systemd is running: please install libsystemd-dev (DEB) or systemd-devel (RPM)")
    endif()
endif()

# The XSI version of strerror_r return an int and the GNU version a char*
check_cxx_source_compiles("
  #define _GNU_SOURCE 1
  #include <string.h>\n
  int main(){\n
      char errbuf[200];\n
      return strerror_r(13, errbuf, sizeof(errbuf)) == errbuf;\n
  }\n"
  HAVE_GLIBC)

if(HAVE_GLIBC)
  add_definitions(-DHAVE_GLIBC=1)
endif()

# The tgkill() wrapper was added in glibc 2.30. CentOS 7 and RockyLinux 8 have
# older versions of glibc where the wrapper is not yet present.
check_cxx_source_compiles("
  #include <signal.h>\n
  #include <unistd.h>\n
  int main(){\n
      tgkill(getpid(), gettid(), SIGTERM);\n
      return 0;\n
  }\n"
  HAVE_TGKILL)

if(HAVE_TGKILL)
  add_definitions(-DHAVE_TGKILL=1)
endif()

if(WITH_ASAN)
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-lcrypt")
  set(TEST_CODE "#include <crypt.h>
int main()
{
struct crypt_data data{};
return crypt_r(\"hello\", \"$6$world\", &data) ? 0 : 1;
}")
  check_cxx_source_runs("${TEST_CODE}" CRYPT_R_WORKS_WITH_ASAN)
else()
  set(CRYPT_R_WORKS_WITH_ASAN 1)
endif()
