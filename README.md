# RTClib (DS3231 only)

This is a fork of Adafruit's fork of JeeLab's real-time clock (RTC) library for Arduino.
Notably, it supports only one of the chips the Adafruit package has, and expands the functionality of the DS3231

_DS3231 Extremely accurate clock_
* [DS3231 datasheet](https://cdn-shop.adafruit.com/product-files/3013/DS3231.pdf)
* [Adafruit product](https://www.adafruit.com/product/3013)) (also comes as a Feather Wing)

An extremely precise clock that, at the most granular resolution, exposes seconds.
But offers two ways to know the *exact* time
* A square wave output, at max 8KHz
* An alarm system that can trigger an alarm at a time match, or every second, triggering an interrupt

We'll use the second for getting cameras to shoot at the exact right moment.

_Adafruit's RTClib_
For the detailed API documentation, see https://adafruit.github.io/RTClib/html/index.html

To install, clone this directory into your `Arduino > Libraries` folder.

### Author
Any questions, please contact Jimmy Chion (jimmy.chion@nytimes.com)