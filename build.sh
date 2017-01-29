#!/bin/sh
gcc -Wall -c programme.c -o programme.o
gcc -Wall -c gpio.c -o gpio.o
gcc -Wall -c fermenter.c -o fermenter.o
gcc -Wall fermenter.o programme.o gpio.o -lpthread -o fermenter
