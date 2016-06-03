#include <Wire.h>
#include <math.h>

// Metro mini pinout
#define I2C_SLAVE_ADDR 8
#define REDPIN 10
#define GREENPIN 11
#define BLUEPIN 9
#define WHITEPIN 3
#define FADESPEED 800     // (microseconds) make this higher to slow down

volatile int a = 0; // Brightness sent from the other controller


void receiveValue(int howMany)
{
  // We're expecting two bytes (a full Arduino integer)
  if( howMany == 2 )
  {
    int result;

    result = Wire.read();
    result <<= 8;
    result |= Wire.read();

    a = result;
  }

  //Serial.println(a);

  // Throw away any garbage
  while( Wire.available() > 0 ) 
  {
    Wire.read ();
  }
}

void setup() 
{
  Serial.begin(9600);
  
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(receiveValue);
  
  pinMode(WHITEPIN, OUTPUT);
  pinMode(REDPIN, OUTPUT);
  pinMode(GREENPIN, OUTPUT);
  pinMode(BLUEPIN, OUTPUT);
  
  analogWrite(WHITEPIN, 0);
  analogWrite(REDPIN, 0);
  analogWrite(GREENPIN, 0);
  analogWrite(BLUEPIN, 0);
}
 
void loop() 
{
  int r, g, b, w, lowest;

  // hot pink     254 0   8
  // magenta      254 0   16
  // yellow       254 87  0
  // warm yellow  254 70  0
  // orange       254 20  0
  // purple       254 0   128
  // cyan         0   254 254
  // blue-white   254 80  100
  // soft white   254 80  7
  r = 254; // 60 120
  g = 80; // 25 40 
  b = 50; // 2 15
  w = 254;
  lowest = 0;
 
  analogWrite(WHITEPIN, scale(w, a, lowest));
  analogWrite(REDPIN,   scale(r, a, lowest));
  analogWrite(GREENPIN, scale(g, a, lowest));
  analogWrite(BLUEPIN,  scale(b, a, lowest));

  delayMicroseconds(FADESPEED);
}


/** 
 *  Manage color value when scaled with relative values
 *  
 *  @param color INT 0-254 relative weight for the color against other colors
 *  @param brightness 0-1023 global brightness
 *  @param lowest INT 0-1022 lowest value the input brightness will have, NOT an output floor!
 */
int scale( int color, int brightness, int lowest )
{ 
  int base = 255;

  // Map the brightness (0-1023) into the PWM domain space (0-255)
  // Brightness is floor'd at the lowest 
  int m_brightness = map( brightness, lowest, 1023, 0, 255 );
  
  float scale = (float)color / (float)base;
  float f_brightness = m_brightness * scale;

  return lrint(f_brightness);
}


