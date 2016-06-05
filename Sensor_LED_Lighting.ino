#include <math.h>
#include <SharpIR.h>
#include <EEPROM.h>
#include <Wire.h>


#define SENSORPIN A0      // Distance-proximity sensor reading pin

#define AMBBUTTONPIN 2    // Button to disable/enable ambient brightness adjustment (interrupt)
#define AMBIENTPIN A1     // Ambient sensor reading
#define AMBIENTINDPIN 6   // Ambient button indicator light output pin
#define AMBIENTADDRESS 3  // EEPROM memory location to store ambient state

#define OFFBUTTONPIN 3    // Button to put the lamp to 'sleep'
#define OFFINDPIN 5       // Sleep button indicator light output pin
#define SLEEPADDRESS 2      // EEPROM memory location to store sleep state

#define VERSIONADDRESS 1  // EEPROM bootstrap memory location
#define VERSION 0x0001    // EEPROM bootstrap value - validate whether other addresses are valid

#define I2C_SLAVE_ADDR 8  // I2C bus address of light control processor


// Preferences
#define FADESPEED 800     // (microseconds) make this higher to slow down
#define ONLENGTH  30000   // (milliseconds) Time to stay on after user leaves proximity


// Variables that can be modified by interrupts
volatile boolean switchLockout = false;    // Value to manage whether to ignore input from a switch
volatile boolean ambientActive = true;     // Whether to use the ambient sensor
volatile boolean isSleeping    = false;    // Whether the device is asleep


/**
 * Activate the distance-proximity sensor
 */
SharpIR SharpIR(SENSORPIN, 20150);


/**
 * Flip whether to use the ambient sensor
 * Called via an interrupt
 */
void switchAmbient()
{
  if(switchLockout)
  {
    return;
  }

  switchLockout = true;
  ambientActive = !ambientActive;

  // Persist the setting
  EEPROM.write(AMBIENTADDRESS, ambientActive);
}


/**
 * Flip sleep mode
 * Called via an interrupt
 */
void switchOffButton()
{
  if(switchLockout)
  {
    return;
  }

  switchLockout = true;
  isSleeping = !isSleeping;

  // Persist the setting
  EEPROM.write(SLEEPADDRESS, isSleeping);
}


void setup() 
{
  // Initialize EEPROM if necessary
  if( EEPROM.read(VERSIONADDRESS) != VERSION )
  {
    EEPROM.write(SLEEPADDRESS, isSleeping);
    EEPROM.write(AMBIENTADDRESS, ambientActive);
    EEPROM.write(VERSIONADDRESS, VERSION);
  }

  // Configure the sleep on/off button and indicator light
  pinMode(OFFBUTTONPIN, INPUT_PULLUP);
  pinMode(OFFINDPIN, OUTPUT);
  analogWrite(OFFINDPIN, 127);
  attachInterrupt(digitalPinToInterrupt(OFFBUTTONPIN), switchOffButton, FALLING);
  isSleeping = (boolean)EEPROM.read(SLEEPADDRESS);

  // Configure the ambient light sensor, on/off button, and indicator light
  pinMode(AMBIENTPIN, INPUT_PULLUP);
  pinMode(AMBIENTINDPIN, OUTPUT);
  pinMode(AMBBUTTONPIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(AMBBUTTONPIN), switchAmbient, FALLING);
  ambientActive = (boolean)EEPROM.read(AMBIENTADDRESS);

  // Activate the I2C bus and Serial console output
  Wire.begin();
  Serial.begin(9600);
}


void loop() 
{
  static boolean       loopOne                  = true;
  static boolean       on                       = true;
  static int           brightness               = 0; // Final, calculated brightness maintained btwn cycles
  static int           offButtonBrightness      = 127; // Start with the button on full
  static int           ambientButtonBrightness  = 127; // Start with the button on full
  int                  maxBrightness            = 0;  // Adjusts based on ambient sensor settings
  int                  minBrightness            = 20; // Minimum calculated brightness on 0-1022 scale
  int                  lowest                   = 0; // Minimum for final getBrightness function
  

  // Reset the switch timer
  static unsigned long switchLockoutTimer = 0;
  if( switchLockout )
  {
    if( switchLockoutTimer > 0 )
    {
      if( millis() - switchLockoutTimer > 500 )
      {
        switchLockout = false;
        switchLockoutTimer = 0;
      }
    }

    else
    {
      switchLockoutTimer = millis();
    }
  }
  

  // Ambient light reading
  maxBrightness = getAmbientBrightness(minBrightness, loopOne);

  // Manage the ambient sensor indicator
  int scaledAmbientBrightness;
  
  ambientButtonBrightness = getBrightness(ambientActive, ambientButtonBrightness, minBrightness, maxBrightness);
  scaledAmbientBrightness = scale(64, ambientButtonBrightness, 0);

  // Make sure it turns completely off when the sensor is disabled
  if( !ambientActive )
  {
    scaledAmbientBrightness = scaledAmbientBrightness < 3 ? 0 : scaledAmbientBrightness;
  }
  
  analogWrite(AMBIENTINDPIN, scaledAmbientBrightness);
  

  // Distance calculations
  if( !isSleeping )
  {
    on = checkDistanceSensor(loopOne);
  }


  // Manage off/sleeping button indicator
  if( !isSleeping )
  {
    int offButtonMaxBrightness = maxBrightness < minBrightness ? minBrightness : maxBrightness;
    offButtonBrightness = getBrightness(true, offButtonBrightness, minBrightness, offButtonMaxBrightness);
    analogWrite(OFFINDPIN, scale(64, offButtonBrightness, 0));
  }

  else
  {
    int minMultiplier = 1;
    static int pulseOffButtonBrighter = false;
    static unsigned long pulseStartStamp;
    
    int offButtonMaxBrightness = maxBrightness < minBrightness*minMultiplier ? minBrightness*minMultiplier : maxBrightness;
    offButtonBrightness = getBrightness(pulseOffButtonBrighter, offButtonBrightness, minBrightness, offButtonMaxBrightness);

    // Set the brighter/darker flag once the brightness hits a max or min
    // Make sure pulses are spaced out so they don't get faster when darker
    if( offButtonBrightness <= minBrightness*minMultiplier )
    {
      pulseOffButtonBrighter = true;
      pulseStartStamp = millis();
    }

    else if( offButtonBrightness >= offButtonMaxBrightness )
    {
      if( millis() - pulseStartStamp > 2000 )
      {
        pulseOffButtonBrighter = false;
        pulseStartStamp = 0;
      }
    }
    
    analogWrite(OFFINDPIN, scale(64, offButtonBrightness, 0));
  }


  // If the lamp is sleeping, override other settings so it stays off
  if( isSleeping )
  {
    on = false;
  }


  // Calculate the lamps brightness
  brightness = getBrightness( on, brightness, lowest, maxBrightness );

  // Send calculated lamp brightness to the controller
  // The brightness is a two-byte INT, but only one byte
  // can be sent at a time
  Wire.beginTransmission(I2C_SLAVE_ADDR);
    Wire.write( highByte(brightness) );
    Wire.write( lowByte(brightness) );
  Wire.endTransmission();
  

  loopOne = false;
  delayMicroseconds(FADESPEED);
}



/**
 * Distance sensor decision
 */
boolean checkDistanceSensor( boolean forceReading )
{
  static int distance                 = 20;
  static unsigned long timerStartTime = 0;
  int triggerDistance                 = 100;
  static boolean on;
  
  if( millis() % 100 == 0 || forceReading )
  {
    int minDistance = 20; // Sensor unreliable closer
    int maxDistance = 150; // Sensor unreliable further
    int rawDistance;
    
    rawDistance = (int)SharpIR.distance();
    rawDistance = rawDistance < minDistance ? maxDistance : rawDistance;
    
    distance = constrain(rawDistance, minDistance, maxDistance);
  }

  // Turn on if someone is close
  boolean sensorReading;
  sensorReading = distance < triggerDistance ? true : false;

  if( sensorReading == true )
  {
    timerStartTime = millis();
    on = true;
  }

  else if( sensorReading == false && on == true )
  {
    unsigned long now = millis();

    // If we should keep the light on
    if( now - timerStartTime < ONLENGTH )
    {
      on = true;
    }

    else
    {
      on = false;
    }
  }

  else
  {
    on = sensorReading;
  }

  return on;
}



/** 
 *  Ambient light reading
 */
int getAmbientBrightness( int minBrightness, boolean forceCalculation )
{
  int ambientReading, oldAmbient;
  int stepThreshold = 100; // How much ambient needs to change on 0-1023 scale
  static int ambientValue = 1023;

  if( ambientActive )
  {
    // Don't constantly check the ambient light value unless forced
    // The delay prevents too many adjustments in a short period, which are uncomfortable for a user
    if( millis() % 500 == 0 || forceCalculation )
    {
      // So we can calculate a difference
      oldAmbient     = ambientValue;

      int ambientVal;
      int valueCount = 20;
      int ambValArr[valueCount]; // Store a list of readings that we can average to smooth reading anomalies

      // Take a series of readings from the sensor
      for( int i=0; i<valueCount; i++)
      {
        ambientReading = (int)constrain(analogRead(AMBIENTPIN), 40, 250); // 20 super bright, 250+ darkness

        // Flip and map the readings to the brightness scale
        // Readings are inversely proportional to brightness (lower is brighter) on a 40-250 scale
        // Brightness values are proportional (higher is brighter) on a 0-1023 scale
        ambientVal     = map( (int)ambientReading, 40, 250, 1023, minBrightness ); // Blue (at 7) becomes unstable below 150   

        ambValArr[i] = ambientVal;
      }

      // Sum all the readings together (first step of average calculation)
      int avg_sum = 0;
      for( int i=0; i<valueCount; i++)
      {
        avg_sum += ambValArr[i];
      }

      // Calculate the avg of the test period
      ambientValue = (int)(avg_sum / valueCount);

      // Don't get stuck just over min
      // Min is minBrightness
      if ( ambientValue < stepThreshold - minBrightness )
      {
        ambientValue = minBrightness;
      }
      
      // Don't get stuck just under max
      // Max is 1023 for brightness
      else if( 1023 - ambientValue - stepThreshold < 0 )
      {
        ambientValue = 1023;
      }
      
      // Determine whether to change the effective output, based on stepThreshold
      else
      {
        ambientValue = abs(ambientValue - oldAmbient) > stepThreshold ? ambientValue : oldAmbient;
      }

      //Serial.println(ambientValue);
    }

    return ambientValue;
  }

  // The ambient sensor is disabled. Return maximum brightness.
  else
  {
    return 1023;
  }
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
  // Smallest value of input brightness is 'lowest' 
  int m_brightness = map( brightness, lowest, 1023, 0, 255 );
  
  float scale = (float)color / (float)base;
  float f_brightness = m_brightness * scale;

  return lrint(f_brightness);
}



/**
 * Stateless increment/decrement for brightness values
 * 
 * @param on BOOLEAN Whether to increase or decrease the brightness
 * @param brightness INT Starting value
 * @param lowest INT 
 */
int getBrightness(boolean on, int brightness, int lowest, int highest)
{
  if( on == true )
  {
    if( brightness < highest )
    {
      brightness++;
    }

    // Only happens if the current brightness value is 
    // BRIGHTER than the maximum allowed value. 
    // In that case, we want to try to bring the current
    // value in line between lowest and highest.
    else if( brightness > lowest )
    {
      brightness--;
    }

    // Else do not change the input brightness value
  }

  else
  {
    if( brightness > lowest )
    {
      brightness--;
    }

    // Only happens if the current brightness value is 
    // DIMMER than the minimum allowed value. 
    // In that case, we want to try to bring the current
    // value in line between lowest and highest.
    else if( brightness < highest )
    {
      brightness++;
    }

    // Else do not change the input brightness value
  }

  return brightness;
}

