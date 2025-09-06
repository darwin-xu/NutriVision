#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

// ArduCam library - you'll need to install this from Library Manager
#include "ArduCAM.h"
#include "ov5642_regs.h"

// HX711 Scale circuit
#include "HX711.h"

#define DOUT 2 // DT to D2
#define CLK  3 // SCK to D3

HX711 scale;

// Pin definitions for Arduino Nano ESP32
#define CS_PIN 10 // D10
// SDA and SCL use default I2C pins (A4, A5)

// Create ArduCAM instance for OV5642
ArduCAM myCAM(OV5642, CS_PIN);

// WiFi credentials (optional - for web server)
const char* ssid     = "a-tp";
const char* password = "opop9090";

// Web server on port 80
WebServer server(80);

// Server configuration
const char* serverURL = "http://192.168.2.123:3000/api/analyze-food";

// State tracking
bool                weightDetected       = false;
unsigned long       lastWeightCheck      = 0;
const unsigned long WEIGHT_DEBOUNCE_TIME = 2000; // 2 seconds debounce

void setup()
{
    Serial.begin(115200);

    // Initialize I2C (uses default pins A4=SDA, A5=SCL)
    Wire.begin();

    // Initialize SPI (uses default pins: D11=MOSI, D12=MISO, D13=SCK)
    SPI.begin();

    // Initialize CS pin
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);

    // Initialize ArduCAM
    myCAM.write_reg(0x07, 0x80);
    delay(100);
    myCAM.write_reg(0x07, 0x00);
    delay(100);

    // Check if OV5642 is detected
    uint8_t vid, pid;
    myCAM.wrSensorReg16_8(0xff, 0x01);
    myCAM.rdSensorReg16_8(OV5642_CHIPID_HIGH, &vid);
    myCAM.rdSensorReg16_8(OV5642_CHIPID_LOW, &pid);

    if ((vid != 0x56) || (pid != 0x42))
    {
        Serial.println("Can't find OV5642 module!");
        Serial.print("VID: 0x");
        Serial.print(vid, HEX);
        Serial.print(", PID: 0x");
        Serial.println(pid, HEX);
        while (1)
            ;
    }
    else
    {
        Serial.println("OV5642 detected.");
    }

    // Initialize OV5642 camera
    myCAM.set_format(JPEG);
    myCAM.InitCAM();
    myCAM.write_reg(ARDUCHIP_TIM, VSYNC_LEVEL_MASK); // VSYNC is active HIGH
    myCAM.OV5642_set_JPEG_size(OV5642_320x240);      // Set initial resolution

    delay(1000);
    myCAM.clear_fifo_flag();

    // Optional: Setup WiFi and web server
    setupWiFi();
    setupWebServer();

    // Init scale
    Serial.println("Initializing the scale");
    scale.begin(DOUT, CLK);

    // Calibration factor will be the (reading) / (known weight)
    // Adjust this calibration factor as needed
    scale.set_scale(398.f); // this value is obtained by calibrating the scale
                            // with known weights
    if (scale.is_ready())
    {
        Serial.println("Setup complete. Scale ready!");
    }

    // Reset the scale to 0
    scale.tare();

    Serial.println("Setup complete. Camera ready!");
    Serial.println("Commands:");
    Serial.println("- Type 'capture' to take a photo");
    Serial.println("- Type 'stream' to start streaming mode");
    Serial.println("- Type 'res320' for 320x240 resolution");
    Serial.println("- Type 'res640' for 640x480 resolution");
    Serial.println("- Type 'res1024' for 1024x768 resolution");
    Serial.println("- Type 'res1280' for 1280x960 resolution");
    Serial.println("- Type 'res1600' for 1600x1200 resolution");
    Serial.println("- Type 'res2048' for 2048x1536 resolution");
    Serial.println("- Type 'res2592' for 2592x1944 resolution (5MP)");
}

void loop()
{
    // Handle web server requests
    server.handleClient();

    // Handle serial commands
    if (Serial.available())
    {
        String command = Serial.readStringUntil('\n');
        command.trim();

        if (command == "capture")
        {
            captureImage();
        }
        else if (command == "stream")
        {
            streamImages();
        }
        else if (command == "res320")
        {
            myCAM.OV5642_set_JPEG_size(OV5642_320x240);
            Serial.println("Resolution set to 320x240");
        }
        else if (command == "res640")
        {
            myCAM.OV5642_set_JPEG_size(OV5642_640x480);
            Serial.println("Resolution set to 640x480");
        }
        else if (command == "res1024")
        {
            myCAM.OV5642_set_JPEG_size(OV5642_1024x768);
            Serial.println("Resolution set to 1024x768");
        }
        else if (command == "res1280")
        {
            myCAM.OV5642_set_JPEG_size(OV5642_1280x960);
            Serial.println("Resolution set to 1280x960");
        }
        else if (command == "res1600")
        {
            myCAM.OV5642_set_JPEG_size(OV5642_1600x1200);
            Serial.println("Resolution set to 1600x1200");
        }
        else if (command == "res2048")
        {
            myCAM.OV5642_set_JPEG_size(OV5642_2048x1536);
            Serial.println("Resolution set to 2048x1536");
        }
        else if (command == "res2592")
        {
            myCAM.OV5642_set_JPEG_size(OV5642_2592x1944);
            Serial.println("Resolution set to 2592x1944 (5MP)");
        }
    }

    // Serial.println("scale.read() = " + String(scale.read()));
    // Serial.println("scale.get_units() = " + String(scale.get_units()));
    //  if weight is heavier than 100 grams, capture an image and upload to
    //  server.

    float         currentWeight = scale.get_units();
    unsigned long currentTime   = millis();

    // Check if weight exceeds threshold and debounce to prevent multiple
    // uploads
    if (currentWeight > 100 && !weightDetected &&
        (currentTime - lastWeightCheck > WEIGHT_DEBOUNCE_TIME))
    {
        Serial.println("Weight exceeded threshold: " + String(currentWeight) +
                       "g, capturing image...");

        // Capture the image and send it to the server hosted on
        // HTTP://192.168.2.123:3000/api/analyze-food Using MIME Multipart Media
        // Encapsulation There are two parts of the content:
        // 1. The image data, in JPEG format, the name is "foodImage" and the
        // value is the image data.
        // 2. The scale data, in text, the name is "weight" and the value is the
        // weight in grams.

        if (captureAndUploadImage(currentWeight))
        {
            weightDetected  = true;
            lastWeightCheck = currentTime;
            Serial.println("Image uploaded successfully!");
        }
        else
        {
            Serial.println("Failed to upload image");
            lastWeightCheck = currentTime; // Still update to prevent spam
        }
    }

    // Reset weight detection when weight drops below threshold
    if (currentWeight < 50 && weightDetected)
    {
        Serial.println("Weight removed, ready for next detection");
        weightDetected = false;
    }

    delay(500);
}

void captureImage()
{
    Serial.println("Capturing image...");

    myCAM.flush_fifo();
    myCAM.clear_fifo_flag();
    myCAM.start_capture();

    // Wait for capture to complete
    while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK))
        ;

    uint32_t length = myCAM.read_fifo_length();
    Serial.print("Image size: ");
    Serial.print(length);
    Serial.println(" bytes");

    if (length >= MAX_FIFO_SIZE)
    {
        Serial.println("Over size.");
        return;
    }

    if (length == 0)
    {
        Serial.println("Size is 0.");
        return;
    }

    // Read and output image data
    myCAM.CS_LOW();
    myCAM.set_fifo_burst();

    Serial.println("--- IMAGE DATA START ---");

    // Use bulk SPI transfer for better performance
    const size_t chunkSize = 128;
    uint8_t      buffer[chunkSize];
    uint32_t     bytesRemaining = length;

    while (bytesRemaining > 0)
    {
        size_t bytesToRead = min(chunkSize, bytesRemaining);

        // Fill buffer with zeros for SPI transfer
        memset(buffer, 0x00, bytesToRead);

        // Bulk SPI transfer
        SPI.transfer(buffer, bytesToRead);

        // Print data as hex
        for (size_t i = 0; i < bytesToRead; i++)
        {
            if (buffer[i] < 16)
                Serial.print("0");
            Serial.print(buffer[i], HEX);
            Serial.print(" ");

            // Check for JPEG end marker
            if (i > 0 && buffer[i] == 0xD9 && buffer[i - 1] == 0xFF)
            {
                Serial.println();
                goto jpeg_end;
            }
        }

        bytesRemaining -= bytesToRead;
    }

jpeg_end:

    myCAM.CS_HIGH();
    Serial.println("\n--- IMAGE DATA END ---");

    myCAM.clear_fifo_flag();
}

void streamImages()
{
    Serial.println("Starting stream mode (press any key to stop)...");

    while (!Serial.available())
    {
        captureImage();
        delay(1000); // 1 second between captures
    }

    // Clear the serial buffer
    while (Serial.available())
    {
        Serial.read();
    }

    Serial.println("Stream stopped.");
}

void setupWiFi()
{
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println();
        Serial.print("WiFi connected! IP address: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("\nWiFi connection failed. Continuing without WiFi.");
    }
}

void setupWebServer()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    server.on("/", handleRoot);
    server.on("/capture", handleCapture);
    server.on("/status", handleStatus);
    server.begin();
    Serial.println("Web server started");
}

void handleRoot()
{
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, "
            "initial-scale=1.0'>";
    html += "<title>ArduCAM ESP32</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 40px; "
            "background-color: #f5f5f5; }";
    html += ".container { max-width: 600px; margin: 0 auto; background: white; "
            "padding: 30px; border-radius: 10px; box-shadow: 0 4px 6px "
            "rgba(0,0,0,0.1); }";
    html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
    html += ".btn { display: inline-block; padding: 12px 24px; margin: 10px; "
            "background-color: #007bff; color: white; text-decoration: none; "
            "border-radius: 5px; transition: background-color 0.3s; }";
    html += ".btn:hover { background-color: #0056b3; }";
    html += ".status { margin: 20px 0; padding: 15px; background-color: "
            "#e9f7ef; border-left: 4px solid #27ae60; }";
    html += "</style></head><body>";
    html += "<div class='container'>";
    html += "<h1>ArduCAM ESP32 Control Panel</h1>";
    html += "<div class='status'>";
    html += "<strong>Status:</strong> Camera Ready<br>";
    html +=
        "<strong>IP Address:</strong> " + WiFi.localIP().toString() + "<br>";
    html +=
        "<strong>Free Heap:</strong> " + String(ESP.getFreeHeap()) + " bytes";
    html += "</div>";
    html += "<p><a href='/capture' class='btn'>📸 Capture Image</a></p>";
    html += "<p><a href='/status' class='btn'>🔍 Check Status</a></p>";
    html += "<p><small>Tip: The capture endpoint streams JPEG data directly to "
            "your browser</small></p>";
    html += "</div></body></html>";
    server.send(200, "text/html", html);
}

void handleCapture()
{
    Serial.println("Web capture requested");

    myCAM.flush_fifo();
    myCAM.clear_fifo_flag();
    myCAM.start_capture();

    while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK))
        ;

    uint32_t length = myCAM.read_fifo_length();

    if (length >= MAX_FIFO_SIZE || length == 0)
    {
        server.send(500, "text/plain", "Camera error");
        return;
    }

    // Send image directly to browser
    server.setContentLength(length);
    server.send(200, "image/jpeg", "");

    WiFiClient client = server.client();

    // Check if client is still connected
    if (!client.connected())
    {
        myCAM.clear_fifo_flag();
        return;
    }

    myCAM.CS_LOW();
    myCAM.set_fifo_burst();

    // Use optimized bulk SPI transfer for maximum performance
    const size_t spiChunkSize =
        1024; // Optimal chunk size for SPI bulk transfer
    uint8_t  spiBuffer[spiChunkSize];
    uint32_t bytesRemaining = length;

    while (bytesRemaining > 0 && client.connected())
    {
        // Calculate how many bytes to read in this chunk
        size_t bytesToRead = min(spiChunkSize, bytesRemaining);

        // Fill buffer with zeros (dummy data to send)
        memset(spiBuffer, 0x00, bytesToRead);

        // Bulk SPI transfer - much faster than individual transfers
        SPI.transfer(spiBuffer, bytesToRead);

        // Send the received data to client
        size_t bytesWritten = client.write(spiBuffer, bytesToRead);

        // Handle partial writes
        if (bytesWritten < bytesToRead)
        {
            // If not all bytes were written, we need to handle the remainder
            size_t remainingBytes = bytesToRead - bytesWritten;
            size_t offset         = bytesWritten;

            while (remainingBytes > 0 && client.connected())
            {
                size_t additionalWritten =
                    client.write(spiBuffer + offset, remainingBytes);
                offset += additionalWritten;
                remainingBytes -= additionalWritten;

                if (additionalWritten == 0)
                {
                    delay(1); // Small delay if client is busy
                }
            }
        }

        bytesRemaining -= bytesToRead;

        // Yield to prevent watchdog timeout on large images
        if (bytesRemaining % (spiChunkSize * 4) == 0)
        {
            yield();
        }
    }

    myCAM.CS_HIGH();
    myCAM.clear_fifo_flag();

    if (!client.connected())
    {
        Serial.println("Client disconnected during transfer");
    }
}

void handleStatus()
{
    String json = "{";
    json += "\"status\": \"online\",";
    json += "\"freeHeap\": " + String(ESP.getFreeHeap()) + ",";
    json += "\"uptime\": " + String(millis()) + ",";
    json += "\"wifiRSSI\": " + String(WiFi.RSSI()) + ",";
    json += "\"ipAddress\": \"" + WiFi.localIP().toString() + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

bool captureAndUploadImage(float weight)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi not connected, cannot upload");
        return false;
    }

    Serial.println("Starting image capture for upload...");

    // Capture image
    myCAM.flush_fifo();
    myCAM.clear_fifo_flag();
    myCAM.start_capture();

    // Wait for capture to complete
    while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK))
        ;

    uint32_t length = myCAM.read_fifo_length();
    Serial.print("Captured image size: ");
    Serial.print(length);
    Serial.println(" bytes");

    if (length >= MAX_FIFO_SIZE || length == 0)
    {
        Serial.println("Image capture failed - invalid size");
        myCAM.clear_fifo_flag();
        return false;
    }

    // Create WiFi client and connect to server
    WiFiClient client;

    // Parse server URL (assuming http://192.168.2.123:3000/api/analyze-food)
    const char* host = "192.168.2.123";
    const int   port = 3000;
    const char* path = "/api/analyze-food";

    if (!client.connect(host, port))
    {
        Serial.println("Connection to server failed");
        myCAM.clear_fifo_flag();
        return false;
    }

    Serial.println("Connected to server, sending data...");

    // Generate boundary for multipart
    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";

    // Prepare multipart data parts
    String weightPart = "--" + boundary + "\r\n";
    weightPart += "Content-Disposition: form-data; name=\"weight\"\r\n\r\n";
    weightPart += String(weight, 2) + "\r\n";

    String imagePart = "--" + boundary + "\r\n";
    imagePart += "Content-Disposition: form-data; name=\"foodImage\"; "
                 "filename=\"food.jpg\"\r\n";
    imagePart += "Content-Type: image/jpeg\r\n\r\n";

    String endBoundary = "\r\n--" + boundary + "--\r\n";

    // Calculate total content length
    size_t totalLength = weightPart.length() + imagePart.length() + length +
                         endBoundary.length();

    // Send HTTP headers
    client.print("POST " + String(path) + " HTTP/1.1\r\n");
    client.print("Host: " + String(host) + ":" + String(port) + "\r\n");
    client.print("Content-Type: multipart/form-data; boundary=" + boundary +
                 "\r\n");
    client.print("Content-Length: " + String(totalLength) + "\r\n");
    client.print("Connection: close\r\n\r\n");

    // Send weight part
    client.print(weightPart);

    // Send image part header
    client.print(imagePart);

    // Send image data using optimized SPI transfer
    myCAM.CS_LOW();
    myCAM.set_fifo_burst();

    const size_t chunkSize = 512;
    uint8_t      buffer[chunkSize];
    uint32_t     bytesRemaining = length;
    size_t       totalSent      = 0;

    while (bytesRemaining > 0 && client.connected())
    {
        size_t bytesToRead = min(chunkSize, bytesRemaining);

        // Fill buffer with zeros for SPI transfer
        memset(buffer, 0x00, bytesToRead);

        // Bulk SPI transfer
        SPI.transfer(buffer, bytesToRead);

        // Send to server
        size_t bytesWritten = client.write(buffer, bytesToRead);
        totalSent += bytesWritten;

        if (bytesWritten != bytesToRead)
        {
            Serial.println("Warning: Partial write to server");
            break;
        }

        bytesRemaining -= bytesToRead;

        // Progress indicator
        if (totalSent % 2048 == 0)
        {
            Serial.print(".");
        }

        // Yield to prevent watchdog timeout
        yield();
    }

    Serial.println(); // New line after progress dots

    myCAM.CS_HIGH();
    myCAM.clear_fifo_flag();

    if (!client.connected())
    {
        Serial.println("Connection lost during image transfer");
        return false;
    }

    // Send end boundary
    client.print(endBoundary);

    Serial.println("Data sent, waiting for response...");

    // Wait for response
    unsigned long timeout  = millis() + 10000; // 10 second timeout
    String        response = "";

    while (client.connected() && millis() < timeout)
    {
        if (client.available())
        {
            response += client.readString();
            break;
        }
        delay(10);
    }

    client.stop();

    if (response.length() > 0)
    {
        Serial.println("Server response received:");
        Serial.println(response);

        // Check if response indicates success (look for HTTP 200)
        if (response.indexOf("200 OK") >= 0 || response.indexOf("201") >= 0)
        {
            return true;
        }
    }
    else
    {
        Serial.println("No response from server (timeout)");
    }

    return false;
}