#
# Simple documentation template engine implemented with Jinja2 and
# Requests. Reads all .jinja files from the current directory and renders the
# documents into the parent directory.
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


def generate_auth_token():
    res = requests.get("http://localhost:8989/v1/auth", auth=("admin", "mariadb"))
    if not res.ok:
        res.raise_for_status()
    return res.text


def open_sql_conn(payload):
    res = requests.post("http://localhost:8989/v1/sql/", json=payload, auth=("admin", "mariadb"))
    if not res.ok:
        print("Failed to open SQL connection:", payload, res.text)
        res.raise_for_status()

    js = res.json()
    token = js["meta"]["token"]
    conn_id = js["data"]["id"]
    return token, conn_id


def get_sql_conn():
    token, uuid = open_sql_conn({"user": "maxuser", "password": "maxpwd", "target": "server1"})
    rval = get("/v1/sql/" + uuid)
    requests.delete("http://localhost:8989/v1/sql/" + uuid + "?token=" + token, auth=("admin", "mariadb"))
    return rval


def get_all_sql_conns():
    token1, uuid1 = open_sql_conn({"user": "maxuser", "password": "maxpwd", "target": "server1"})
    token2, uuid2 = open_sql_conn({"user": "maxuser", "password": "maxpwd", "target": "server1"})
    rval = get("/v1/sql/")
    requests.delete("http://localhost:8989/v1/sql/" + uuid1 + "?token=" + token1, auth=("admin", "mariadb"))
    requests.delete("http://localhost:8989/v1/sql/" + uuid2 + "?token=" + token2, auth=("admin", "mariadb"))
    return rval


def post_sql_conn():
    res = requests.post("http://localhost:8989/v1/sql/",
                        json={"user": "maxuser", "password": "maxpwd", "target": "server1"},
                        auth=("admin", "mariadb"))
    if not res.ok:
        res.raise_for_status()

    js = res.json()
    token = js["meta"]["token"]
    uuid = js["data"]["id"]
    requests.delete("http://localhost:8989/v1/sql/" + uuid + "?token=" + token, auth=("admin", "mariadb"))
    return res.text


def get_query_result(sql):
    token, uuid = open_sql_conn({"user": "maxuser", "password": "maxpwd", "target": "server1"})
    rval = requests.post("http://localhost:8989/v1/sql/" + uuid + "/queries?token=" + token,
                          json={"sql": sql}, auth=("admin", "mariadb"))
    requests.delete("http://localhost:8989/v1/sql/" + uuid + "?token=" + token, auth=("admin", "mariadb"))
    return rval.text


def get_async_query_result(sql, should_wait):
    token, uuid = open_sql_conn({"user": "maxuser", "password": "maxpwd", "target": "server1"})
    rval = requests.post("http://localhost:8989/v1/sql/" + uuid + "/queries?token=" + token + "&async=true",
                          json={"sql": sql}, auth=("admin", "mariadb"))

    if should_wait:
        time.sleep(1)
        js = rval.json()
        query_url = js["links"]["self"];
        rval = requests.get(query_url + "?token=" + token, auth=("admin", "mariadb"))

    requests.delete("http://localhost:8989/v1/sql/" + uuid + "?token=" + token, auth=("admin", "mariadb"))
    return rval.text


def do_etl(endpoint):
    with mariadb.connect(user="maxuser", password="maxpwd", host="127.0.0.1", port=3000) as srv1, \
         mariadb.connect(user="maxuser", password="maxpwd", host="127.0.0.1", port=3001) as srv2, \
         srv1.cursor() as cur1, \
         srv2.cursor() as cur2:
        cur2.execute("STOP SLAVE")
        cur1.execute("SET SESSION SQL_LOG_BIN=0");
        cur1.execute("CREATE OR REPLACE TABLE test.t1(id SERIAL, data VARCHAR(255))")
        cur1.execute("INSERT INTO test.t1(data) VALUES('Hello world!')")
        srv1.commit()

        src_payload = {"target": "odbc", "connection_string": "DRIVER=libmaodbc.so;UID=maxuser;PWD=maxpwd;SERVER=127.0.0.1;PORT=3000;DATABASE=test"}
        dest_payload = {"user": "maxuser", "password": "maxpwd", "target": "server2", "db": "test"}
        src_tok, src_id = open_sql_conn(src_payload)
        dest_tok, dest_id = open_sql_conn(dest_payload)

        etl_payload = {
            "type": "mariadb",
            "tables": [{"table": "t1", "schema": "test"}],
            "target": dest_id,
        }
        res = requests.post("http://localhost:8989/v1/sql/" + src_id + "/" + endpoint + "?token=" + src_tok + "&target_token=" + dest_tok,
                            json=etl_payload, auth=("admin", "mariadb"))
        js = res.json()
        link = js["links"]["self"]

        while res.status_code == 202:
            res = requests.get(link + "?token=" + src_tok, auth=("admin", "mariadb"))
            time.sleep(1)

        cur1.execute("DROP TABLE IF EXISTS test.t1")
        cur2.execute("DROP TABLE IF EXISTS test.t1")
        cur2.execute("START SLAVE")

        if not res.ok:
            res.raise_for_status()

        return res.text


print("Start docker containers")
test_dir = "../../../test/"
pwd = os.getcwd()
os.chdir(test_dir)
os.system("docker-compose up -d server1 server2")
os.chdir(pwd)
image = os.environ.get("MAXSCALE_IMAGE", "mariadb/maxscale:22.08")
os.system("docker rm -vf mxs")
os.system("docker run --name mxs -d --rm -v " + pwd + "/rest_api.cnf:/etc/maxscale.cnf --network=host " + image)

# Install the MariaDB ODBC connector, needed for generating the ETL output
os.system("docker exec mxs dnf -y --disablerepo mariadb-maxscale install mariadb-connector-odbc")

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
cur.execute("CREATE OR REPLACE TABLE test.t1(id INT)")
cur.execute("INSERT INTO test.t1(id) VALUES (1), (2), (3)")
conn.commit()

print("Generate documentation")
env = Environment(loader=FileSystemLoader("."), keep_trailing_newline=True)
for tmpl in env.list_templates(extensions=["jinja"]):
    t = env.get_template(tmpl)
    print("Processing " + t.name + "...")
    t.stream(get=get,
             generate_auth_token=generate_auth_token,
             get_sql_conn=get_sql_conn,
             get_all_sql_conns=get_all_sql_conns,
             post_sql_conn=post_sql_conn,
             get_query_result=get_query_result,
             get_async_query_result=get_async_query_result,
             do_etl=do_etl
             ).dump("../" + t.name.removesuffix(".jinja"))

print("Cleanup")
cur.close()
conn.close()

os.chdir(test_dir)
os.system("docker-compose down -v")
os.system("docker rm -vf mxs")
os.chdir(pwd)
