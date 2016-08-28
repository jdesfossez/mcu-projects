/*
 * Copyright (C) 2016 Julien Desfossez <ju@klipix.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <LEDFader.h>
#include <SnoozeLib.h>

#define NR_STEPS 12
#define DIRECTION_UP 0
#define DIRECTION_DOWN 1

int calibration_time = 30; /* Sensor calibration delay (seconds) */
int steps_pin[NR_STEPS]; /* 0 is upstairs */
int loops_between_steps = 1000; /* Number of iteration before lighting the next step */
int delay_after_lights_up = 10; /* How long the lights stay on (seconds). */
int max_brightness = 16; /* 255 == max */
int delay_fade = 1000; /* How long a led strip takes to reach full brightness (and 0) in ms */

int sensor_up_pin = 15;
int sensor_down_pin = 16;
int up_step_pin = 2; /* Pin of the first step (upstairs), expect contiguous and growing up */

LEDFader leds[NR_STEPS];

void setup()
{
	int i, j;

	pinMode(sensor_up_pin, INPUT);
        pinMode(sensor_down_pin, INPUT);

	j = up_step_pin;
	for (i = 0; i < NR_STEPS; i++) {
		steps_pin[i] = j++;
		pinMode(steps_pin[i], OUTPUT);
		leds[i] = LEDFader(steps_pin[i]);
	}
        control_leds(DIRECTION_DOWN, max_brightness);

	Serial.begin(9600);
	Serial.print("Calibrating sensor");
	for (i = 0; i < calibration_time; i++) {
		Serial.print(".");
		delay(1000);
	}
        fade_down(DIRECTION_UP);
	Serial.println(" done");

}

int get_next_led(int current_led, int direction)
{
	if (direction == DIRECTION_UP) {
		if (current_led > 0)
			return --current_led;
		else
			return -1;
	} else {
		if (current_led < (NR_STEPS - 1))
			return ++current_led;
		else
			return -1;
	}
}

void control_leds(int direction, int brightness)
{
	int i, nr_loop, ready, current_led;

	nr_loop = 0;

	/* going upstairs, start sequence from downstairs */
	if (direction == DIRECTION_UP)
		current_led = NR_STEPS - 1;
	else
		current_led = 0;

	/* Start the sequence */
	while (current_led != -1) {
		if ((nr_loop++ % loops_between_steps) == 0) {
			leds[current_led].fade(brightness, delay_fade);
			Serial.print("Led ");
			Serial.print(current_led);
			Serial.print(" fading towards ");
			Serial.println(brightness);
			current_led = get_next_led(current_led, direction);
		}
		for (i = 0; i < NR_STEPS; i++)
			leds[i].update();
	}

	ready = 0;
	/* Wait for all the LED to have reached the desired brightness. */
	while (!ready) {
		ready = 1;
		for (i = 0; i < NR_STEPS; i++) {
			if (leds[i].get_value() != brightness) {
				ready = 0;
				leds[i].update();
			}
		}
	}
	Serial.println("Done");
}

void fade_down(int direction)
{
  int i;
  
restart:
  /*
   * Keep the lights on until the delay is expired,
   * but check every 500ms to only start fading out when
   * no movement is detected.
   */
  for (i = 0; i < delay_after_lights_up * 2; i++) {
    delay(500);
    if ((digitalRead(sensor_up_pin) == HIGH) ||
              (digitalRead(sensor_down_pin) == HIGH))
      goto restart;
  }
  control_leds(direction, 0);
}

void loop()
{
	if (digitalRead(sensor_up_pin) == HIGH) {
		control_leds(DIRECTION_DOWN, max_brightness);
                fade_down(DIRECTION_UP);
	} else if (digitalRead(sensor_down_pin) == HIGH) {
		control_leds(DIRECTION_UP, max_brightness);
                fade_down(DIRECTION_DOWN);
	}
        snoozeLib.snooze(100);
}
