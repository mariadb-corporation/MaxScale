# This Dockerfile builds an image for MariaDB MaxScale:
# https://mariadb.com/products/technology/maxscale
# The example configuration file maxscale.cnf should be in the build directory
# when building the image.

FROM alpine:latest

# Packages required for building MaxScale and Avro-C
ARG DEPENDENCIES_PKGS="bash bison cmake curl flex gcc git gnutls gnutls-dev g++ \
jansson jansson-dev libedit libedit-dev libgcrypt libgcrypt-dev libstdc++ lua \
lua-dev make ncurses ncurses-dev openssl openssl-dev perl sqlite sqlite-dev \
sqlite-libs tcl-dev util-linux-dev xz xz-dev"

# Packages that can be removed after build
ARG REM_PKGS="bison bash cmake flex gcc git gnutls-dev g++ jansson-dev \
libedit-dev libgcrypt-dev lua-dev make ncurses-dev openssl-dev perl sqlite-dev \
tcl-dev util-linux-dev xz-dev"

# MaxScale-specific parameters
ARG MS_DIR=/MaxScale_workdir
ARG MS_CMAKE_PARS="-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=RelWithDebInfo \
-DWITH_SCRIPTS=N -DTARGET_COMPONENT=all"
ARG MS_BRANCH=develop
ARG MS_REPO=https://github.com/mariadb-corporation/MaxScale.git

# Avro-specific parameters
ARG AV_DIR=/Avro_workdir
ARG AV_PREF=avro-c-1.8.2
ARG AV_ARCH=$AV_PREF.tar.gz
ARG AV_URL=ftp://ftp.funet.fi/pub/mirrors/apache.org/avro/avro-1.8.2/c/$AV_ARCH
ARG AV_CMAKE_PARS="-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=RelWithDebInfo \
-DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC"

RUN apk -U add $DEPENDENCIES_PKGS && \
    mkdir $MS_DIR && mkdir $AV_DIR && mkdir $MS_DIR/Build && mkdir $AV_DIR/Build && \
    cd $AV_DIR && \
# Download Avro-C, build and install
    curl --output $AV_ARCH $AV_URL && \
    tar -zxvf $AV_ARCH && cd Build && \
    cmake $AV_CMAKE_PARS ../$AV_PREF && \
    make install && \
    cd $MS_DIR && \
# Download MaxScale, build and install
    git clone --depth 1 --branch $MS_BRANCH $MS_REPO && cd Build && \
    cmake $MS_CMAKE_PARS ../MaxScale && \
    make install && (./postinst || true) && \
# Remove unneeded packages and work directories
    apk del $REM_PKGS && \
    cd / && rm -rf $MS_DIR $AV_DIR

COPY ./maxscale.cnf /etc/
ENTRYPOINT ["maxscale", "-d"]
CMD ["-lstdout"]
