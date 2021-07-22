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
SYSTEM_MODE(AUTOMATIC);


// We create a USB Serial log handler 
SerialLogHandler logHandler(LOG_LEVEL_INFO);


/* Function prototypes -------------------------------------------------------*/
int tinkerDigitalRead(String pin);
int tinkerDigitalWrite(String command);
int tinkerAnalogRead(String pin);
int tinkerAnalogWrite(String command);

/* This function is called once at start up ----------------------------------*/
void setup() {
	// wait for USB serial logging to start
	Serial.begin();
	delay(3000);
	Log.info("=== setup ===");

	// Register all the Tinker cloud functions. 
	// These can be called from the Particle Cloud console or eg the mobile app. 
	Particle.function("digitalread", tinkerDigitalRead);
	Particle.function("digitalwrite", tinkerDigitalWrite);
	Particle.function("analogread", tinkerAnalogRead);
	Particle.function("analogwrite", tinkerAnalogWrite);
}

/* This function loops forever --------------------------------------------*/
void loop() {
	//This will run in a loop
	delay(3000);
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

