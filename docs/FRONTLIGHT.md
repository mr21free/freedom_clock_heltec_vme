# Optional Front Light

Freedom Clock uses an e-ink display, so it does not have a real backlight. For night use, the right add-on is a small front light: warm-white LEDs around the display bezel shining softly across the screen.

## Best First Prototype

Use a physical momentary button and a few warm-white LEDs with built-in resistors.

This is the simplest, most reliable version:
- it works while the ESP32 is asleep
- it does not change firmware behavior
- there is no risk of a GPIO being left on in deep sleep
- the user only lights the screen while pressing the button

Recommended LEDs:
- Adafruit Warm White LED Sequins: easy to solder, each has a resistor, about `5 mA` at `3.3 V`, size `4 mm x 9 mm x 2 mm`
- DFRobot 3 V warm-white ultra-thin strip: `2.5 mm` wide, `1 mm` thick, `138 LEDs/m`, can be cut one LED at a time, better for a cleaner bezel-style light pipe
- Pre-wired warm-white `0402` or `0603` LEDs with resistors: smallest visually, but much harder for normal users to install cleanly

For a first product-quality prototype, I would start with `2` to `4` warm-white LED sequins placed around the display edges behind a small diffuser. It is much easier to debug than a tiny raw LED strip.

## Recommended Board Points

The Heltec Vision Master E-series boards expose useful pads on the side headers. Use the official pin map for your exact board revision before soldering.

Recommended points:
- `3V3`: LED power
- `GND`: LED ground
- `GPIO42`: optional firmware-control signal or MOSFET gate

Avoid these pins:
- `GPIO1` to `GPIO6`: used by the e-ink display
- `GPIO21`: already used as the user button
- `BOOT` / `GPIO0`: boot strap behavior
- `GPIO17`: marked as `ADC_CTRL` on the Heltec pin map
- `GPIO45` / `GPIO46`: avoid because they are already involved in board-specific control/strap behavior
- `5V` or `VBAT`: avoid for the first prototype unless the LED module is explicitly designed for it

## QuickLink And UART Connectors

Heltec lists the Vision Master E213 sensor interfaces as `SH2.0-4P` / QuickLink-compatible connectors. Confirm the same connector and pinout on E290 before ordering or soldering. The battery connector is different: `SH1.25-2P`.

If a connector is labelled `3V3`, `GND`, `RX`, and `TX`, treat it as a UART connector:
- `3V3` and `GND` can power a tiny LED module if the current draw is small
- `RX` and `TX` are serial data pins, not LED power pins
- do not connect LEDs directly to `RX` or `TX`

For a plug-in cable, do not rely on only one name. Heltec calls the board connector `SH2.0-4P`, but sellers often use different names for 2.0 mm 4-pin cables. Search for:

```text
SH2.0-4P cable
PH2.0 4P cable
HY2.0 4P cable
2.0mm 4 pin pigtail
Grove 4 pin cable
Heltec QuickLink 4P cable
```

Connector naming is messy across sellers. `JST-SH` normally means 1.0 mm pitch, so the phrase `JST SH 2.0` can be misleading. Match the pitch, pin count, latch shape, and photos before ordering. Do not confuse it with the smaller `SH1.25-2P` battery plug.

## Solar

The E-series product information documents a 3.7 V lithium battery connector and onboard charge/discharge management, but it does not document a dedicated solar panel input. For solar, use an external solar LiPo charger/power-manager module and feed the device through the normal battery/USB power path only after testing voltage, polarity, and charge behavior.

Do not connect a bare solar panel directly to the battery connector.

## Wiring Option A: Simple Manual Button

Use this for the first prototype.

Parts:
- `2` to `4` warm-white LED sequins, or a very short `3 V` warm-white LED strip
- one normally-open momentary tactile button
- thin silicone wire
- optional diffuser film or translucent tape

Wiring:

```text
3V3 -> momentary button -> LED + pads
GND ---------------------> LED - pads
```

If you use raw LEDs without built-in resistors, each LED needs its own current-limiting resistor. Do not connect raw LEDs directly to `3V3`.

## Wiring Option B: Firmware-Controlled GPIO42

Use this only after the manual prototype looks good.

Do not power a strip directly from a GPIO. Use a small logic-level N-MOSFET.

Parts:
- small logic-level N-MOSFET such as `AO3400`, `IRLML2502`, `BSS138`, or `2N7002`
- `100` to `220 ohm` gate resistor
- `100k` gate pulldown resistor
- LED sequins or a short `3 V` LED strip

Wiring:

```text
3V3 -------------------------------> LED +
LED - -----------------------------> MOSFET drain
MOSFET source ---------------------> GND
GPIO42 -> 100-220 ohm resistor ----> MOSFET gate
MOSFET gate -> 100k resistor ------> GND
```

This lets the ESP32 switch the light without pushing LED current through the GPIO pin.

## Physical Placement

Best placement:
- hide LEDs under the plastic bezel edge
- aim them sideways across the e-ink surface, not straight at the user
- use warm white, not cold blue-white
- add diffuser material so the screen glows softly instead of showing bright dots
- start dim; e-ink needs surprisingly little light in a dark room

Avoid:
- putting LEDs directly over the visible screen area
- shining LEDs into the user's eyes
- using addressable RGB LEDs unless you really need color effects
- running a long strip from the small board regulator

## Firmware Direction

The clean firmware-controlled version would be:
- keep `GPIO42` as `FRONTLIGHT_PIN`
- use the existing `21` button to wake the device
- after the selected screen is drawn, keep the front light on for a short window such as `15` to `20` seconds
- turn the light off before deep sleep

I would not make this default until the hardware layout is tested, because the best button behavior depends on the case design.

## References

- Heltec Vision Master E213 docs and pin map: https://docs.heltec.org/en/node/esp32/ht_vme213/index.html
- Heltec Vision Master E213 pin map image: https://resource.heltec.cn/download/HT-VME213/HT-VME213.png
- Heltec Vision Master E290 docs and pin map: check the current Heltec documentation for your exact board revision before soldering
- Adafruit Warm White LED Sequins: https://www.adafruit.com/product/1758
- DFRobot 3 V warm-white ultra-thin LED strip: https://www.dfrobot.com/product-2402.html
