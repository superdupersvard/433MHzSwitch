/*
License:  CC-BY-SA, https://creativecommons.org/licenses/by-sa/2.0/
Date:  2015-06-12
File: 433MHzGateway.c

This sketch receives RFM wireless data and forwards it to Mosquitto relay
It also subscripe to Mosquitto Topics starting with RFM/<network_number> followed  by /<node_id>
The message is parsed and put bak to the same payload structure as the one received from teh nodes


*/

//general --------------------------------
//#define SERIAL_BAUD   115200
#ifdef DAEMON
#define LOG(...) do { syslog(LOG_INFO, __VA_ARGS__); } while (0)
#define LOG_E(...) do { syslog(LOG_ERR, __VA_ARGS__); } while (0)
#else
#ifdef DEBUG
#define LOG(...) do { printf(__VA_ARGS__); } while (0)
#define LOG_E(...) do { printf(__VA_ARGS__); } while (0)
#else
#define LOG(...)
#define LOG_E(...)
#endif
#endif

#define USE_TELLDUS

#ifdef USE_TELLDUS
#define CMD_ON 			0x20
#define CMD_OFF 		0x30
#define CMD_GROUP_ON 	0x00
#define CMD_GROUP_OFF	0x10
#define CMD_LEN			32
#endif

#ifdef USE_ERIKC
#define CMD_ON 			0xc
#define CMD_OFF 		0x3
#define CMD_GROUP_ON 	0xc
#define CMD_GROUP_OFF	0x3
#define CMD_LEN			24
#endif


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <wiringPi.h>

// Mosquitto---------------
#include <mosquitto.h>

/* How many seconds the broker should wait between sending out
* keep-alive messages. */
#define KEEPALIVE_SECONDS 60
/* Hostname and port for the MQTT broker. */
#define BROKER_HOSTNAME "localhost"
#define BROKER_PORT 1883

#define MQTT_ROOT "433Switch"
#define MQTT_CLIENT_ID "433Gateway"

#define TX_PIN	0

typedef struct {
	short           nodeID;
	short		sensorID;
	uint32_t        var1_usl;
}
Payload;

/*
typedef struct {
	short           nodeID;
	short			sensorID;		
	unsigned long   var1_usl;
	float           var2_float;
	float			var3_float;		//
	int             var4_int;
}
SensorNode;
SensorNode sensorNode;
*/
static void die(const char *msg);
static void hexDump (char *desc, void *addr, int len, int bloc);

static bool set_callbacks(struct mosquitto *m);
static bool connect(struct mosquitto *m);
static int run_loop(struct mosquitto *m);

void transmitCode(uint32_t code, uint8_t rep);

static void uso(void) {
	fprintf(stderr, "Use:\n Simply use it without args :D\n");
	exit(1);
}

int main(int argc, char* argv[]) {
	if (argc != 1) 
		uso();

#ifdef DAEMON
	//Adapted from http://www.netzmafia.de/skripten/unix/linux-daemon-howto.html
	pid_t pid, sid;

	openlog("433MHzGatewayd", LOG_PID, LOG_USER);

	pid = fork();
	if (pid < 0) {
		LOG_E("fork failed");
		exit(EXIT_FAILURE);
	}
	/* If we got a good PID, then
		 we can exit the parent process. */
	if (pid > 0) {
		LOG("Child spawned, pid %d\n", pid);
		exit(EXIT_SUCCESS);
	}

	/* Change the file mode mask */
	umask(0);

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0) {
		LOG_E("setsid failed");
		exit(EXIT_FAILURE);
	}

	/* Change the current working directory */
	if ((chdir("/")) < 0) {
	  LOG_E("chdir failed");
	  exit(EXIT_FAILURE);
	}

	/* Close out the standard file descriptors */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
#endif //DAEMON

	// Mosquitto ----------------------
	struct mosquitto *m = mosquitto_new(MQTT_CLIENT_ID, true, NULL);
	if (m == NULL) { die("init() failure\n"); }

	if (!set_callbacks(m)) { die("set_callbacks() failure\n"); }
	if (!connect(m)) { die("connect() failure\n"); }

	// Mosquitto subscription ---------
	char subsciptionMask[128];
	sprintf(subsciptionMask, "%s/#", MQTT_ROOT);
	LOG("Subscribe to Mosquitto topic: %s\n", subsciptionMask);
	mosquitto_subscribe(m, NULL, subsciptionMask, 0);

	// 433MHz radio
	wiringPiSetup();
	pinMode(TX_PIN, OUTPUT);

	LOG("setup complete\n");
	return run_loop(m);
}  // end of setup

/* Loop until it is explicitly halted or the network is lost, then clean up. */
static int run_loop(struct mosquitto *m) {
	int res;
	long lastMess; 
	for (;;) {
		res = mosquitto_loop(m, 10, 1);
	}

	mosquitto_destroy(m);
	(void)mosquitto_lib_cleanup();

	if (res == MOSQ_ERR_SUCCESS) {
		return 0;
	} else {
		return 1;
	}
}

/* Fail with an error message. */
static void die(const char *msg) {
	fprintf(stderr, "%s", msg);
	exit(1);
}

/* Binary Dump utility function */
#define MAX_BLOC 16
const unsigned char hex_asc[] = "0123456789abcdef";
static void hexDump (char *desc, void *addr, int len, int bloc) {
    int i, lx, la, l, line;
	long offset = 0;
    unsigned char hexbuf[MAX_BLOC * 3 + 1];	// Hex part of the data (2 char + 1 space)
	unsigned char ascbuf[MAX_BLOC + 1];	// ASCII part of the data
    unsigned char *pc = (unsigned char*)addr;
	unsigned char ch;

	// nothing to output
	if (!len)
		return;

	// Limit the line length to MAX_BLOC
	if (bloc > MAX_BLOC) 
		bloc = MAX_BLOC;

	// Output description if given.
    if (desc != NULL)
		LOG("%s:\n", desc);

	line = 0;
	do
		{
		l = len - (line * bloc);
		if (l > bloc)
			l = bloc;

		for (i=0, lx = 0, la = 0; i < l; i++) {
			ch = pc[i];
			hexbuf[lx++] = hex_asc[((ch) & 0xF0) >> 4];
			hexbuf[lx++] = hex_asc[((ch) & 0xF)];
			hexbuf[lx++] = ' ';

			ascbuf[la++]  = (ch > 0x20 && ch < 0x7F) ? ch : '.';
			}

		for (; i < bloc; i++) {
			hexbuf[lx++] = ' ';
			hexbuf[lx++] = ' ';
			hexbuf[lx++] = ' ';
		}
		// nul terminate both buffer
		hexbuf[lx++] = 0;
		ascbuf[la++] = 0;

		// output buffers
		LOG("%04x %s %s\n", line * bloc, hexbuf, ascbuf);

		line++;
		pc += bloc;
		}
	while (line * bloc < len);
}

/* Connect to the network. */
static bool connect(struct mosquitto *m) {
	int res = mosquitto_connect(m, BROKER_HOSTNAME, BROKER_PORT, KEEPALIVE_SECONDS);
	LOG("Connect return %d\n", res);
	return res == MOSQ_ERR_SUCCESS;
}

/* Callback for successful connection: add subscriptions. */
static void on_connect(struct mosquitto *m, void *udata, int res) {
	if (res == 0) {   /* success */
		LOG("Connect succeed\n");
	} else {
		die("connection refused\n");
	}
}

/* Handle a message that just arrived via one of the subscriptions. */
static void on_message(struct mosquitto *m, void *udata, const struct mosquitto_message *msg) {
	if (msg == NULL) 
		{ return; }

	LOG("-- got message @ %s: (%d, QoS %d, %s) '%s'\n",
		msg->topic, msg->payloadlen, msg->qos, msg->retain ? "R" : "!r",
		msg->payload);

	if (strlen((const char *)msg->topic) < strlen(MQTT_ROOT) + 2 + 3 + 1) {return; }	// message is smaller than "433Switch/xxx/x" so likey invalid

	Payload data;
	uint32_t groupId;
	uint32_t nodeId;

	// extract the target network and the target node from the topic
	sscanf(msg->topic, "433Switch/%lx/%lx", &groupId, &nodeId);
	LOG("GroupId 0x%x\tNodeId 0x%x\n", groupId, nodeId);

	if (strncmp(msg->topic, MQTT_ROOT, strlen(MQTT_ROOT)) == 0) {
		uint32_t cmd = groupId | nodeId;
		int i = 0;
		char cmdStr[10];

		sscanf((const char *)msg->payload, "%9s", cmdStr);

		while (cmdStr[i])
		{
			cmdStr[i] = tolower(cmdStr[i]);
			i++;
		}

        if (strncmp(cmdStr, "on", 2) == 0)
        {
            cmd |= CMD_ON;
        }
        if (strncmp(cmdStr, "off", 3) == 0)
        {
            cmd |= CMD_OFF;
        }
		if (strncmp(cmdStr, "group-on", 8) == 0)
		{
			cmd |= CMD_GROUP_ON;
		}
		if (strncmp(cmdStr, "group-off", 9) == 0)
		{
			cmd |= CMD_GROUP_OFF;
		}

		LOG("Send code 0x%x\n", cmd);

		transmitCode(cmd, 4);
	}
}

/* A message was successfully published. */
static void on_publish(struct mosquitto *m, void *udata, int m_id) {
//	LOG(" -- published successfully\n");
}

/* Successful subscription hook. */
static void on_subscribe(struct mosquitto *m, void *udata, int mid,
		int qos_count, const int *granted_qos) {
//	LOG(" -- subscribed successfully\n");
}

/* Register the callbacks that the mosquitto connection will use. */
static bool set_callbacks(struct mosquitto *m) {
	mosquitto_connect_callback_set(m, on_connect);
	mosquitto_publish_callback_set(m, on_publish);
	mosquitto_subscribe_callback_set(m, on_subscribe);
	mosquitto_message_callback_set(m, on_message);
	return true;
}

void sendPulse(uint8_t highLength, uint8_t lowLength){
    /* Transmit a HIGH signal - the duration of transmission will be determined 
       by the highLength and timeDelay variables */
    digitalWrite(TX_PIN, HIGH);
    usleep(highLength*120);

    /* Transmit a LOW signal - the duration of transmission will be determined 
       by the lowLength and timeDelay variables */
    digitalWrite(TX_PIN, LOW);
    usleep(lowLength*120);
}

#ifdef USE_TELLDUS
void sendBit(int bit)
{
    if (bit)
    {
        sendPulse(1, 2);
        sendPulse(1, 10);
    }
    else
    {
        sendPulse(1, 10);
        sendPulse(1, 2);
    }
}
/*
void transmitCode(uint32_t code, uint8_t rep){
  uint32_t mask = 0x80000000UL;
  
  //The signal is transmitted 8 times in succession - this may vary with your remote.       
  for(int j = 0; j<8; j++){
    for (int i = 0; i < 32; i++){
      int bit = code & mask;
      sendBit(bit);
      mask = mask >> 1;
    }
    sendPulse(1, 91);
    sendPulse(1, 23);
    mask = 0x80000000UL;
  }
}*/
#endif

#ifdef USE_ERIKC
void sendBit(int bit)
{
    if (bit)
    {
        sendPulse(1, 4);
    }
    else
    {
        sendPulse(4, 1);
    }
}
#endif
void transmitCode(uint32_t code, uint8_t rep){
  uint32_t mask = 0x1UL << (CMD_LEN -1); //0x80000000UL;

  LOG("MASK = 0x%lx\n", mask);
  
  //The signal is transmitted 8 times in succession - this may vary with your remote.       
  for(int j = 0; j<8; j++){
    for (int i = 0; i < CMD_LEN; i++){
      int bit = code & mask;
      sendBit(bit);
      mask = mask >> 1;
    }
    sendPulse(1, 47);
    mask = 0x1UL << (CMD_LEN -1); //0x80000000UL;
  }
}

