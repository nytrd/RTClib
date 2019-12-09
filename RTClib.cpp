/**************************************************************************/
/*!
  @file     RTClib.cpp

  @mainpage Adafruit RTClib

  @section intro Introduction

  This is a fork of JeeLab's fantastic real time clock library for Arduino.

  For details on using this library with an RTC module like the DS1307, PCF8523, or DS3231,
  see the guide at: https://learn.adafruit.com/ds1307-real-time-clock-breakout-board-kit/overview

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  @section classes Available classes

  This library provides the following classes:

  - Classes for manipulating dates, times and durations:
    - DateTime represents a specific point in time; this is the data
      type used for setting and reading the supported RTCs
    - TimeSpan represents the length of a time interval
  - Interfacing specific RTC chips:
    - RTC_DS3231


  @section license License

  Original library by JeeLabs http://news.jeelabs.org/code/, released to the public domain.

  This version: MIT (see LICENSE)
*/
/**************************************************************************/

#ifdef __AVR_ATtiny85__
 #include <TinyWireM.h>
 #define Wire TinyWireM
#else
#include <Wire.h>
#endif

#include "RTClib.h"
#ifdef __AVR__
 #include <avr/pgmspace.h>
#elif defined(ESP8266)
 #include <pgmspace.h>
#elif defined(ARDUINO_ARCH_SAMD)
// nothing special needed
#elif defined(ARDUINO_SAM_DUE)
 #define PROGMEM
 #define pgm_read_byte(addr) (*(const unsigned char *)(addr))
 #define Wire Wire1
#endif



#if (ARDUINO >= 100)
 #include <Arduino.h> // capital A so it is error prone on case-sensitive filesystems
 // Macro to deal with the difference in I2C write functions from old and new Arduino versions.
 #define _I2C_WRITE write   ///< Modern I2C write
 #define _I2C_READ  read    ///< Modern I2C read
#else
 #include <WProgram.h>
 #define _I2C_WRITE send    ///< Legacy I2C write
 #define _I2C_READ  receive ///< legacy I2C read
#endif


/**************************************************************************/
/*!
    @brief  Read a byte from an I2C register
    @param addr I2C address
    @param reg Register address
    @return Register value
*/
/**************************************************************************/
static uint8_t read_i2c_register(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire._I2C_WRITE((byte)reg);
  Wire.endTransmission();

  Wire.requestFrom(addr, (byte)1);
  return Wire._I2C_READ();
}

/**************************************************************************/
/*!
    @brief  Write a byte to an I2C register
    @param addr I2C address
    @param reg Register address
    @param val Value to write
*/
/**************************************************************************/
static void write_i2c_register(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire._I2C_WRITE((byte)reg);
  Wire._I2C_WRITE((byte)val);
  Wire.endTransmission();
}


/**************************************************************************/
// utility code, some of this could be exposed in the DateTime API if needed
/**************************************************************************/

/**
  Number of days in each month, from January to November. December is not
  needed. Omitting it avoids an incompatibility with Paul Stoffregen's Time
  library. C.f. https://github.com/adafruit/RTClib/issues/114
*/
const uint8_t daysInMonth [] PROGMEM = { 31,28,31,30,31,30,31,31,30,31,30 };

/**************************************************************************/
/*!
    @brief  Given a date, return number of days since 2000/01/01, valid for 2001..2099
    @param y Year
    @param m Month
    @param d Day
    @return Number of days
*/
/**************************************************************************/
static uint16_t date2days(uint16_t y, uint8_t m, uint8_t d) {
    if (y >= 2000)
        y -= 2000;
    uint16_t days = d;
    for (uint8_t i = 1; i < m; ++i)
        days += pgm_read_byte(daysInMonth + i - 1);
    if (m > 2 && y % 4 == 0)
        ++days;
    return days + 365 * y + (y + 3) / 4 - 1;
}

/**************************************************************************/
/*!
    @brief  Given a number of days, hours, minutes, and seconds, return the total seconds
    @param days Days
    @param h Hours
    @param m Minutes
    @param s Seconds
    @return Number of seconds total
*/
/**************************************************************************/
static long time2long(uint16_t days, uint8_t h, uint8_t m, uint8_t s) {
    return ((days * 24L + h) * 60 + m) * 60 + s;
}



/**************************************************************************/
/*!
    @brief  DateTime constructor from unixtime
    @param t Initial time in seconds since Jan 1, 1970 (Unix time)
*/
/**************************************************************************/
DateTime::DateTime (uint32_t t) {
  t -= SECONDS_FROM_1970_TO_2000;    // bring to 2000 timestamp from 1970

  ss = t % 60;
  t /= 60;
  mm = t % 60;
  t /= 60;
  hh = t % 24;
  uint16_t days = t / 24;
  uint8_t leap;
  for (yOff = 0; ; ++yOff) {
    leap = yOff % 4 == 0;
    if (days < 365 + leap)
      break;
    days -= 365 + leap;
  }
  for (m = 1; m < 12; ++m) {
    uint8_t daysPerMonth = pgm_read_byte(daysInMonth + m - 1);
    if (leap && m == 2)
      ++daysPerMonth;
    if (days < daysPerMonth)
      break;
    days -= daysPerMonth;
  }
  d = days + 1;
}

/**************************************************************************/
/*!
    @brief  DateTime constructor from Y-M-D H:M:S
    @param year Year, 2 or 4 digits (year 2000 or higher)
    @param month Month 1-12
    @param day Day 1-31
    @param hour 0-23
    @param min 0-59
    @param sec 0-59
*/
/**************************************************************************/
DateTime::DateTime (uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec) {
    if (year >= 2000)
        year -= 2000;
    yOff = year;
    m = month;
    d = day;
    hh = hour;
    mm = min;
    ss = sec;
}

/**************************************************************************/
/*!
    @brief  DateTime copy constructor using a member initializer list
    @param copy DateTime object to copy
*/
/**************************************************************************/
DateTime::DateTime (const DateTime& copy):
  yOff(copy.yOff),
  m(copy.m),
  d(copy.d),
  hh(copy.hh),
  mm(copy.mm),
  ss(copy.ss)
{}

/**************************************************************************/
/*!
    @brief  Convert a string containing two digits to uint8_t, e.g. "09" returns 9
    @param p Pointer to a string containing two digits
*/
/**************************************************************************/
static uint8_t conv2d(const char* p) {
    uint8_t v = 0;
    if ('0' <= *p && *p <= '9')
        v = *p - '0';
    return 10 * v + *++p - '0';
}

/**************************************************************************/
/*!
    @brief  A convenient constructor for using "the compiler's time":
            DateTime now (__DATE__, __TIME__);
            NOTE: using F() would further reduce the RAM footprint, see below.
    @param date Date string, e.g. "Dec 26 2009"
    @param time Time string, e.g. "12:34:56"
*/
/**************************************************************************/
DateTime::DateTime (const char* date, const char* time) {
    // sample input: date = "Dec 26 2009", time = "12:34:56"
    yOff = conv2d(date + 9);
    // Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec
    switch (date[0]) {
        case 'J': m = (date[1] == 'a') ? 1 : ((date[2] == 'n') ? 6 : 7); break;
        case 'F': m = 2; break;
        case 'A': m = date[2] == 'r' ? 4 : 8; break;
        case 'M': m = date[2] == 'r' ? 3 : 5; break;
        case 'S': m = 9; break;
        case 'O': m = 10; break;
        case 'N': m = 11; break;
        case 'D': m = 12; break;
    }
    d = conv2d(date + 4);
    hh = conv2d(time);
    mm = conv2d(time + 3);
    ss = conv2d(time + 6);
}

/**************************************************************************/
/*!
    @brief  A convenient constructor for using "the compiler's time":
            This version will save RAM by using PROGMEM to store it by using the F macro.
            DateTime now (F(__DATE__), F(__TIME__));
    @param date Date string, e.g. "Dec 26 2009"
    @param time Time string, e.g. "12:34:56"
*/
/**************************************************************************/
DateTime::DateTime (const __FlashStringHelper* date, const __FlashStringHelper* time) {
    // sample input: date = "Dec 26 2009", time = "12:34:56"
    char buff[11];
    memcpy_P(buff, date, 11);
    yOff = conv2d(buff + 9);
    // Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec
    switch (buff[0]) {
        case 'J': m = (buff[1] == 'a') ? 1 : ((buff[2] == 'n') ? 6 : 7); break;
        case 'F': m = 2; break;
        case 'A': m = buff[2] == 'r' ? 4 : 8; break;
        case 'M': m = buff[2] == 'r' ? 3 : 5; break;
        case 'S': m = 9; break;
        case 'O': m = 10; break;
        case 'N': m = 11; break;
        case 'D': m = 12; break;
    }
    d = conv2d(buff + 4);
    memcpy_P(buff, time, 8);
    hh = conv2d(buff);
    mm = conv2d(buff + 3);
    ss = conv2d(buff + 6);
}

/**************************************************************************/
/*!
    @brief  Return DateTime in based on user defined format.
    @param buffer: array of char for holding the format description and the formatted DateTime. 
                   Before calling this method, the buffer should be initialized by the user with 
                   a format string, e.g. "YYYY-MM-DD hh:mm:ss". The method will overwrite 
                   the buffer with the formatted date and/or time.
    @return a pointer to the provided buffer. This is returned for convenience, 
            in order to enable idioms such as Serial.println(now.toString(buffer));
*/
/**************************************************************************/

char* DateTime::toString(char* buffer){
		for(int i=0;i<strlen(buffer)-1;i++){
		if(buffer[i] == 'h' && buffer[i+1] == 'h'){
			buffer[i] = '0'+hh/10;
			buffer[i+1] = '0'+hh%10;
		}
		if(buffer[i] == 'm' && buffer[i+1] == 'm'){
			buffer[i] = '0'+mm/10;
			buffer[i+1] = '0'+mm%10;
		}
		if(buffer[i] == 's' && buffer[i+1] == 's'){
			buffer[i] = '0'+ss/10;
			buffer[i+1] = '0'+ss%10;
		}
    if(buffer[i] == 'D' && buffer[i+1] =='D' && buffer[i+2] =='D'){
      static PROGMEM const char day_names[] = "SunMonTueWedThuFriSat";
      const char *p = &day_names[3*dayOfTheWeek()];
      buffer[i] = pgm_read_byte(p);
      buffer[i+1] = pgm_read_byte(p+1);
      buffer[i+2] = pgm_read_byte(p+2);
    }else
		if(buffer[i] == 'D' && buffer[i+1] == 'D'){
			buffer[i] = '0'+d/10;
			buffer[i+1] = '0'+d%10;
		}
    if(buffer[i] == 'M' && buffer[i+1] =='M' && buffer[i+2] =='M'){
      static PROGMEM const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
      const char *p = &month_names[3*(m-1)];
      buffer[i] = pgm_read_byte(p);
      buffer[i+1] = pgm_read_byte(p+1);
      buffer[i+2] = pgm_read_byte(p+2);      
    }else
		if(buffer[i] == 'M' && buffer[i+1] == 'M'){
			buffer[i] = '0'+m/10;
			buffer[i+1] = '0'+m%10;
		}
		if(buffer[i] == 'Y'&& buffer[i+1] == 'Y'&& buffer[i+2] == 'Y'&& buffer[i+3] == 'Y'){
			buffer[i] = '2';
			buffer[i+1] = '0';
			buffer[i+2] = '0'+(yOff/10)%10;
			buffer[i+3] = '0'+yOff%10;
		}else
		if(buffer[i] == 'Y'&& buffer[i+1] == 'Y'){
			buffer[i] = '0'+(yOff/10)%10;
			buffer[i+1] = '0'+yOff%10;
		}

	}
	return buffer;
}

/**************************************************************************/
/*!
    @brief  Return the day of the week for this object, from 0-6.
    @return Day of week 0-6 starting with Sunday, e.g. Sunday = 0, Saturday = 6
*/
/**************************************************************************/
uint8_t DateTime::dayOfTheWeek() const {
    uint16_t day = date2days(yOff, m, d);
    return (day + 6) % 7; // Jan 1, 2000 is a Saturday, i.e. returns 6
}

/**************************************************************************/
/*!
    @brief  Return unix time, seconds since Jan 1, 1970.
    @return Number of seconds since Jan 1, 1970
*/
/**************************************************************************/
uint32_t DateTime::unixtime(void) const {
  uint32_t t;
  uint16_t days = date2days(yOff, m, d);
  t = time2long(days, hh, mm, ss);
  t += SECONDS_FROM_1970_TO_2000;  // seconds from 1970 to 2000

  return t;
}

/**************************************************************************/
/*!
    @brief  Convert the DateTime to seconds
    @return The object as seconds since 2000-01-01
*/
/**************************************************************************/
long DateTime::secondstime(void) const {
  long t;
  uint16_t days = date2days(yOff, m, d);
  t = time2long(days, hh, mm, ss);
  return t;
}

/**************************************************************************/
/*!
    @brief  Add a TimeSpan to the DateTime object
    @param span TimeSpan object
    @return new DateTime object with span added to it
*/
/**************************************************************************/
DateTime DateTime::operator+(const TimeSpan& span) {
  return DateTime(unixtime()+span.totalseconds());
}

/**************************************************************************/
/*!
    @brief  Subtract a TimeSpan from the DateTime object
    @param span TimeSpan object
    @return new DateTime object with span subtracted from it
*/
/**************************************************************************/
DateTime DateTime::operator-(const TimeSpan& span) {
  return DateTime(unixtime()-span.totalseconds());
}

/**************************************************************************/
/*!
    @brief  Subtract one DateTime from another
    @param right The DateTime object to subtract from self (the left object)
    @return TimeSpan of the difference between DateTimes
*/
/**************************************************************************/
TimeSpan DateTime::operator-(const DateTime& right) {
  return TimeSpan(unixtime()-right.unixtime());
}

/**************************************************************************/
/*!
    @brief  Is one DateTime object less than (older) than the other?
    @param right Comparison DateTime object
    @return True if the left object is older than the right object
*/
/**************************************************************************/
bool DateTime::operator<(const DateTime& right) const {
  return unixtime() < right.unixtime();
}

/**************************************************************************/
/*!
    @brief  Is one DateTime object equal to the other?
    @param right Comparison DateTime object
    @return True if both DateTime objects are the same
*/
/**************************************************************************/
bool DateTime::operator==(const DateTime& right) const {
  return unixtime() == right.unixtime();
}

/**************************************************************************/
/*!
    @brief  ISO 8601 Timestamp
    @param opt Format of the timestamp
    @return Timestamp string, e.g. "2000-01-01T12:34:56"
*/
/**************************************************************************/
String DateTime::timestamp(timestampOpt opt){
  char buffer[20];

  //Generate timestamp according to opt
  switch(opt){
    case TIMESTAMP_TIME:
    //Only time
    sprintf(buffer, "%02d:%02d:%02d", hh, mm, ss);
    break;
    case TIMESTAMP_DATE:
    //Only date
    sprintf(buffer, "%d-%02d-%02d", 2000+yOff, m, d);
    break;
    default:
    //Full
    sprintf(buffer, "%d-%02d-%02dT%02d:%02d:%02d", 2000+yOff, m, d, hh, mm, ss);
  }
  return String(buffer);
}



/**************************************************************************/
/*!
    @brief  Create a new TimeSpan object in seconds
    @param seconds Number of seconds
*/
/**************************************************************************/
TimeSpan::TimeSpan (int32_t seconds):
  _seconds(seconds)
{}

/**************************************************************************/
/*!
    @brief  Create a new TimeSpan object using a number of days/hours/minutes/seconds
            e.g. Make a TimeSpan of 3 hours and 45 minutes: new TimeSpan(0, 3, 45, 0);
    @param days Number of days
    @param hours Number of hours
    @param minutes Number of minutes
    @param seconds Number of seconds
*/
/**************************************************************************/
TimeSpan::TimeSpan (int16_t days, int8_t hours, int8_t minutes, int8_t seconds):
  _seconds((int32_t)days*86400L + (int32_t)hours*3600 + (int32_t)minutes*60 + seconds)
{}

/**************************************************************************/
/*!
    @brief  Copy constructor, make a new TimeSpan using an existing one
    @param copy The TimeSpan to copy
*/
/**************************************************************************/
TimeSpan::TimeSpan (const TimeSpan& copy):
  _seconds(copy._seconds)
{}

/**************************************************************************/
/*!
    @brief  Add two TimeSpans
    @param right TimeSpan to add
    @return New TimeSpan object, sum of left and right
*/
/**************************************************************************/
TimeSpan TimeSpan::operator+(const TimeSpan& right) {
  return TimeSpan(_seconds+right._seconds);
}

/**************************************************************************/
/*!
    @brief  Subtract a TimeSpan
    @param right TimeSpan to subtract
    @return New TimeSpan object, right subtracted from left
*/
/**************************************************************************/
TimeSpan TimeSpan::operator-(const TimeSpan& right) {
  return TimeSpan(_seconds-right._seconds);
}



/**************************************************************************/
/*!
    @brief  Convert a binary coded decimal value to binary. RTC stores time/date values as BCD.
    @param val BCD value
    @return Binary value
*/
/**************************************************************************/
static uint8_t bcd2bin (uint8_t val) { return val - 6 * (val >> 4); }

/**************************************************************************/
/*!
    @brief  Convert a binary value to BCD format for the RTC registers
    @param val Binary value
    @return BCD value
*/
/**************************************************************************/
static uint8_t bin2bcd (uint8_t val) { return val + 6 * (val / 10); }



/**************************************************************************/
/*!
    @brief  Start I2C for the DS3231 and test succesful connection
    @return True if Wire can find DS3231 or false otherwise.
*/
/**************************************************************************/
boolean RTC_DS3231::begin(void) {
  Wire.begin();
  Wire.beginTransmission (DS3231_ADDRESS);
  if (Wire.endTransmission() == 0) return true;												
  return false;
}

/**************************************************************************/
/*!
    @brief  Check the status register Oscillator Stop Flag to see if the DS3231 stopped due to power loss
    @return True if the bit is set (oscillator stopped) or false if it is running
*/
/**************************************************************************/
bool RTC_DS3231::lostPower(void) {
  return (read_i2c_register(DS3231_ADDRESS, DS3231_STATUSREG) >> 7);
}

/**************************************************************************/
/*!
    @brief  Set the date and flip the Oscillator Stop Flag
    @param dt DateTime object containing the date/time to set
*/
/**************************************************************************/
void RTC_DS3231::adjust(const DateTime& dt) {
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire._I2C_WRITE((byte)0); // start at location 0
  Wire._I2C_WRITE(bin2bcd(dt.second()));
  Wire._I2C_WRITE(bin2bcd(dt.minute()));
  Wire._I2C_WRITE(bin2bcd(dt.hour()));
  Wire._I2C_WRITE(bin2bcd(0)); //-- skip "day" (1-7)
  Wire._I2C_WRITE(bin2bcd(dt.day())); //-- aka "date" (1-31) in RTC
  Wire._I2C_WRITE(bin2bcd(dt.month()));
  Wire._I2C_WRITE(bin2bcd(dt.year() - 2000));
  Wire.endTransmission();

  uint8_t statreg = read_i2c_register(DS3231_ADDRESS, DS3231_STATUSREG);
  statreg &= ~0x80; // flip OSF bit
  write_i2c_register(DS3231_ADDRESS, DS3231_STATUSREG, statreg);
}

/**************************************************************************/
/*!
    @brief  Get the current date/time
    @return DateTime object with the current date/time
*/
/**************************************************************************/
DateTime RTC_DS3231::now() {
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire._I2C_WRITE((byte)0);
  Wire.endTransmission();

  Wire.requestFrom(DS3231_ADDRESS, 7);
  uint8_t ss = bcd2bin(Wire._I2C_READ() & 0x7F);
  uint8_t mm = bcd2bin(Wire._I2C_READ());
  uint8_t hh = bcd2bin(Wire._I2C_READ());
  Wire._I2C_READ();
  uint8_t d = bcd2bin(Wire._I2C_READ());
  uint8_t m = bcd2bin(Wire._I2C_READ());
  uint16_t y = bcd2bin(Wire._I2C_READ()) + 2000;

  return DateTime (y, m, d, hh, mm, ss);
}

/**************************************************************************/
/*!
    @brief  Read the SQW pin mode
    @return Pin mode, see Ds3231SqwPinMode enum
*/
/**************************************************************************/
Ds3231SqwPinMode RTC_DS3231::readSqwPinMode() {
  int mode;

  Wire.beginTransmission(DS3231_ADDRESS);
  Wire._I2C_WRITE(DS3231_CONTROL);
  Wire.endTransmission();

  Wire.requestFrom((uint8_t)DS3231_ADDRESS, (uint8_t)1);
  mode = Wire._I2C_READ();

  mode &= 0x93;
  return static_cast<Ds3231SqwPinMode>(mode);
}

/**************************************************************************/
/*!
    @brief  Set the SQW pin mode
    @param mode Desired mode, see Ds3231SqwPinMode enum
*/
/**************************************************************************/
void RTC_DS3231::writeSqwPinMode(Ds3231SqwPinMode mode) {
  uint8_t ctrl;
  ctrl = read_i2c_register(DS3231_ADDRESS, DS3231_CONTROL);

  if (mode != DS3231_Interrupt) {
    ctrl &= ~0x04; // turn off INTCN
  }
  ctrl &= ~0x18; // set freq bits to 0
  ctrl |= mode;

  write_i2c_register(DS3231_ADDRESS, DS3231_CONTROL, ctrl);

}

/**************************************************************************/
/*!
    @brief  Get the current temperature from the DS3231's temperature sensor
    @return Current temperature (float)
*/
/**************************************************************************/
float RTC_DS3231::getTemperature()
{
  uint8_t msb, lsb;
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire._I2C_WRITE(DS3231_TEMPERATUREREG);
  Wire.endTransmission();

  Wire.requestFrom(DS3231_ADDRESS, 2);
  msb = Wire._I2C_READ();
  lsb = Wire._I2C_READ();

  return (float) msb + (lsb >> 6) * 0.25f;
}

/**************************************************************************/
/*!
    @brief Enable Alarm1 on the DS3231 by setting A1IE bit on the control register
    
*/
/**************************************************************************/
void RTC_DS3231::enableAlarm1() {
  uint8_t ctrl;
  ctrl = read_i2c_register(DS3231_ADDRESS, DS3231_CONTROL);
  ctrl |= 0x01;
  write_i2c_register(DS3231_ADDRESS, DS3231_CONTROL, ctrl);
  ctrl = read_i2c_register(DS3231_ADDRESS, DS3231_CONTROL);
}

/**************************************************************************/
/*!
    @brief Disables Alarm1 on the DS3231 by zeroing the A1IE bit on Register 0x0E    
*/
/**************************************************************************/
void RTC_DS3231::disableAlarm1() {
  uint8_t ctrl;
  ctrl = read_i2c_register(DS3231_ADDRESS, DS3231_CONTROL);
  ctrl &= ~0x01;
  write_i2c_register(DS3231_ADDRESS, DS3231_CONTROL, ctrl);
}



/**************************************************************************/
/*!
    @brief Clear the flag for Alarm1 on the DS3231 by zeroing A1F bit on the status register
    This will stop the SQW/INT pin from asserting the alarm
    
*/
/**************************************************************************/
void RTC_DS3231::clearAlarm1() {
  uint8_t status;
  status = read_i2c_register(DS3231_ADDRESS, DS3231_STATUSREG);
  status &= ~0x01; // zero the lsb, if possible
  write_i2c_register(DS3231_ADDRESS, DS3231_STATUSREG, status);
}


static void setMSB(uint8_t reg, bool high) {
  uint8_t b;
  b = read_i2c_register(DS3231_ADDRESS, reg);
  if( high ) {
    b |= 0x80;
  } else {
    b &= ~0x80;
  }
  write_i2c_register(DS3231_ADDRESS, reg, b);
}



/**************************************************************************/
/*!
    @brief Per the data sheet, to set the alarm mode, we need to 
    set the mask bits (bit 7) on registers 0x07-0x0A to determine
    what time registers need to match for the alarm to go off
*/
/**************************************************************************/
void RTC_DS3231::setModeAlarm1(Ds3231Alarm1Mode mode) {
  uint8_t b;

  switch(mode) {
    case DS3231_OncePerSec:
      setMSB(DS3231_ALARM1DAYDATE, true); //A1M4
      setMSB(DS3231_ALARM1HOUR, true); // A1M3
      setMSB(DS3231_ALARM1MIN, true); //A1M2
      setMSB(DS3231_ALARM1SEC, true); //A1M1
    break;

    case DS3231_MatchSec:
      setMSB(DS3231_ALARM1DAYDATE, true); //A1M4
      setMSB(DS3231_ALARM1HOUR, true); // A1M3
      setMSB(DS3231_ALARM1MIN, true); //A1M2
      setMSB(DS3231_ALARM1SEC, false); //A1M1
    break;

    case DS3231_MatchMinSec:
      setMSB(DS3231_ALARM1DAYDATE, true); //A1M4
      setMSB(DS3231_ALARM1HOUR, true); // A1M3
      setMSB(DS3231_ALARM1MIN, false); //A1M2
      setMSB(DS3231_ALARM1SEC, false); //A1M1
    break;

    case DS3231_MatchHourMinSec:
      setMSB(DS3231_ALARM1DAYDATE, true); //A1M4
      setMSB(DS3231_ALARM1HOUR, false); // A1M3
      setMSB(DS3231_ALARM1MIN, false); //A1M2
      setMSB(DS3231_ALARM1SEC, false); //A1M1
    break;

    case DS3231_MatchDateHourMinSec:
      setMSB(DS3231_ALARM1DAYDATE, false); //A1M4
      setMSB(DS3231_ALARM1HOUR, false); // A1M3
      setMSB(DS3231_ALARM1MIN, false); //A1M2
      setMSB(DS3231_ALARM1SEC, false); //A1M1
      b = read_i2c_register(DS3231_ADDRESS, DS3231_ALARM1DAYDATE);
      b &= ~0x40; //-- zero the DY/DT bit
      write_i2c_register(DS3231_ADDRESS, DS3231_ALARM1DAYDATE, b);
    break;

    case DS3231_MatchDayHourMinSec:
      setMSB(DS3231_ALARM1DAYDATE, false); //A1M4
      setMSB(DS3231_ALARM1HOUR, false); // A1M3
      setMSB(DS3231_ALARM1MIN, false); //A1M2
      setMSB(DS3231_ALARM1SEC, false); //A1M1
      b = read_i2c_register(DS3231_ADDRESS, DS3231_ALARM1DAYDATE);
      b |= 0x40; //-- turn on the DY/DT bit
      write_i2c_register(DS3231_ADDRESS, DS3231_ALARM1DAYDATE, b);
    break;

    default:
    break;
  }
}

/**************************************************************************/
/*!
    @brief Sets the time into the registers of the DS3231. If the proper
    bits are enabled, then, when the time matches these registers, the alarm goes off.
*/
/**************************************************************************/
void RTC_DS3231::setTimeAlarm1(const DateTime& dt) {
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire._I2C_WRITE((byte)DS3231_ALARM1SEC); // start at location 0x07
  Wire._I2C_WRITE(bin2bcd(dt.second()));
  Wire._I2C_WRITE(bin2bcd(dt.minute()));
  Wire._I2C_WRITE(bin2bcd(dt.hour()));
  Wire._I2C_WRITE(bin2bcd(dt.day())); //-- aka "date" (1-31) in RTC
  Wire._I2C_WRITE(bin2bcd(dt.month()));
  Wire._I2C_WRITE(bin2bcd(dt.year() - 2000));
  Wire.endTransmission();
}

uint8_t RTC_DS3231::getRegister(uint8_t reg) {
  uint8_t b;
  b = read_i2c_register(DS3231_ADDRESS, reg);
  return b;
}


