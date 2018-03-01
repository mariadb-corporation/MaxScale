#!/usr/bin/env python3

# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2019-07-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.

# This program requires the kafka-python package which you can install with:
#
#     pip install kafka-python
#

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
producer = KafkaProducer(bootstrap_servers=[opts.kafka_broker])
sys.stdin = sys.stdin.detach()

while True:
   try:
      buf = sys.stdin.readline()

      if len(buf) == 0:
         break

      data = buf[:-1]
      producer.send(topic=opts.kafka_topic, value=data)
      producer.flush()

   # All other errors should interrupt the processing
   except Exception as ex:
      print(ex)
      break
