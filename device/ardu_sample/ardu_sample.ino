void setup()
{
    Serial.begin(115200);
    // On NANO ESP32:
    // The earliest number you can see this is about 70 to 80, so there is a
    // window of about 770ms in which Serial.println(i) will lose data.

    // On UNO:
    // The earliest number you can see this is 0.
    for (int i = 0; i < 100; i++)
    {
        Serial.println(i);
        delay(10);
    }
    Serial.print("start: ");
}

void loop()
{
}
