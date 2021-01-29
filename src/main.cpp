/*
The MIT License (MIT)

Copyright (c) 2020-2021 riraotech.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// red >AB Shutter3       , Address: ff:ff:3d:f9:98:73, appearance: 961, serviceUUID: 00001812-0000-1000-8000-00805f9b34fb ペリフェラル（BLEサーバー）
// blue>AB Shutter3       , Address: ff:ff:ae:c7:9c:96, appearance: 961, serviceUUID: 00001812-0000-1000-8000-00805f9b34fb　ペリフェラル（BLEサーバー）

#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <ESP32_SPIFFS_ShinonomeFNT.h>
#include <ESP32_SPIFFS_UTF8toSJIS.h>
#include <SerialTelnetBridge.h>
#include <esp32-hal-log.h>

#include <BLEDevice.h>
#include <BLEScan.h>

// The remote service we wish to connect to.
static BLEUUID serviceUUID("00001812-0000-1000-8000-00805f9b34fb"); //DISO AB Shutter3(red)
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID("00002a4d-0000-1000-8000-00805f9b34fb"); //DISO AB Shutter3(red)

static BLEAddress *pServerAddress;
static BLERemoteCharacteristic *pRemoteCharacteristic;

#define HOSTNAME "esp32_clock"
#define DIST_HOSTNAME "esp32"
#define MONITOR_SPEED 115200
#define AP_NAME "ESP32-G-AP"
#define MSG_CONNECTED "        WiFi Started."

//LED port settings
#define PORT_SE_IN 13
#define PORT_AB_IN 27
#define PORT_A3_IN 23
#define PORT_A2_IN 21
#define PORT_A1_IN 25
#define PORT_A0_IN 26
#define PORT_DG_IN 19
#define PORT_CLK_IN 18
#define PORT_WE_IN 17
#define PORT_DR_IN 16
#define PORT_ALE_IN 22

#define PANEL_NUM 2 //LED Panel
#define R 1         //red
#define O 2         //orange
#define G 3         //green

#define CLOCK_EN_S 6  //Start AM6:00 (set 24hour)
#define CLOCK_EN_E 23 //End   PM9:00 (set 24hour)

ESP32_SPIFFS_ShinonomeFNT SFR;
const char *UTF8SJIS_file = "/Utf8Sjis.tbl";
const char *Shino_Half_Font_file = "/shnm8x16.bdf"; //半角フォントファイル名
const char *dummy = "/";

DNSServer dns;
AsyncWebServer server(80);
AsyncWiFiManager wifiManager(&server, &dns);
const String APIURI("/esp/sensor/all");

Ticker clocker;
Ticker blinker;
Ticker checker;
Ticker sensor_checker;

//SerialTelnetBridgeClass inSTB;

DynamicJsonDocument doc(192); //store json body

//message ID
enum class MESSAGE
{
    MSG_COMMAND_NOTHING,
    MSG_COMMAND_SENSOR,
    MSG_COMMAND_TEMPERATURE,
    MSG_COMMAND_PRESSURE,
    MSG_COMMAND_HUMIDITY,
    MSG_COMMAND_CLOCK,
    MSG_COMMAND_BLE_INIT,
    MSG_COMMAND_BLE_DO_CONNECT,
    MSG_COMMAND_BLE_CONNECTED,
    MSG_COMMAND_BLE_DISCONNECTED,
    MSG_COMMAND_BLE_NOT_FOUND,
};

MESSAGE message = MESSAGE::MSG_COMMAND_NOTHING;

static uint8_t retry = 0;   //GET request retry count
static bool active = false; //clock check timer state

//log_v(format, ...); // verbose   5
//log_d(format, ...); // debug     4
//log_i(format, ...); // info      3
//log_w(format, ...); // warning   2
//log_e(format, ...); // error     1
//log_n(format, ...); // normal    0

//Write setting to LED Panel
void setRAMAdder(uint8_t lineNumber)
{
    uint8_t A[4] = {0};
    uint8_t adder = 0;

    adder = lineNumber;

    for (int i = 0; i < 4; i++)
    {
        A[i] = adder % 2;
        adder /= 2;
    }

    digitalWrite(PORT_A0_IN, A[0]);
    digitalWrite(PORT_A1_IN, A[1]);
    digitalWrite(PORT_A2_IN, A[2]);
    digitalWrite(PORT_A3_IN, A[3]);
}

void send_line_data(uint8_t iram_adder, uint8_t ifont_data[], uint8_t color_data[])
{
    uint8_t font[8] = {0};
    uint8_t tmp_data = 0;
    int k = 0;
    for (int j = 0; j < 4 * PANEL_NUM; j++)
    {
        tmp_data = ifont_data[j];
        for (int i = 0; i < 8; i++)
        {
            font[i] = tmp_data % 2;
            tmp_data /= 2;
        }

        for (int i = 7; i >= 0; i--)
        {
            digitalWrite(PORT_DG_IN, LOW);
            digitalWrite(PORT_DR_IN, LOW);
            digitalWrite(PORT_CLK_IN, LOW);

            if (font[i] == 1)
            {
                if (color_data[k] == R)
                {
                    digitalWrite(PORT_DR_IN, HIGH);
                }

                if (color_data[k] == G)
                {
                    digitalWrite(PORT_DG_IN, HIGH);
                }

                if (color_data[k] == O)
                {
                    digitalWrite(PORT_DR_IN, HIGH);
                    digitalWrite(PORT_DG_IN, HIGH);
                }
            }
            else
            {
                digitalWrite(PORT_DR_IN, LOW);
                digitalWrite(PORT_DG_IN, LOW);
            }

            delayMicroseconds(1);
            digitalWrite(PORT_CLK_IN, HIGH);
            delayMicroseconds(1);

            k++;
        }
    }
    //アドレスをポートに入力
    setRAMAdder(iram_adder);
    //ALE　Highでアドレスセット
    digitalWrite(PORT_ALE_IN, HIGH);
    //WE Highでデータを書き込み
    digitalWrite(PORT_WE_IN, HIGH);
    //WE Lowをセット
    digitalWrite(PORT_WE_IN, LOW);
    //ALE Lowをセット
    digitalWrite(PORT_ALE_IN, LOW);
}

void shift_bit_left(uint8_t dist[], uint8_t src[], int len, int n)
{
    uint8_t mask = 0xFF << (8 - n);
    for (int i = 0; i < len; i++)
    {
        if (i < len - 1)
        {
            dist[i] = (src[i] << n) | ((src[i + 1] & mask) >> (8 - n));
        }
        else
        {
            dist[i] = src[i] << n;
        }
    }
}

void shift_color_left(uint8_t dist[], uint8_t src[], int len)
{
    for (int i = 0; i < len * 8; i++)
    {
        if (i < len * 8 - 1)
        {
            dist[i] = src[i + 1];
        }
        else
        {
            dist[i] = 0;
        }
    }
}
////////////////////////////////////////////////////////////////////
void scrollLEDMatrix(int16_t sj_length, uint8_t font_data[][16], uint8_t color_data[], uint16_t intervals)
{
    uint8_t src_line_data[sj_length] = {0};
    uint8_t dist_line_data[sj_length] = {0};
    uint8_t tmp_color_data[sj_length * 8] = {0};
    uint8_t tmp_font_data[sj_length][16] = {0};
    uint8_t ram = LOW;

    int n = 0;
    for (int i = 0; i < sj_length; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            tmp_color_data[n++] = color_data[i];
        }

        for (int j = 0; j < 16; j++)
        {
            tmp_font_data[i][j] = font_data[i][j];
        }
    }

    for (int k = 0; k < sj_length * 8 + 2; k++)
    {
        ram = ~ram;
        digitalWrite(PORT_AB_IN, ram); //write to RAM-A/RAM-B
        for (int i = 0; i < 16; i++)
        {
            for (int j = 0; j < sj_length; j++)
            {
                src_line_data[j] = tmp_font_data[j][i];
            }

            send_line_data(i, src_line_data, tmp_color_data);
            shift_bit_left(dist_line_data, src_line_data, sj_length, 1);

            for (int j = 0; j < sj_length; j++)
            {
                tmp_font_data[j][i] = dist_line_data[j];
            }
        }
        shift_color_left(tmp_color_data, tmp_color_data, sj_length);
        delay(intervals);
    }
}

//Print static
void printLEDMatrix(uint16_t sj_length, uint8_t font_data[][16], uint8_t color_data[])
{
    uint8_t src_line_data[sj_length] = {0};
    uint8_t tmp_color_data[sj_length * 8] = {0};
    uint8_t tmp_font_data[sj_length][16] = {0};
    uint8_t ram = LOW;

    int n = 0;
    for (int i = 0; i < sj_length; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            tmp_color_data[n++] = color_data[i];
        }

        for (int j = 0; j < 16; j++)
        {
            tmp_font_data[i][j] = font_data[i][j];
        }
    }

    for (int k = 0; k < sj_length * 8 + 2; k++)
    {
        digitalWrite(PORT_AB_IN, ram); //write to RAM-A/RAM-B
        for (int i = 0; i < 16; i++)
        {
            for (int j = 0; j < sj_length; j++)
            {
                src_line_data[j] = tmp_font_data[j][i];
            }

            send_line_data(i, src_line_data, tmp_color_data);
        }
        ram = ~ram;
    }
}

void setAllPortOutput()
{
    pinMode(PORT_SE_IN, OUTPUT);
    pinMode(PORT_AB_IN, OUTPUT);
    pinMode(PORT_A3_IN, OUTPUT);
    pinMode(PORT_A2_IN, OUTPUT);
    pinMode(PORT_A1_IN, OUTPUT);
    pinMode(PORT_A0_IN, OUTPUT);
    pinMode(PORT_DG_IN, OUTPUT);
    pinMode(PORT_CLK_IN, OUTPUT);
    pinMode(PORT_WE_IN, OUTPUT);
    pinMode(PORT_DR_IN, OUTPUT);
    pinMode(PORT_ALE_IN, OUTPUT);
}

void setAllPortLow()
{
    //digitalWrite(PORT_SE_IN, LOW);
    digitalWrite(PORT_AB_IN, LOW);
    digitalWrite(PORT_A3_IN, LOW);
    digitalWrite(PORT_A2_IN, LOW);
    digitalWrite(PORT_A1_IN, LOW);
    digitalWrite(PORT_A0_IN, LOW);
    digitalWrite(PORT_DG_IN, LOW);
    digitalWrite(PORT_CLK_IN, LOW);
    digitalWrite(PORT_WE_IN, LOW);
    digitalWrite(PORT_DR_IN, LOW);
    digitalWrite(PORT_ALE_IN, LOW);
}

void setAllPortHigh()
{
    digitalWrite(PORT_SE_IN, HIGH);
    digitalWrite(PORT_AB_IN, HIGH);
    digitalWrite(PORT_A3_IN, HIGH);
    digitalWrite(PORT_A2_IN, HIGH);
    digitalWrite(PORT_A1_IN, HIGH);
    digitalWrite(PORT_A0_IN, HIGH);
    digitalWrite(PORT_DG_IN, HIGH);
    digitalWrite(PORT_CLK_IN, HIGH);
    digitalWrite(PORT_WE_IN, HIGH);
    digitalWrite(PORT_DR_IN, HIGH);
    digitalWrite(PORT_ALE_IN, HIGH);
}

void PrintTime(String &str, int flag)
{
    char tmp_str[10] = {0};
    struct tm *tm;

    time_t t = time(NULL);
    tm = localtime(&t);

    if (flag == 0)
    {
        sprintf(tmp_str, "  %02d:%02d ", tm->tm_hour, tm->tm_min);
    }
    else
    {
        sprintf(tmp_str, "  %02d %02d ", tm->tm_hour, tm->tm_min);
    }

    str = tmp_str;
}

void printTimeLEDMatrix()
{
    uint8_t time_font_buf[8][16] = {0};
    String str;

    static int flag = 0;

    flag = ~flag;
    PrintTime(str, flag);

    uint8_t time_font_color[8] = {O, O, G, G, O, G, G, O};
    uint16_t sj_length = SFR.StrDirect_ShinoFNT_readALL(str, time_font_buf);
    printLEDMatrix(sj_length, time_font_buf, time_font_color);
}

void blink()
{
    printTimeLEDMatrix();
}

void connecting()
{
    uint16_t sj_length = 0;
    uint8_t _font_buf[8][16] = {0};
    uint8_t _font_color[8] = {O, O, O, O, O, O, O, O};

    static int num = 0;

    num = ~num;

    if (num)
    {
        sj_length = SFR.StrDirect_ShinoFNT_readALL("        ", _font_buf);
        printLEDMatrix(sj_length, _font_buf, _font_color);
    }
    else
    {
        sj_length = SFR.StrDirect_ShinoFNT_readALL("       .", _font_buf);
        printLEDMatrix(sj_length, _font_buf, _font_color);
    }
}

void initWiFi()
{
    wifiManager.setDebugOutput(true);

    if (!wifiManager.autoConnect(AP_NAME))
    {
        ESP.restart();
    }

    blinker.detach();

    Serial.println("WiFi Started");

    uint16_t sj_length = 0;
    uint8_t font_buf[32][16] = {0};
    uint8_t font_color1[32] = {G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G};

    sj_length = SFR.StrDirect_ShinoFNT_readALL(MSG_CONNECTED, font_buf);
    scrollLEDMatrix(sj_length, font_buf, font_color1, 30);

    sj_length = SFR.StrDirect_ShinoFNT_readALL("        " + WiFi.localIP().toString(), font_buf);
    scrollLEDMatrix(sj_length, font_buf, font_color1, 30);
}

void print_blank()
{
    uint8_t _font_buf[8][16] = {0};
    uint8_t _font_color[8] = {G, G, G, G, G, G, G, G};
    printLEDMatrix(8, _font_buf, _font_color);
}

void clearLEDMatrix()
{
    print_blank();
    print_blank();
}

void printStatic(String str)
{
    uint8_t _font_buf[8][16] = {0};
    uint8_t _font_color[8] = {G, G, G, G, G, G, G, G};

    if (str.length() < 9)
    {
        log_i("str : %s", str.c_str());
        uint16_t sj_length = SFR.StrDirect_ShinoFNT_readALL(str, _font_buf);
        printLEDMatrix(sj_length, _font_buf, _font_color);
    }
    else
    {
        log_e("couldn't set string. string length : %d", str.length());
    }
}

void initLCDMatrix()
{
    setAllPortOutput();

    digitalWrite(PORT_SE_IN, HIGH); //to change manual mode
    print_blank();
    print_blank();

    blinker.attach_ms(500, connecting);
}

void initSerial()
{
    Serial.begin(MONITOR_SPEED);
}

void initOta()
{
    ArduinoOTA.onStart([]() {
        String type;

        print_blank();

        clocker.detach();
        checker.detach();
        sensor_checker.detach();

        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "sketch";
            printStatic("sketch..");
        }
        else
        {
            type = "filesystem";
            printStatic("spiffs..");
        }
        log_d("Start updating : %s\r\n", type.c_str());
    });

    ArduinoOTA.onEnd([]() {
        printStatic("Uploaded");
        log_d("End");
        delay(2000);
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        String str = String(progress / (total / 100));

        log_d("Progress: %s%%", str.c_str());
        log_printf("\033[1F");

        str += "%";
        str.trim();

        //printStatic(str);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        log_e("Error[%u]: ", error);
        printStatic("Error!!!");

        if (error == OTA_AUTH_ERROR)
        {
            log_e("Auth Failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            log_e("Begin Failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            log_e("Connect Failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            log_e("Receive Failed");
        }
        else if (error == OTA_END_ERROR)
        {
            log_e("End Failed");
        }
    });

    ArduinoOTA.setMdnsEnabled(true);
    ArduinoOTA.setHostname(HOSTNAME);

    log_d("- Hostname : %s.local", ArduinoOTA.getHostname().c_str());

    ArduinoOTA.begin();
    log_d("- OTA Started");
}

bool check_clock_enable(uint8_t start_hour, uint8_t end_hour)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    log_i("HH:MM:SS = %02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);

    if (start_hour <= tm->tm_hour && tm->tm_hour < end_hour)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void checkSensor()
{
    message = MESSAGE::MSG_COMMAND_SENSOR;
}

void stopClock()
{
    clocker.detach();
    sensor_checker.detach();
    active = false;
}

void startClock()
{
    clocker.attach_ms(500, blink);
    sensor_checker.attach(60, checkSensor);
    active = true;
}

void check_clock()
{
    bool IsClock = check_clock_enable(CLOCK_EN_S, CLOCK_EN_E);

    if (IsClock == true)
    {
        if (active == false)
        {
            startClock();
            active = true;
        }
    }
    else
    {
        if (active == true)
        {
            stopClock();
            clearLEDMatrix();
            active = false;
        }
    }
}

String getServerInfo(String hostName, String uri)
{
    String jsonBody;
    HTTPClient http;

    log_d("Starting connection to %s.local Web server...", hostName.c_str());

    IPAddress ip = MDNS.queryHost(hostName);
    log_i("Hostname : %s IPaddress : %s", hostName.c_str(), ip.toString().c_str());

    http.begin(ip.toString(), 80, uri);
    int httpCode = http.GET();

    if (httpCode < 0)
    {
        log_e("Connection failed! code : %d", httpCode);
        jsonBody = "";
    }
    else
    {
        log_i("Connected to server! code : %d", httpCode);
        jsonBody = http.getString();
    }

    if (http.connected())
    {
        http.end();
    }

    return jsonBody;
}

void initClock()
{
    //Get NTP Time
    configTzTime("JST-9", "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

    delay(2000);

    check_clock();

    checker.attach(60, check_clock);
    //sensor_checker.attach(60 * 15, checkSensor);
    sensor_checker.attach(60, checkSensor);
}

void printTemperature()
{
    String temperature;
    char buffer[10] = {0};

    float _temperature = doc["temperatur"]; // 21.93

    snprintf(buffer, 9, "T:%4.1f*C", _temperature);
    temperature = buffer;
    log_i("temperature : [%s]", temperature.c_str());

    printStatic(temperature);

    delay(5000);
}

void printPressur()
{
    String pressur;
    char buffer[10] = {0};

    float _pressur = doc["pressur"]; // 1015.944

    snprintf(buffer, 9, "P:%6.1f", _pressur);
    pressur = buffer;
    log_i("pressur : [%s]", pressur.c_str());

    printStatic(pressur);

    delay(5000);
}

void printHumidity()
{
    String humidity;
    char buffer[10] = {0};

    float _humidity = doc["humidity"]; // 39.27832

    snprintf(buffer, 9, "H:%5.1f%%", _humidity);
    humidity = buffer;
    log_i("humidity : [%s]", humidity.c_str());

    printStatic(humidity);

    delay(5000);
}

void initHttpClient()
{
    String json = getServerInfo(DIST_HOSTNAME, APIURI);
    log_d("Body = %s", json.c_str());
}

void initFont()
{
    SFR.SPIFFS_Shinonome_Init3F(UTF8SJIS_file, Shino_Half_Font_file, dummy);
}

void getBME280Info()
{
    String json = getServerInfo(DIST_HOSTNAME, APIURI);
    if (json.isEmpty())
    {
        if (retry < 2)
        {
            retry++;
            checkSensor();
            delay(3000);
            return;
        }
        else
        {
            retry = 0;
            message = MESSAGE::MSG_COMMAND_NOTHING;
            return;
        }
    }
    else
    {
        retry = 0;

        stopClock();

        log_d("Body = %s", json.c_str());
        deserializeJson(doc, json);

        message = MESSAGE::MSG_COMMAND_TEMPERATURE;
    }
}

static void notifyCallback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
    log_i("Notify callback for characteristic %s of data length %d", pBLERemoteCharacteristic->getUUID().toString().c_str(), length);
}

class MyClientCallback : public BLEClientCallbacks
{
    void onConnect(BLEClient *pclient)
    {
        log_i("onConnect");
        message = MESSAGE::MSG_COMMAND_BLE_CONNECTED;
    }

    void onDisconnect(BLEClient *pclient)
    {
        log_i("onDisconnect");
        message = MESSAGE::MSG_COMMAND_BLE_DISCONNECTED;
    }
};

bool connectToServer(BLEAddress pAddress)
{
    log_i("Forming a connection to %s", pAddress.toString().c_str());

    BLEClient *pClient = BLEDevice::createClient();
    log_i(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(pAddress);
    log_i(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr)
    {
        log_i("Failed to find our service UUID: %s", serviceUUID.toString().c_str());
        return false;
    }
    log_i(" - Found our service");

    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr)
    {
        log_i("Failed to find our characteristic UUID: %s", charUUID.toString().c_str());
        return false;
    }

    log_i(" - Found our characteristic");

    // Read the value of the characteristic.
    std::string value = pRemoteCharacteristic->readValue();
    log_i("The characteristic value was: %s", value.c_str());

    pRemoteCharacteristic->registerForNotify(notifyCallback);

    return true;
}

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
    /**
   * Called for each advertising BLE server.
   */
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {
        log_i("BLE Advertised Device found: %s", advertisedDevice.toString().c_str());

        // We have found a device, let us now see if it contains the service we are looking for.
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(serviceUUID))
        {
            log_i("Found our device!  address: %s", advertisedDevice.getAddress().toString().c_str());
            advertisedDevice.getScan()->stop();

            pServerAddress = new BLEAddress(advertisedDevice.getAddress());
            message = MESSAGE::MSG_COMMAND_BLE_DO_CONNECT;

        } // Found our server
        else
        {
            message = MESSAGE::MSG_COMMAND_BLE_NOT_FOUND;
        } // Not found our server
    }     // onResult
};        // MyAdvertisedDeviceCallbacks

static MyAdvertisedDeviceCallbacks *pAdvertisedDeviceCallback;

void initBLE()
{
    log_i("Starting Arduino BLE Client application...");

    BLEDevice::deinit();

    BLEDevice::init("");
    // Retrieve a Scanner and set the callback we want to use to be informed when we
    // have detected a new device.  Specify that we want active scanning and start the
    // scan to run for 30 seconds.
    BLEScan *pBLEScan = BLEDevice::getScan();
    pAdvertisedDeviceCallback = new MyAdvertisedDeviceCallbacks();

    pBLEScan->setAdvertisedDeviceCallbacks(pAdvertisedDeviceCallback);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(30); //waitting to find BLE server
}

void setup()
{
    initSerial();
    initFont();
    initLCDMatrix();
    initWiFi();
    initClock();
    initOta();
    initHttpClient();
    message = MESSAGE::MSG_COMMAND_BLE_INIT;
}

void loop()
{
    ArduinoOTA.handle();

    switch (message)
    {
    case MESSAGE::MSG_COMMAND_SENSOR:
        getBME280Info();
        break;
    case MESSAGE::MSG_COMMAND_TEMPERATURE:

        printTemperature();
        message = MESSAGE::MSG_COMMAND_HUMIDITY;
        break;
    case MESSAGE::MSG_COMMAND_HUMIDITY:

        printHumidity();
        message = MESSAGE::MSG_COMMAND_PRESSURE;
        break;
    case MESSAGE::MSG_COMMAND_PRESSURE:

        //printPressur();
        message = MESSAGE::MSG_COMMAND_CLOCK;
        break;
    case MESSAGE::MSG_COMMAND_CLOCK:

        startClock();
        message = MESSAGE::MSG_COMMAND_NOTHING;
        break;
    case MESSAGE::MSG_COMMAND_BLE_INIT:
        initBLE();
        break;
    case MESSAGE::MSG_COMMAND_BLE_DO_CONNECT:
        log_i("We wish to connect BLE Server. pServerAddress = 0x%x", pServerAddress);
        // We have scanned for and found the desired BLE Server with which we wish to connect.
        // Now we connect to it. Once we are connected we set "MSG_COMMAND_BLE_CONNECTED"
        if (connectToServer(*pServerAddress))
        {
            log_i("We are now connected to the BLE Server");
            message = MESSAGE::MSG_COMMAND_BLE_CONNECTED;
        }
        else
        {
            log_i("We have failed to connect to the server; there is nothing more we will do.");
            message = MESSAGE::MSG_COMMAND_BLE_DISCONNECTED;
        }
        break;
    case MESSAGE::MSG_COMMAND_BLE_CONNECTED:
        log_i("We are connected to a peer BLE Server");
        // If we are connected to a peer BLE Server, update the characteristic each time we are reached
        // with the current time since boot.

        //String newValue = "Time since boot: " + String(millis() / 1000);
        //log_i("Setting new characteristic value to \"%s\"", newValue.c_str());

        // Set the characteristic's value to be the array of bytes that is actually a string.
        //pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
        message = MESSAGE::MSG_COMMAND_NOTHING;
        break;
    case MESSAGE::MSG_COMMAND_BLE_DISCONNECTED:
        log_i("Disconnected our service");

        //TODO LED ON or OFF? To indicate for human.
        message = MESSAGE::MSG_COMMAND_BLE_INIT;
        break;
    case MESSAGE::MSG_COMMAND_BLE_NOT_FOUND:
        log_i("Not found our service");

        //TODO LED ON or OFF? To indicate for human.
        message = MESSAGE::MSG_COMMAND_NOTHING;
        break;
    case MESSAGE::MSG_COMMAND_NOTHING:
    default:; //nothing
    }

    yield();
}
