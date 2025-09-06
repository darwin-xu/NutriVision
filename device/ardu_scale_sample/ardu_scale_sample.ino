#include "HX711.h"

#define DT  3 // Data pin connects to Digital 3
#define SCK 2 // Clock pin connects to digital 2

HX711 scale;

// Helper: read a line from Serial (non-blocking timeout)
String readSerialLine(unsigned long timeoutMs = 20000)
{
    unsigned long start = millis();
    String        s     = "";
    while (millis() - start < timeoutMs)
    {
        while (Serial.available())
        {
            char c = Serial.read();
            if (c == '\r')
                continue;
            if (c == '\n')
            {
                s.trim();
                if (s.length() > 0)
                    return s;
                // otherwise keep waiting for some input
            }
            else
            {
                s += c;
            }
        }
        // short yield to avoid locking up
        delay(10);
    }
    s.trim();
    return s; // may be empty on timeout
}

// Calibration routine; if knownWeight <= 0 interactive prompt will ask for it
// (default 100g)
void calibrate(float knownWeight = 0.0)
{
    Serial.println();
    Serial.println("=== Calibration mode ===");
    Serial.println("Step 1: Remove all items from the scale.");
    Serial.println("When ready press Enter (or send any data) to tare.");

    // wait for user confirmation (or timeout)
    readSerialLine(60000); // give user up to 60s

    // Tare (zero) the scale
    scale.tare();
    Serial.println("Tare complete. Zero set.");
    long rawTare = scale.read_average(20);

    // If a weight was provided in the command, use it. Otherwise ask.
    if (knownWeight <= 0.0)
    {
        Serial.println("Step 2: Place a known weight on the scale.");
        Serial.println("Send the weight in grams (e.g. 100) and press Enter, "
                       "or just press Enter to use default 100g:");
        String line = readSerialLine(60000);
        if (line.length() > 0)
        {
            float v = line.toFloat();
            if (v > 0.0)
            {
                knownWeight = v;
            }
            else
            {
                Serial.println("Invalid number received; using default 100 g");
                knownWeight = 100.0;
            }
        }
        else
        {
            Serial.println("No value received; using default 100 g");
            knownWeight = 100.0;
        }
    }
    else
    {
        Serial.print("Using provided known weight: ");
        Serial.print(knownWeight);
        Serial.println(" g");
        Serial.println(
            "Place that weight on the scale and press Enter when stable.");
        readSerialLine(60000);
    }

    Serial.println("Stabilizing and measuring...");
    delay(1000);

    if (!scale.is_ready())
    {
        Serial.println("Scale not ready; aborting calibration.");
        return;
    }

    // Read an averaged raw value
    long raw = scale.read_average(20);

    if (knownWeight <= 0.0)
        knownWeight = 100.0; // fallback safety

    // scale factor such that weight (grams) = raw / scale_factor
    float newScale = (float)(raw - rawTare) / knownWeight;

    // Apply the new scale factor
    scale.set_scale(newScale);

    Serial.println("Calibration complete.");
    Serial.print("Known weight (g): ");
    Serial.println(knownWeight);
    Serial.print("Raw averaged reading: ");
    Serial.println(raw);
    Serial.print("New scale factor (use in set_scale()): ");
    Serial.println(newScale, 6);
    Serial.println("For persistent calibration, copy this value into your "
                   "sketch and upload it.");
    Serial.println("Exiting calibration mode.");
    Serial.println();
}

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
    // Handle incoming serial commands
    if (Serial.available())
    {
        String cmd =
            readSerialLine(50); // quick read; may be empty if only newline
        if (cmd.length() > 0)
        {
            cmd.trim();
            // if command starts with "cal" optionally followed by a number
            if (cmd.startsWith("cal"))
            {
                float providedWeight = 0.0;
                // try to parse an argument after a space: "cal 150"
                int sp = cmd.indexOf(' ');
                if (sp > 0)
                {
                    String arg = cmd.substring(sp + 1);
                    arg.trim();
                    if (arg.length() > 0)
                    {
                        providedWeight = arg.toFloat();
                        if (providedWeight <= 0.0)
                            providedWeight = 0.0; // treat invalid as none
                    }
                }
                calibrate(providedWeight);
            }
            else
            {
                Serial.print("Unknown command: ");
                Serial.println(cmd);
                Serial.println("Available commands: cal, cal <grams>");
            }
        }
    }

    // Regular reporting
    if (scale.is_ready())
    {
        Serial.print("Raw value: ");
        Serial.print(scale.read());
        Serial.print(", units value: ");
        Serial.println(scale.get_units(1));
    }
    delay(1000);
}
