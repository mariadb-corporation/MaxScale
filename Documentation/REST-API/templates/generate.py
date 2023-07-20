#
# Simple documentation template engine implemented with Jinja2 and
# Requests. Reads all .jinja files from the current directory and renders the
# documents in to the parent directory.
#
# Requirements: Python 3.9, Jinja2, Requests, MariaDB Python Connector
#

from jinja2 import Environment, FileSystemLoader
import requests
import os
import mariadb
import time

def get(endpoint):
    res = requests.get("http://localhost:8989" + endpoint, auth=("admin", "mariadb"))
    if not res.ok:
        res.raise_for_status()
    return res.text


print("Start docker containers")
test_dir = "../../../test/"
pwd = os.getcwd()
os.chdir(test_dir)
os.system("docker-compose up -d server1 server2")
os.chdir(pwd)
os.system("docker run --pull always --name mxs -d --rm -v " + pwd + "/rest_api.cnf:/etc/maxscale.cnf --network=host mariadb/maxscale:6.4")

print("Give MaxScale and the databases a few seconds to start up")
time.sleep(10)

print("Create a connection to MaxScale and do a query")
conn = mariadb.connect(
    user="maxuser",
    password="maxpwd",
    host="127.0.0.1",
    port=4006,
    database="test")

cur = conn.cursor()
cur.execute("SELECT 1")

print("Generate documentation")
env = Environment(loader=FileSystemLoader("."), keep_trailing_newline=True)
for tmpl in env.list_templates(extensions=["jinja"]):
    t = env.get_template(tmpl)
    print("Processing " + t.name + "...")
    t.stream(get=get).dump("../" + t.name.removesuffix(".jinja"))

print("Cleanup")
cur.close()
conn.close()

os.chdir(test_dir)
os.system("docker-compose down -v")
os.system("docker rm -vf mxs")
os.chdir(pwd)
