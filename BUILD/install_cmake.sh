#!/bin/bash

function is_arm() {
    [ "$(arch)" == "aarch64" ]
}

# Methods for comparing software versions.
verlte() {
    [  "$1" = "`echo -e "$1\n$2" | sort -V | head -n1`" ]
}

verlt() {
    [ "$1" = "$2" ] && return 1 || verlte $1 $2
}

# Check CMake version installed by package manager.
cmake_vrs_cmd="cmake --version"
cmake_version_ok=0
cmake_version_required="3.16.0"
if command -v ${cmake_vrs_cmd} &> /dev/null ; then
  cmake_version=`${cmake_vrs_cmd} | grep "cmake version" | awk '{ print $3 }'`
  if verlt $cmake_version $cmake_version_required ; then
    echo "Found CMake ${cmake_version}, which is too old."
  else
    cmake_version_ok=1
    echo "Found CMake ${cmake_version}, which is recent enough."
  fi
else
  echo "CMake not found"
fi

if [ $cmake_version_ok -eq 0 ] ; then
  cmake_version_install="3.16.9"
  cmake_filename="cmake-${cmake_version_install}-Linux-x86_64.tar.gz"

  if is_arm
  then
    # CMake 3.16 does not have arm builds available.
    cmake_version_install="3.20.2"
    cmake_filename="cmake-${cmake_version_install}-linux-aarch64.tar.gz"
  fi
  cmake_remotepath="v${cmake_version_install}/${cmake_filename}"

  wget --quiet https://github.com/Kitware/CMake/releases/download/${cmake_remotepath}
  sudo tar -axf ${cmake_filename} -C /usr/ --strip-components=1

  cmake_version=`${cmake_vrs_cmd} | grep "cmake version" | awk '{ print $3 }'`
  if verlt $cmake_version $cmake_version_required ; then
    echo "CMake installation failed"
    exit 1
  fi
fi
