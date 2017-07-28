#Checks for all the C system headers found in all the files

include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckIncludeFiles)
include(CheckCXXSourceCompiles)

check_include_files(arpa/inet.h HAVE_ARPA_INET)
check_include_files(crypt.h HAVE_CRYPT)
check_include_files(ctype.h HAVE_CTYPE)
check_include_files(dirent.h HAVE_DIRENT)
check_include_files(dlfcn.h HAVE_DLFCN)
check_include_files(errno.h HAVE_ERRNO)
check_include_files(execinfo.h HAVE_EXECINFO)
check_include_files(fcntl.h HAVE_FCNTL)
check_include_files(ftw.h HAVE_FTW)
check_include_files(getopt.h HAVE_GETOPT)
check_include_files(ini.h HAVE_INI)
check_include_files(math.h HAVE_MATH)
check_include_files(netdb.h HAVE_NETDB)
check_include_files(netinet/in.h HAVE_NETINET_IN)
check_include_files(openssl/aes.h HAVE_OPENSSL_AES)
check_include_files(openssl/sha.h HAVE_OPENSSL_SHA)
check_include_files(pthread.h HAVE_PTHREAD)
check_include_files(pwd.h HAVE_PWD)
check_include_files(regex.h HAVE_REGEX)
check_include_files(signal.h HAVE_SIGNAL)
check_include_files(stdarg.h HAVE_STDARG)
check_include_files(stdbool.h HAVE_STDBOOL)
check_include_files(stdint.h HAVE_STDINT)
check_include_files(stdio.h HAVE_STDIO)
check_include_files(stdlib.h HAVE_STDLIB)
check_include_files(string.h HAVE_STRING)
check_include_files(strings.h HAVE_STRINGS)
check_include_files(sys/epoll.h HAVE_SYS_EPOLL)
check_include_files(sys/ioctl.h HAVE_SYS_IOCTL)
check_include_files(syslog.h HAVE_SYSLOG)
check_include_files(sys/param.h HAVE_SYS_PARAM)
check_include_files(sys/socket.h HAVE_SYS_SOCKET)
check_include_files(sys/stat.h HAVE_SYS_STAT)
check_include_files(sys/time.h HAVE_SYS_TIME)
check_include_files(sys/types.h HAVE_SYS_TYPES)
check_include_files(sys/un.h HAVE_SYS_UN)
check_include_files(time.h HAVE_TIME)
check_include_files(unistd.h HAVE_UNISTD)

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
