#!/bin/sh
ps -ef|grep mftp|grep -v gedit|cut -c 9-15|xargs kill -9
ps -ef|grep retcp|grep -v gedit|cut -c 9-15|xargs kill -9
