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
SYSTEM_MODE(MANUAL);

#include "fsm.h"


// State machine triggers (events)
#define FLIP_LIGHT_SWITCH 1
#define MAKE_CONNECTION		2
#define BREAK_CONNECTION	4
#define ENABLE_GPS			8
#define DISABLE_GPS			16


const int USER_LED_PIN = D7;


static uint32_t gps_lock_count = 0;

// We create a USB Serial log handler 
SerialLogHandler logHandler(LOG_LEVEL_ALL);
// SerialLogHandler logHandler(LOG_LEVEL_INFO, {
//     { "net.ppp.client", LOG_LEVEL_ALL },
//     { "ncp.at", LOG_LEVEL_ALL },
//     { "app", LOG_LEVEL_ALL },
// 	{"gsm0710muxer", LOG_LEVEL_WARN},
// });

static uint32_t last_gps_check_time = 0;
static uint32_t last_gps_update_time = 0;

static char last_gps_loc_str[92] = {};
static char publish_buf[512] = {};


// protos
static int qgpsloc_cb(int type, const char* buf, int len, char* gps_loc_str);


// State machine cb functions

void set_gps_priority(bool enable) {
	if (enable) {
		// Make GNSS the priority over WWAN.
		Cellular.command(1000, "AT+QGPSCFG=\"priority\",0,0\r\n");
		digitalWrite(USER_LED_PIN, HIGH);
	}
	else {
		// Make WWAN priority over GNSS
		Cellular.command(1000, "AT+QGPSCFG=\"priority\",1,0\r\n");
		digitalWrite(USER_LED_PIN, LOW);

		//check eDRX
		// Cellular.command("AT+CEDRXRDP\r\n");
	}
}

void gps_begin_session() {
	Cellular.command(1000, "AT+QGPS=1\r\n");
}

void gps_end_session() {
    Cellular.command(1000,"AT+QGPSEND\r\n");
}

void on_gps_poll_enter() {
	if (0 == gps_lock_count) {
		gps_begin_session();
		set_gps_priority(true);
	}
}

void on_gps_poll_update() {
	uint32_t cur_time = millis();
	if ((cur_time - last_gps_check_time) > 15000) {

		if (gps_lock_count > 0) {
			set_gps_priority(true);
			delay(1500);//time to switch priorities
		}

		int gps_resp = Cellular.command(qgpsloc_cb, last_gps_loc_str, 1000, "AT+QGPSLOC=2\r\n");
		last_gps_check_time = cur_time;
		if (RESP_OK == gps_resp) {
			// response format:
			// +QGPSLOC: <UTC>,<latitude>,<longitude>,<HDOP>,<altitude>,<fix>,<COG>,<spkm>,<spkn>,<date>,<nsat>
			// example: "022137.000,37.87512,-122.29062,0.8,11.8,3,0.00,0.0,0.0,280821,09"
			// `170144.000,37.87508,-122.29074,1.3,38.4,2,0.00,0.0,0.0,280821,05`
			
			last_gps_update_time = cur_time;
			gps_lock_count += 1;
			Log.warn("GPSLOC resp: `%s`", last_gps_loc_str);
			set_gps_priority(false);
	
		}
		else {
			last_gps_update_time = 0;
		}
	}

}

void on_gps_poll_exit() {
	if (0 == gps_lock_count) {
		set_gps_priority(false);
	}
}


State state_gps_idle(nullptr, nullptr, nullptr);
State state_gps_poll(on_gps_poll_enter, &on_gps_poll_update, &on_gps_poll_exit);
Fsm gps_fsm(&state_gps_idle);

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



void gps_pre_init() {
	Log.info("=== pre-init gps ===");

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
    gps_end_session();

    // prep for gpsOneXTRA (correction) file download
    Cellular.command(1000,"AT+QGPSXTRA=1\r\n");
    Cellular.command(1000,"AT+QGPSXTRATIME?\r\n");
    Cellular.command(1000,"AT+QGPSCFG=\"xtra_info\"\r\n");
    Cellular.command(1000,"AT+QGPSXTRADATA?\r\n");

    // enable eDRX interval of 40 seconds
    // Note that this apparently needs to be longer than the GNSS reporting interval
    // (see Quectel GNSS App Note)
	// Cellular.command(1000,"AT+CEDRXS=0,4\r\n"); //disable eDRX
    // Cellular.command(1000,"AT+CEDRXS=1,4,\"0011\"\r\n"); // 40 secs
    // Cellular.command(1000,"AT+CEDRXS=1,4,\"0100\"\r\n"); // 60 secs
	Cellular.command(1000,"AT+CEDRXS=1,4,\"0111\"\r\n"); // 122 secs


    // Begin GPS
    Cellular.command(5000, "AT+QGPSCFG=\"outport\",\"none\"");    // disable NMEA output
    // Cellular.command(5000, "AT+QGPSCFG=\"outport\",\"uartnmea\"");    // specified GPX_TX pin to output nmea data
    // Cellular.command(5000, "AT+QGPSCFG=\"outport\",\"usbnmea\"");     // specified usb port to output nmea data

    // Make GNSS the priority over WWAN.
    // Cellular.command(1000, "AT+QGPSCFG=\"priority\",0,1\r\n");

	// Make WWAN the priority over GNSS (GNSS will read while WWAN is sleeping)
	// Cellular.command(1000, "AT+QGPSCFG=\"priority\",1,1\r\n");

}

/* This function is called once at start up ----------------------------------*/
void setup() {

	// wait for USB serial logging to start
	// Serial.begin();
	delay(3000);
	Log.info("==== setup ====");
	pinMode(USER_LED_PIN, OUTPUT);

	// Cellular.setActiveSim(INTERNAL_SIM);

	//Cellular.command("ATI0\r\n");
	//Cellular.command("ATI9\r\n");
	//Cellular.command("AT+CCID\r\n");

	// ensure that a GNSS session is not runnin
    // Cellular.command(1000,"AT+QGPSEND\r\n");

	// set WWAN as the priority over GNSS before enabling eDRX
	Cellular.command(1000, "AT+QGPSCFG=\"priority\",1,1\r\n");

	// set eDRX interval
    // Cellular.command(1000,"AT+CEDRXS=1,4,\"0100\"\r\n"); // 60 secs

	gps_fsm.add_transition(&state_gps_idle, &state_gps_poll, ENABLE_GPS, nullptr);
	gps_fsm.add_transition(&state_gps_poll, &state_gps_idle, DISABLE_GPS, nullptr);
	gps_fsm.run_machine();

	delay(3000);
	Log.info("=== Starting first Particle.connect ===");
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
	static bool gps_activated = false;

	//This will run in a loop
	if (!Particle.connected()) {
		Log.info("=== connection wait... ===");
		Cellular.command("AT+CCID\r\n");
		delay(3000);
		return;
	}
	else if (!gps_activated) {
		gps_pre_init();
		gps_activated = true;
		delay(5000);
		gps_fsm.trigger(ENABLE_GPS);
		return;
	}
	else {
		gps_fsm.run_machine();
		if (last_gps_update_time > 0) {
			gps_fsm.trigger(DISABLE_GPS);
			sprintf(publish_buf, "{ \"time\": %lu, \"gpsloc2\": \"%s\" }",last_gps_update_time, last_gps_loc_str);
			Log.trace("publish....");
			bool acked = Particle.publish("bg77raw",publish_buf,WITH_ACK);
			if (acked) {
				last_gps_update_time = 0;
				gps_fsm.trigger(ENABLE_GPS);
				Log.info("published: %d", gps_lock_count);
			}
			else {
				Log.warn("publish failed!");
				gps_fsm.trigger(DISABLE_GPS);
				gps_end_session();
			}
		}


	}
		  


}

