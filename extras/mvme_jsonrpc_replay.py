#!/usr/bin/env python3

import argparse
import json
import socket
import sys
import time

# 3.3: time.monotonic
required_python_version = (3, 3)

if (sys.version_info.major < required_python_version[0]
        or (sys.version_info.major == required_python_version[0]
            and sys.version_info.minor < required_python_version[1])):
    print("This script requires Python version >= %d.%d" % required_python_version)
    sys.exit(1)

MaxResponseSize   = 1024 * 256
SocketReceiveSize = 4096

last_request_id = 0
last_response_id = 0

def send_request(sock, method, params = None):
    global last_request_id
    request_id = last_request_id + 1

    request_object = {
        'jsonrpc': '2.0',
        'id': str(request_id),
        'method': method,
    }

    if params is not None:
        request_object["params"] = params

    request_json = json.dumps(request_object, sort_keys=True)
    #print("--->", request_json)

    s.sendall(bytes(request_json, 'utf-8'))
    last_request_id = request_id

def receive_response(sock):
    global last_request_id
    global last_response_id

    response = bytes()

    # Read data until the JSON can be decoded or MaxResponseSize is exceeded.
    while True:
        try:
            response += s.recv(SocketReceiveSize)
            response_json = json.loads(response.decode('utf-8'))
            #print("<---", json.dumps(response_json, sort_keys=True))
            last_response_id = int(response_json["id"])
            assert last_response_id == last_request_id
            break
        except json.decoder.JSONDecodeError:
            if len(response) > MaxResponseSize:
                raise Exception("Maximum response size exceeded.")

    return response_json

def transaction(sock, method, params = None):
    send_request(sock, method, params)
    response = receive_response(sock)
    #print(f"{response=}")
    if "error" in response:
        raise Exception(response["error"])
    return response["result"]

def now_str():
    return time.strftime("%x %X")

if __name__ == "__main__":
    argparser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    argparser.add_argument('--host', metavar='<host>', type=str, default='localhost',
            help='The host running mvme')
    argparser.add_argument('--port', metavar='<port>', type=int, default=13800,
            help='Port of the JSON-RPC server')

    argparser.add_argument('--analysis', metavar='<filename>', type=str,
                           help='analysis file to load for the replay')

    argparser.add_argument('--loadAnalysis', action='store_true',
                           help='if true load the analysis from the zipfile archive')

    argparser.add_argument('--keepHistoContents', action='store_true',
                           help='on replay start keep analysis histogram contents')

    argparser.add_argument('--replayAllParts', action='store_true',
                           help='if the input is a split listfile (ending in "partNNN.zip") replay data from all parts')

    argparser.add_argument("listfiles", nargs='+')

    args = argparser.parse_args()

    loadAnalysisFromListfile = args.loadAnalysis
    replayAllParts = args.replayAllParts
    keepHistoContents = args.keepHistoContents

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((args.host, args.port))

        if (args.analysis):
            try:
                transaction(s, "loadAnalysis", [args.analysis])
                print(f"Loaded analysis {args.analysis}")
            except Exception as e:
                print(f"Error loading analysis {args.analysis}: {e}")
                sys.exit(1)

        filecount = len(args.listfiles)

        # Iterate through the listfiles passed on the command line. Call
        # 'loadListfile' and 'startReplay' for each of the files then poll until
        # the mvme system state is 'Idle', meaning the replay has finished (or
        # an error occured).
        for fileidx, filepath in enumerate(args.listfiles):
            try:
                transaction(s, "loadListfile", [filepath, loadAnalysisFromListfile, replayAllParts])
                transaction(s, "startReplay", [keepHistoContents])

                print(f"File {fileidx+1}/{filecount}: started replay from {filepath}")
                mvmeState = "Running"
                while mvmeState != "Idle":
                    time.sleep(1)
                    mvmeState = transaction(s, "getSystemState")

                print(f"File {fileidx+1}/{filecount}: finished replay from {filepath}")

            except Exception as e:
                print(f"Error replaying listfile {filepath}: {e}")
                sys.exit(1)

    sys.exit(0)
