#!/bin/sh
tc qdisc del dev eth4 root netem
tc qdisc del dev eth5 root netem
tc qdisc del dev eth6 root netem
tc qdisc del dev eth7 root netem
