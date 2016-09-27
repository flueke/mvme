# vim:ft=make
#CC=g++
CC=clang++-3.5

vme-script-qi: vme_script_qi.cc vme_script_qi.h
	$(CC) -std=c++11 -o $@ $<
