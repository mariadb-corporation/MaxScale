FROM mariadb:11.3

# Ssh username.
ENV TEST_USER=test-admin
# Tool for service management.
ARG S6_OVERLAY_VERSION=3.1.6.2

RUN apt-get update && apt-get install -y openssh-server sudo nano && apt-get clean
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

# Setup sshd and mariadbd services. MariaDB does not start automatically. It can be started with:
# /command/s6-svc -u /run/service/mariadbd
# and stopped with /command/s6-svc -d /run/service/mariadbd
RUN mkdir -p /etc/services.d/sshd/ && mkdir -p /etc/services.d/mariadbd/
COPY --chmod=771 run_sshd /etc/services.d/sshd/run
COPY --chmod=771 run_mariadbd /etc/services.d/mariadbd/run
RUN touch /etc/services.d/mariadbd/down

RUN mkdir /run/sshd

EXPOSE 22 3306

ENTRYPOINT ["/init"]
