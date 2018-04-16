# Dockerfile for the 2.2 GA version of MariaDB MaxScale
FROM centos:7

RUN curl -sS https://downloads.mariadb.com/MariaDB/mariadb_repo_setup | bash && yum -y install maxscale
COPY ./maxscale.cnf /etc/
ENTRYPOINT ["maxscale", "-d", "-U", "maxscale"]
CMD ["-lstdout"]
