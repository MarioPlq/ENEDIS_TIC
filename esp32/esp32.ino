/*
Lecture des données du Linky sur l'interface TIC
*/

//Librairies
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>    //Modification On The Air
#include <RemoteDebug.h>   //Debug via Wifi
#include <esp_task_wdt.h>  //Pour un Watchdog
#include <PubSubClient.h>  //Librairie pour la gestion Mqtt

//Program routines
#include "PageWeb.h"

// ** WIFI à AJUSTER **
const char* ssid = "wifi_ssid";          // Nom du Wifi (SSID)
const char* password = "wifi_password";;  // WIFI password - Mot de passe
// ***************************

// ++ Compléter  ci dessous pour une adresse IP fixe ++
const bool IPfixe = true;              //A mettre true si on souhaite positionner les adresses ci dessous. Sinon laisser des adresses quelconques
IPAddress local_IP(192, 168, 1, 253);  //IP à donner à cet ESP 192.168.1.253
// Set your Gateway IP address
IPAddress gateway(192, 168, 1, 254);  //passerelle, adresse IP de la box réseau
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);    //optional
IPAddress secondaryDNS(8, 8, 4, 4);  //optional
// ++++++++++++++++++++++++++++++

// ** A renseigner si - MQTT **
const char* mqtt_server = "MQTT_server_IP";
const char* mqtt_username = "MQTT_user";
const char* mqtt_password = "MQTT_password";
const char* mqtt_clientID = "client_linky";
// ************************************

//Debug via WIFI instead of Serial
//Connect a Telnet terminal on port 23
RemoteDebug Debug;

//PINS - GPIO
#define RXD2 26
#define TXD2 27
#define LED 19

//VARIABLES

int WIFIbug = 0;
int IdxDataRawLinky = 0;
int IdxBufferLinky = 0;

char DataRawLinky[1000];
char BufferLinky[30];
bool LFon = false;

float Iinst = 0;  //I instantané
float Imoy = 0;   // I moyen sur 5mn
float Papp = 0;   //PVA instantané
float PappM = 0;  //PVA moyen sur 5mn
float tabI[600];
float tabP[600];
int IdxStock = 0;
long HCHC = 0;
long HCHP = 0;
long HCHC_last = 0;
long HCHP_last = 0;
int tabHC[600];
int tabHP[600];
int PWHP = 0;
int PWHC = 0;

//Internal Timers
unsigned long previousWifiMillis;
unsigned long previousWatchdogMillis;
unsigned long previousHistoryMillis;

WebServer server(80);  // Simple Web Server on port 80

//Watchdog de 120 secondes. Le systeme se Reset si pas de dialoque avec le LINKY pendant 120s
#define WDT_TIMEOUT 120

WiFiClient MqttClient;
PubSubClient client(mqtt_server ,1883, MqttClient);

void setup() {
  //Pin initialisation
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  //Watchdog initialisation
  esp_task_wdt_init(WDT_TIMEOUT, true);  //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);                //add current thread to WDT watch
  //Ports Série
  Serial.begin(115200);
  Serial2.begin(1200, SERIAL_7E1, RXD2, TXD2);  //  7-bit Even parity 1 stop bit pour le Linky
  Serial.println("Booting");

  // Configures static IP address
  if (IPfixe) {
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("STA Failed to configure");
    }
  }
  esp_task_wdt_reset();
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Debug.println("Connection Failed! Rebooting...");
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // init remote debug
  Debug.begin("ESP32");  // Telnet on port 23
  //Init Software update via WIFI (On The Air)
  initOTA();

  Debug.println("Ready");
  Debug.print("IP address: ");
  Debug.println(WiFi.localIP());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //MQTT parameters
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  Debug.print("Attempting MQTT connection...");

  if (client.connect(mqtt_clientID, mqtt_username, mqtt_password)) {
    Debug.println("connected");
    delay(100);
    client.subscribe("esp32/Papp_i_output");
    client.subscribe("esp32/Papp_m_output");
    client.subscribe("esp32/I_i_output");
    client.subscribe("esp32/I_m_output");
    client.subscribe("esp32/Conso_i_output");
  } else {
      Debug.print(" MQTT reconnection failed. ");
    }

  //Init Web Server on port 80
  server.on("/", handleRoot);
  server.on("/JavascriptPageWeb", handleJavascriptPageWeb);
  server.on("/ajax_histo", handleAjaxHisto);
  server.on("/ajax_dataLinky", handleAjaxLinky);
  server.on("/ajax_data_5mn", handleAjaxData5mn);
  server.on("/restart", handleRestart);
  server.onNotFound(handleNotFound);
  server.begin();
  Debug.println("HTTP server started");

  //Timers
  previousWifiMillis = millis();
  previousWatchdogMillis = millis();
  previousHistoryMillis = millis();
}

void loop() {
  ArduinoOTA.handle();
  Debug.handle();
  server.handleClient();
  // ici
  LectureLinky();

  if (!client.connected()) {
      reconnect_mqtt();
  } else {
      client.loop();
  }

  long tps = millis();
  if (tps - previousHistoryMillis >= 300000) {  //Historique consommation par pas de 5mn
    previousHistoryMillis = tps;
    tabI[IdxStock] = Imoy;
    tabP[IdxStock] = PappM;
    PWHC = 0;
    if (HCHC > 0 && HCHC_last > 0) {
      PWHC = HCHC - HCHC_last;
      PWHC = PWHC * 12;  //Puissance heure creuse moyenne sur 5mn
    }
    tabHC[IdxStock] = PWHC;
    PWHP = 0;
    if (HCHP > 0 && HCHP_last > 0) {
      PWHP = HCHP - HCHP_last;
      PWHP = PWHP * 12;  //Puissance heure pleine moyenne sur 5mn
    }
    tabHP[IdxStock] = PWHP;
    HCHC_last = HCHC;
    HCHP_last = HCHP;
    IdxStock = (IdxStock + 1) % 600;
  }

  if (tps - previousWifiMillis > 30000) {  //Test présence WIFI toutes les 30s
    previousWifiMillis = tps;
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Debug.println("Connection Failed! #" + String(WIFIbug));
      Serial.println("Connection Failed! #" + String(WIFIbug));
      WIFIbug++;
      if (WIFIbug > 20) {
        ESP.restart();
      }
    } else {
      WIFIbug = 0;
    }
    //Serial.print("Niveau Signal WIFI:");
    //Serial.println(WiFi.RSSI());
    Debug.print("Niveau Signal WIFI:");
    Debug.println(WiFi.RSSI());
  }
}

//MQTT Reconnect si je perds la connexion serveur
//********
void reconnect_mqtt() {

  while (!client.connected()) {

    Debug.print("Attempting MQTT connection...");

    if (client.connect(mqtt_clientID, mqtt_username, mqtt_password)) {

      Debug.println("connected");

      delay(100);
      client.subscribe("esp32/Papp_i_output");
      client.subscribe("esp32/Papp_m_output");
      client.subscribe("esp32/I_i_output");
      client.subscribe("esp32/I_m_output");
      client.subscribe("esp32/Conso_i_output");
    } else {

      Debug.print(" MQTT reconnection failed. ");

      if (true) {
        Debug.print("Client state: ");
        Debug.print(client.state());
        Debug.println(". Try again in 2.5 seconds");
        } 
    }

    // Wait 2.5 seconds before retrying
    delay(2500);
  }
}
// CALLBACK pour réponse MQTT du RPI
void callback(char* topic, byte* message, unsigned int length) {

  Debug.print("Message arrived on topic: ");
  Debug.println(topic);

  if (String(topic) == "esp32/Papp_i_output") {
    char PappStr[8];
    dtostrf(Papp, 1, 2, PappStr);
    client.publish("esp32/Papp_i", PappStr);
  }
  if (String(topic) == "esp32/Papp_m_output") {
    char PappMStr[8];
    dtostrf(PappM, 1, 2, PappMStr);
    client.publish("esp32/Papp_m", PappMStr);
  }
  if (String(topic) == "esp32/I_i_output") {
    char IinstStr[8];
    dtostrf(Iinst, 1, 2, IinstStr);
    client.publish("esp32/I_i", IinstStr);
  }
  if (String(topic) == "esp32/I_m_output") {
    char ImoyStr[8];
    dtostrf(Imoy, 1, 2, ImoyStr);
    client.publish("esp32/I_m", ImoyStr);
  }
  if (String(topic) == "esp32/Conso_i_output") {
    char HCHPStr[8];
    dtostrf(HCHP, 1, 2, HCHPStr);
    client.publish("esp32/Conso_i", HCHPStr);
  }
}

// LINKY
//********
void LectureLinky() {  //Lecture port série du LINKY
  if (Serial2.available() > 0) {
    int V = Serial2.read();
    if (V == 2) {  //STX (Start Text)
      for (int i = 0; i < 5; i++) {
        DataRawLinky[IdxDataRawLinky] = '-';
        IdxDataRawLinky = (IdxDataRawLinky + 1) % 1000;
      }
      digitalWrite(LED, LOW);
    }
    if (V == 3) {  //ETX (End Text)
      digitalWrite(LED, HIGH);
      //Reset du Watchdog . Il faut des messages du Linky
      if (millis() - previousWatchdogMillis > 3000) {
        esp_task_wdt_reset();
        previousWatchdogMillis = millis();
      }
    }
    if (V > 9) {  //Autre que ETX et STX
      switch (V) {
        case 10:  // Line Feed. Debut Groupe
          LFon = true;
          IdxBufferLinky = 0;
          break;
        case 13:  // Fin Groupe
          if (LFon) {  //Debut groupe OK
            LFon = false;
            int nb_blanc = 0;
            String code = "";
            String val = "";
            for (int i = 0; i < IdxBufferLinky; i++) {
              if (BufferLinky[i] == ' ') {
                nb_blanc++;
              }
              if (nb_blanc == 0) {
                code += BufferLinky[i];
              }
              if (nb_blanc == 1) {
                val += BufferLinky[i];
              }
              if (nb_blanc < 2) {  //On ne prend pas le check somme, uniquement 2 premier champs
                DataRawLinky[IdxDataRawLinky] = BufferLinky[i];
                IdxDataRawLinky = (IdxDataRawLinky + 1) % 1000;
              }
            }
            DataRawLinky[IdxDataRawLinky] = char(13);
            IdxDataRawLinky = (IdxDataRawLinky + 1) % 1000;
            if (code.indexOf("IINST") == 0) {
              Iinst = val.toFloat();
              if (Imoy == 0) { Imoy = Iinst; }
              Imoy = (Iinst + 149 * Imoy) / 150;  //moyenne courant efficace 5 dernieres minutes environ
            }
            if (code.indexOf("PAPP") == 0) {
              Papp = val.toFloat();
              if (PappM == 0) { PappM = Papp; }
              PappM = (Papp + 149 * PappM) / 150;  //moyenne puissance apparente 5 dernieres minutes environ
              //Debug.println("");
              //Debug.print(PappM);
              //Debug.println(" VA");
            }
            if (code.indexOf("HCHP") == 0 || code.indexOf("BASE") == 0) {
              HCHP = val.toInt();
            }
            if (code.indexOf("HCHC") == 0) {
              HCHC = val.toInt();
            }
          }
          break;
        default:
          BufferLinky[IdxBufferLinky] = char(V);
          IdxBufferLinky = (IdxBufferLinky + 1) % 30;
          break;
      }
      //Serial.print(char(V));
      //Debug.print(char(V)); // Si on veut print la lecture du linky
    }
  }
}

// Modification du programme par le Wifi (On The Air)
//***************************************************
void initOTA() {
  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);
  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("ESP32-Test");
  // No authentication by default
  
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else  // U_SPIFFS
        type = "filesystem";
      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Debug.println("Start updating " + type);
    })
    .onEnd([]() {
      Debug.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Debug.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Debug.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Debug.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Debug.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Debug.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Debug.println("Receive Failed");
      else if (error == OTA_END_ERROR) Debug.println("End Failed");
    });
  ArduinoOTA.begin();
}

//  SERVER HTML (pas obligatoire) utile pour avoir une aide en plus du debug via telnet
//***********
void handleRoot() {  //Main web page called
  Debug.println(F("Client Web"));
  server.send(200, "text/html", String(getPageWeb()));
}
void handleJavascriptPageWeb() {  //Code Javascript
  server.send(200, "text/html", String(getJavascriptPageWeb()));  // Javascript code
}
void handleAjaxLinky() {  // Envoi des dernières données  brutes reçues du LINKY
  int LastIdx = server.arg(0).toInt();
  String S = "";
  while (LastIdx != IdxDataRawLinky) {
    S += String(DataRawLinky[LastIdx]);
    LastIdx = (1 + LastIdx) % 1000;
  }
  S += "|" + String(IdxDataRawLinky);

  server.send(200, "text/html", S);
}
void handleAjaxHisto() {  // Envoi Historique de 50h (600points) toutes les 5mn
  String S = "";
  int iS = IdxStock;
  for (int i = 0; i < 600; i++) {
    S += String(tabI[iS]) + ",";
    iS = (1 + iS) % 600;
  }
  S += "|";
  iS = IdxStock;
  for (int i = 0; i < 600; i++) {
    S += String(tabP[iS]) + ",";
    iS = (1 + iS) % 600;
  }
  S += "|";
  iS = IdxStock;
  for (int i = 0; i < 600; i++) {
    S += String(tabHC[iS]) + ",";
    iS = (1 + iS) % 600;
  }
  S += "|";
  iS = IdxStock;
  for (int i = 0; i < 600; i++) {
    S += String(tabHP[iS]) + ",";
    iS = (1 + iS) % 600;
  }
  server.send(200, "text/html", S);
}
void handleAjaxData5mn() {  //Moyenne des 5 dernières minutes.  Pour extension vers un affichage déporté
  String S = String(Papp) + "," + String(PappM) + "," + String(Iinst) + "," + String(Imoy) + "," + String(PWHP) + "," + String(PWHC);  //Puissance apparente,Courant effic., Puissancse heure pleine et creuse
  server.send(200, "text/html", S);
}
void handleRestart() {  // Eventuellement Rester l'ESP32 à distance
  ESP.restart();
}

void handleNotFound() {  //Page Web pas trouvé
  Debug.println(F("File Not Found"));
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}
