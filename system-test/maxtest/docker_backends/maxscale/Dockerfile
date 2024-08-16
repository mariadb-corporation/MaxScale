FROM ubuntu:jammy

# Ssh username.
ENV TEST_USER=test-admin
# MaxScale user
ENV MXS_USER=maxscale
# Tool for service management.
ARG S6_OVERLAY_VERSION=3.1.6.2

# Install ssh server and MaxScale dependencies
RUN apt-get update  \
    && apt-get install -y openssh-server sudo nano xz-utils curl  \
    libcrypt1 libcurl4 libgnutls30 libgssapi-krb5-2 liblzma5 libodbc2 libpam0g libpq5 \
    libsasl2-2 libsqlite3-0 libssl3 libuuid1 libzstd1 zlib1g libasan8 libubsan1 \
    && apt-get clean

# Add ssh user.
RUN useradd -m -s /bin/bash ${TEST_USER} && passwd -d ${TEST_USER} && adduser ${TEST_USER} sudo

# Setup ssh-user for keyfile auth.
COPY ./admin_key.pub /tmp
RUN mkdir -p /home/${TEST_USER}/.ssh && touch /home/${TEST_USER}/.ssh/authorized_keys && \
    cat /tmp/admin_key.pub >> /home/${TEST_USER}/.ssh/authorized_keys && \
    chmod 700 /home/${TEST_USER}/.ssh && chmod 600 /home/${TEST_USER}/.ssh/authorized_keys && \
    chown -R ${TEST_USER}:${TEST_USER} /home/${TEST_USER}/.ssh

# Install S6 Overlay
ADD https://github.com/just-containers/s6-overlay/releases/download/v${S6_OVERLAY_VERSION}/s6-overlay-noarch.tar.xz /tmp
RUN tar -C / -Jxpf /tmp/s6-overlay-noarch.tar.xz
ADD https://github.com/just-containers/s6-overlay/releases/download/v${S6_OVERLAY_VERSION}/s6-overlay-x86_64.tar.xz /tmp
RUN tar -C / -Jxpf /tmp/s6-overlay-x86_64.tar.xz

# Setup sshd and MaxScale services. MaxScale does not start automatically. It can be started with:
# /command/s6-svc -u /run/service/maxscale
# and stopped with /command/s6-svc -d /run/service/maxscale
RUN mkdir -p /etc/services.d/sshd/ && mkdir -p /etc/services.d/maxscale/
COPY --chmod=771 run_sshd /etc/services.d/sshd/run
COPY --chmod=771 run_maxscale /etc/services.d/maxscale/run
RUN touch /etc/services.d/maxscale/down

RUN mkdir /run/sshd

# Add MaxScale user and directories.
RUN groupadd ${MXS_USER}
RUN useradd -m -s /bin/bash -g ${MXS_USER} ${MXS_USER} && passwd -d ${MXS_USER}

# Fool MaxScale postinst script by temporarily renaming systemctl, as it doesn't work and causes the script
# to fail.
RUN mv /usr/bin/systemctl /usr/bin/systemctlA
RUN --mount=type=bind,source=maxscale.deb,target=maxscale.deb apt-get install -y ./maxscale.deb
RUN mv /usr/bin/systemctlA /usr/bin/systemctl

EXPOSE 22 4006 4007

ENTRYPOINT ["/init"]
