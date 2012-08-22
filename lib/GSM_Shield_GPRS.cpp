/*
 * GSM_Shield_GPRS.cpp
 *
 *  Created on: 29 июля 2012
 *      Author: adik
 *
 *
 *      https://github.com/shklovich/Picon2GSA3/blob/50749272628da61d967e7053455c0ae3c3270b7f/src/modem.c
 */

#include <GSM_Shield_GPRS.h>
#include <GSM_Shield.h>

/**********************************************************
 Class constructor
**********************************************************/
GPRS::GPRS(void) : GSM(), gprs_state(PDP_DEACT) { }


/**********************************************************

**********************************************************/
void GPRS::WaitGprsData() {

	unsigned long 	prev_time;
	int 			c = 0;
	byte			state;
	byte			read_byte_count = 0;
	uint16_t		recv_data_size = 0,
					left_data_size = 0;
	uint16_t		*pt;

	enum req_state_enum
	{
		SEND_REQ_HEADER = 0,
		REQ_HEADER_PARSE,
		GET_DATA
	} req_state;

	// waiting for receive +CIPRXGET:1
	prev_time = millis();
	do {
		// break if timeout
		if (millis() - prev_time > 60000) return;
		state = WaitResp(1000, 1000, "T:1");

	} while
		(state != RX_FINISHED_STR_RECV);

	// init start variables
	req_state = SEND_REQ_HEADER;
	prev_time = millis();

	while (1) {

		// break if timeout
		if (millis() - prev_time > 60000) {
			return;
		}

		// receive nn bytes of data
		if (req_state == SEND_REQ_HEADER) {
			mySerial.flush();
			vprintf_P(mySerial, PSTR("AT+CIPRXGET=2,220")); // TODO: calculate buffer size
			mySerial.write('\r');

			req_state = REQ_HEADER_PARSE;
			read_byte_count = 0;
			recv_data_size = 0;
			left_data_size = 0;
			pt = &recv_data_size;
			continue;
		}

		//
		c = mySerial.read();
		if (c <= 0) continue;

		// if there are some received bytes postpone the timeout
		prev_time = millis();
		read_byte_count++;

		// process states
		switch(req_state) {
		case REQ_HEADER_PARSE:
			//\r\n+CIPRXGET:2,255,52
			if ( read_byte_count > 14 ) {
				if (c == '\n') {
					/*#ifdef DEBUG_PRINT
					DebugPrint(F("read_byte_count:"), 0);
					Serial.println(read_byte_count);
					DebugPrint(F("recv_data_size:"), 0);
					Serial.println(recv_data_size);
					DebugPrint(F("left_data_size:"), 0);
					Serial.println(left_data_size);
					#endif*/
					read_byte_count = 0;
					req_state = GET_DATA;
				}
				else if(c >= '0' && c <= '9') {
					*pt = *pt * 10 + c - '0';
				}
				else {
					pt = &left_data_size;
				}

			}
			break;

		//
		case GET_DATA:
			// handle received
			_onReceiveHandler(c);

			if (read_byte_count == recv_data_size) {
				// going to new iteration because we haven't read all
				if ((left_data_size > 0)) {
					req_state = SEND_REQ_HEADER;
				}
				// end receiving data
				else {
					return;
				}
			}
			break;
		}
	}//end while
}


/**********************************************************

**********************************************************/
void GPRS::fetchState() {

	SetCommLineStatus(CLS_ATCMD);

	vprintf_P(mySerial,	PSTR("AT+CIPSTATUS")); mySerial.print('\r');

	// 5 sec. for initial comm tmout
	// 50 msec. for inter character timeout
	if (RX_FINISHED == WaitResp(5000, 50)) {
	    if(IsStringReceived("INIT")) {
			setState(IP_INITIAL);
	    }
	    else if(IsStringReceived("CP CO")) {
	    	setState(TCP_CONNECTING);
	    }
	    else if(IsStringReceived("T OK")) {
	    	setState(CONNECT_OK);
	    }
	    else if(IsStringReceived("CLOS")) {
	    	setState(TCP_CLOSED);
	    }
	    else {
	    	setState(PDP_DEACT);
	    }
	} else {

	}

	SetCommLineStatus(CLS_FREE);
}


/**********************************************************
 Module saves current TCPIP Application Contexts to NVRAM. When
 system is rebooted, the parameters will be loaded automatically.
 **********************************************************/
void GPRS::GPRS_Context2Nvram() {
	SendATCmdWaitResp(1000, 50, "OK", 3, PSTR("AT+CIPSCONT"));
}


/*
 * Start Up TCP Connection
 *
 */
void GPRS::TCP_Connect() {
	/*
	 * This command allows establishment of a TCP/UDP connection only
	 *   when the state is IP INITIAL or IP STATUS when it is in single state.
	 *   In multi-IP state, the state is in IP STATUS only. So it is necessary to
	 *   process "AT+CIPSHUT" before user establishes a TCP/UDP
	 *   connection with this command when the state is not IP INITIAL or IP
	 *   STATUS.
	 * When module is in multi-IP state, before this command is executed, it
	 *   is necessary to process "AT+CSTT, AT+CIICR, AT+CIFSR".
	 */
	if (AT_RESP_OK == SendATCmdWaitResp(1000, 5000, "T OK", 1,
					PSTR("AT+CIPSTART=\"TCP\",\"ws.pusherapp.com\",\"80\""))) {

		setState(CONNECT_OK);
	}
}


void GPRS::TCP_Send(const prog_char *data, ... ) {
	va_list   args;

	/* This Command is used to send changeable length data
	 * If single IP is connected (+CIPMUX=0)
	 * If connection is not established or module is disconnected: +CME ERROR <err>
	 * If sending is successful:  When +CIPQSEND=0 = "SEND OK"  When +CIPQSEND=1  "DATA ACCEPT:<length>"
	 * If sending fails: "SEND FAIL"
	 */
	if (AT_RESP_OK == SendATCmdWaitResp(2000, 100, ">", 1, PSTR("AT+CIPSEND")))
	{
		va_start(args, data);
		vprintf_P(mySerial, data, args); mySerial.write('\x1A');
		va_end(args);

		WaitGprsData();
	}
}



void GPRS::TCP_Close() {
	/*
	 * AT+CIPCLOSE only closes connection at the status of TCP/UDP which
	 * returns CONNECTING or CONNECT OK, otherwise it will return
	 * ERROR, after the connection is closed, the status is IP CLOSE in single IP mode *
	 */
	if (AT_RESP_OK == SendATCmdWaitResp(1000, 50, "OK", 3, PSTR("AT+CIPCLOSE=0"))) {
		setState(TCP_CLOSED);
	}
}


/* *********************************************
 *  Start Task and Set APN
 *  Bring Up Wireless Connection with GPRS and Get Local IP Address
 *
 */
void GPRS::GPRS_attach() {
	byte status;
    byte no_of_attempts = 0;

	if (AT_RESP_OK == SendATCmdWaitResp(1000, 50, "OK", 3, PSTR("AT+CGATT=1"))) {

		/*
		 * Get Data from Network Manually
		 */
		SendATCmdWaitResp(1000, 50, "OK", 3, PSTR("AT+CIPRXGET=1"));

		/* The write command and execution command of this command is valid only
		 * at the state of IP INITIAL. After this command is executed, the state will be
		 * changed to IP START.  */
		SendATCmdWaitResp(1000, 50, "OK", 3, PSTR("AT+CSTT=\"internet\",\"\",\"\""));

		/* AT+CIICR only activates moving scene at the status of IP START,
		 * after operating this Command is executed, the state will be changed to
		 * IP CONFIG.
		 * After module accepts the activated operation, if it is activated
		 * successfully, module state will be changed to IP GPRSACT, and it
		 * responds OK, otherwise it will responsd ERROR.  */
		SendATCmdWaitResp(1000, 50, "OK", 3, PSTR("AT+CIICR"));

		/*
		 * Only after PDP context is activated, local IP Address can be obtained by
		 * AT+CIFSR, otherwise it will respond ERROR. The active status are IP
		 * GPRSACT, TCP/UDP CONNECTING, CONNECT OK, IP CLOSE. */
		do {
			if (++no_of_attempts > 4) {
				setState(IP_BUSY);
				goto finish;
			}

			SetCommLineStatus(CLS_ATCMD);
			vprintf_P(mySerial, PSTR("AT+CIFSR"));mySerial.write('\r');
			status = WaitResp(1000, 50);
			SetCommLineStatus(CLS_FREE);

			delay(1000);

		} while ((status != RX_FINISHED) || IsStringReceived("ERROR"));

		setState(IP_STATUS);


		// Add an IP Head at the Beginning of a Package Received
		//      (0)	not add IP header
		//      (1) add IP header, the format is "+IPD,data length:"
		SendATCmdWaitResp(1000, 50, "OK", 3, PSTR("AT+CIPHEAD=0"));

		/*
		 * Select TCPIP Application Mode
		 * 0 normal mode
		 * 1 transparent mode
		 */
		SendATCmdWaitResp(1000, 50, "OK", 1, PSTR("AT+CIPMODE=0"));

		/*
		 * Select Data Transmitting Mode
		 *
		 * (0) Normal mode – when the server receives TCP data, it will
		 * responsd SEND OK.
		 *
		 * (1) Quick send mode – when the data is sent to module, it will
		 * responsd DATA ACCEPT:<n>,<length>, while not responding
		 * SEND OK.
		 */
		SendATCmdWaitResp(1000, 50, "OK", 1, PSTR("AT+CIPQSEND=0"));
	}

finish:
	return;
}


/**********************************************************
  Deactivate GPRS PDP Context
**********************************************************/
void GPRS::GPRS_detach() {
	/* If this command is executed in multi-connection mode, all of the IP
	 * connection will be shut.
	 *   User can close gprs pdp context by AT+CIPSHUT. After it is closed,
	 * the status is IP INITIAL.
	 *   If "+PDP: DEACT" urc is reported which means the gprs is released by
	 * the network, then user still needs to execute "AT+CIPSHUT"
	 * command to make PDP context come back to original state. */
	SendATCmdWaitResp(1000, 50, "OK", 3, PSTR("AT+CIPSHUT"));
	setState(IP_INITIAL);
}


