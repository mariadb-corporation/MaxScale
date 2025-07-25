# Generating the REST API Documentation

* Make sure you have Python3 and the venv module for it installed.

* Install MariaDB Connector/C (i.e. the `MariaDB-devel` package) from the
  mariadb.com repositories, needed by the Python connector.

* Run the `build_local_image.sh` script to build MaxScale inside of a docker container.

* Run the `regenerate_documentation.sh` script to generate the documentation.

* Add any new parts into the documentation, the counters and timestamps don't
  necessarily need to be updated every time.
