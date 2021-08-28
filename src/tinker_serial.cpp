/**
 * Copyright (c) 2021 Particle Industries, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Tinker
 * This is a simple application to read and toggle pins on a Particle device.
 * This version has been modified to support USB serial logging.
 */

#include <Particle.h>

// This version of Tinker separates system thread from user thread,
// and makes the connection mode explicitly automatic.
SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(SEMI_AUTOMATIC);


// We create a USB Serial log handler 
// SerialLogHandler logHandler(LOG_LEVEL_ALL);
SerialLogHandler logHandler(LOG_LEVEL_INFO, {
    { "net.ppp.client", LOG_LEVEL_INFO },
    { "ncp.at", LOG_LEVEL_ALL },
    { "app", LOG_LEVEL_ALL },
	{"gsm0710muxer", LOG_LEVEL_WARN},
});

static uint32_t last_gps_check_time = 0;
static char last_gps_loc_str[92] = {};


/* Function prototypes -------------------------------------------------------*/
int tinkerDigitalRead(String pin);
int tinkerDigitalWrite(String command);
int tinkerAnalogRead(String pin);
int tinkerAnalogWrite(String command);


/**
 * Parse response from AT+GMR
 * eg BG77LAR02A04
 */ 
static int modem_id_str_cb(int type, const char* buf, int len, char* model_str)
{
    // Log.info("handle type: %d len: %d buf: '%s'", type, len, buf);
    if (sscanf(buf," %s ", model_str) > 0) {
      Log.info("found: %s", model_str);
      return RESP_OK;
    }

  return WAIT;
}

void run_gps_setup() {
	char modem_str[64] = {};
 	Cellular.command(modem_id_str_cb, modem_str, 6000, "AT+GMR");    

	// Enable GPS antenna power on the appropriate pins
    if (nullptr != strstr( modem_str, "BG77")) {
      Log.info("BG77 modem");
      Cellular.command(1000, "AT+QCFG=\"GPIO\",1,1,1,0,0\r\n");
      Cellular.command(1000, "AT+QCFG=\"GPIO\",3,1,1\r\n");
      Cellular.command(1000, "AT+QCFG=\"GPIO\",2,1\r\n");
    }
    else if (nullptr != strstr( modem_str, "BG95")) {
      Log.info("BG95 modem");
      Cellular.command(5000, "AT+QCFG=\"GPIO\",1,26,1,0,0\r\n");
      Cellular.command(5000, "AT+QCFG=\"GPIO\",3,26,1\r\n");
      Cellular.command(1000, "AT+QCFG=\"GPIO\",2,26\r\n");
    }

    // Disable existing GPS session, if any
    // This is necessary to ensure subsequent GPS cfg statements are effective
    Cellular.command(1000,"AT+QGPSEND\r\n");

    // prep for gpsOneXTRA (correction) file download
    Cellular.command(1000,"AT+QGPSXTRA=1\r\n");
    Cellular.command(1000,"AT+QGPSXTRATIME?\r\n");
    Cellular.command(1000,"AT+QGPSCFG=\"xtra_info\"\r\n");
    Cellular.command(1000,"AT+QGPSXTRADATA?\r\n");

    // enable eDRX interval of 40 seconds
    // Note that this apparently needs to be longer than the GNSS reporting interval
    // (see Quectel GNSS App Note)
    //Cellular.command(1000,"AT+CEDRXS=1,4,\"0011\"\r\n");

    // Begin GPS
    Cellular.command(5000, "AT+QGPSCFG=\"outport\",\"none\"");    // disable NMEA output
    // Cellular.command(5000, "AT+QGPSCFG=\"outport\",\"uartnmea\"");    // specified GPX_TX pin to output nmea data
    // Cellular.command(5000, "AT+QGPSCFG=\"outport\",\"usbnmea\"");     // specified usb port to output nmea data

    // Make GPS the priority vs WWAN.
    // This appears to give a GNSS lock faster at startup.
    // Once a lock has been obtained you can swap the priority back
    Cellular.command(1000, "AT+QGPSCFG=\"priority\",0,1\r\n");

	auto res = Cellular.command(1000, "AT+QGPS=1\r\n");
	Log.info("QPGS result: %d", res);

}

/* This function is called once at start up ----------------------------------*/
void setup() {

	// wait for USB serial logging to start
	// Serial.begin();
	delay(3000);
	Log.info("==== setup ====");
	// Cellular.setActiveSim(INTERNAL_SIM);

	// Register all the Tinker cloud functions. 
	// These can be called from the Particle Cloud console or eg the mobile app. 
	Particle.function("digitalread", tinkerDigitalRead);
	Particle.function("digitalwrite", tinkerDigitalWrite);
	Particle.function("analogread", tinkerAnalogRead);
	Particle.function("analogwrite", tinkerAnalogWrite);

	Cellular.command("ATI0\r\n");
	Cellular.command("ATI9\r\n");

	Cellular.command("AT+CCID?\r\n");

	// run_gps_setup();
	// delay(3000);

	Particle.connect();

}


/**
 * Find gpsloc callback, eg:
 * +QGPSLOC: 142008.000,37.87498,-122.29064,1.0,19.4,2,0.00,0.0,0.0,040821,06
 * Note that five digits of decimal degree precision is approximates one meter accuracy--
 * ie this is plenty for driving scenarios.
 */
static int qgpsloc_cb(int type, const char* buf, int len, char* gps_loc_str)
{
    // Log.info("handle type: %d len: %d buf: '%s'", type, len, buf);
    if (sscanf(buf," +QGPSLOC: %s ", gps_loc_str) > 0) {
      return RESP_OK;
    }

  return WAIT;
}

/* This function loops forever --------------------------------------------*/
void loop() {
	static bool gps_once = false;
	//This will run in a loop
	if (!Particle.connected()) {
		Log.info("connection wait...");
		Cellular.command("AT+CCID?\r\n");
		delay(3000);
		return;
	}
	uint32_t cur_time = millis();
  	if ((cur_time - last_gps_check_time) >= 10000) {
		int gps_resp = Cellular.command(qgpsloc_cb, last_gps_loc_str, 1000, "AT+QGPSLOC=2\r\n");
		if (RESP_OK == gps_resp) {
			Log.info("GPSLOC: %s", last_gps_loc_str);
			if (!Particle.connected()) {
				Log.info("reconnect particle!");
				Particle.connect();
				waitFor(Particle.connected, 30000);
			}
		}
		else if ((-3 == gps_resp) && !gps_once) {
			Log.warn("error 505 ?");
			run_gps_setup();
			gps_once = true;
			delay(3000);

			auto res = Cellular.command(1000, "AT+QGPS=1\r\n");
			Log.info("QPGS result: %d", res);
		}
		else {
			Log.warn("error: %d", gps_resp);
		}
		last_gps_check_time = cur_time;
  	}
	else {
		delay(3000);

	}

}

/*******************************************************************************
 * Function Name  : tinkerDigitalRead
 * Description    : Reads the digital value of a given pin
 * Input          : Pin
 * Output         : None.
 * Return         : Value of the pin (0 or 1) in INT type
                    Returns a negative number on failure
 *******************************************************************************/
int tinkerDigitalRead(String param) {

	Log.info("digitalRead: %s", param.c_str());

	//convert ascii to integer
	int pinNumber = param.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber < 0 || pinNumber > 7) return -1;

	if(param.startsWith("D")) {
		pinMode(pinNumber, INPUT_PULLDOWN);
		return digitalRead(pinNumber);
	}
	else if (param.startsWith("A")) {
		pinMode(pinNumber+10, INPUT_PULLDOWN);
		return digitalRead(pinNumber+10);
	}
	return -2;
}

/*******************************************************************************
 * Function Name  : tinkerDigitalWrite
 * Description    : Sets the specified pin HIGH or LOW
 * Input          : Pin and value
 * Output         : None.
 * Return         : 1 on success and a negative number on failure
 *******************************************************************************/
int tinkerDigitalWrite(String param) {
	Log.info("digitalWrite: %s", param.c_str());

	bool value = 0;
	//convert ascii to integer
	int pinNumber = param.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber< 0 || pinNumber >7) return -1;

	if (param.substring(3,7) == "HIGH") value = 1;
	else if (param.substring(3,6) == "LOW") value = 0;
	else return -2;

	if (param.startsWith("D")) {
		pinMode(pinNumber, OUTPUT);
		digitalWrite(pinNumber, value);
		return 1;
	}
	else if(param.startsWith("A")) {
		pinMode(pinNumber+10, OUTPUT);
		digitalWrite(pinNumber+10, value);
		return 1;
	}
	else return -3;
}

/*******************************************************************************
 * Function Name  : tinkerAnalogRead
 * Description    : Reads the analog value of a pin
 * Input          : Pin
 * Output         : None.
 * Return         : Returns the analog value in INT type (0 to 4095)
                    Returns a negative number on failure
 *******************************************************************************/
int tinkerAnalogRead(String param) {

	Log.info("analogRead: %s", param.c_str());

	//convert ascii to integer
	int pinNumber = param.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber < 0 || pinNumber > 7) return -1;

	if(param.startsWith("D")) {
		return -3;
	}
	else if (param.startsWith("A")) {
		return analogRead(pinNumber+10);
	}

	return -2;
}

/*******************************************************************************
 * Function Name  : tinkerAnalogWrite
 * Description    : Writes an analog value (PWM) to the specified pin
 * Input          : Pin and Value (0 to 255)
 * Output         : None.
 * Return         : 1 on success and a negative number on failure
 *******************************************************************************/
int tinkerAnalogWrite(String param)
{
	Log.info("analogWrite: %s", param.c_str());

	//convert ascii to integer
	int pinNumber = param.charAt(1) - '0';
	//Sanity check to see if the pin numbers are within limits
	if (pinNumber < 0 || pinNumber > 7) return -1;

	String value = param.substring(3);

	if(param.startsWith("D")) {
		pinMode(pinNumber, OUTPUT);
		analogWrite(pinNumber, value.toInt());
		return 1;
	}
	else if(param.startsWith("A")) {
		pinMode(pinNumber+10, OUTPUT);
		analogWrite(pinNumber+10, value.toInt());
		return 1;
	}
	else return -2;
}

