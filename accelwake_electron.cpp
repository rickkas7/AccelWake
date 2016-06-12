
#include "Particle.h"

#include "ADXL362DMA/ADXL362DMA.h"

// Project Location:
// https://github.com/rickkas7/AccelWake

// Libraries above can be found here:
// https://github.com/rickkas7/ADXL362DMA

// System threading is required for this project
SYSTEM_THREAD(ENABLED);

// Connect the ADXL362 breakout:
// VIN: 3V3
// GND: GND
// SCL: A3 (SCK)
// SDA: A5 (MOSI)
// SDO: A4 (MISO)
// CS: A2 (SS)
// INT1: D2
// INT2: no connection
ADXL362DMA accel(SPI, A2);

FuelGauge batteryMonitor;

// This is the pin connected to the INT1 pin on the ADXL362 breakout
// Don't use D0 or D1; they are used for I2C to the PowerShield battery monitor
const int ACCEL_INT_PIN = D2;

// This is the name of the Particle event to publish for battery or movement detection events
// It is a private event.
const char *eventName = "accel";

// Various timing constants
const unsigned long MAX_TIME_TO_PUBLISH_MS = 60000; // Only stay awake for 60 seconds trying to connect to the cloud and publish
const unsigned long TIME_AFTER_PUBLISH_MS = 4000; // After publish, wait 4 seconds for data to go out
const unsigned long TIME_AFTER_BOOT_MS = 10000; // At boot, wait 10 seconds before going to sleep again
const unsigned long TIME_PUBLISH_BATTERY_SEC = 4 * 60 * 60; // every 4 hours, send a battery update

// Stuff for the finite state machine
enum State { RESET_STATE, RESET_WAIT_STATE, PUBLISH_STATE, SLEEP_STATE, SLEEP_WAIT_STATE, BOOT_WAIT_STATE };
State state = RESET_STATE;
unsigned long stateTime = 0;
int awake = 0;

void setup() {
	Serial.begin(9600);
}


void loop() {
	uint8_t status;

	switch(state) {
	case RESET_STATE:
		Serial.println("resetting accelerometer");
		accel.softReset();
		state = RESET_WAIT_STATE;
		break;

	case RESET_WAIT_STATE:
		status = accel.readStatus();
		if (status != 0) {
			// Reset complete

			// Set accelerometer parameters

			// uint8_t range, bool halfBW, bool extSample, uint8_t odr
			// Set sample rate to 50 samples per second
			accel.writeFilterControl(accel.RANGE_2G, false, false, accel.ODR_50);

			// Set activity and inactivity thresholds, change these to make the sensor more or less sensitive
			accel.writeActivityThreshold(250);
			accel.writeInactivityThreshold(150);

			// Activity timer is not used because when inactive we go into sleep mode; it automatically wakes after 1 sample
			// Set inactivity timer to 250 or 5 seconds at 50 samples/sec
			accel.writeInactivityTime(250);

			// Enable loop operation (automatically move from activity to inactivity without having to clear interrupts)
			// Enable activity and inactivity detection in reference mode (automatically accounts for gravity)
			// uint8_t linkLoop, bool inactRef, bool inactEn, bool actRef, bool actEn
			accel.writeActivityControl(accel.LINKLOOP_LOOP, true, true, true, true);

			// Map the AWAKE bit to INT1 so activity can wake up the Photon
			accel.writeIntmap1(accel.STATUS_AWAKE);

			// Enable measuring mode with auto-sleep
			// bool extClock, uint8_t lowNoise, bool wakeup, bool autosleep, uint8_t measureMode
			accel.writePowerCtl(false, false, false, true, accel.MEASURE_MEASUREMENT);

			state = BOOT_WAIT_STATE;
		}
		break;

	case PUBLISH_STATE:
		if (Particle.connected()) {
			// The publish data contains 3 comma-separated values:
			// whether movement was detected (1) or not (0) The not detected publish is used for battery status updates
			// cell voltage (decimal)
			// state of charge (decimal)
			char data[32];
			float cellVoltage = batteryMonitor.getVCell();
			float stateOfCharge = batteryMonitor.getSoC();
			snprintf(data, sizeof(data), "%d,%.02f,%.02f", awake, cellVoltage, stateOfCharge);

			Particle.publish(eventName, data, 60, PRIVATE);

			// Wait for the publish to go out
			stateTime = millis();
			state = SLEEP_WAIT_STATE;
		}
		else {
			// Haven't come online yet
			if (millis() - stateTime >= MAX_TIME_TO_PUBLISH_MS) {
				// Took too long to publish, just go to sleep
				state = SLEEP_STATE;
			}
		}
		break;

	case SLEEP_WAIT_STATE:
		if (millis() - stateTime >= TIME_AFTER_PUBLISH_MS) {
			state = SLEEP_STATE;
		}
		break;

	case BOOT_WAIT_STATE:
		if (millis() - stateTime >= TIME_AFTER_BOOT_MS) {
			// To publish the battery stats after boot, set state to PUBLISH_STATE
			// To go to sleep immediately, set state to SLEEP_STATE
			state = PUBLISH_STATE;
		}
		break;

	case SLEEP_STATE:
		// Sleep
		System.sleep(ACCEL_INT_PIN, RISING, TIME_PUBLISH_BATTERY_SEC);

		awake = ((accel.readStatus() & accel.STATUS_AWAKE) != 0);

		state = PUBLISH_STATE;
		stateTime = millis();
		break;
	}

}
