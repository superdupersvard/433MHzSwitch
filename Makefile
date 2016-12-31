install : 433MHzGatewayd
	cp 433MHzGatewayd /usr/local/bin
	cp 433MHzGatewaydService /etc/init.d/433MHzGatewayd
	chmod +x /etc/init.d/433MHzGatewayd
	update-rc.d 433MHzGatewayd defaults
	service 433MHzGatewayd start
	
	
uninstall:
	service 433HzGatewayd stop
	update-rc.d -f 433MHzGatewayd remove
	rm /etc/init.d/433MHzGatewayd
	rm /usr/local/bin/433MHzGatewayd

433MHzGatewayd : 433MHzGateway.c 
	g++ 433MHzGateway.c -o 433MHzGatewayd -lwiringPi -lmosquitto -DRASPBERRY -DDAEMON

433MHzGateway : 433MHzGateway.c 
	g++ 433MHzGateway.c -o 433MHzGateway -lwiringPi -lmosquitto -DRASPBERRY -DDEBUG

