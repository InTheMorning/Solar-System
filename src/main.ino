#include <Arduino.h>

const long BITS_PER_SECOND = 9600;

const unsigned char stage_2_relay = 2;
const unsigned char stage_1_relay_pin = 4;
const unsigned char fan_relay_pin = 5;
const unsigned char red_light_pin= 9;
const unsigned char green_light_pin = 10;
const unsigned char blue_light_pin = 11;

const unsigned long serial_timeout = 1000;
const unsigned long thermostat_timeout = 600000;
const unsigned long warmup_timeout = 300000; // heater warmup before full heat
const unsigned long cooldown_timeout = 300000; // fan cooldown after heating
int led_brightness = 128; // max is 255
const int debounce = 500; //time between state changes
const unsigned char led_max_brightness = 128; // max is 255

// actual state of the heater relays
int current_relay_state = -1;

// operating state of the controller
int current_mode = 0;
int target_mode = -1; // used when in a temporary mode

char *status_strings[] = 
	{
	"Off",
	"Fan",
	"Low heat",
	"High heat",
	"Warming up",
	"Cooling down"
	};

unsigned long message_timestamp = 0;
unsigned long state_change_timestamp = 0;
unsigned long start_warmup_timestamp = 0;
unsigned long start_cooldown_timestamp = 0;
\
void led_control(int r, int g, int b)
{
	static unsigned char Red, Green, Blue; // current color
	static unsigned char Brightness; // current brightness
	static unsigned int Phase; // phase of waveform in degrees
	static unsigned long Timestamp;
	
	float rv, gv, bv;
	float brightness_multiplier;
	
	// if any values are negative, just step the brightness
	if (r < 0 || g < 0 || b < 0) 
	{
		if (millis() - Timestamp >= 5)
		{
			// wrap around phase
			if (Phase >= 360)
			{
				Phase = 0;
			}

			// obtain multiplier from wave function
			float brightness_multiplier = ((1 + cos(Phase * 3.141592 / 180.0)) / 2.0);
			// brightness_multiplier = Phase;
			
			// modify the brightness
			Brightness = (unsigned long) (brightness_multiplier * led_max_brightness) + 0.5;
			
			// compute the new r,g,b values
			rv = (Red / 255.0 * Brightness) + 0.5;
			gv = (Green / 255.0 * Brightness) + 0.5;
			bv = (Blue / 255.0 * Brightness) + 0.5;
			
			// increment the phase
			Phase += 1;
			Timestamp = millis();
			
			led_write((unsigned char) rv, (unsigned char) gv, (unsigned char) bv);
		}
	}
	
	else
	{
		// reset the phase and brightness
		Phase = 0;
		Brightness = led_max_brightness;
		
		// save provided color values
		Red = (unsigned char) r;
		Green = (unsigned char) g;
		Blue = (unsigned char) b;
		led_write(Red, Green, Blue);
	}
}

void led_write(unsigned char r, unsigned char g, unsigned char b)
{	// write to led
	analogWrite(red_light_pin, r);
	analogWrite(green_light_pin, g);
	analogWrite(blue_light_pin, b);
}

int int_from_serial()
// returns an int from serial
{
		char integerValue = 0;      // throw away previous integerValue
        unsigned long serialTime = millis();
		
		// force into a loop until 'n' is received
		while(1)
		{
			char incomingByte = Serial.read();
			if (incomingByte == '\n') break;   // exit the while(1), we're done receiving
			if (incomingByte == -1) continue;  // if no characters are in the buffer read() returns -1
			integerValue *= 10;  // shift left 1 decimal place
			// convert ASCII to integer, add, and shift left 1 decimal place
			integerValue = ((incomingByte - 48) + integerValue);
		}
        return integerValue;
}

void set_hvac_state(int n)
// relay and led definitions for hvac states
{
	if (n == current_relay_state)
	// do nothing
	{
		return;
	}
	
    else if (n == 3)
	// state 3: full heat
	{
		digitalWrite(stage_2_relay, LOW);
		digitalWrite(stage_1_relay_pin, LOW);
		digitalWrite(fan_relay_pin, HIGH);
		led_control(255,0,0);
        current_relay_state = 3;
	}

	else if (n == 2)
	// state 2: low heat
	{
		digitalWrite(stage_2_relay, HIGH);
		digitalWrite(stage_1_relay_pin, LOW);
		digitalWrite(fan_relay_pin, HIGH);
		led_control(255,80,0);
		current_relay_state = 2;
	}

	else if (n == 1)
	// state 1: fan only
	{
		digitalWrite(stage_2_relay, HIGH);
		digitalWrite(stage_1_relay_pin, HIGH);
		digitalWrite(fan_relay_pin, LOW);
		led_control(0,255,0);
		current_relay_state = 1;
	}

	else if (n == 0)
	// state 0: off
	{
		digitalWrite(stage_2_relay, HIGH);
		digitalWrite(stage_1_relay_pin, HIGH);
		digitalWrite(fan_relay_pin, HIGH);
		led_control(8,8,12);
		current_relay_state = 0;
	}
    else
    {
        return;
    }
	state_change_timestamp = millis();
	delay(debounce);
}

void command_hvac(int n)
// handle incoming request
{
    if (current_mode == 0 || current_mode == 1)
	{
		if (n == 0 || n == 1)
		{
			// allow toggle if different
			if (current_mode != n)
			{
				set_hvac_state(n);
				current_mode = n;
			}
		}
		else if (n == 2 || n == 3)
		{
			// require warmup
			start_warmup_timestamp = millis();
			set_hvac_state(2);
			current_mode = 4;
			target_mode = n;
		}
	}
	else if (current_mode == 2 || current_mode == 3)
	{
		if (n == 0 || n == 1)
		{
			// require cooldown
			start_cooldown_timestamp = millis();
			set_hvac_state(1);
			current_mode = 5;
			target_mode = n;
		}
		else if (n == 2 || n == 3)
		{
			// allow toggle if different
			if (current_mode != n)
			{
				set_hvac_state(n);
				current_mode = n;
			}
		}
	}
	else if (current_mode == 4)
	{
		if (n == 2)
		{
			// cancel warmup
			set_hvac_state(n);
			current_mode = n;
		}
	}
	
	else if (current_mode == 5)
	{
		if (n == 2)
		{
			// cancel cooldown
			set_hvac_state(n);
			current_mode = n;
		}
	}
}

void emergency_mode_loop()
{
	led_control(-1, -1, -1);
	command_hvac(0);
}

void warmup_mode_loop(int t)
{
	led_control(-1, -1, -1);
	// check if we should exit this mode
	if (millis() - start_warmup_timestamp > warmup_timeout)
	{
		set_hvac_state(t);
		current_mode = t;
	}
}

void cooldown_mode_loop(int t)
{
	led_control(-1, -1, -1);
	// check if we should exit this mode
	if (millis() - start_cooldown_timestamp > cooldown_timeout)
	{
		set_hvac_state(t);
		current_mode = t;
	}
}

void monitor_serial()
// monitor the serial connection, and either respond or initiate a hvac state request
{
    if (Serial.available() > 0)
    {
	    int serialRequest = int_from_serial();
        
        if (serialRequest == 10)
		{
        	// special code to retrieve current mode via serial
			delay(50);
			Serial.println(status_strings[current_mode]);
		}
		else if (serialRequest == 11)
		{
        	// special code to retrieve heater state via serial
			delay(50);
			Serial.println(current_relay_state);
		}
        
        else if (serialRequest < 0 || serialRequest > 3)
        // if we timed out(-1), or the integer is invalid
        {
            return; // don't do anything
        }

        else
		{
			command_hvac(serialRequest);
		}
        
        message_timestamp = millis();
    }
}

void setup()
{
    // builtin led
	pinMode(13, OUTPUT);
    
    // relays
	pinMode(stage_2_relay, OUTPUT);
	pinMode(stage_1_relay_pin, OUTPUT);
	pinMode(fan_relay_pin, OUTPUT);
    
    // color led
	pinMode(red_light_pin, OUTPUT);
	pinMode(green_light_pin, OUTPUT);
	pinMode(blue_light_pin, OUTPUT);

	// turn everything off
    digitalWrite(13, LOW);
    set_hvac_state(0);
	  
	//start serial connection
    Serial.begin(BITS_PER_SECOND);
    
    message_timestamp = millis();
}

void loop()
{
	monitor_serial();
	
	// check if we are abandoned
	if (millis() - message_timestamp > thermostat_timeout)
	{
		emergency_mode_loop();
	}
	
	// also check if we are in transition
	if (current_mode == 4)
	// warming up
	{
		warmup_mode_loop(target_mode);
	}
	else if (current_mode == 5)
	// cooling down
	{
		cooldown_mode_loop(target_mode);
	}
}
