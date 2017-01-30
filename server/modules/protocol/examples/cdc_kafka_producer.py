#!/usr/bin/env python3

# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl.
#
# Change Date: 2019-01-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.

# This program requires the kafka-python package which you can install with:
#
#     pip install kafka-python
#

import json
import sys
import argparse
from kafka import KafkaProducer

parser = argparse.ArgumentParser(description = "Publish JSON data read from standard input to a Kafka broker")
parser.add_argument("-K", "--kafka-broker", dest="kafka_broker",
                    help="Kafka broker in host:port format",
                    default=None, required=True)
parser.add_argument("-T", "--kafka-topic", dest="kafka_topic",
                    help="Kafka topic where the data is published",
                    default=None, required=True)

opts = parser.parse_args(sys.argv[1:])
decoder = json.JSONDecoder()
rbuf = bytes()
producer = KafkaProducer(bootstrap_servers=[opts.kafka_broker])

while True:
   try:
      buf = sys.stdin.readline()

      if len(buf) == 0:
         break

      rbuf += buf.encode()

      while True:
         rbuf = rbuf.lstrip()
         data = decoder.raw_decode(rbuf.decode('ascii'))
         rbuf = rbuf[data[1]:]
         producer.send(topic=opts.kafka_topic, value=json.dumps(data[0]).encode())
         producer.flush()

   # JSONDecoder will return a ValueError if a partial JSON object is read
   except ValueError as err:
      pass

   # All other errors should interrupt the processing
   except Exception as ex:
      print(ex)
      break
