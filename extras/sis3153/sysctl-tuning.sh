#!/bin/bash
set -e

# Network TCP/UDP tuning to support high-bandwith applications
#
sysctl -w net.core.rmem_max=8388608
sysctl -w net.core.wmem_max=8388608
sysctl -w net.core.rmem_default=65536
sysctl -w net.core.wmem_default=65536
sysctl -w net.core.netdev_max_backlog=2000

# udp
sysctl -w net.ipv4.udp_mem='8388608 8388608 8388608'

# tcp
sysctl -w net.ipv4.tcp_rmem='4096 87380 8388608'
sysctl -w net.ipv4.tcp_wmem='4096 65536 8388608'
sysctl -w net.ipv4.tcp_mem='8388608 8388608 8388608'

# Apply the above settings now
sysctl -w net.ipv4.route.flush=1
