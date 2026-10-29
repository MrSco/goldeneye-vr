"""Relay a dedicated server inside WSL2 onto this Windows PC's network.

WSL2 sits behind its own address, so a headset on the network can neither
reach a server running there nor hear its LAN beacons (docs/dedicated-server.md).
This listens on the PC's game port and gives each headset its own socket to
the server, so replies go back to the right one. With LAN_BROADCAST it also
takes the beacons the server sends to Windows (gevr-server --beacon-to, the
WSL default gateway) and broadcasts them on the LAN, so the server shows in
the launcher's LAN list at this PC's address. No Windows or WSL settings are
changed; Windows Firewall may ask once to let Python receive connections.

    py tools/gevr_udp_relay.py WSL_IP [PORT] [LAN_BROADCAST]
    py tools/gevr_udp_relay.py 172.25.33.54 27007 10.0.3.255
"""
import select
import socket
import sys
import time

DISCOVERY_PORT = 27008   # net_protocol.h GEVR_DISCOVERY_PORT

target = (sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 27007)
lan_broadcast = sys.argv[3] if len(sys.argv) > 3 else None
listen = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
listen.bind(("0.0.0.0", target[1]))
beacons = None
if lan_broadcast:
    beacons = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    beacons.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    beacons.bind(("0.0.0.0", DISCOVERY_PORT))
clients = {}   # headset address -> [socket to the server, last packet time]
owners = {}    # that socket -> headset address
print(f"relay: UDP {target[1]} on this PC -> {target[0]}:{target[1]}"
      + (f"; its beacons -> {lan_broadcast}:{DISCOVERY_PORT}" if lan_broadcast else ""), flush=True)
announced = False
while True:
    readable, _, _ = select.select([listen, *owners, *([beacons] if beacons else [])], [], [], 5.0)
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
        elif sock is beacons:
            data, addr = beacons.recvfrom(2048)
            if addr[0] == target[0]:   # only the server's, never our own broadcast coming back
                beacons.sendto(data, (lan_broadcast, DISCOVERY_PORT))
                if not announced:
                    print(f"relay: passing the server's LAN beacons on", flush=True)
                    announced = True
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
