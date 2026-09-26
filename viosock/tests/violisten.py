#!/usr/bin/python

import socket
import os, os.path
import time
from collections import deque

server = socket.socket(socket.AF_VSOCK, socket.SOCK_STREAM)
server.bind((-1,401))
while True:
  server.listen(1)
  conn, addr = server.accept()
  packet = conn.recv(65536)
  if packet:
    print('Recv: ' + str(packet) + 'from\n')
    print(addr)
    conn.send('Recv OK')
  conn.close()
