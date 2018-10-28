/*
  FSWebServer - Example WebServer with SPIFFS backend for esp8266
  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  This file is part of the ESP8266WebServer library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  upload the contents of the data folder with MkSPIFFS Tool ("ESP8266 Sketch Data Upload" in Tools menu in Arduino IDE)
  or you can upload the contents of a folder if you CD in that folder and run the following command:
  for file in `ls -A1`; do curl -F "file=@$PWD/$file" esp8266fs.local/edit; done

  access the sample web page at http://roboter.local
  edit the page by going to http://roboter.local/edit
*/
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <aREST.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

#define DBG_OUTPUT_PORT Serial
#define LISTEN_PORT         8080

#define leftMotorForward    14
#define leftMotorBackward   12
#define rightMotorForward   13
#define rightMotorBackward  15


char* ssid = "Dein WLAN";
char* password = "deinWlanPasswort";
char* host = "roboter";

const char* AP_ssid = "JUKS Robo 1";
const char* AP_password = "12345678";


const char *OTAName = "JUKS Roboter Master";           // A name and a password for the OTA service
const char *OTAPassword = "esp8266";

int spannung;
String nachricht = "";

const size_t bufferSize = JSON_OBJECT_SIZE(2) + 40;
DynamicJsonBuffer jsonBuffer(bufferSize);

//const char* json = "{\"richtung\":\"vor\",\"geschwindigkeit\":1024}";

//JsonObject& root = jsonBuffer.parseObject(json);

//const char* richtung = root["richtung"]; // "vor"
//int geschwindigkeit = root["geschwindigkeit"]; // 1024

aREST rest = aREST();
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
WiFiServer restServer(LISTEN_PORT);
//holds the current upload
File fsUploadFile;
//--------------------------------------Dateihandhabung und Texteditor----------------------------//
//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

bool handleFileRead(String path) {
  DBG_OUTPUT_PORT.println("handleFileRead: " + path);
  if (path.endsWith("/")) {
    path += "index.htm";
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz)) {
      path += ".gz";
    }
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload() {
  if (server.uri() != "/edit") {
    return;
  }
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    DBG_OUTPUT_PORT.print("handleFileUpload Name: "); DBG_OUTPUT_PORT.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
    }
    DBG_OUTPUT_PORT.print("handleFileUpload Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
  }
}

void handleFileDelete() {
  if (server.args() == 0) {
    return server.send(500, "text/plain", "BAD ARGS");
  }
  String path = server.arg(0);
  DBG_OUTPUT_PORT.println("handleFileDelete: " + path);
  if (path == "/") {
    return server.send(500, "text/plain", "BAD PATH");
  }
  if (!SPIFFS.exists(path)) {
    return server.send(404, "text/plain", "FileNotFound");
  }
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate() {
  if (server.args() == 0) {
    return server.send(500, "text/plain", "BAD ARGS");
  }
  String path = server.arg(0);
  DBG_OUTPUT_PORT.println("handleFileCreate: " + path);
  if (path == "/") {
    return server.send(500, "text/plain", "BAD PATH");
  }
  if (SPIFFS.exists(path)) {
    return server.send(500, "text/plain", "FILE EXISTS");
  }
  File file = SPIFFS.open(path, "w");
  if (file) {
    file.close();
  } else {
    return server.send(500, "text/plain", "CREATE FAILED");
  }
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if (!server.hasArg("dir")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = server.arg("dir");
  DBG_OUTPUT_PORT.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while (dir.next()) {
    File entry = dir.openFile("r");
    if (output != "[") {
      output += ',';
    }
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }

  output += "]";
  server.send(200, "text/json", output);
}

//--------------------------------------Motorfunktionen----------------------------------------//
int MotorForward(String command) {
  DBG_OUTPUT_PORT.println("Motor vorwaerts");
  digitalWrite(leftMotorForward, HIGH);
  digitalWrite(rightMotorForward, HIGH);
  digitalWrite(rightMotorBackward, LOW);
  digitalWrite(leftMotorBackward, LOW);
}
int MotorBackward(String command) {
  DBG_OUTPUT_PORT.println("Motor zurück");
  digitalWrite(leftMotorForward, LOW);
  digitalWrite(rightMotorForward, LOW);
  digitalWrite(rightMotorBackward, HIGH);
  digitalWrite(leftMotorBackward, HIGH);
}
int MotorRight(String command) {
  DBG_OUTPUT_PORT.println("Motor rechts");
  digitalWrite(leftMotorForward, LOW);
  digitalWrite(rightMotorForward, HIGH);
  digitalWrite(rightMotorBackward, LOW);
  digitalWrite(leftMotorBackward, LOW);
}
int MotorLeft(String command) {
  DBG_OUTPUT_PORT.println("Motor links");
  digitalWrite(leftMotorForward, HIGH);
  digitalWrite(rightMotorForward, LOW);
  digitalWrite(rightMotorBackward, LOW);
  digitalWrite(leftMotorBackward, LOW);
}
int MotorStop(String command) {
  DBG_OUTPUT_PORT.println("Motor stop");
  digitalWrite(leftMotorForward, LOW);
  digitalWrite(rightMotorForward, LOW);
  digitalWrite(rightMotorBackward, LOW);
  digitalWrite(leftMotorBackward, LOW);
}

void startWebSocket() { // Den WebSocket Server starten. Dieser wird vom Web-Interface genutzt
  webSocket.begin();                          // WebSocket Server starten
  webSocket.onEvent(webSocketEvent);          // Wenn eine Nachricht per WebSocket ankommt, wird die Funktion 'webSocketEvent'
  DBG_OUTPUT_PORT.println("WebSocket server started.");
}
//Ablauf wenn eine Nachricht per WebSocket ankommt
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) { // When a WebSocket message is received
  switch (type) {
    case WStype_DISCONNECTED:             // Wenn der Client sich abmeldet
      DBG_OUTPUT_PORT.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {              // Wenn sich ein neuer Client anmeldet
        IPAddress ip = webSocket.remoteIP(num);
        DBG_OUTPUT_PORT.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload); //Hier wir die IP Addresse des neuen Clients in der Seriellen Konsole ausgegeben
      }
      break;
    case WStype_TEXT: {                     // Wenn Text Daten ankommen
        DBG_OUTPUT_PORT.printf("[%u] get Text: %s\n", num, payload);
        String text = (char*) payload;                //payload ist der ankommende Text. Dieser wir in der Variable text gespeichert
        //Wir prüfen, welcher Text angekommen ist, je nach ankommendem Text, wir eine andere Funktion aufgerufen
        if (text == "stop") {
          MotorStop("");
        }
        if (text == "vor") {
          MotorForward("");
        }
        if (text == "zurueck") {
          MotorBackward("");
        }
        if (text == "rechts") {
          MotorRight("");
        }
        if (text == "links") {
          MotorLeft("");
        }
        if (text == "power") {
          spannung = map(analogRead(A0), 0, 1000, 0, 6000);
          DBG_OUTPUT_PORT.print("Spannung: ");
          DBG_OUTPUT_PORT.println(analogRead(A0));
          nachricht = String(spannung) + "mV";
          webSocket.sendTXT(num, nachricht);
        }
        break;

      }
  }
}
void startOTA() { //Ermöglicht den Code Upload über WLAN
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    DBG_OUTPUT_PORT.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    DBG_OUTPUT_PORT.println("\r\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    DBG_OUTPUT_PORT.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    DBG_OUTPUT_PORT.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) DBG_OUTPUT_PORT.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) DBG_OUTPUT_PORT.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) DBG_OUTPUT_PORT.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) DBG_OUTPUT_PORT.println("Receive Failed");
    else if (error == OTA_END_ERROR) DBG_OUTPUT_PORT.println("End Failed");
  });
  ArduinoOTA.begin();
  DBG_OUTPUT_PORT.println("OTA ready\r\n");
}
void startRest() { //Die App nutzt die REST API um mit dem Controller zu kommunizieren
  // Function to be exposed
  rest.function("vor", MotorForward);
  rest.function("zur", MotorBackward);
  rest.function("links", MotorLeft);
  rest.function("rechts", MotorRight);
  rest.function("stop", MotorStop);

  // ID und Name des Roboters
  rest.set_id("1");
  rest.set_name("WLAN_Roboter1");

  restServer.begin();
}
void setup(void) {
  int counter = 0;
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.print("\n");
  DBG_OUTPUT_PORT.setDebugOutput(true);
  SPIFFS.begin(); //Dateisystem starten
  //Ausgabe aller vorhandenen Dateien auf dem DBG_OUTPU_PORT
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    DBG_OUTPUT_PORT.printf("\n");
  }
  startWebSocket();            // WebSocket Server starten
  startOTA();                  // OTA Update Server starten
  startRest();                 // REST Server starten

  //WIFI INIT
  /*OPTIONAL: WLAN Zugangsdaten in konfiguration.txt ablegen um Sie einfacher zu ändern
   * File konfiguration = SPIFFS.open("/konfiguration.txt", "r");
  if (konfiguration) { //Wurde die Datei erfolgreich geöffnet?
    ssid = konfiguration.readStringUntil('\n');     //Erste Zeile der Konfigurationsdatei enthält die SSID des Netzwerks
    password = konfiguration.readStringUntil('\n'); //Zweite Zeile der Konfigurationsdatei enthält das Passwort des Netzwerks
    konfiguration.close();
  }
  */
  DBG_OUTPUT_PORT.printf("Connecting to %s\n", ssid);
  DBG_OUTPUT_PORT.printf("Passwort: %s\n", password);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (String(WiFi.SSID()) != String(ssid)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }

  while (WiFi.status() != WL_CONNECTED && counter < 10) {
    delay(500);
    counter++;
    DBG_OUTPUT_PORT.print(".");
    if (counter = 9) {
      WiFi.mode(WIFI_AP);
      WiFi.softAP(AP_ssid, AP_password);
      counter++;
    }
  }
  DBG_OUTPUT_PORT.println("");
  DBG_OUTPUT_PORT.print("Connected! IP address: ");
  DBG_OUTPUT_PORT.println(WiFi.localIP());

  MDNS.begin(host);
  DBG_OUTPUT_PORT.print("http://");
  DBG_OUTPUT_PORT.print(host);
  DBG_OUTPUT_PORT.println(".local/edit öffnen um den Datei Explorer zu sehen");


  //Webserver initialisieren
  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, []() {
    if (!handleFileRead("/edit.htm")) {
      server.send(404, "text/plain", "FileNotFound");
    }
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, []() {
    server.send(200, "text/plain", "");
  }, handleFileUpload);

  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "FileNotFound");
    }
  });

  //get heap status, analog input value and all GPIO statuses in one json call

  /*
    server.on("/all", HTTP_GET, []() {
    String json = "{";
    json += "\"heap\":" + String(ESP.getFreeHeap());
    json += ", \"analog\":" + String(analogRead(A0));
    json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += "}";
    server.send(200, "text/json", json);
    json = String();
    });
  */
  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");

  pinMode(leftMotorForward, OUTPUT);
  pinMode(leftMotorBackward, OUTPUT);
  pinMode(rightMotorForward, OUTPUT);
  pinMode(rightMotorBackward, OUTPUT);
  pinMode(A0, INPUT);
  digitalWrite(leftMotorBackward, LOW);
  digitalWrite(leftMotorForward, LOW);
  digitalWrite(rightMotorBackward, LOW);
  digitalWrite(rightMotorForward, LOW);

}

void loop(void) {
  webSocket.loop();                           // constantly check for websocket events
  server.handleClient();
  ArduinoOTA.handle();                        // listen for OTA events
  WiFiClient client = restServer.available();
  if (!client) {
    return;
  }
  while (!client.available()) {
    delay(1);
  }
  rest.handle(client);
}
