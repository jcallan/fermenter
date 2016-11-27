#!/bin/sh
gcc -c programme.c -o programme.o
gcc -c gpio.c -o gpio.o
gcc -c fermenter.c -o fermenter.o
gcc fermenter.o programme.o gpio.o -lpthread -o fermenter
