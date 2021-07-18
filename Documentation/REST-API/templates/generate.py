#!/usr/bin/env python3

#
# Simple documentation template engine implemented with Jinja2 and
# Requests. Reads all .jinja files from the current directory and renders the
# documents in to the parent directory.
#
# Requirements: Python 3.9, Jinja2, Requests
#

from jinja2 import Environment, FileSystemLoader
import requests

def get(endpoint):
    res = requests.get("http://localhost:8989" + endpoint, auth=("admin", "mariadb"))
    if not res.ok:
        res.raise_for_status()
    return res.text


env = Environment(loader=FileSystemLoader("."), keep_trailing_newline=True)
for tmpl in env.list_templates(extensions=["jinja"]):
    t = env.get_template(tmpl)
    print("Processing " + t.name + "...")
    t.stream(get=get).dump("../" + t.name.removesuffix(".jinja"))
