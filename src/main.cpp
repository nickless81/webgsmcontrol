/*
 * Target: AVR328P
 * Crystal: 16.000Mhz

 *  DOCS:
 *  http://imall.iteadstudio.com/im120417009.html
 *
 *  minicom -c on -D /dev/ttyACM0

 */
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <GSM_Shield_GPRS.h>
#include <GSM_Shield.h>
#include <main.h>

#include "SimpleJsonParser.h"
#include "sha256.h"

#ifdef DEBUG_PRINT
char _serial_buffer[SERIAL_BUFF_SIZE+1];
#endif


//for enable disable debug rem or not the string #define DEBUG_PRINT
// definition of instance of GSM class
GPRS gsm;


// declare parser variable
static json_parser_t json_parser = {};


const prog_char HTTP_connectWS[] PROGMEM = {
			"GET /app/%p?client=js&version=1.12&protocol=5 HTTP/1.1\r\n"
			"Upgrade: WebSocket\r\n"
			"Connection: Upgrade\r\n"
			"Host: %p\r\n"
			"Origin: ArduinoWebSocketClient\r\n"
			"\r\n" };



static const prog_char  Pusher_Key[] PROGMEM= {"8185ce71534c69c42b72"};
static const uint8_t Pusher_Secret[]= {"c30a8df113dd52dc64e4"};

static char auth_token[65];

/**********************************************************

**********************************************************/
void generate_auth(char *str) {

	uint8_t *hash;
	char 	*ap;

	// encryption
	Sha256Class *Sha256 = new Sha256Class();

	Sha256->initHmac(Pusher_Secret, 20);
	Sha256->print(str);

	hash = Sha256->resultHmac();

	ap = &auth_token[0];

	for (uint8_t i=0; i<HASH_LENGTH; i++) {
			*ap++ = "0123456789abcdef"[hash[i]>>4];
			*ap++ = "0123456789abcdef"[hash[i]&0xf];
	}
	*ap='\0';
	delete Sha256;
}

/**********************************************************
 Receive GPRS data handler
**********************************************************/

// declare state
enum recv_state_enum {
	GET_HEADER = 0,
	WAIT_HEADER_END = 1,
	HEADER_RECEIVED = 3
};
static byte recv_state;

inline byte get_recv_state() { return recv_state; }
inline void set_recv_state(byte state) { recv_state = state; }

void recv_data(byte chr) {
	// skip header
	switch (get_recv_state()) {
	case HEADER_RECEIVED:
		goto process_data;
		break;

	case GET_HEADER:
		if (chr == '\r')
			goto process_header;
		if (chr == '\n')
			set_recv_state(WAIT_HEADER_END);
		goto process_header;
		break;

	case WAIT_HEADER_END:
		if (chr == '\r')
			goto process_header;
		if (chr == '\n')
			set_recv_state(HEADER_RECEIVED);
		else
			set_recv_state(GET_HEADER);

		goto process_header;
		break;
	}

process_data:

#ifdef DEBUG_PRINT
	//Serial.write(chr);
#endif

// parse received json stream
	if ( json_parse(&json_parser, chr) ) {

		char *event, *data;

		// get event
		event = json_get_tag_value(&json_parser, "event");

		if (event) {

			Serial.print(event);

			if (strstr(event, "pusher")) {

				if (strstr(event, "connection_established")) {
					if ((data = json_get_tag_value(&json_parser, "data"))) {
						char tmp[24];
						memset(tmp,'\0', 24);
						char channel[]=":private-cmd";

						//"{\"socket_id\":\"12035.86349\"}";
						strncpy(tmp, data+14, (strlen(data)-2-14));
						strcat(tmp, channel);

						generate_auth(tmp);

						gsm.TCP_Send(
								PSTR("%d{\"event\":\"%p\",\"data\":{\"channel\":\"private-cmd\",\"auth\":\"%p:%s\"}}%d"),
								0,
								PSTR("pusher:subscribe"),
								Pusher_Key,
								auth_token,
								255
						) ;

						//Serial.println(data);
						//Serial.println(auth_token);
						//Serial.println((char*)Pusher_Key);
						//Serial.println((char*)Pusher_Key);
					}
					free(data);
				}
				else if (strstr(event, "ping")) {
						gsm.TCP_Send( PSTR("%d{\"event\":\"pusher:pong\",\"data\":\"pong\"}%d"),
								0,
								255
						) ;
				}

			}
			else  {
				gsm.TCP_Send(
					PSTR("%d{\"event\":\"client-responce\",\"data\":\"somedata\",\"channel\":\"private-cmd\"}%d"),
					0,
					255
				);
			}
		}

		// clean garbage
		free(event);
		json_clean_tokens(&json_parser);
	}

process_header:
	return;
}


/**********************************************************

**********************************************************/
void ws_event( const prog_char *fmt, ... ) {
	va_list  args;
	va_start(args, fmt);
	//gsm.TCP_Send(HTTP_WSEvent, 0, fmt, args, 255 );
	va_end(args);
}

/**********************************************************
	- Connect to WebSocket
	- Send Handshake
	- Subscribe to a channel
**********************************************************/
void connect_ws() {
	// connect to WS
	gsm.TCP_Connect(F("ws.pusherapp.com"));

	if (CONNECT_OK == gsm.getState()) {
		set_recv_state(GET_HEADER);
		// send handshake

		gsm.TCP_Send( HTTP_connectWS, Pusher_Key, PSTR("ws.pusherapp.com"));
	}
}



/**********************************************************

**********************************************************/
inline void setup() {
#ifdef DEBUG_PRINT
	//Serial.begin(38400);
	Serial.begin(38400);
#endif
	//gsm.InitSerLine(9600); //initialize serial 1
	gsm.TurnOn(38400); //module power on
	//gsm.TurnOn(38400); //module power on
	//gsm.TurnOn(19200); //module power on
	//gsm.InitSerLine(9600); //initialize serial 1
	gsm.InitParam(PARAM_SET_0); //configure the module
	//
	//
	json_init(&json_parser);
	gsm.setRecvHandler(recv_data);
}


/**********************************************************

**********************************************************/
static unsigned long last_fetch_time;

inline void loop() {

	if ((unsigned long)(millis() - last_fetch_time) >= 10000) {
		gsm.fetchState();
		last_fetch_time = millis();
	}

	switch(gsm.getState()) {
	case TCP_CONNECTING:
		break;

	case CONNECT_OK:
		gsm.handleCommunication();
		break;

	case TCP_CLOSED:
		connect_ws();
		// need reconnect timeout
		break;

	default:
		gsm.GPRS_detach();
		gsm.GPRS_attach();
		connect_ws();
		break;
	}

#ifdef DEBUG_PRINT
	// process serial commands
	if (Serial.available()){
		onSerialReceive(_serial_buffer);
	}
#endif
	//delay(6000);
}

////////// ---------------------------------- ////////
int main(void) {
	init();
	setup();
	for (;;) {
		loop();
		//if (serialEventRun) serialEventRun();
	}
	return 0;
}

/****************************************************************************
 *
 * export TTYDEV=/dev/ttyACM0
 * stty raw speed 9600 -crtscts cs8 -parenb -parodd cstopb -hupcl cread clocal ignbrk ignpar -F $TTYDEV
 * xxd -c1 $TTYDEV
 *
 * echo -en "AT\r" > $TTYDEV
 *
 */
#ifdef DEBUG_PRINT

byte incomingByte;
volatile byte recv_byte = 0;

inline void onSerialReceive(char *buffer) {
	incomingByte = Serial.read();
	buffer[recv_byte] = incomingByte;
	recv_byte++;

	if (incomingByte == '\0' || recv_byte > SERIAL_BUFF_SIZE ) {
		buffer[recv_byte] = '\0';
		SerialProcessCommand(buffer);

		*buffer = '\0';
		recv_byte = 0;
	}
}
inline int8_t SerialProcessCommand(char const *buffer) {
	Serial.print(F("GetCommand\r\n"));
	gsm.SendATCmdWaitResp(1000, 50, "", 1, PSTR("%s"), buffer);
	return 1;
}
#endif
