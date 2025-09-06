void setup()
{
    Serial.begin(115200);
    for (int i = 0; i < 100; i++)
    {
        // The earliest you can see this is about 77, so there is a window of
        // about 770ms in which Serial.println(i) will lose data.
        Serial.println(i);
        delay(10);
    }
    Serial.print("start: ");
}

void loop()
{
}
