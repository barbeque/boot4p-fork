all:	zout/boot2.cmd

clean:
	rm -rf zout

hex2b2: zout/boot2.cmd
	pl 

zout/boot2.cmd: boot2.z
	../zmac/src/zmac boot2.z
