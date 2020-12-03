#!/usr/bin/env python3

import argparse
import json
import random
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

MaxWaitTime_s = 10.0    # Maximum time to wait for DAQ to start/stop/etc
PollSleepTime_s = 0.25  # Time between polling for state changes

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
    if "error" in response:
        raise Exception(response["error"])
    return response["result"]

def start_daq(sock):
    result = transaction(sock, "getDAQState")

    if result != "Idle":
        raise Exception("Error: DAQ is not idle.")

    if not transaction(sock, "startDAQ"):
        raise Exception("Unknown error starting DAQ.")

    tStart = time.monotonic()

    while transaction(sock, "getDAQState") != "Running":
        if time.monotonic() - tStart > MaxWaitTime_s:
            raise Exception("Timeout starting DAQ run")

        time.sleep(PollSleepTime_s)

def stop_daq(sock):
    result = transaction(sock, "getDAQState")

    if result != "Running":
        raise Exception("DAQ is not running.")

    if not transaction(sock, "stopDAQ"):
        raise Exception("Unknown error stopping DAQ.")

    tStart = time.monotonic()

    while transaction(sock, "getDAQState") != "Idle":
        if time.monotonic() - tStart > MaxWaitTime_s:
            raise Exception("Timeout starting DAQ run")

        time.sleep(PollSleepTime_s)

def now_str():
    return time.strftime("%x %X")

if __name__ == "__main__":
    argparser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    argparser.add_argument('--host', metavar='<host>', type=str, default='localhost',
            help='The host running mvme')
    argparser.add_argument('--port', metavar='<port>', type=int, default=13800,
            help='Port of the JSON-RPC server')

    argparser.add_argument('--runs', metavar='<runs>', type=int, default=1,
            help='Number of DAQ runs')
    argparser.add_argument('--duration', metavar='<duration>', type=float,
            required=True, help='Duration of each run in seconds.')

    args = argparser.parse_args()

    if (args.duration is None or args.duration <= 0):
        print("Error: Given run duration must be a positive number")
        sys.exit(1)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((args.host, args.port))

        run = 0

        while run < args.runs:
            print("%s Starting DAQ run %d of %d (duration=%.2f s)" % (
                now_str(), run+1, args.runs, args.duration))

            start_daq(s)

            print("%s Started DAQ run %d of %d" % (
                now_str(), run+1, args.runs))

            tStart = time.monotonic();

            while True:
                time.sleep(0.25)
                now = time.monotonic()

                daqState = transaction(s, "getDAQState")

                if daqState != "Running":
                    raise Exception("Error: DAQ State changed unexpectedly: %s" % (
                        daqState, ))

                elapsed = now - tStart

                if elapsed >= args.duration:
                    break

            print("%s Stopping DAQ run %d of %d" % (now_str(), run+1, args.runs))

            stop_daq(s)

            run += 1

    sys.exit(0)
