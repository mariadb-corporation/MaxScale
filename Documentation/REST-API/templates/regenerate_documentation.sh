#!/bin/bash

python3 -m venv venv
source venv/bin/activate
pip install mariadb jinja2 requests
python3 generate.py
deactivate
rm -r venv
