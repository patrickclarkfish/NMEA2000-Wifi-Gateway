// NMEA2000-Wifi-Gateway is an ESP32 based NMEA to wifi gateway specifically designed to work with iNavX
// Copyright 2020 Patrick Clark. All rights reserved.
// Licensed under GNU GPL-3.0-only

/***************************************************************************
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
***************************************************************************/


#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <ArduinoOTA.h>
#include <Stream.h>
#include <string.h>

#define ARDUINO_ARCH_ESP32

// set the n2kSourceId to a unique number on the NMEA2000 bus
const uint8_t n2kSourceId = 36;
#define ESP32_CAN_TX_PIN GPIO_NUM_21
#define ESP32_CAN_RX_PIN GPIO_NUM_22

// Requires the following libraries:
// https://github.com/ttlappalainen/NMEA2000
// https://github.com/ttlappalainen/NMEA2000_esp32

#include <NMEA2000_CAN.h>
#include <N2kMessages.h>
#include <N2kMessagesEnumToStr.h>
#include <Seasmart.h>

void HandleN2kMsg(const tN2kMsg &N2kMsg);

#define MAX_SRV_CLIENTS 4

WiFiServer wifiServ(2114);
WiFiClient serverClients[MAX_SRV_CLIENTS];

void setup() {

  Serial.begin(115200);
  while (!Serial) 
    delayMicroseconds(200000);
  delayMicroseconds(2000000);

  Serial.println();
  Serial.println("NMEA2000-Wifi-Gateway Copyright (C) 2020 Patrick Clark");
  Serial.println("This program comes with ABSOLUTELY NO WARRANTY.");
  Serial.println("This is free software, and you are welcome to redistribute it under certain conditions.");
  Serial.println("See GNU GPL-3.0 at https://www.gnu.org/licenses/ or the LICENSE file included with this software for details.");
  Serial.println();
  Serial.print( F("Heap: ") ); Serial.println(ESP.getFreeHeap());
  Serial.print( F("Boot Vers: ") ); Serial.println(esp_get_idf_version());
  Serial.print( F("CPU: ") ); Serial.println(ESP.getCpuFreqMHz());
  Serial.print( F("SDK: ") ); Serial.println(ESP.getSdkVersion());
  Serial.print( F("Chip ID: ") ); Serial.printf("%X\n",ESP.getEfuseMac());
  Serial.print( F("Flash Size: ") ); Serial.println(ESP.getFlashChipSize());
  Serial.println();

  ArduinoOTA.onStart([]() {
    Serial.println(F("Start"));
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("\nEnd"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
  });

  delay(2000);

  Serial.println(F("Starting N2k/CAN BUS..."));
   
  Stream *ForwardStream = &Serial;

  // Do not forward bus messages at all
  NMEA2000.SetForwardType(tNMEA2000::fwdt_Text);
  NMEA2000.SetForwardStream(ForwardStream);
  NMEA2000.SetMode(tNMEA2000::N2km_ListenOnly,n2kSourceId);
  // Set false below, if you do not want to see messages parsed to HEX withing library
  NMEA2000.EnableForward(false);
  NMEA2000.SetForwardOnlyKnownMessages(false);
  NMEA2000.SetMsgHandler(HandleN2kMsg);
  //NMEA2000.SetN2kCANMsgBufSize(2);
  NMEA2000.Open();

  Serial.println(F("Starting WiFi..."));
  
  Serial.printf("ESP32 Chip id = %08X\n", ESP.getEfuseMac());
  char hostname_and_ssid[20];
  sprintf(hostname_and_ssid,"Mahalo-NMEA2k",ESP.getEfuseMac());
  Serial.print(F("Setting Hostname & SSID = "));Serial.println(hostname_and_ssid);
  WiFi.softAP(hostname_and_ssid, "forscience");
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  
  delay(2000);

  Serial.println("Starting NMEA server...");
  wifiServ.begin();
  wifiServ.setNoDelay(true);

  Serial.print("Running...\n");

  ArduinoOTA.begin();
}

//*****************************************************************************

//NMEA 2000 message handler
void HandleN2kMsg(const tN2kMsg &N2kMsg) 
{
  // SeaSmart.NET protocol N2K output
  // https://digitalmarinegauges.com/content/SeaSmart/SeaSmart_HTTP_Protocol_RevH.pdf
  char strBuf[500];

  size_t valid = N2kToSeasmart(N2kMsg, millis(), strBuf, sizeof(strBuf));

  if(valid)
  {
    for(int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
      if(serverClients[i] && serverClients[i].connected())
        serverClients[i].println(String(strBuf));
    }
  }
  else Serial.println("Invalid N2k packet");
}


//*****************************************************************************

void loop() {
  NMEA2000.ParseMessages();
  ArduinoOTA.handle();
  
  uint8_t i;
  if (true) //wifiMulti.run() == WL_CONNECTED) 
  {
    //check if there are any new clients
    if (wifiServ.hasClient()){
      Serial.println("Client available!");
      for(i = 0; i < MAX_SRV_CLIENTS; i++){
        //find free/disconnected spot
        if (!serverClients[i] || !serverClients[i].connected()){
          if(serverClients[i]) serverClients[i].stop();
          serverClients[i] = wifiServ.available();
          if (!serverClients[i]) Serial.println("available broken");
          Serial.print("New client: ");
          Serial.print(i); Serial.print(' ');
          Serial.println(serverClients[i].remoteIP());
          break;
        }
      }
      if (i >= MAX_SRV_CLIENTS) {
        Serial.println("No spots available, rejected!");
        //no free/disconnected spot so reject
        wifiServ.available().stop();
      }
    }
    //check clients for data
    for(i = 0; i < MAX_SRV_CLIENTS; i++){
      if (serverClients[i] && serverClients[i].connected()){
        if(serverClients[i].available()){
          //get data from the telnet client and push it to the UART
          while(serverClients[i].available()) Serial.write(serverClients[i].read());
        }
      }
      else {
        if (serverClients[i]) {
          Serial.println("Client not available. Disconnecting!");
          serverClients[i].stop();
        }
      }
    }
  }
  else {
    Serial.println("WiFi not connected!");
    for(i = 0; i < MAX_SRV_CLIENTS; i++) {
      if (serverClients[i]) serverClients[i].stop();
    }
    delay(1000);
  }
}
