all: ssh telnet-client

ssh: ssh.o
	gcc ssh.o -o ssh.out -L../lib -Wl,-rpath,../lib -lssh

ssh.o: ssh.c
	gcc -c ssh.c

telnet-client: telnet-client.o ../lib/liblibtelnet.a
	gcc telnet-client.o -o telnet-client.out -L../lib/ -llibtelnet
	
telnet-client.o: telnet-client.c
	gcc -c telnet-client.c