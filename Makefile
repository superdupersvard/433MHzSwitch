install : 433MHzGatewayd
	cp 433MHzGatewayd /usr/local/bin
	cp 433MHzGatewayd.service /usr/lib/systemd/system/433MHzGatewayd.service
	systemctl daemon-reload
	systemctl enable 433MHzGatewayd.service
	systemctl start 433MHzGatewayd.service

	
uninstall:
	systemctl stop 433MHzGatewayd.service
	systemctl disable 433MHzGatewayd.service
	rm /usr/lib/systemd/system/433MHzGatewayd.service
	systemctl daemon-reload
	rm /usr/local/bin/433MHzGatewayd

433MHzGatewayd : 433MHzGateway.c 
	g++ 433MHzGateway.c -o 433MHzGatewayd -lwiringPi -lmosquitto -DRASPBERRY -DDAEMON

433MHzGateway : 433MHzGateway.c 
	g++ 433MHzGateway.c -o 433MHzGateway -lwiringPi -lmosquitto -DRASPBERRY -DDEBUG

