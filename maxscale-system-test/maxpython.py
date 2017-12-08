
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
    def connect(self, options = ""):
        try:
            self.conn = jaydebeapi.connect("org.mariadb.jdbc.Driver", ["jdbc:mariadb://" + self.host + ":" + self.port + "/test?" + options, self.user, self.password],"./maxscale/java/mariadb-java-client-1.3.3.jar")
        except Exception as ex:
            print("Failed to connect to " + self.host + ":" + self.port + " as " + self.user + ":" + self.password)
            print(unicode(ex))
            exit(1)

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

        self.testname = testname
        prepare_test(testname)

        # MaxScale connections
        self.maxscale = dict()
        self.maxscale['rwsplit'] = SQLConnection(host = os.getenv("maxscale_IP"), port = "4006", user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.maxscale['rcmaster'] = SQLConnection(host = os.getenv("maxscale_IP"), port = "4008", user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.maxscale['rcslave'] = SQLConnection(host = os.getenv("maxscale_IP"), port = "4009", user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))

        # Master-Slave nodes
        self.repl = dict()
        self.repl['node0'] = SQLConnection(host = os.getenv("node_000_network"), port = os.getenv("node_000_port"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.repl['node1'] = SQLConnection(host = os.getenv("node_001_network"), port = os.getenv("node_001_port"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.repl['node2'] = SQLConnection(host = os.getenv("node_002_network"), port = os.getenv("node_002_port"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.repl['node3'] = SQLConnection(host = os.getenv("node_003_network"), port = os.getenv("node_003_port"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))

        # Galera nodes
        self.galera = dict()
        self.galera['node0'] = SQLConnection(host = os.getenv("galera_000_network"), port = os.getenv("galera_000_port"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.galera['node1'] = SQLConnection(host = os.getenv("galera_001_network"), port = os.getenv("galera_001_port"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.galera['node2'] = SQLConnection(host = os.getenv("galera_002_network"), port = os.getenv("galera_002_port"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))
        self.galera['node3'] = SQLConnection(host = os.getenv("galera_003_network"), port = os.getenv("galera_003_port"), user = os.getenv("maxscale_user"), password = os.getenv("maxscale_password"))

    def __del__(self):
        subprocess.call(os.getcwd() + "/copy_logs.sh " + str(self.testname), shell=True)

# Read test environment variables
def prepare_test(testname = "replication"):
    subprocess.call(os.getcwd() + "/non_native_setup " + str(testname), shell=True)
