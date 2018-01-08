#!/bin/bash

set -e

# Network TCP/UDP tuning to support high-bandwith applications.
# See https://www.kernel.org/doc/Documentation/sysctl/ and
# https://www.kernel.org/doc/Documentation/networking/ip-sysctl.txt for more
# information.

# rmem_default - The default setting of the socket receive buffer in bytes.
sysctl -w net.core.rmem_default=10485760 # 10MB

# rmem_max - The maximum receive socket buffer size in bytes.
sysctl -w net.core.rmem_max=104857600 # 100MB

# Note: rmem_default and rmem_max specify the default and max values for the
# SO_RCVBUF socket option.

# netdev_max_backlog
# Maximum number  of  packets,  queued  on  the  INPUT  side, when the interface
# receives packets faster than kernel can process them.
sysctl -w net.core.netdev_max_backlog=100000

# udp_mem - vector of 3 INTEGERs: min, pressure, max
# Number of pages allowed for queueing by all UDP sockets.
# The default is calculated by the kernel at boot time and depends on the
# amount of system memory.
sysctl -w net.ipv4.udp_mem='8388608 8388608 8388608'

# udp_rmem_min
# Minimal size of receive buffer used by UDP sockets in moderation.
# Each UDP socket is able to use the size for receiving data, even if
# total pages of UDP sockets exceed udp_mem pressure. The unit is byte.
# Default: 1 page
sysctl -w net.ipv4.udp_rmem_min=10485760

# Apply the above settings now
sysctl -w net.ipv4.route.flush=1
