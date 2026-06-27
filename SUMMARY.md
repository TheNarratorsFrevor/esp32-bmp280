### Parts used

    ESP32 board
    BMP280 sensor
    128x32 OLED display
    1 red LED
    1 green LED
    2 resistors for the LEDs
    Breadboard and jumper wires

LEDs connected to ESP32 GPIO pins use a resistor in series, and simple ESP32 LED control is normally done with pinMode() and digitalWrite().

### Sensor behavior

The ESP32 checks the BMP280 every second. Then it shows:

    temperature in Celsius

    pressure in hPa

    altitude in meters

on the OLED screen.

### LED behavior

The two LEDs show whether the temperature changed compared to the last reading:

    red LED on = temperature went up

    green LED on = temperature went down

    both off = temperature stayed the same

Only temperature controls the LEDs. The comparison uses the first decimal place, so a change like 31.3 to 31.4 is enough to change the LED state. Rounding to one decimal place matches how values are usually shown on small OLED displays.

### Software specs

The OLED driver is provided by the Adafruit library, the same I used for the BMP280. the code also includes logic for a scanner, since soldering the BMP280 wasn't an option and at its current state, the sensor is a bit loose and might not always connect.
