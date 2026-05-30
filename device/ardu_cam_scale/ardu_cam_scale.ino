#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "mbedtls/base64.h"

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

// WiFi credentials
const char* ssid     = "a-tp";
const char* password = "opop9090";

// Web server on port 80
WebServer server(80);

// State tracking
bool                weightDetected       = false;
unsigned long       lastWeightCheck      = 0;
const unsigned long WEIGHT_DEBOUNCE_TIME = 2000; // 2 seconds debounce
const size_t        LOCAL_IMAGE_MAX_BYTES = 180 * 1024;
const float         AUTO_CAPTURE_THRESHOLD_GRAMS = 50.0;
const float         WEIGHT_REMOVED_THRESHOLD_GRAMS = 25.0;
const size_t        LLM_IMAGE_MAX_BYTES = 60 * 1024;
const size_t        AI_ERROR_LOG_MAX_CHARS = 700;
const size_t        AI_CONTENT_LOG_MAX_CHARS = 700;
const size_t        AI_RESPONSE_LOG_MAX_CHARS = 1200;
const unsigned long AI_REQUEST_COOLDOWN_MS = 0;
const unsigned long AI_START_DELAY_MS = 900;
const char*         AI_API_URL = "https://openrouter.ai/api/v1/chat/completions";
const char*         AI_MODEL   = "mistralai/mistral-small-2603";
const char*         AI_BACKUP_MODEL =
    "nvidia/nemotron-3-nano-omni-30b-a3b-reasoning:free";
const char* AI_API_KEY =
    "REDACTED_OPENROUTER_API_KEY";

uint8_t*      latestImage       = nullptr;
size_t        latestImageSize   = 0;
uint32_t      latestAnalysisId  = 0;
float         latestWeightGrams = 0;
unsigned long latestCaptureMs   = 0;
unsigned long lastAiRequestMs    = 0;
unsigned long pendingAiStartMs    = 0;
bool          pendingAiAnalysis  = false;
String        deviceState        = "ready";
String        latestAnalysisJson = "";
String        activeUserProfile =
    "{\"age\":null,\"sex\":\"\",\"heightCm\":null,\"weightKg\":null,"
    "\"allergens\":[],\"goals\":{\"fatLoss\":false,\"muscleGain\":false},"
    "\"conditions\":{\"diabetes\":false,\"hypertension\":false,"
    "\"kidneyDisease\":false,\"gout\":false,\"allergy\":false}}";

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>食物分析与营养追踪</title>
  <style>
    body{margin:0;font-family:Arial,"Microsoft YaHei",sans-serif;background:#f5f7fb;color:#1f2937}
    .container{max-width:960px;margin:0 auto;padding:24px}
    .panel,.results{background:#fff;border:1px solid #e5e7eb;border-radius:8px;padding:20px;margin-bottom:18px}
    h1{margin:0 0 10px;font-size:28px}.muted{color:#64748b}.status{padding:12px;border-left:4px solid #16a34a;background:#ecfdf5;margin:16px 0}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px}
    label{font-size:13px;color:#475569}input,select{width:100%;box-sizing:border-box;margin-top:4px;padding:9px;border:1px solid #cbd5e1;border-radius:6px}
    .checks{display:flex;gap:12px;flex-wrap:wrap}.checks label{display:flex;align-items:center;gap:5px}.checks input{width:auto;margin:0}
    button,.btn{display:inline-block;border:0;border-radius:6px;padding:10px 14px;background:#2563eb;color:#fff;text-decoration:none;cursor:pointer}
    .secondary{background:#475569}.actions{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-top:14px}
    .food{max-width:100%;border-radius:8px;border:1px solid #e5e7eb}.nutrition{display:grid;grid-template-columns:repeat(auto-fit,minmax(110px,1fr));gap:10px}
    .metric{border:1px solid #e5e7eb;border-radius:8px;padding:12px}.value{font-size:22px;font-weight:700}.hidden{display:none}
  </style>
</head>
<body>
  <main class="container">
    <section class="panel">
      <h1>食物分析系统</h1>
      <p class="muted">页面和 API 现在由 Arduino Nano ESP32 直接提供。把食物放在秤上，设备会拍照并更新结果。</p>
      <div class="status"><strong id="statusText">系统已就绪 - 等待设备数据</strong><div id="lastUpdate" class="muted"></div></div>
      <div class="actions">
        <button onclick="manualCapture()">手动拍照</button>
        <button class="secondary" onclick="refreshData()">刷新结果</button>
      </div>
    </section>

    <section class="panel">
      <h2>用户画像</h2>
      <div class="grid">
        <div><label>年龄<input id="profileAge" type="number" min="0" max="120"></label></div>
        <div><label>性别<select id="profileSex"><option value="">未选择</option><option value="male">男</option><option value="female">女</option><option value="other">其他</option></select></label></div>
        <div><label>身高（cm）<input id="profileHeightCm" type="number" min="0" max="250"></label></div>
        <div><label>体重（kg）<input id="profileWeightKg" type="number" min="0" max="300" step="0.1"></label></div>
        <div><label>过敏原（逗号分隔）<input id="profileAllergens" type="text"></label></div>
      </div>
      <p>目标</p>
      <div class="checks"><label><input id="goalFatLoss" type="checkbox">减脂</label><label><input id="goalMuscleGain" type="checkbox">增肌</label></div>
      <p>慢病/情况</p>
      <div class="checks"><label><input id="condDiabetes" type="checkbox">糖尿病</label><label><input id="condHypertension" type="checkbox">高血压</label><label><input id="condKidney" type="checkbox">肾病</label><label><input id="condGout" type="checkbox">痛风</label><label><input id="condAllergy" type="checkbox">过敏</label></div>
      <div class="actions"><button onclick="saveProfile()">保存画像</button><span id="profileStatus" class="muted"></span></div>
    </section>

    <section id="results" class="results hidden"></section>
  </main>
  <script>
    const $=id=>document.getElementById(id);
    function profileFromForm(){return{age:+$('profileAge').value||null,sex:$('profileSex').value,heightCm:+$('profileHeightCm').value||null,weightKg:+$('profileWeightKg').value||null,allergens:$('profileAllergens').value.split(/[,，]/).map(s=>s.trim()).filter(Boolean),goals:{fatLoss:$('goalFatLoss').checked,muscleGain:$('goalMuscleGain').checked},conditions:{diabetes:$('condDiabetes').checked,hypertension:$('condHypertension').checked,kidneyDisease:$('condKidney').checked,gout:$('condGout').checked,allergy:$('condAllergy').checked}}}
    function fillProfile(p){if(!p)return;$('profileAge').value=p.age||'';$('profileSex').value=p.sex||'';$('profileHeightCm').value=p.heightCm||'';$('profileWeightKg').value=p.weightKg||'';$('profileAllergens').value=(p.allergens||[]).join(', ');$('goalFatLoss').checked=!!p.goals?.fatLoss;$('goalMuscleGain').checked=!!p.goals?.muscleGain;$('condDiabetes').checked=!!p.conditions?.diabetes;$('condHypertension').checked=!!p.conditions?.hypertension;$('condKidney').checked=!!p.conditions?.kidneyDisease;$('condGout').checked=!!p.conditions?.gout;$('condAllergy').checked=!!p.conditions?.allergy}
    async function saveProfile(){try{const p=profileFromForm();localStorage.setItem('nutrivision.userProfile.v1',JSON.stringify(p));const r=await fetch('/api/profile',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});$('profileStatus').textContent=r.ok?'已保存':'保存失败'}catch(e){$('profileStatus').textContent='保存失败'}}
    function metric(label,value){return `<div class="metric"><div class="value">${value}</div><div>${label}</div></div>`}
    function render(d){if(!d.success){$('statusText').textContent=d.status==='ready'?'设备就绪':'系统已就绪 - 等待设备数据';$('lastUpdate').textContent=d.error||'等待下一次称重';$('results').classList.add('hidden');$('results').innerHTML='';return}const x=d.data;if(x.status==='processing'){$('statusText').textContent='查询中……';$('lastUpdate').textContent='已拍照，正在分析营养建议';$('results').classList.remove('hidden');$('results').innerHTML=`<h2>查询中……</h2><img class="food" src="${x.image.path}" alt="食物图片"><p><strong>重量：</strong>${x.weight}g</p><p>正在生成营养分析，请稍候。</p>`;return}const a=x.analysis,n=a.nutrition;$('statusText').textContent='分析完成';$('lastUpdate').textContent='设备运行时间：'+Math.round(x.timestampMs/1000)+' 秒';$('results').classList.remove('hidden');$('results').innerHTML=`<h2>食物分析结果</h2><img class="food" src="${x.image.path}" alt="食物图片"><p><strong>重量：</strong>${x.weight}g</p><p><strong>食物类型：</strong>${a.foodType}</p><div class="nutrition">${metric('热量',n.calories)}${metric('蛋白质',n.protein+'g')}${metric('碳水',n.carbs+'g')}${metric('脂肪',n.fat+'g')}${metric('膳食纤维',n.fiber+'g')}${metric('GI',n.GI)}${metric('GL',n.GL)}</div><h3>健康建议</h3><ul>${a.healthSuggestions.map(s=>`<li>${s}</li>`).join('')}</ul><h3>菜品推荐</h3><ul>${a.dishSuggestions.map(s=>`<li>${s}</li>`).join('')}</ul>`}
    async function refreshData(){try{const r=await fetch('/api/latest-analysis?t='+Date.now());render(await r.json())}catch(e){$('statusText').textContent='连接异常 - 请检查设备'}}
    async function manualCapture(){try{$('statusText').textContent='正在拍照...';const r=await fetch('/capture?json=1&t='+Date.now());render(await r.json())}catch(e){$('statusText').textContent='拍照失败 - 请检查设备'}}
    (async()=>{try{fillProfile(JSON.parse(localStorage.getItem('nutrivision.userProfile.v1')||'null'));const r=await fetch('/api/profile');const j=await r.json();if(j.success)fillProfile(j.data)}catch(e){}refreshData();setInterval(refreshData,750)})();
  </script>
</body>
</html>
)rawliteral";

// ***************************************************************************
// Setup functions

void setupCamera()
{
    Serial.println("Initializing the ArduCAM...");

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

        // Halt execution on error
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
    Serial.println("The ArduCAM initialized.");
}

void setupScale()
{
    // Init scale
    Serial.println("Initializing the scale");
    scale.begin(DOUT, CLK);

    int i = 0;
    while (true)
    {
        ++i;
        if (scale.is_ready())
            break;
        if (i == 10)
            Serial.println(
                "The scale seems to fail to initialize after 10 seconds.");
        delay(1000);
    }

    scale.tare();
    scale.set_scale(403.f);
    Serial.println("The scale initialized.");
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
        Serial.println("\nWiFi connection failed.");

        // Halt execution on error
        while (1)
            ;
    }
}

void setupWebServer()
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    server.on("/", HTTP_GET, handleRoot);
    server.on("/capture", HTTP_GET, handleCapture);
    server.on("/latest.jpg", HTTP_GET, handleLatestImage);
    server.on("/status", handleStatus);
    server.on("/api/health", HTTP_GET, handleHealth);
    server.on("/api/latest-analysis", HTTP_GET, handleLatestAnalysis);
    server.on("/api/profile", HTTP_GET, handleGetProfile);
    server.on("/api/profile", HTTP_POST, handlePostProfile);
    server.on("/api/analyze-food", HTTP_POST, handleAnalyzeFood);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("Web server started");
}

// ***************************************************************************
// Helper functions

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

void handleRoot()
{
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleCapture()
{
    Serial.println("Web capture requested");

    float weight = scale.get_units();
    if (!captureAndStoreImage(weight))
    {
        if (server.hasArg("json"))
        {
            server.send(500, "application/json",
                        "{\"success\":false,\"error\":\"Camera error\"}");
        }
        else
        {
            server.send(500, "text/plain", "Camera error");
        }
        return;
    }

    if (server.hasArg("json"))
    {
        server.send(200, "application/json", buildAnalysisResponseJson());
        return;
    }

    handleLatestImage();
}

void handleLatestImage()
{
    if (latestImage == nullptr || latestImageSize == 0)
    {
        server.send(404, "text/plain", "No image captured yet");
        return;
    }

    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(latestImageSize);
    server.send(200, "image/jpeg", "");

    WiFiClient client = server.client();
    const size_t chunkSize = 1024;
    size_t offset = 0;
    while (offset < latestImageSize && client.connected())
    {
        size_t bytesToWrite = min(chunkSize, latestImageSize - offset);
        size_t written = client.write(latestImage + offset, bytesToWrite);
        if (written == 0)
        {
            delay(1);
            continue;
        }
        offset += written;
        yield();
    }
}

bool captureAndStoreImage(float weight)
{
    myCAM.flush_fifo();
    myCAM.clear_fifo_flag();
    myCAM.start_capture();

    while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK))
        ;

    uint32_t length = myCAM.read_fifo_length();

    if (length >= MAX_FIFO_SIZE || length == 0 ||
        length > LOCAL_IMAGE_MAX_BYTES)
    {
        Serial.println("Image capture failed - invalid size");
        Serial.print("Captured size: ");
        Serial.print(length);
        Serial.print(" bytes, local limit: ");
        Serial.println(LOCAL_IMAGE_MAX_BYTES);
        myCAM.clear_fifo_flag();
        return false;
    }

    uint8_t* newImage = (uint8_t*)malloc(length);
    if (newImage == nullptr)
    {
        Serial.println("Not enough heap to store latest image");
        myCAM.clear_fifo_flag();
        return false;
    }

    myCAM.CS_LOW();
    myCAM.set_fifo_burst();

    const size_t spiChunkSize = 1024;
    uint32_t bytesRemaining = length;
    size_t   offset         = 0;

    while (bytesRemaining > 0)
    {
        size_t bytesToRead = min(spiChunkSize, bytesRemaining);
        memset(newImage + offset, 0x00, bytesToRead);
        SPI.transfer(newImage + offset, bytesToRead);
        offset += bytesToRead;
        bytesRemaining -= bytesToRead;
        yield();
    }

    myCAM.CS_HIGH();
    myCAM.clear_fifo_flag();

    if (latestImage != nullptr)
    {
        free(latestImage);
    }

    latestImage       = newImage;
    latestImageSize   = length;
    latestWeightGrams = weight;
    latestCaptureMs   = millis();
    latestAnalysisId++;
    latestAnalysisJson = "";
    deviceState = "processing";
    pendingAiAnalysis = true;
    pendingAiStartMs = millis() + AI_START_DELAY_MS;

    Serial.print("Stored latest image for local API: ");
    Serial.print(latestImageSize);
    Serial.println(" bytes");
    Serial.println("Analysis queued; UI can show processing state.");

    return true;
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

String jsonEscape(String value)
{
    value.replace("\\", "\\\\");
    value.replace("\"", "\\\"");
    value.replace("\n", "\\n");
    value.replace("\r", "\\r");
    return value;
}

String buildFallbackAnalysisObjectJson(String reason)
{
    int calories = max(0, (int)round(latestWeightGrams * 1.2));
    int protein = max(0, (int)round(latestWeightGrams * 0.04));
    int carbs = max(0, (int)round(latestWeightGrams * 0.18));
    int fat = max(0, (int)round(latestWeightGrams * 0.02));
    int fiber = max(0, (int)round(latestWeightGrams * 0.015));
    int gl = max(0, (int)round((carbs * 55.0) / 100.0));

    String json = "{";
    json += "\"foodType\":\"Unknown food\",";
    json += "\"confidence\":0.3,";
    json += "\"nutrition\":{";
    json += "\"calories\":" + String(calories) + ",";
    json += "\"protein\":" + String(protein) + ",";
    json += "\"carbs\":" + String(carbs) + ",";
    json += "\"fat\":" + String(fat) + ",";
    json += "\"fiber\":" + String(fiber) + ",";
    json += "\"GI\":55,";
    json += "\"GL\":" + String(gl);
    json += "},";
    json += "\"healthSuggestions\":[";
    json += "\"OpenRouter 分析暂不可用，当前显示基于重量的估算。\",";
    json += "\"" + jsonEscape(reason) + "\"";
    json += "],";
    json += "\"dishSuggestions\":[";
    json += "\"搭配一份蔬菜和适量优质蛋白，获得更均衡的一餐。\"";
    json += "]";
    json += "}";
    return json;
}

String buildAnalysisResponseJson()
{
    String imagePath = "/latest.jpg?t=" + String(latestAnalysisId);
    String status = latestAnalysisJson.length() > 0 ? "complete" : deviceState;
    String analysis =
        latestAnalysisJson.length() > 0
            ? latestAnalysisJson
            : buildFallbackAnalysisObjectJson("尚未取得 OpenRouter 分析结果。");

    String json = "{\"success\":true,\"data\":{";
    json += "\"id\":" + String(latestAnalysisId) + ",";
    json += "\"status\":\"" + status + "\",";
    json += "\"image\":{\"filename\":\"latest.jpg\",\"originalName\":\"camera.jpg\",";
    json += "\"size\":" + String(latestImageSize) + ",";
    json += "\"path\":\"" + imagePath + "\"},";
    json += "\"weight\":" + String((int)round(latestWeightGrams)) + ",";
    json += "\"analysis\":" + analysis + ",";
    json += "\"timestampMs\":" + String(latestCaptureMs);
    json += "}}";
    return json;
}

String base64EncodeLatestImage()
{
    if (latestImage == nullptr || latestImageSize == 0 ||
        latestImageSize > LLM_IMAGE_MAX_BYTES)
    {
        return "";
    }

    size_t encodedLen = 0;
    mbedtls_base64_encode(nullptr, 0, &encodedLen, latestImage, latestImageSize);

    unsigned char* encoded = (unsigned char*)malloc(encodedLen + 1);
    if (encoded == nullptr)
    {
        Serial.println("Not enough heap to base64 encode image");
        return "";
    }

    int rc = mbedtls_base64_encode(encoded, encodedLen + 1, &encodedLen,
                                   latestImage, latestImageSize);
    if (rc != 0)
    {
        free(encoded);
        Serial.println("Base64 encode failed");
        return "";
    }

    encoded[encodedLen] = '\0';
    String result = String((char*)encoded);
    free(encoded);
    return result;
}

String jsonUnescape(String input)
{
    String output = "";
    output.reserve(input.length());
    bool escaping = false;
    for (size_t i = 0; i < input.length(); i++)
    {
        char c = input[i];
        if (escaping)
        {
            if (c == 'n')
                output += '\n';
            else if (c == 'r')
                output += '\r';
            else if (c == 't')
                output += '\t';
            else
                output += c;
            escaping = false;
        }
        else if (c == '\\')
        {
            escaping = true;
        }
        else
        {
            output += c;
        }
    }
    return output;
}

String extractContentFromAiApi(String response)
{
    int key = response.indexOf("\"content\"");
    if (key < 0)
        return "";

    int colon = response.indexOf(':', key);
    int start = response.indexOf('"', colon + 1);
    if (colon < 0 || start < 0)
        return "";

    String encoded = "";
    bool escaping = false;
    for (int i = start + 1; i < response.length(); i++)
    {
        char c = response[i];
        if (escaping)
        {
            encoded += '\\';
            encoded += c;
            escaping = false;
        }
        else if (c == '\\')
        {
            escaping = true;
        }
        else if (c == '"')
        {
            break;
        }
        else
        {
            encoded += c;
        }
    }

    return jsonUnescape(encoded);
}

String extractJsonObject(String text)
{
    int start = text.indexOf('{');
    int end = text.lastIndexOf('}');
    if (start < 0 || end <= start)
        return "";
    return text.substring(start, end + 1);
}

String debugVisibleText(String text, size_t maxChars)
{
    String output = "";
    size_t limit = min(maxChars, text.length());
    for (size_t i = 0; i < limit; i++)
    {
        char c = text[i];
        if (c == '\n')
            output += "\\n";
        else if (c == '\r')
            output += "\\r";
        else if (c == '\t')
            output += "\\t";
        else if ((uint8_t)c < 32)
        {
            output += "\\x";
            if ((uint8_t)c < 16)
                output += "0";
            output += String((uint8_t)c, HEX);
        }
        else
            output += c;
    }

    if (text.length() > maxChars)
        output += "...<truncated>";

    return output;
}

void logAiResponseDebug(int status, String response)
{
    Serial.print("OpenRouter HTTP status: ");
    Serial.println(status);
    Serial.print("OpenRouter response length: ");
    Serial.println(response.length());
    Serial.print("OpenRouter response preview: [");
    Serial.print(debugVisibleText(response, AI_RESPONSE_LOG_MAX_CHARS));
    Serial.println("]");

    int choicesIdx = response.indexOf("\"choices\"");
    int contentIdx = response.indexOf("\"content\"");
    int errorIdx = response.indexOf("\"error\"");
    Serial.print("OpenRouter response indexes choices/content/error: ");
    Serial.print(choicesIdx);
    Serial.print("/");
    Serial.print(contentIdx);
    Serial.print("/");
    Serial.println(errorIdx);
}

void logAiErrorResponse(int status, String response)
{
    Serial.print("OpenRouter request failed: ");
    Serial.println(status);
    if (response.length() == 0)
    {
        Serial.println("OpenRouter error body: <empty>");
        return;
    }

    Serial.print("OpenRouter error body: ");
    if (response.length() > AI_ERROR_LOG_MAX_CHARS)
    {
        Serial.println(response.substring(0, AI_ERROR_LOG_MAX_CHARS) +
                       "...<truncated>");
    }
    else
    {
        Serial.println(response);
    }
}

void logAiUnparseableContent(String content, String response)
{
    if (content.length() > 0)
    {
        Serial.print("OpenRouter content without JSON object: ");
        if (content.length() > AI_CONTENT_LOG_MAX_CHARS)
        {
            Serial.println(content.substring(0, AI_CONTENT_LOG_MAX_CHARS) +
                           "...<truncated>");
        }
        else
        {
            Serial.println(content);
        }
        return;
    }

    Serial.print("OpenRouter response without content field: [");
    if (response.length() > AI_CONTENT_LOG_MAX_CHARS)
    {
        Serial.print(debugVisibleText(response, AI_CONTENT_LOG_MAX_CHARS));
        Serial.println("]");
    }
    else
    {
        Serial.print(debugVisibleText(response, AI_CONTENT_LOG_MAX_CHARS));
        Serial.println("]");
    }
}

String analyzeWithAiApi()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return buildFallbackAnalysisObjectJson(
            "Wi-Fi 未连接，无法调用 OpenRouter API。");
    }

    unsigned long now = millis();
    if (AI_REQUEST_COOLDOWN_MS > 0 && lastAiRequestMs > 0 &&
        now - lastAiRequestMs < AI_REQUEST_COOLDOWN_MS)
    {
        Serial.print("OpenRouter request skipped due to cooldown. Remaining ms: ");
        Serial.println(AI_REQUEST_COOLDOWN_MS - (now - lastAiRequestMs));
        return buildFallbackAnalysisObjectJson(
            "OpenRouter 请求冷却中，请等待几秒后再试。");
    }

    lastAiRequestMs = now;

    String base64Image = base64EncodeLatestImage();
    bool hasImage = base64Image.length() > 0;

    String prompt =
        "你是一名营养分析助手。请根据食物图片（如果提供）和重量生成中文营养分析。"
        "仅返回 JSON 对象，不要 markdown，不要额外解释。必须使用下面的精确字段名，"
        "nutrition 里的值必须都是数字，未知也要估算或填 0。结构必须为："
        "{\"foodType\":string,\"confidence\":number,\"nutrition\":{\"calories\":number,"
        "\"protein\":number,\"carbs\":number,\"fat\":number,\"fiber\":number,"
        "\"GI\":number,\"GL\":number},\"healthSuggestions\":string[],"
        "\"dishSuggestions\":string[]}。重量为 " +
        String(latestWeightGrams, 1) +
        " 克。用户画像 JSON：" + activeUserProfile +
        "。healthSuggestions 2-4 条，dishSuggestions 1-3 条。";

    if (!hasImage)
    {
        prompt += "当前图片过大或不可用，请明确说明识别置信度较低，并主要基于重量和用户画像给出建议。";
    }

    String payloadPrefix = "{\"model\":\"";
    String payloadSuffix =
        "\",\"messages\":[{\"role\":\"system\",\"content\":\"你是一名营养分析助手，请用中文回答，并且仅输出有效 JSON。\"},{\"role\":\"user\",\"content\":";
    if (hasImage)
    {
        payloadSuffix += "[{\"type\":\"text\",\"text\":\"";
        payloadSuffix += jsonEscape(prompt);
        payloadSuffix += "\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
        payloadSuffix += base64Image;
        payloadSuffix += "\"}}]";
    }
    else
    {
        payloadSuffix += "\"";
        payloadSuffix += jsonEscape(prompt);
        payloadSuffix += "\"";
    }

    payloadSuffix += "}],\"response_format\":{\"type\":\"json_object\"},\"temperature\":0.2,\"max_tokens\":700}";

    for (int attempt = 1; attempt <= 3; attempt++)
    {
        const char* model = attempt < 3 ? AI_MODEL : AI_BACKUP_MODEL;
        String payload = payloadPrefix + String(model) + payloadSuffix;
        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.setTimeout(60000);
        if (!http.begin(client, AI_API_URL))
        {
            return buildFallbackAnalysisObjectJson(
                "无法初始化 OpenRouter HTTPS 请求。");
        }

        http.addHeader("Authorization", "Bearer " + String(AI_API_KEY));
        http.addHeader("Content-Type", "application/json");
        http.addHeader("HTTP-Referer", "http://" + WiFi.localIP().toString());
        http.addHeader("X-Title", "NutriVision ESP32");
        http.addHeader("Connection", "close");

        Serial.print("Calling OpenRouter for nutrition analysis, attempt ");
        Serial.println(attempt);
        Serial.print("OpenRouter model: ");
        Serial.println(model);
        Serial.print("OpenRouter payload length: ");
        Serial.println(payload.length());
        Serial.print("OpenRouter image included: ");
        Serial.println(hasImage ? "yes" : "no");

        int status = http.POST(payload);
        String response = http.getString();
        http.end();
        logAiResponseDebug(status, response);

        if (status < 200 || status >= 300)
        {
            logAiErrorResponse(status, response);
            return buildFallbackAnalysisObjectJson("OpenRouter HTTP 状态码：" +
                                                   String(status));
        }

        String content = extractContentFromAiApi(response);
        Serial.print("OpenRouter content length: ");
        Serial.println(content.length());
        String analysis = extractJsonObject(content);
        Serial.print("OpenRouter extracted analysis length: ");
        Serial.println(analysis.length());
        if (analysis.length() > 0)
        {
            Serial.println("OpenRouter analysis completed");
            return analysis;
        }

        Serial.println("OpenRouter returned no parseable JSON analysis");
        logAiUnparseableContent(content, response);

        bool emptySuccessfulResponse =
            status >= 200 && status < 300 && content.length() == 0 &&
            response.indexOf("\"choices\"") < 0 &&
            response.indexOf("\"error\"") < 0;

        if (attempt < 3 && emptySuccessfulResponse)
        {
            if (attempt == 1)
            {
                Serial.println(
                    "Retrying primary OpenRouter model after empty successful response...");
            }
            else
            {
                Serial.println(
                    "Primary model returned empty twice; trying backup OpenRouter model...");
            }
            delay(700);
            continue;
        }

        return buildFallbackAnalysisObjectJson(
            "OpenRouter 返回内容无法解析为 JSON。");
    }

    return buildFallbackAnalysisObjectJson("OpenRouter 分析重试失败。");
}

void processPendingAnalysis()
{
    if (!pendingAiAnalysis || millis() < pendingAiStartMs)
        return;

    pendingAiAnalysis = false;
    Serial.println("Starting queued OpenRouter analysis...");
    latestAnalysisJson = analyzeWithAiApi();
    deviceState = "complete";
}

void resetForNextItem()
{
    pendingAiAnalysis = false;
    latestAnalysisJson = "";
    latestImageSize = 0;
    latestWeightGrams = 0;
    latestCaptureMs = 0;
    deviceState = "ready";

    if (latestImage != nullptr)
    {
        free(latestImage);
        latestImage = nullptr;
    }
}

void handleHealth()
{
    String json = "{\"status\":\"OK\",";
    json += "\"timestampMs\":" + String(millis()) + ",";
    json += "\"ipAddress\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + "}";
    server.send(200, "application/json", json);
}

void handleLatestAnalysis()
{
    if (deviceState == "ready" || latestImage == nullptr || latestImageSize == 0)
    {
        server.send(200, "application/json",
                    "{\"success\":false,\"status\":\"ready\",\"error\":\"设备就绪\"}");
        return;
    }

    server.send(200, "application/json", buildAnalysisResponseJson());
}

void handleGetProfile()
{
    server.send(200, "application/json",
                "{\"success\":true,\"data\":" + activeUserProfile + "}");
}

void handlePostProfile()
{
    String body = server.arg("plain");
    body.trim();
    if (body.length() == 0 || body[0] != '{')
    {
        server.send(400, "application/json",
                    "{\"success\":false,\"error\":\"Invalid profile payload\"}");
        return;
    }

    activeUserProfile = body;
    server.send(200, "application/json",
                "{\"success\":true,\"data\":" + activeUserProfile + "}");
}

void handleAnalyzeFood()
{
    float weight = scale.get_units();
    if (server.hasArg("weight"))
    {
        weight = server.arg("weight").toFloat();
    }

    if (!captureAndStoreImage(weight))
    {
        server.send(500, "application/json",
                    "{\"success\":false,\"error\":\"Camera error\"}");
        return;
    }

    server.send(200, "application/json", buildAnalysisResponseJson());
}

void handleNotFound()
{
    server.send(404, "application/json", "{\"error\":\"Route not found\"}");
}

// ***************************************************************************
// Arduino main functions

void setup()
{
    // ESP32 will have a window of 1 second after boot where serial is
    // unavailable.
    delay(2000);
    Serial.begin(115200);
    Serial.println("**************************************************");
    Serial.println("System Initializing...");

    setupCamera();
    setupWiFi();
    setupWebServer();
    setupScale();

    Serial.println("System Initialized successfully.");

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
    processPendingAnalysis();

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
    //  If weight is heavier than the threshold, capture an image and publish it
    //  through the ESP32-hosted web API.

    float         currentWeight = scale.get_units();
    unsigned long currentTime   = millis();

    // Check if weight exceeds threshold and debounce to prevent multiple
    // uploads
    if (currentWeight > AUTO_CAPTURE_THRESHOLD_GRAMS && !weightDetected &&
        !pendingAiAnalysis && deviceState != "processing" &&
        (currentTime - lastWeightCheck > WEIGHT_DEBOUNCE_TIME))
    {
        Serial.println("Weight exceeded threshold: " + String(currentWeight) +
                       "g, capturing image...");

        if (captureAndStoreImage(currentWeight))
        {
            weightDetected  = true;
            lastWeightCheck = currentTime;
            Serial.println("Image captured and published locally!");
        }
        else
        {
            Serial.println("Failed to capture image");
            lastWeightCheck = currentTime; // Still update to prevent spam
        }
    }

    // Reset weight detection when weight drops below threshold
    if (currentWeight < WEIGHT_REMOVED_THRESHOLD_GRAMS && weightDetected)
    {
        Serial.println("Weight removed, ready for next detection");
        weightDetected = false;
        resetForNextItem();
    }

    delay(500);
}
