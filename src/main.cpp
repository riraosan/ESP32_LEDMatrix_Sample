/*
The MIT License (MIT)

Copyright (c) 2020-2021 riraosan.github.io

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

#define TS_ENABLE_SSL  // Don't forget it for ThingSpeak.h!!
#include <Arduino.h>
#include <AutoConnect.h>
#include <ESP32_SPIFFS_ShinonomeFNT.h>
#include <ESP32_SPIFFS_UTF8toSJIS.h>
#include <ESPUI.h>
#include <ThingSpeak.h>
#include <Ticker.h>
#include <esp32-hal-log.h>
#include <ledmatrix.h>
#include <secrets.h>
#include <timezone.h>

#define HOSTNAME      "esp32_clock"
#define MSG_CONNECTED "        WiFi Started."

#define CLOCK_EN_S    6   //Start AM 6:00
#define CLOCK_EN_E    23  //End   PM11:00

const String UTF8SJIS_FILE("/Utf8Sjis.tbl");
const String SHINO_HALF_FONT_FILE("/shnm8x16.bdf");  //半角フォントファイル名
const String DUMMY("");
const String APIURI("/esp/sensor/all");

const String sensors_all("/api/v1/devices/sensors/1/all");
const String sensors_temperature("/api/v1/devices/sensors/1/temperature");
const String sensors_humidity("/api/v1/devices/sensors/1/humidity");
const String sensors_pressure("/api/v1/devices/sensors/1/pressure");

Ticker clocker;
Ticker connectBlinker;
Ticker clockChecker;
Ticker sensorChecker;

StaticJsonDocument<384> doc;

ESP32_SPIFFS_ShinonomeFNT SFR;

AsyncWebServer *g_server;

uint16_t timeLabelId;
uint16_t temperatureLabelId;
uint16_t humidityLabelId;
uint16_t pressurLabelId;

//message ID
enum class MESSAGE {
    MSG_COMMAND_NOTHING,
    MSG_COMMAND_GET_SENSOR_DATA,
    MSG_COMMAND_PRINT_TEMPERATURE_VALUE,
    MSG_COMMAND_PRINT_PRESSURE_VALUE,
    MSG_COMMAND_PRINT_HUMIDITY_VALUE,
    MSG_COMMAND_START_CLOCK,
    MSG_COMMAND_STOP_CLOCK,
    MSG_COMMAND_BLE_INIT,
    MSG_COMMAND_BLE_DO_CONNECT,
    MSG_COMMAND_BLE_CONNECTED,
    MSG_COMMAND_BLE_DISCONNECTED,
    MSG_COMMAND_BLE_NOT_FOUND,
};

MESSAGE message = MESSAGE::MSG_COMMAND_NOTHING;

String makeCreateTime() {
    time_t t      = time(NULL);
    struct tm *tm = localtime(&t);

    char buffer[128] = {0};
    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d+0900",
            tm->tm_year + 1900,
            tm->tm_mon + 1,
            tm->tm_mday,
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec);

    log_i("[time] %s", String(buffer).c_str());

    return String(buffer);
}

void PrintTime(String &str, int flag) {
    char tmp_str[10] = {0};
    time_t t         = time(NULL);
    struct tm *tm    = localtime(&t);

    if (flag == 0) {
        sprintf(tmp_str, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        sprintf(tmp_str, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    }

    str = tmp_str;
}

void printTimeLEDMatrix() {
    static int flag = 0;
    String str;
    uint8_t time_font_buf[8][16] = {0};
    uint8_t time_font_color[8]   = {G, G, O, G, G, O, G, G};

    flag = ~flag;
    PrintTime(str, flag);

    uint16_t sj_length = SFR.StrDirect_ShinoFNT_readALL(str, time_font_buf);
    printLEDMatrix(sj_length, time_font_buf, time_font_color);
}

void blink() {
    printTimeLEDMatrix();
}

void connecting() {
    uint16_t sj_length       = 0;
    uint8_t _font_buf[8][16] = {0};
    uint8_t _font_color[8]   = {G, G, G, G, G, G, G, O};

    static int num = 0;

    num = ~num;

    if (num) {
        sj_length = SFR.StrDirect_ShinoFNT_readALL("init   .", _font_buf);
        printLEDMatrix(sj_length, _font_buf, _font_color);
    } else {
        sj_length = SFR.StrDirect_ShinoFNT_readALL("init    ", _font_buf);
        printLEDMatrix(sj_length, _font_buf, _font_color);
    }
}

void printConnected() {
    uint16_t sj_length       = 0;
    uint8_t font_buf[32][16] = {0};
    uint8_t font_color1[32]  = {G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G, G};

    sj_length = SFR.StrDirect_ShinoFNT_readALL(MSG_CONNECTED, font_buf);
    scrollLEDMatrix(sj_length, font_buf, font_color1, 30);

    sj_length = SFR.StrDirect_ShinoFNT_readALL("        " + WiFi.localIP().toString(), font_buf);
    scrollLEDMatrix(sj_length, font_buf, font_color1, 30);
}

void print_blank() {
    uint8_t _font_buf[8][16] = {0};
    uint8_t _font_color[8]   = {G, G, G, G, G, G, G, G};
    printLEDMatrix(8, _font_buf, _font_color);
}

void clearLEDMatrix() {
    print_blank();
    print_blank();
}

void printStatic(String str) {
    uint8_t _font_buf[8][16] = {0};
    uint8_t _font_color[8]   = {G, G, G, G, G, G, G, G};

    if (str.length() < 9) {
        log_i("str : %s", str.c_str());
        uint16_t sj_length = SFR.StrDirect_ShinoFNT_readALL(str, _font_buf);
        printLEDMatrix(sj_length, _font_buf, _font_color);
    } else {
        log_e("couldn't set string. string length : %d", str.length());
    }
}

void initFont() {
    SFR.SPIFFS_Shinonome_Init3F(UTF8SJIS_FILE.c_str(), SHINO_HALF_FONT_FILE.c_str(), DUMMY.c_str());
}

void initLEDMatrix() {
    initFont();

    setAllPortOutput();
    setAllPortLow();

    digitalWrite(PORT_SE_IN, HIGH);  //to change manual mode

    print_blank();
    print_blank();
}

bool check_clock_enable(uint8_t start_hour, uint8_t end_hour) {
    time_t t      = time(NULL);
    struct tm *tm = localtime(&t);

    log_i("HH:MM:SS = %02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);

    if (start_hour <= tm->tm_hour && tm->tm_hour < end_hour) {
        return true;
    } else {
        return false;
    }
}

void checkSensor() {
    message = MESSAGE::MSG_COMMAND_GET_SENSOR_DATA;
}

void stopClock() {
    clocker.detach();
}

void startClock() {
    clocker.attach_ms(500, blink);
}

void check_clock() {
    if (check_clock_enable(CLOCK_EN_S, CLOCK_EN_E)) {
        message = MESSAGE::MSG_COMMAND_START_CLOCK;
    } else {
        initLEDMatrix();
        stopClock();
        digitalWrite(PORT_LAMP, LOW);
    }
}

bool WaitSeconds(int second) {
    time_t t      = time(NULL);
    struct tm *tm = localtime(&t);

    if (tm->tm_sec == second) {
        return false;
    }

    return true;
}

void initClock() {
    //Get NTP Time
    configTzTime("JST-9", "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    delay(2000);

    while (WaitSeconds(0)) {
        delay(100);
        yield();
    }

    check_clock();
    clockChecker.attach(60, check_clock);

    while (WaitSeconds(30)) {
        delay(100);
        yield();
    }

    //sensorChecker.attach(60, checkSensor);
}

void printTemperature() {
    char buffer[10]    = {0};
    float _temperature = doc["temperatur"];  // 21.93
    sprintf(buffer, "T:%4.1f*C", _temperature);
    log_i("temperature : [%s]", String(buffer));

    printStatic(String(buffer));
}

void printPressure() {
    char buffer[10] = {0};
    float _pressure = doc["pressur"];  // 1015.944
    sprintf(buffer, "P:%6.1f", _pressure);
    log_i("pressure : [%s]", String(buffer).c_str());

    printStatic(String(buffer));
}

void printHumidity() {
    char buffer[10] = {0};
    float _humidity = doc["humidity"];  // 39.27832
    sprintf(buffer, "H:%5.1f%%", _humidity);
    log_i("humidity : [%s]", String(buffer).c_str());

    printStatic(String(buffer));
}

void selectHour(Control *sender, int value) {
    log_d("Select: ID: %d, Value: %d", sender->id, sender->value);
}

void selectMinuit(Control *sender, int value) {
    log_d("Select: ID: %d, Value: %d", sender->id, sender->value);
}

void initESPUI() {
    ESPUI.setVerbosity(Verbosity::Quiet);

    uint16_t hour = ESPUI.addControl(ControlType::Select, "Hour", "", ControlColor::Alizarin, Control::noParent, &selectHour);
    ESPUI.addControl(ControlType::Option, "6 am", "6", ControlColor::Alizarin, hour);
    ESPUI.addControl(ControlType::Option, "7 am", "7", ControlColor::Alizarin, hour);
    ESPUI.addControl(ControlType::Option, "8 am", "8", ControlColor::Alizarin, hour);

    uint16_t minuit = ESPUI.addControl(ControlType::Select, "Minuit", "", ControlColor::Alizarin, Control::noParent, &selectMinuit);
    ESPUI.addControl(ControlType::Option, "0", "0", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "10", "10", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "20", "20", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "30", "30", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "40", "40", ControlColor::Alizarin, minuit);
    ESPUI.addControl(ControlType::Option, "50", "50", ControlColor::Alizarin, minuit);

    ESPUI.begin("LAMP Alarm Clock Setting");
}

String getSensorDeviceName() {
    return "BME280";
}

const char HEX_CHAR_ARRAY[17] = "0123456789ABCDEF";
/**
* convert char array (hex values) to readable string by seperator
* buf:           buffer to convert
* length:        data length
* strSeperator   seperator between each hex value
* return:        formated value as String
*/
String byteToHexString(uint8_t *buf, uint8_t length, String strSeperator = "-") {
    String dataString = "";
    for (uint8_t i = 0; i < length; i++) {
        byte v = buf[i] / 16;
        byte w = buf[i] % 16;
        if (i > 0) {
            dataString += strSeperator;
        }
        dataString += String(HEX_CHAR_ARRAY[v]);
        dataString += String(HEX_CHAR_ARRAY[w]);
    }
    dataString.toUpperCase();
    return dataString;
}  // byteToHexString

String _getESP32ChipID() {
    uint64_t chipid;
    chipid          = ESP.getEfuseMac();  //The chip ID is essentially its MAC address(length: 6 bytes).
    int chipid_size = 6;
    uint8_t chipid_arr[chipid_size];
    for (uint8_t i = 0; i < chipid_size; i++) {
        chipid_arr[i] = (chipid >> (8 * i)) & 0xff;
    }
    return byteToHexString(chipid_arr, chipid_size, ":");
}

String makeJsonResponse(String selfApiURI, String nextApiURI, String status) {
    String response;

    StaticJsonDocument<384> _local;

    _local["cip_id"]     = _getESP32ChipID();
    _local["created_at"] = makeCreateTime();

    JsonObject _links = _local.createNestedObject("_links");

    _links["self"]["href"] = selfApiURI;
    _links["next"]["href"] = nextApiURI;

    JsonObject _embedded_sensor_1     = _local["_embedded"]["sensor"].createNestedObject("1");
    _embedded_sensor_1["device_name"] = getSensorDeviceName();
    _embedded_sensor_1["temperature"] = doc["temperatur"];
    _embedded_sensor_1["humidity"]    = doc["humidity"];
    _embedded_sensor_1["pressure"]    = doc["pressur"];
    _embedded_sensor_1["status"]      = status;

    serializeJson(_local, response);

    return response;
}

void initWebServer() {
    if (g_server != nullptr) {
        makeJsonResponse("", "", "");

        g_server->on(sensors_all.c_str(), HTTP_GET, [](AsyncWebServerRequest *request) {
            log_d("[HTTP_GET] %s", sensors_all.c_str());
            String response = makeJsonResponse(sensors_all, sensors_temperature, "online");
            request->send(200, "application/json; charset=UTF-8", response);
        });

        g_server->on(sensors_temperature.c_str(), HTTP_GET, [](AsyncWebServerRequest *request) {
            log_d("[HTTP_GET] %s", sensors_temperature.c_str());

            request->send(200, "application/json; charset=UTF-8", "{\"code\": 200}");
        });

        g_server->on(sensors_humidity.c_str(), HTTP_GET, [](AsyncWebServerRequest *request) {
            log_d("[HTTP_GET] %s", sensors_humidity.c_str());

            request->send(200, "application/json; charset=UTF-8", "{\"code\": 200}");
        });

        g_server->on(sensors_pressure.c_str(), HTTP_GET, [](AsyncWebServerRequest *request) {
            log_d("[HTTP_GET] %s", sensors_pressure.c_str());

            request->send(200, "application/json; charset=UTF-8", "{\"code\": 200}");
        });
    }
}

void setup() {
    initLEDMatrix();

    connectBlinker.attach_ms(500, connecting);

    initClock();
    initESPUI();

    connectBlinker.detach();

    print_blank();
    print_blank();
}

void loop() {
    switch (message) {
        case MESSAGE::MSG_COMMAND_GET_SENSOR_DATA:

            message = MESSAGE::MSG_COMMAND_NOTHING;
            break;
        case MESSAGE::MSG_COMMAND_PRINT_TEMPERATURE_VALUE:

            printTemperature();
            delay(3000);
            message = MESSAGE::MSG_COMMAND_PRINT_HUMIDITY_VALUE;
            break;
        case MESSAGE::MSG_COMMAND_PRINT_HUMIDITY_VALUE:

            printHumidity();
            delay(3000);
            message = MESSAGE::MSG_COMMAND_PRINT_PRESSURE_VALUE;
            break;
        case MESSAGE::MSG_COMMAND_PRINT_PRESSURE_VALUE:

            printPressure();
            delay(3000);
            message = MESSAGE::MSG_COMMAND_START_CLOCK;
            break;
        case MESSAGE::MSG_COMMAND_START_CLOCK:
            digitalWrite(PORT_LAMP, HIGH);
            startClock();
            message = MESSAGE::MSG_COMMAND_NOTHING;
            break;
        case MESSAGE::MSG_COMMAND_STOP_CLOCK:

            stopClock();
            message = MESSAGE::MSG_COMMAND_PRINT_TEMPERATURE_VALUE;
            break;
#ifdef ESP32_BLE
        case MESSAGE::MSG_COMMAND_BLE_INIT:
            initBLE();
            break;
        case MESSAGE::MSG_COMMAND_BLE_DO_CONNECT:
            log_i("We wish to connect BLE Server. pServerAddress = 0x%x", pServerAddress);
            // We have scanned for and found the desired BLE Server with which we wish to connect.
            // Now we connect to it. Once we are connected we set "MSG_COMMAND_BLE_CONNECTED"
            if (connectToServer(*pServerAddress)) {
                log_i("We are now connected to the BLE Server");
                message = MESSAGE::MSG_COMMAND_BLE_CONNECTED;
            } else {
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
#endif
        case MESSAGE::MSG_COMMAND_NOTHING:
        default:;  //nothing
    }

    yield();
}
