#include "HX711.h"

#define DT  3 // Data pin connects to Digital 3
#define SCK 2 // Clock pin connects to digital 2

HX711 scale;

void setup()
{
    Serial.begin(115200);
    Serial.println("begin");
    scale.begin(DT, SCK);

    int i = 0;
    while (true)
    {
        ++i;
        if (scale.is_ready())
            break;
        if (i == 10)
            Serial.println("scale init failed after 10 times.");
        delay(1000);
    }
    Serial.println("scale init successed.");

    // Reset the scale to 0
    scale.tare();

    // Calibration factor will be the (reading) / (known weight)
    // Adjust this calibration factor as needed
    scale.set_scale(398.f); // this value is obtained by calibrating the scale
                            // with known weights
    if (scale.is_ready())
    {
        Serial.println("Setup complete. Scale ready!");
    }
}

void loop()
{
    if (scale.is_ready())
    {
        Serial.print("Raw value: ");
        Serial.print(scale.read());
        Serial.print(", units value: ");
        Serial.println(scale.get_units());
    }
    delay(1000);
}
