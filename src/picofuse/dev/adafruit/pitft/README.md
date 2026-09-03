# Adafruit 2.8" PiTFT - Capacitive Touch

2.8" display with 320x240 16-bit color pixels and a capacitive touch overlay

![src/picofuse/dev/adafruit/pitft/adafruit_products_pictpschem.png](adafruit_products_pictpschem.png)

## Pinout

|Signal|Pin|Pin|Signal|
|------|---|---|------|
|  3.3V|  1|2  |5V    |
|   SDA|  3|4  |5V    |
|   SCL|  5|6  |GND   |
|      |  7|8  |      |
|   GND|  9|10 |      |
|   SW4| 11|12 |LEDK  |
|   SW3| 13|14 |GND   |
|   SW2| 15|16 |SW1   |
|      | 17|18 |CTPINT|
|  MOSI| 19|20 |      |
|  MISO| 21|22 |TFTDC |
|SPICLK| 23|24 |TFTCS |
|   GND| 25|26 | |

Here is an overview of the pin functions:

* Power
  * 3.3V
  * 5V
  * GND
* Touch overlay (FT6236 over I2C)
  * SDA (I2C data line for touch controller)
  * SCL (I2C clock line for touch controller)
  * CTPINT (interrupt pin from the touch controller)
* Backlight (PWM)
  * LEDK (backlight control pin)
* Display Controller (ILI9341 over SPI)
  * MISO (data line from the SPI bus used by the display controller)
  * MOSI (data line to the SPI bus used by the display controller)
  * SPICLK (clock line for the SPI bus used by the display controller)
  * TFTCS (chip select pin for the display controller)
  * TFTDC (data/command select pin for the display controller)
* Switches (Pull-down)
  * SW1
  * SW2
  * SW3
  * SW4

## References

<https://learn.adafruit.com/adafruit-2-8-pitft-capacitive-touch>
