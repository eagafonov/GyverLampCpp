#include <Arduino.h>

#if defined(ESP32)
#include <esp_wifi.h>
#include <WiFi.h>
#include <SPIFFS.h>
#define FLASHFS SPIFFS
#else
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#define FLASHFS LittleFS
#endif

#include <EEPROM.h>

#include "LocalDNS.h"
#include "MyMatrix.h"
#include "EffectsManager.h"
#include "Settings.h"
#include "TimeClient.h"

#include "GyverButton.h"
#include "LampWebServer.h"

#include "effects/Effect.h"

#include "Spectrometer.h"
#include "MqttClient.h"

namespace  {

uint16_t webServerPort = 80;

#if defined(ESP32)
const uint8_t btnPin = 15;
const GButton::PullType btnType = GButton::PullTypeLow;
#elif defined(SONOFF)
const uint8_t btnPin = 0;
const GButton::PullType btnType = GButton::PullTypeHigh;
const uint8_t relayPin = 12;
const uint8_t miniLedPin = 13;
#else
const uint8_t btnPin = 4;
const GButton::PullType btnType = GButton::PullTypeLow;
#endif
const GButton::DefaultState btnState = GButton::DefaultStateOpen;

GButton *button = nullptr;

int stepDirection = 1;
bool isHolding = false;

uint32_t logTimer = 0;

bool setupMode = false;
bool connectFinished = false;

void processMatrix()
{
    if (mySettings->generalSettings.working) {
        effectsManager->loop();
    } else {
        myMatrix->clear(true);
    }
}

void printFlashInfo()
{
    uint32_t ideSize = ESP.getFlashChipSize();
    FlashMode_t ideMode = ESP.getFlashChipMode();

    Serial.printf_P(PSTR("Flash ide  size: %u bytes\n"), ideSize);
    Serial.printf_P(PSTR("Flash ide speed: %u Hz\n"), ESP.getFlashChipSpeed());
    Serial.print(F("Flash ide mode:  "));
    Serial.println((ideMode == FM_QIO ? F("QIO") : ideMode == FM_QOUT ? F("QOUT") : ideMode == FM_DIO ? F("DIO") : ideMode == FM_DOUT ? F("DOUT") : F("UNKNOWN")));

#if defined(ESP8266)
    uint32_t realSize = ESP.getFlashChipRealSize();
    Serial.printf_P(PSTR("Flash real id:   %08X\n"), ESP.getFlashChipId());
    Serial.printf_P(PSTR("Flash real size: %u bytes\n\n"), realSize);
    if (ideSize != realSize) {
      Serial.println(F("Flash Chip configuration wrong!"));
    } else {
      Serial.println(F("Flash Chip configuration ok."));
    }
#endif

    Serial.print(F("Sketch size: "));
    Serial.println(ESP.getSketchSize());
    Serial.print(F("Sketch free: "));
    Serial.println(ESP.getFreeSketchSpace());

#if defined(ESP32)
    Serial.print(F("Total heap: "));
    Serial.println(ESP.getHeapSize());
    Serial.print(F("Min free heap: "));
    Serial.println(ESP.getMinFreeHeap());
    Serial.print(F("Max alloc heap: "));
    Serial.println(ESP.getMaxAllocHeap());
#endif
}

void printFreeHeap()
{
    static uint32_t s_counter = 0;
    Serial.print(++s_counter);
    Serial.print(F("_FreeHeap: "));
    Serial.println(ESP.getFreeHeap());
//    Serial.flush();
}

void processButton()
{
    if (mySettings->buttonSettings.pin == 255) {
        return;
    }
    button->tick();
    if (button->isSingle() && !mySettings->busy) {
        Serial.println(F("Single button"));
        mySettings->generalSettings.working = !mySettings->generalSettings.working;
        mySettings->saveLater();
        if (mqtt) {
            mqtt->update();
        }
        if (lampWebServer) {
            lampWebServer->update();
        }
    }
    if (!mySettings->generalSettings.working) {
        return;
    }
    if (button->isDouble() && !mySettings->busy) {
        Serial.println(F("Double button"));
        effectsManager->next();
    }
    if (button->isTriple() && !mySettings->busy) {
        Serial.println(F("Triple button"));
        effectsManager->previous();
    }
    if (button->isHolded() && !mySettings->busy) {
        Serial.println(F("Holded button"));
        isHolding = true;
        const uint8_t brightness = effectsManager->activeEffect()->settings.brightness;
        if (brightness <= 1) {
            stepDirection = 1;
        } else if (brightness == 255) {
            stepDirection = -1;
        }
        mySettings->saveLater();
        if (mqtt) {
            mqtt->update();
        }
        if (lampWebServer) {
            lampWebServer->update();
        }
    }
    if (isHolding && button->isStep()) {
        uint8_t brightness = effectsManager->activeEffect()->settings.brightness;
        if (stepDirection < 0 && brightness == 1) {
            return;
        }
        if (stepDirection > 0 && brightness == 255) {
            return;
        }
        brightness += stepDirection;
        Serial.printf_P(PSTR("Step button %d. brightness: %u\n"), stepDirection, brightness);
        effectsManager->activeEffect()->settings.brightness = brightness;
        myMatrix->setBrightness(brightness);
        mySettings->saveLater();
        if (mqtt) {
            mqtt->update();
        }
        if (lampWebServer) {
            lampWebServer->update();
        }
    }
    if (button->isRelease() && isHolding) {
        Serial.println(F("Release button"));
        isHolding = false;
    }
}

void setupSerial()
{
    Serial.begin(115200);
    Serial.println(F("\nHappy debugging!"));
    Serial.flush();
}

}

void clearWifi()
{
#if defined(ESP32)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    delay(2000);
    esp_wifi_restore();
#else
    WiFi.disconnect();
#endif
}

void setup() {
#if defined(ESP8266)
    ESP.wdtDisable();
    ESP.wdtEnable(0);
#endif

    delay(5000);

    setupSerial();
    printFlashInfo();
    printFreeHeap();

#if defined(ESP32)
    if(!FLASHFS.begin(true)) {
        Serial.println(F("An Error has occurred while mounting FLASHFS"));
        return;
    }
#else
    if(!FLASHFS.begin()) {
        Serial.println(F("An Error has occurred while mounting FLASHFS"));
        return;
    }
#endif

    Settings::Initialize();
// default values for button
    mySettings->buttonSettings.pin = btnPin;
    mySettings->buttonSettings.type = btnType;
    mySettings->buttonSettings.state = btnState;
    if (!mySettings->readSettings()) {
        mySettings->buttonSettings.pin = 255;
    }

    Serial.printf_P(PSTR("Button pin: %d\n"), mySettings->buttonSettings.pin);

    EffectsManager::Initialize();
    mySettings->readEffects();
    MyMatrix::Initialize();

#if defined(SONOFF)
    pinMode(relayPin, OUTPUT);
    pinMode(miniLedPin, OUTPUT);
#endif

    button = new GButton(mySettings->buttonSettings.pin,
                         mySettings->buttonSettings.type,
                         mySettings->buttonSettings.state);
    button->setTickMode(false);
    button->setStepTimeout(20);

    if (mySettings->generalSettings.working) {
        myMatrix->matrixTest();
    }

    button->tick();
    if (button->state()) {
        Serial.println(F("!!! Setup mode entered. No effects !!!"));
        myMatrix->setBrightness(80);
        myMatrix->fill(CRGB(0, 20, 0), true);
        setupMode = true;
        myMatrix->show();
        clearWifi();
    }

    LampWebServer::Initialize(webServerPort);
    if (setupMode) {
        lampWebServer->enterSetupMode();
    }

    Serial.println(F("AutoConnect started"));
    lampWebServer->onConnected([](bool isConnected) {
        if (connectFinished) {
            return;
        }
        Serial.println(F("AutoConnect finished"));
        if (isConnected) {
            LocalDNS::Initialize();
            if (localDNS->begin()) {
                localDNS->addService(F("http"), F("tcp"), webServerPort);
            } else {
                Serial.println(F("An Error has occurred while initializing mDNS"));
            }
            if (!setupMode) {
                TimeClient::Initialize();
                MqttClient::Initialize();
            }
        }

//    if (mySettings->generalSettings.soundControl) {
//        Spectrometer::Initialize();
//    }
        if (!setupMode) {
            effectsManager->activateEffect(mySettings->generalSettings.activeEffect, false);
        }
        connectFinished = true;
    });
    lampWebServer->autoConnect();
}

void loop() {
#if defined(ESP8266)
    ESP.wdtFeed();
#endif

    if (mySettings->generalSettings.logInterval > 0 && millis() - logTimer > mySettings->generalSettings.logInterval) {
        printFreeHeap();
        logTimer = millis();
    }

    lampWebServer->loop();

    if (!connectFinished) {
        return;
    }

    if (lampWebServer->isUpdating()) {
        return;
    }

    localDNS->loop();

    if (setupMode) {
        return;
    }

    if (lampWebServer->isConnected()) {
        timeClient->loop();
    }
    processButton();
#if defined(SONOFF)
    digitalWrite(relayPin, mySettings->generalSettings.working);
    digitalWrite(miniLedPin, mySettings->generalSettings.working);
#endif

//    if (mySettings->generalSettings.soundControl) {
//        mySpectrometer->loop();
//    }

    processMatrix();
    mySettings->loop();
}
