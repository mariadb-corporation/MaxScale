import sys
import subprocess
import os
import time
import jaydebeapi

# Abstract SQL connection
class SQLConnection:
    def __init__(self, port = '3306', host = '127.0.0.1', user = 'root', password = ''):
        self.host = str(host)
        self.port = str(port)
        self.user = str(user)
        self.password = str(password)

    # Connect to a server
    def connect(self):
        self.conn = jaydebeapi.connect("org.mariadb.jdbc.Driver", ["jdbc:mariadb://" + self.host + ":" + self.port, self.user, self.password],"./maxscale/java/mariadb-java-client-1.3.3.jar")

    # Start a transaction
    def begin(self):
        curs = self.conn.cursor()
        curs.execute("BEGIN")
        curs.close()
    # Commit a transaction
    def commit(self):
        curs = self.conn.cursor()
        curs.execute("COMMIT")
        curs.close()

    # Query and test if the result matches the expected value if one is provided
    def query(self, query, compare = None, column = 0):
        curs = self.conn.cursor()
        curs.execute(query)
        return curs.fetchall()

    def query_and_compare(self, query, column):
        data = self.query(query)
        for row in data:
            if str(row[column]) == compare:
                return True
        return False

    def disconnect(self):
        self.conn.close()

    def query_and_close(self, query):
        self.connect()
        self.query(query)
        self.disconnect()

# Test environment abstraction
class MaxScaleTest:
    def __init__(self, testname = "python_test"):
        subprocess.call(os.getcwd() + "/non_native_setup " + str(testname), shell=True)
        envfile = open("test.environment")

        for var in envfile.readlines():
            part = var.partition("=")
            if part[0] not in os.environ:
                os.putenv(part[0], part[2])

        # MaxScale connections
        self.maxscale = dict()
        self.maxscale['rwsplit'] = SQLConnection(host = os.getenv("maxscale_IP"), port = "4006", user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.maxscale['rcmaster'] = SQLConnection(host = os.getenv("maxscale_IP"), port = "4008", user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.maxscale['rcslave'] = SQLConnection(host = os.getenv("maxscale_IP"), port = "4009", user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))

        # Master-Slave nodes
        self.repl = dict()
        self.repl['node0'] = SQLConnection(host = os.getenv("repl_000"), port = os.getenv("repl_port_000"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.repl['node1'] = SQLConnection(host = os.getenv("repl_001"), port = os.getenv("repl_port_001"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.repl['node2'] = SQLConnection(host = os.getenv("repl_002"), port = os.getenv("repl_port_002"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.repl['node3'] = SQLConnection(host = os.getenv("repl_003"), port = os.getenv("repl_port_003"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))

        # Galera nodes
        self.galera = dict()
        self.galera['node0'] = SQLConnection(host = os.getenv("galera_000"), port = os.getenv("galera_port_000"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.galera['node1'] = SQLConnection(host = os.getenv("galera_001"), port = os.getenv("galera_port_001"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.galera['node2'] = SQLConnection(host = os.getenv("galera_002"), port = os.getenv("galera_port_002"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.galera['node3'] = SQLConnection(host = os.getenv("galera_003"), port = os.getenv("galera_port_003"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
