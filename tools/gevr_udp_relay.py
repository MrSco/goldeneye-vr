"""Relay a UDP port from this Windows PC to a server inside WSL2.

WSL2 sits behind its own address, so a headset on the network cannot reach a
dedicated server running there (docs/dedicated-server.md). This listens on the
PC's port and gives each headset its own socket to the server, so replies go
back to the right one. No Windows or WSL settings are changed; Windows Firewall
may ask once to let Python receive connections.

    py tools/gevr_udp_relay.py WSL_IP [PORT]
"""
import select
import socket
import sys
import time

target = (sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 27007)
listen = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
listen.bind(("0.0.0.0", target[1]))
clients = {}   # headset address -> [socket to the server, last packet time]
owners = {}    # that socket -> headset address
print(f"relay: UDP {target[1]} on this PC -> {target[0]}:{target[1]}", flush=True)
while True:
    readable, _, _ = select.select([listen, *owners], [], [], 5.0)
    now = time.monotonic()
    for sock in readable:
        if sock is listen:
            data, addr = listen.recvfrom(65536)
            if addr not in clients:
                upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                upstream.connect(target)
                clients[addr] = [upstream, now]
                owners[upstream] = addr
                print(f"relay: {addr[0]}:{addr[1]} joined", flush=True)
            clients[addr][1] = now
            clients[addr][0].send(data)
        else:
            try:
                data = sock.recv(65536)
            except OSError:
                continue
            listen.sendto(data, owners[sock])
    for addr, (upstream, last) in list(clients.items()):
        if now - last > 120:
            print(f"relay: {addr[0]}:{addr[1]} quiet for 2 min, dropped", flush=True)
            del owners[upstream]
            del clients[addr]
            upstream.close()
