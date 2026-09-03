#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include "Led_PWM.h"

#include "Led_Driver_IC.h"
#include <esp_gap_ble_api.h>
#include <esp_bt_defs.h>
#include "BLEOTA.h"

#include "flashz.hpp"
#include "OTA.h"
#include <Preferences.h>

#define ENABLE_DEBUG  // Comment this to disable debug prints

#ifdef ENABLE_DEBUG
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif


// UUIDs
#define SERVICE_UUID           "cea51ba3-b9a8-4f36-b1b6-bba5c1572fc5"
#define FAN_CHAR_UUID          "42c6a920-c211-4708-8296-82a28ef60ade"
#define RGB_CHAR_UUID          "61cdd6ff-c436-4c9d-8927-27d79e6ef910"
#define DUTY_CHAR_UUID         "f5045f2f-121b-4d34-bb28-882b0d83fbbf"
#define LED_CTRL_UUID          "4e255541-b445-4901-b184-bada894f7e34"
#define OTA_CONTROL_CHAR_UUID  "35ccb38b-37c6-43be-a90e-7f34be91cde8"
#define DEVICE_STATUS          "cc685f2b-0a4b-4657-a2a1-b3f62c55c99c"


#define ZERO                    0
#define ON                      1


const char pub_key[] =
"-----BEGIN PUBLIC KEY-----\n"
"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEApzV7vRKNlb7W8Er9H0tQ\n"
"mDL7bKX7mAeVrqP49lJ92vKyQmE28SnNZx2hblGmCIq5++EIhVGrdkMd51XrHq//\n"
"Iz9xsgPQblbQPsbHNt8IcYnqBBjncOhVmuDYin+9bNrIExCQhgi8WLi9VMOfcImL\n"
"/bdPzaCyvYjlI0Qfv8bCDEIJoh9GKcBRREShasqNNZfxHZfqM/DO3NLSuNXDEffi\n"
"bh1RrL55ppK39xKh0fHSCsrMtPEJVFY7/h7xUb/Q31Od8v3rp8lWLF04Y+dGQVjP\n"
"zpvU7VKNEzzLtaDAGkMbWbFDWzxtDzfVmJ5pP0LaTUHQ69oCwiPISYC8c5cY8iNt\n"
"NwIDAQAB\n"
"-----END PUBLIC KEY-----\n";

bool deviceConnected = false;
bool oldDeviceConnected = false;

BLEServer* pServer = NULL;

BLECharacteristic *fanChar;
BLECharacteristic *rgbChar;
BLECharacteristic *dutyChar;
BLECharacteristic *ledCtrlChar;
BLECharacteristic *deviceStatusChar;
BLECharacteristic *passwordChar;
bool isDeviceStarted = false;

String savedPassword = "123456";  // default

Preferences preferences;
bool authenticated = false;
  
// Store parsed duty cycle values
int dutyR = 0, dutyG = 0, dutyB = 0, dutyW1=0, dutyW2=0;
String ledCtrlMode = "OLD";  

class DeviceStatusCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();
    Serial.print("Received DEVICE_STATUS: ");
    DEBUG_PRINTLN(value);
    if (value == "START") {
      isDeviceStarted = true;
      DEBUG_PRINTLN("Device STARTED");
    } else if (value == "STOP") {
      isDeviceStarted = false;
      DEBUG_PRINTLN("Device STOPPED – Forcing OFF");
      ledcWrite(FAN1_PWM, 0);
      ledcWrite(FAN2_PWM, 0);
      ledcWrite(RED_PWM, 0);
      ledcWrite(GREEN_PWM, 0);
      ledcWrite(BLUE_PWM, 0);
      ledcWrite(W_PWM, 0);
      ledcWrite(WW_PWM, 0);
      setAllColor(0, 0, 0, 0, 0);
      sendWS2805(colors, TOTAL_BYTES);

    }
  }
};

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    DEBUG_PRINTLN("Client connected"); 
  }

  void onDisconnect(BLEServer* pServer) {
      ledcWrite(FAN1_PWM, 0);
      ledcWrite(FAN2_PWM, 0);
      ledcWrite(RED_PWM, 0);
      ledcWrite(GREEN_PWM, 0);
      ledcWrite(BLUE_PWM, 0);
      ledcWrite(W_PWM, 0);
      ledcWrite(WW_PWM, 0);
      setAllColor(0, 0, 0, 0, 0);
      sendWS2805(colors, TOTAL_BYTES);

    DEBUG_PRINTLN("Client disconnected.");
    BLEDevice::startAdvertising();
  }
};
      
class RGBCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
      if (!isDeviceStarted) return;
  if (rxValue.length() > 0)  {
      Serial.print("Received RGB Value: ");
      DEBUG_PRINTLN(rxValue.c_str());
     
      if (ledCtrlMode == "NEW") {
          ledcWrite(RED_PWM, 0);
          ledcWrite(GREEN_PWM, 0);
          ledcWrite(BLUE_PWM, 0);
          ledcWrite(W_PWM, 0);
          ledcWrite(WW_PWM, 0);
        int r, g, b, w1, w2;
        int parsed = sscanf(rxValue.c_str(), "rgb(%d, %d, %d, %d, %d)", &r, &g, &b, &w1, &w2);
        if (parsed == 5) 
        {
          setAllColor(r, g, b, w1, w2);
          sendWS2805(colors, TOTAL_BYTES);  
          Serial.printf("Parsed RGB => R:%d G:%d B:%d W1:%d W2:%d\n", r, g, b, w1, w2);
        } 
        else
        {
          Serial.printf("RGB parsing failed. Parsed values: %d\n", parsed);
        }
      }
    }
  }
};

class DutyCycleCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
   String rxValue = pCharacteristic->getValue();
     if (!isDeviceStarted) return;
    if (rxValue.length() > 0)  {
      Serial.print("Received DutyCycle: ");
      DEBUG_PRINTLN(rxValue.c_str());
      if (ledCtrlMode == "OLD") {
        setAllColor(0, 0, 0, 0, 0);
        sendWS2805(colors, TOTAL_BYTES);
        int r, g, b, w1, w2;
        int parsed = sscanf(rxValue.c_str(), "duty(%d, %d, %d, %d, %d)", &r, &g, &b, &w1, &w2);
        if (parsed == 5) {
          dutyR = r;
          dutyG = g;
          dutyB = b;
          dutyW1 = w1;
          dutyW2 = w2;

          ledcWrite(RED_PWM, dutyR);
          ledcWrite(GREEN_PWM, dutyG);
          ledcWrite(BLUE_PWM, dutyB);
          ledcWrite(W_PWM, dutyW1);
          ledcWrite(WW_PWM, dutyW2);

          Serial.printf("Parsed Duty => R:%d G:%d B:%d W1:%d W2:%d\n", r, g, b, w1, w2);
        } else {
          Serial.printf("DutyCycle parsing failed. Parsed values: %d\n", parsed);
        }
      }
    }
  }
};

class FanCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();
    value.trim();
  if (!isDeviceStarted) return;
    Serial.print("Received Fan Command: ");
    DEBUG_PRINTLN(value);
   
    int commaIndex = value.indexOf(',');
    if (commaIndex == -1) {
      DEBUG_PRINTLN("Invalid input. Format must be FAN1,FAN2 (e.g., ON,OFF or 100,50)");
      return;
    }

    String fan1Str = value.substring(0, commaIndex);
    String fan2Str = value.substring(commaIndex + 1);

    fan1Str.trim();
    fan2Str.trim();

    // FAN1 Logic
    if (fan1Str.equalsIgnoreCase("ON")) {
      ledcWrite(FAN1_PWM, 255);
      DEBUG_PRINTLN("Fan1 ON (100%)");
    } else if (fan1Str.equalsIgnoreCase("OFF")) {
      ledcWrite(FAN1_PWM, 0);
      DEBUG_PRINTLN("Fan1 OFF (0%)");
    } else {
      int duty1 = fan1Str.toInt();
      if (duty1 >= 0 && duty1 <= 100) {
        int pwm1 = (duty1 * 255) / 100;
        ledcWrite(FAN1_PWM, pwm1);
        Serial.printf("Fan1 set to %d%% -> PWM: %d\n", duty1, pwm1);
      } else {
        DEBUG_PRINTLN("Invalid FAN1 value. Use OFF, ON or 0–100.");
      }
    }
    if(fan2Str.equalsIgnoreCase("ON")) {
      ledcWrite(FAN2_PWM, 255);
      DEBUG_PRINTLN("Fan2 ON (100%)");
    } else if (fan2Str.equalsIgnoreCase("OFF")) {
      ledcWrite(FAN2_PWM, 0);
      DEBUG_PRINTLN("Fan2 OFF (0%)");
    } else {
      int duty2 = fan2Str.toInt();
          if (duty2 >= 0 && duty2 <= 100) {
        int pwm2 = (duty2 * 255) / 100; 
        ledcWrite(FAN2_PWM, pwm2);
        Serial.printf("Fan2 set to %d%% -> PWM: %d\n", duty2, pwm2);
      } else {
        DEBUG_PRINTLN("Invalid FAN2 value. Use OFF, ON or 0–100.");
      }
    }
  }
};


// class LEDCtrlCallbacks : public BLECharacteristicCallbacks {
//   void onWrite(BLECharacteristic *pCharacteristic) {
//     String value = pCharacteristic->getValue();
//      if (!isDeviceStarted) return;
//      if (value.length() > 0) {
//       Serial.print("Received LED Ctrl Mode: ");
//       DEBUG_PRINTLN(value.c_str());
//        if (!isDeviceStarted) return;
//       if (value == "OLD" || value == "NEW") {
//         ledCtrlMode = String(value.c_str());
//         DEBUG_PRINT("LED Control Mode set to: ");
//         DEBUG_PRINTLN(ledCtrlMode);
//       } else {
//         DEBUG_PRINTLN("Invalid LED Ctrl Mode!");
//       }
//     }
//   }
// };  

class LEDCtrlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    if (!isDeviceStarted) return;
    String value = pCharacteristic->getValue();
    value.trim();
    value.toUpperCase();
    if (value.length() == 0) return;
    Serial.print("Received LED Ctrl Mode: ");
    DEBUG_PRINTLN(value);
    if (value == "OLD") {

  ledCtrlMode = "OLD";
  DEBUG_PRINT("LED Control Mode set to: ");
  DEBUG_PRINTLN(ledCtrlMode);

  // OLD mode logic
  int r = 0, g = 0, b = 0, w1 = 0, w2 = 0;

  DEBUG_PRINTF(
    "setAllColor called with -> R:%d G:%d B:%d W1:%d W2:%d\n",
    r, g, b, w1, w2
  );

  setAllColor(r, g, b, w1, w2);
  sendWS2805(colors, TOTAL_BYTES);
}

else if (value == "NEW") {

  ledCtrlMode = "NEW";
  DEBUG_PRINT("LED Control Mode set to: ");
  DEBUG_PRINTLN(ledCtrlMode);

  int red = 0;
  int green = 0;
  int blue = 0;
  int w = 0;
  int ww = 0;

  DEBUG_PRINTF(
    "ledcWrite values -> RED:%d GREEN:%d BLUE:%d W:%d WW:%d\n",
    red, green, blue, w, ww
  );

  ledcWrite(RED_PWM, red);
  ledcWrite(GREEN_PWM, green);
  ledcWrite(BLUE_PWM, blue);
  ledcWrite(W_PWM, w);
  ledcWrite(WW_PWM, ww);
}

    else {
      DEBUG_PRINTLN("Invalid LED Ctrl Mode!");
    }
  }
};

// 
class OTACallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {

    String value = pCharacteristic->getValue();
    value.trim();

    DEBUG_PRINT("OTA write received. DeviceStarted = ");
    DEBUG_PRINTLN(isDeviceStarted);

    if (isDeviceStarted) {
      DEBUG_PRINTLN("OTA blocked: Device is running");
      return;
    }
    DEBUG_PRINT("Received OTA Command: ");  
    DEBUG_PRINTLN(value);

    if (!isDeviceStarted){
        BLEOTA.process();
    } 
   
  }
};




void setup() {
  Serial.begin(115200);
  DEBUG_PRINTLN("Starting BLE Server...");
  Led_PWM_Init();

  setupRMT(); 

  preferences.begin("BLE", true);
  savedPassword = preferences.getString("pass", "123456").c_str();
  preferences.end();
  DEBUG_PRINT("Loaded Password: ");
  DEBUG_PRINTLN(savedPassword.c_str());

  BLEDevice::init("ESP32 BLE Server");

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  pSecurity->setCapability(ESP_IO_CAP_OUT); // Display only
  pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  // pSecurity->setStaticPIN(123456);

  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

BLEOTA.begin(pServer, true);  // true = secure OTA
BLEOTA.setKey(pub_key, strlen(pub_key));  // Set OTA public key

#ifdef MODEL
  BLEOTA.setModel(MODEL);
#endif
#ifdef SERIAL_NUM
  BLEOTA.setSerialNumber(SERIAL_NUM);
#endif
#ifdef FW_VERSION
  BLEOTA.setFWVersion(FW_VERSION);
#endif
#ifdef HW_VERSION
  BLEOTA.setHWVersion(HW_VERSION);
#endif
#ifdef MANUFACTURER
  BLEOTA.setManufactuer(MANUFACTURER);
#endif

  BLEOTA.init();

  // Fan Characteristic
  fanChar = pService->createCharacteristic(
    FAN_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  ); 
  fanChar->setCallbacks(new FanCallbacks());
  fanChar->setValue("OFF");

  // RGB Color Characteristic
  rgbChar = pService->createCharacteristic(
  RGB_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  rgbChar->setCallbacks(new RGBCallbacks());
  rgbChar->setValue("(255, 0, 0, 0, 0)");

  // Duty Cycle Characteristic
  dutyChar = pService->createCharacteristic(
    DUTY_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  dutyChar->setCallbacks(new DutyCycleCallbacks());
  dutyChar->setValue("(100, 0, 0, 0 ,0)");

  // LED Control Method
  ledCtrlChar = pService->createCharacteristic(
    LED_CTRL_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  ledCtrlChar->setCallbacks(new LEDCtrlCallbacks());
  ledCtrlChar->setValue("OLD");

  deviceStatusChar = pService->createCharacteristic(
  DEVICE_STATUS,
  BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );
  deviceStatusChar->setCallbacks(new DeviceStatusCallbacks());
  deviceStatusChar->setValue("stop");


BLECharacteristic* otaControlChar = pService->createCharacteristic(
  OTA_CONTROL_CHAR_UUID,
  BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
);
otaControlChar->setCallbacks(new OTACallbacks());
otaControlChar->setValue("STOP");

  // Start service and advertising
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);    
  pAdvertising->addServiceUUID(BLEOTA.getBLEOTAuuid());  // OTA SERVICE UUID to advertisement
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  DEBUG_PRINTLN("Waiting for client...");

}

void loop()
{
    if (!deviceConnected && oldDeviceConnected) {
      DEBUG_PRINTLN("BLE DISCONNECTED");
      ledcWrite(FAN1_PWM, 0);
      ledcWrite(FAN2_PWM, 0);
      ledcWrite(RED_PWM, 0);
      ledcWrite(GREEN_PWM, 0);
      ledcWrite(BLUE_PWM, 0);
      ledcWrite(W_PWM, 0);
      ledcWrite(WW_PWM, 0);
      setAllColor(0, 0, 0, 0, 0); 
      sendWS2805(colors, TOTAL_BYTES);

      delay(500); 
      pServer->startAdvertising();
      DEBUG_PRINTLN("start advertising");
      oldDeviceConnected = deviceConnected;
  }  

  if (deviceConnected && !oldDeviceConnected)
  {
     DEBUG_PRINTLN("BLE CONNECTED");
     oldDeviceConnected = deviceConnected;
  }
  
}
