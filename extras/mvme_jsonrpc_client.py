#!/usr/bin/env python3

import argparse
import json
import socket
import sys
import time

if (sys.version_info.major < 3
        or (sys.version_info.major == 3 and sys.version_info.minor < 2)):
    print("This script requires Python version >= 3.2")
    sys.exit(1)

# Command line arguments:
#   mvme_jsonrpc_client.py [options] host port command [params]
# Example:
#   mvme_jsonrpc_client.py localhost 13800 getDAQState

MaxResponseSize   = 1024 * 256
SocketReceiveSize = 4096

if __name__ == "__main__":

    argparser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    argparser.add_argument('host', metavar='<host>', type=str)
    argparser.add_argument('port', metavar='<port>', type=int)
    argparser.add_argument('method', metavar='<method>', type=str)
    argparser.add_argument('params', metavar='<param>', type=str, nargs='*')

    argparser.add_argument('-n', metavar='<repetitions>', type=int, default=1,
            help='Number of times to repeat the command.')

    argparser.add_argument('-t', metavar='<interval>', type=float, default=0.0,
            help='Interval between repetitions in seconds. Floating point values can be used.')


    args = argparser.parse_args()

    rep_interval = args.t
    rep_count = args.n

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((args.host, args.port))

        request_id = 0

        for rep in range(rep_count):
            request_object = {
                'jsonrpc': '2.0',
                'id': str(request_id),
                'method': args.method,
                'params': args.params
            }

            request_json = json.dumps(request_object, sort_keys=True)
            print("--->", request_json)

            s.sendall(bytes(request_json, 'utf-8'))
            response = bytes()

            while True:
                try:
                    response += s.recv(SocketReceiveSize)
                    response_json = json.loads(response.decode('utf-8'))
                    print("<---", json.dumps(response_json, sort_keys=True))
                    break
                except json.decoder.JSONDecodeError:
                    if len(response) > MaxResponseSize:
                        print("Maximum response size exceeded.")
                        sys.exit(1)

            request_id += 1

            if (rep < rep_count - 1) and (rep_interval > 0):
                time.sleep(rep_interval)

    sys.exit(0)
