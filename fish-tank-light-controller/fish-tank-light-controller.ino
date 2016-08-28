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

#include <TinyWireM.h>      
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <LEDFader.h>

// ATMEL ATTINY84 / ARDUINO
//
//                           +-\/-+
//                     VCC  1|    |14  GND
//             (D  0)  PB0  2|    |13  PA0  (D 10)        AREF
//             (D  1)  PB1  3|    |12  PA1  (D  9) 
//             (D 11)  PB3  4|    |11  PA2  (D  8) 
//  PWM  INT0  (D  2)  PB2  5|    |10  PA3  (D  7) 
//  PWM        (D  3)  PA7  6|    |9   PA4  (D  6) 
//  PWM        (D  4)  PA6  7|    |8   PA5  (D  5)        PWM
//                           +----+

#define DS3231_I2C_ADDRESS 0x68
#define DIMMER_MAX 255

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

int select_pin = 9;
int plus_pin = 8;
int minus_pin = 7;
// 4 = SCL
int relay1_pin = 0;
int relay2_pin = 1;
int dimmer_pin = 3;

byte dimming_up = 0;
byte dimming_down = 0;

LEDFader dimmer;

// 6 = SDA
byte backlight_timeout;

byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;

enum menu_page {
	MAIN_PAGE 	= 0,
	SET_DATE 	= 1,
	SET_ON1 	= 2,
	SET_OFF1 	= 3,
	SET_ON2 	= 4,
	SET_OFF2 	= 5,
	SET_DIM_DURATION = 6,
	SET_DIM_CUTOFF 	= 7,
};

#define VALID_CONFIG_MARKER 0x42
#define EEPROM_CONFIG_ADDR 0

struct eeprom_config {
	/* To check if the eeprom is initialized with valid data. */
	byte valid_config;
	/* Time to power up socket 1. */
	byte on1_min;
	byte on1_hour;
	/* Time to power up socket 2. */
	byte on2_min;
	byte on2_hour;
	/* Time to power off socket 1. */
	byte off1_min;
	byte off1_hour;
	/* Time to power off socket 2. */
	byte off2_min;
	byte off2_hour;
	/* Threshold to cut off completely socket 1. */
	byte dim_cut_off;
	/* How long the dimming phase lasts (both for on and off). */
	byte dim_duration;
};

byte state1 = state2 = 0;
byte current_menu = MAIN_PAGE;

struct eeprom_config config;
byte backlight_on;

// Convert normal decimal numbers to binary coded decimal
byte decToBcd(byte val)
{
	return ((val/10*16) + (val%10));
}

// Convert binary coded decimal to normal decimal numbers
byte bcdToDec(byte val)
{
	return ((val/16*10) + (val%16));
}

void setDS3231time(byte second, byte minute, byte hour, byte dayOfWeek,
		byte dayOfMonth, byte month, byte year)
{
	// sets time and date data to DS3231
	TinyWireM.beginTransmission(DS3231_I2C_ADDRESS);
	TinyWireM.write(0); // set next input to start at the seconds register
	TinyWireM.write(decToBcd(second)); // set seconds
	TinyWireM.write(decToBcd(minute)); // set minutes
	TinyWireM.write(decToBcd(hour)); // set hours
	TinyWireM.write(decToBcd(dayOfWeek)); // set day of week (1=Sunday, 7=Saturday)
	TinyWireM.write(decToBcd(dayOfMonth)); // set date (1 to 31)
	TinyWireM.write(decToBcd(month)); // set month
	TinyWireM.write(decToBcd(year)); // set year (0 to 99)
	TinyWireM.endTransmission();
}

void readDS3231time(byte *second, byte *minute, byte *hour, byte *dayOfWeek,
		byte *dayOfMonth, byte *month, byte *year)
{
	TinyWireM.beginTransmission(DS3231_I2C_ADDRESS);
	TinyWireM.write(0); // set DS3231 register pointer to 00h
	TinyWireM.endTransmission();
	TinyWireM.requestFrom(DS3231_I2C_ADDRESS, 7);
	// request seven bytes of data from DS3231 starting from register 00h
	*second = bcdToDec(TinyWireM.read() & 0x7f);
	*minute = bcdToDec(TinyWireM.read());
	*hour = bcdToDec(TinyWireM.read() & 0x3f);
	*dayOfWeek = bcdToDec(TinyWireM.read());
	*dayOfMonth = bcdToDec(TinyWireM.read());
	*month = bcdToDec(TinyWireM.read());
	*year = bcdToDec(TinyWireM.read());
}

void print_time(byte print_second, byte _hour, byte _min, byte _sec)
{
	if (_hour < 10) {
		lcd.print("0");
	}
	lcd.print(_hour);
	lcd.print(":");
	if (_min < 10) {
		lcd.print("0");
	}
	lcd.print(_min);
	if (print_second) {
		lcd.print(":");
		if (_sec < 10) {
			lcd.print("0");
		}
		lcd.print(_sec);
	}
}

void displayTime(int print_second)
{
	readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth,
			&month, &year);
	print_time(print_second, hour, minute, second);
}

void switch_off_relay(int relay_pin, byte *_state)
{
	digitalWrite(relay_pin, HIGH);
	*_state = 0;
}

void switch_on_relay(int relay_pin, byte *_state)
{
	digitalWrite(relay_pin, LOW);
	*_state = 1;
}

/*
void init_config()
{
	config.valid_config = VALID_CONFIG_MARKER;
	config.on1_min = 0;
	config.on1_hour = 7;
	config.on2_min = 30;
	config.on2_hour = 7;
	config.off1_min = 30;
	config.off1_hour = 22;
	config.off2_min = 0;
	config.off2_hour = 22;
	config.dim_cut_off = 25;
	config.dim_duration = 30;
	EEPROM.put(EEPROM_CONFIG_ADDR, config);
}
*/

void setup()
{
	TinyWireM.begin();

	EEPROM.get(EEPROM_CONFIG_ADDR, config);
 /*
	if (config.valid_config != VALID_CONFIG_MARKER)
		init_config();
    */

	/* Always bootup with lights off */
	/* 0: off, 1: on, 2: dim */
	state1 = state2 = 0;

	pinMode(select_pin, INPUT);
	digitalWrite(select_pin, HIGH);
	pinMode(plus_pin, INPUT);
	digitalWrite(plus_pin, HIGH);
	pinMode(minus_pin, INPUT);
	digitalWrite(minus_pin, HIGH);

	pinMode(relay1_pin, OUTPUT);
	switch_off_relay(relay1_pin, &state1);
	pinMode(relay2_pin, OUTPUT);
	switch_off_relay(relay2_pin, &state2);

	lcd.begin(16,2);

	lcd.backlight();
	backlight_on = 1;

	pinMode(dimmer_pin, OUTPUT);
	digitalWrite(dimmer_pin, LOW);
	dimmer = LEDFader(dimmer_pin);

/*
	lcd.setCursor(0,0);
	lcd.print("Bonjour   les");
	lcd.setCursor(0,1);
	lcd.print("  poissons");

	delay(1000);
	*/

	lcd.clear();
	backlight_timeout = 200;
}

void menu_set_date(void)
{
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Regler l'heure:");
	lcd.setCursor(0, 1);
	displayTime(0);
}

/*
 * Type : 1 = on, 0 = off
 */
void menu_set_onoff(byte index, byte type, byte _hour, byte _min)
{
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Heure ");
	if (type == 1)
		lcd.print("allum ");
	else
		lcd.print("eteint ");
	lcd.print(index);
	lcd.print(":");
	lcd.setCursor(0, 1);
	print_time(0, _hour, _min, 0);
}

void menu_set_dim_duration(void)
{
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Duree dim:");
	lcd.setCursor(0, 1);
	lcd.print(config.dim_duration);
}

void menu_set_dim_cutoff(void)
{
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print("Dim min val:");
	lcd.setCursor(0, 1);
	lcd.print(config.dim_cut_off);
}

void handle_select_button(void)
{
	current_menu++;

	switch(current_menu) {
	case SET_DATE:
		menu_set_date();
		break;
	case SET_ON1:
		menu_set_onoff(1, 1, config.on1_hour, config.on1_min);
		break;
	case SET_ON2:
		menu_set_onoff(2, 1, config.on2_hour, config.on2_min);
		break;
	case SET_OFF1:
		menu_set_onoff(1, 0, config.off1_hour, config.off1_min);
		break;
	case SET_OFF2:
		menu_set_onoff(2, 0, config.off2_hour, config.off2_min);
		break;
	case SET_DIM_DURATION:
		menu_set_dim_duration();
		break;
	case SET_DIM_CUTOFF:
		menu_set_dim_cutoff();
		break;
	/* Reset to displaying the current time. */
	default:
		current_menu = MAIN_PAGE;
		lcd.clear();
		break;
	}
}

void plus_date(byte *_hour, byte *_min, byte *_sec)
{
	if (*_min == 59) {
		*_min = 0;
		if (*_hour == 23) {
			*_hour = 0;
		} else {
			(*_hour)++;
		}
	} else {
		(*_min)++;
	}

	if (_sec)
		*_sec = 0;
	lcd.setCursor(0, 1);
}

void handle_plus_button(void)
{
	switch(current_menu) {
	case SET_DATE:
		plus_date(&hour, &minute, &second);
		setDS3231time(second, minute, hour, dayOfWeek,
				dayOfMonth, month, year);
		displayTime(0); 
		break;
	case SET_ON1:
		plus_date(&config.on1_hour, &config.on1_min, NULL);
		EEPROM.put(EEPROM_CONFIG_ADDR, config);
		print_time(0, config.on1_hour, config.on1_min, 0);
		break;
	case SET_ON2:
		plus_date(&config.on2_hour, &config.on2_min, NULL);
		EEPROM.put(EEPROM_CONFIG_ADDR, config);
		print_time(0, config.on2_hour, config.on2_min, 0);
		break;
	case SET_OFF1:
		plus_date(&config.off1_hour, &config.off1_min, NULL);
		EEPROM.put(EEPROM_CONFIG_ADDR, config);
		print_time(0, config.off1_hour, config.off1_min, 0);
		break;
	case SET_OFF2:
		plus_date(&config.off2_hour, &config.off2_min, NULL);
		EEPROM.put(EEPROM_CONFIG_ADDR, config);
		print_time(0, config.off2_hour, config.off2_min, 0);
		break;
	case SET_DIM_DURATION:
		if (config.dim_duration < 60) {
			config.dim_duration++;
			EEPROM.put(EEPROM_CONFIG_ADDR, config);
			menu_set_dim_duration();
		}
		break;
	case SET_DIM_CUTOFF:
		if (config.dim_cut_off < 255) {
			config.dim_cut_off++;
			EEPROM.put(EEPROM_CONFIG_ADDR, config);
			menu_set_dim_cutoff();
		}
		break;
	default:
		break;
	}
}

void minus_date(byte *_hour, byte *_min, byte *_sec)
{
	if (*_min == 0) {
		*_min = 59;
		if (*_hour == 0) {
			*_hour = 23;
		} else {
			(*_hour)--;
		}
	} else {
		(*_min)--;
	}

	if (_sec)
		*_sec = 0;
	lcd.setCursor(0, 1);
}

void handle_minus_button(void)
{
	switch(current_menu) {
	case SET_DATE:
		minus_date(&hour, &minute, &second);
		setDS3231time(second, minute, hour, dayOfWeek,
				dayOfMonth, month, year);
		displayTime(0); 
		break;
	case SET_ON1:
		minus_date(&config.on1_hour, &config.on1_min, NULL);
		EEPROM.put(EEPROM_CONFIG_ADDR, config);
		print_time(0, config.on1_hour, config.on1_min, 0);
		break;
	case SET_ON2:
		minus_date(&config.on2_hour, &config.on2_min, NULL);
		EEPROM.put(EEPROM_CONFIG_ADDR, config);
		print_time(0, config.on2_hour, config.on2_min, 0);
		break;
	case SET_OFF1:
		minus_date(&config.off1_hour, &config.off1_min, NULL);
		EEPROM.put(EEPROM_CONFIG_ADDR, config);
		print_time(0, config.off1_hour, config.off1_min, 0);
		break;
	case SET_OFF2:
		minus_date(&config.off2_hour, &config.off2_min, NULL);
		EEPROM.put(EEPROM_CONFIG_ADDR, config);
		print_time(0, config.off2_hour, config.off2_min, 0);
		break;
	case SET_DIM_DURATION:
		if (config.dim_duration > 0) {
			config.dim_duration--;
			EEPROM.put(EEPROM_CONFIG_ADDR, config);
			menu_set_dim_duration();
		}
		break;
	case SET_DIM_CUTOFF:
		if (config.dim_cut_off > 0) {
			config.dim_cut_off--;
			EEPROM.put(EEPROM_CONFIG_ADDR, config);
			menu_set_dim_cutoff();
		}
		break;
	default:
		break;
	}
}

byte should_switch(byte _hour, byte _min, byte _current_state,
		byte _expected_state)
{
	if (hour == _hour && minute == _min) {
		if (_current_state != _expected_state)
			return 1;
	}
	return 0;
}

void check_onoff()
{
	if (dimming_up) {
		if (dimmer.get_value() == DIMMER_MAX) {
			switch_on_relay(relay1_pin, &state1);
			delay(10);
			dimmer.fade(0, 0);
			dimming_up = 0;
		}
		dimmer.update();
	} else if (dimming_down) {
		if (dimmer.get_value() == config.dim_cut_off) {
			dimmer.fade(0, 0);
			dimming_down = 0;
		}
		dimmer.update();
	}

	if (should_switch(config.on1_hour, config.on1_min, state1, 2)) {
		dimmer.fade(config.dim_cut_off, 0);
		dimmer.update();
		dimmer.fade(DIMMER_MAX, config.dim_duration * 60 * 1000);
		dimmer.update();
		dimming_up = 1;
		state1 = 2;
		lcd.clear();
	}
	if (should_switch(config.off1_hour, config.off1_min, state1, 0)) {
		dimmer.fade(DIMMER_MAX, 0);
		dimmer.update();
		dimmer.fade(config.dim_cut_off, config.dim_duration * 60 * 1000);
		dimmer.update();
		dimming_down = 1;
		//state1 = 2;
		switch_off_relay(relay1_pin, &state1);
		lcd.clear();
	}
	if (should_switch(config.on2_hour, config.on2_min, state2, 1)) {
		switch_on_relay(relay2_pin, &state2);
		lcd.clear();
	}
	if (should_switch(config.off2_hour, config.off2_min, state2, 0)) {
		switch_off_relay(relay2_pin, &state2);
		lcd.clear();
	}
}

int switch_on_backlight(void)
{
	backlight_timeout = 200;

	if (backlight_on == 0) {
		lcd.backlight();
		backlight_on = 1;
		return 1;
	}
	return 0;
}

void main_page(void)
{
	lcd.setCursor(0,0);
	displayTime(1);
	lcd.setCursor(0, 1);
	lcd.print("1: ");
	if (state1 == 0)
		lcd.print("OFF");
	else if (state1 == 1)
		lcd.print("ON");
	else if (state1 == 2)
		lcd.print(dimmer.get_value());
	lcd.print(", 2: ");
	if (state2 == 1)
		lcd.print("ON");
	else
		lcd.print("OFF");
}

void loop()
{
restart:
	delay(150);
	switch(current_menu) {
	case MAIN_PAGE:
		main_page();
		break;
	default:
		break;
	}

	if (backlight_timeout == 1) {
		lcd.noBacklight();
		backlight_on = 0;
		current_menu = MAIN_PAGE;
	}
	if (backlight_timeout > 0)
		backlight_timeout--;

	check_onoff();

	if (digitalRead(select_pin) == LOW) {
		if (switch_on_backlight())
			goto restart;
		handle_select_button();
	}
	if (digitalRead(plus_pin) == LOW) {
		if (switch_on_backlight())
			goto restart;
		if (current_menu == MAIN_PAGE) {
			if (state2 == 0)
				switch_on_relay(relay2_pin, &state2);
			else
				switch_off_relay(relay2_pin, &state2);
			lcd.clear();
			main_page();
		} else {
			handle_plus_button();

		}
	}
	if (digitalRead(minus_pin) == LOW) {
		if (switch_on_backlight())
			goto restart;
		if (current_menu == MAIN_PAGE) {
			if (state1 == 0)
				switch_on_relay(relay1_pin, &state1);
			else {
				dimmer.fade(0, 0);
				dimmer.update();
				switch_off_relay(relay1_pin, &state1);
			}
			lcd.clear();
			main_page();
		} else {
			handle_minus_button();
		}
	}
}
