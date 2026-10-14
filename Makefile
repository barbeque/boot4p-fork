all:	zout/boot2.cmd boot2.h boot4p

clean:
	rm -rf zout

boot4p: boot2.h
	g++ -o boot4p boot4p.cpp load.cpp listcom.cpp loader.cpp

boot2.h: zout/boot2.cmd
	perl hexb2.pl

zout/boot2.cmd: boot2.z
	../zmac/src/zmac boot2.z
