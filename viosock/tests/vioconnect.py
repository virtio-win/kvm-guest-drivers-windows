#!/usr/bin/python

import socket
import os, os.path
import time
from collections import deque

with open('viosock.inf') as f:
    read_data = f.read()

client = socket.socket(socket.AF_VSOCK, socket.SOCK_STREAM)
client.connect((3,401))
client.send(read_data.encode('utf-8'))

packet = client.recv(65536)
if packet:
    print('Recv: ' + str(packet))

client.close()

