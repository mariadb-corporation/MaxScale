FROM rockylinux:8 AS builder
ADD ./BUILD/ /BUILD/
RUN dnf -y install sudo && /BUILD/install_build_deps.sh

ADD ./ /src/
RUN mkdir /build/
WORKDIR /build/
RUN cmake /src/ -DCMAKE_BUILD_TYPE=Debug \
    -DPACKAGE=Y -DBUILD_BINLOG=N -DBUILD_CDC=N \
    -DBUILD_GSSAPI=N -DBUILD_GUI=N -DBUILD_KAFKACDC=N \
    -DBUILD_KAFKAIMPORTER=N -DBUILD_KMIP_KEY_MANAGER=N \
    -DBUILD_LUAFILTER=N -DBUILD_MAXCTRL=N -DBUILD_MIRROR=N \
    -DBUILD_NOSQL=N -DBUILD_QC_MYSQLEMBEDDED=N -DBUILD_RABBITMQ=N \
    -DBUILD_STORAGE_MEMCACHED=N -DBUILD_STORAGE_REDIS=N -DBUILD_TESTS=N \
    -DBUILD_TOOLS=N -DBUILD_VAULT_KEY_MANAGER=N && \
    make package -j 8 && mv maxscale*.rpm /maxscale.rpm

FROM rockylinux:8
COPY --from=builder /maxscale.rpm /maxscale.rpm
RUN dnf -y install /maxscale.rpm
ENTRYPOINT ["/usr/bin/maxscale", "-d", "-U", "root"]
