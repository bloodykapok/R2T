#!/bin/sh
ifconfig eth4 down
ifconfig eth5 down
ifconfig eth6 down
ifconfig eth7 down
ifconfig eth4 up
ifconfig eth5 up
ifconfig eth6 up
ifconfig eth7 up
ethtool -s eth4 speed 100 duplex full autoneg off
ethtool -s eth5 speed 100 duplex full autoneg off
ethtool -s eth6 speed 100 duplex full autoneg off
ethtool -s eth7 speed 100 duplex full autoneg off
tc qdisc add dev eth4 root netem delay 5ms
tc qdisc add dev eth5 root netem delay 5ms
tc qdisc add dev eth6 root netem delay 5ms
tc qdisc add dev eth7 root netem delay 5ms
