#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <Wire.h>             
#include <Adafruit_SSD1306.h>
#include "DHT.h"   
#include <Servo.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <timer.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

//Ponemos las credenciales de la conexión de internet
//We put the internet connection credentials
const char *ssid     = "xxxxxx";
const char *password = "xxxxxxxx";

//Configuramos el token del bot que usaremos
//We configure the token of the bot that we will use
const char BotToken[] = "xxxxxxxxx";
WiFiClientSecure client;
UniversalTelegramBot bot (BotToken, client);
int Bot_mtbs = 1000; //Tiempo entre escaneo de mensajes (1s) -- mean time between scan messages
long Bot_lasttime;   //Ultimo escaneo de mensajes -- last time messages scan has been done
bool Start = false;

//Establecemos la conexión con el servidor de hora NTP, en GMT+1
//We establish the connection with the NTP time server, in GMT+1
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

//Creamos un timer para el movimiento del servo
//We created a timer for the servo movement
auto timer = timer_create_default();  

//Definimos el pin y el tipo del sensor de humedad y temperatura
//Define the pin and the type of the humidity and temperature sensor
#define DHTPIN D5 
#define DHTTYPE DHT11 
DHT dht(DHTPIN, DHTTYPE);

//Definimos los pines de conexión del rele, leds y servo
//Define the pin of the rele, led and servo
#define rele D6
#define ledAzul D4
#define ledRojo D3
#define SERVO D7

//Configuramos la pantalla Oled 
//We configure the oled screen
#define ANCHO_PANTALLA 128 // ancho pantalla OLED -- wide Oled screen
#define ALTO_PANTALLA 64 // alto pantalla OLED -- high Oled screen
Adafruit_SSD1306 display(ANCHO_PANTALLA, ALTO_PANTALLA, &Wire, -1);

//Algunas variables que usaremos para configurar el servo
//Some variables we will use to configure the servo
Servo myservo;
int pos = 0;                  //Posición inicial del servo -- Initial position of servo
int  conta = 0;               //Contamos el número de ciclos del servo -- We count the number of servo cycles
int pararServo;               

//Variables para la temp y humedad, así como la temperatura objetivo
//Variables for temp and humidity as well as target temperature
float temp;
float h;
const float setTemp=37.8;

//Variables para contabilizar los días que se faltan del ciclo de incubación
//Variables for counting the days missing from the incubation cycle
unsigned long diasIncubar;
unsigned long diasFaltan;

//-----------------------FUNCIONES------------------------------------------------------------------------------------
//-----------------------FUNCTIONS------------------------------------------------------------------------------------

//Función para calcular los días que faltan del ciclo. Son 21 días. Utilizo el tiempo en formato epoch.
//Function for calculating the remaining days of the cycle. That's 21 days. I use time in epoch format.
int DiasIncubando() {
  unsigned long epochTime = timeClient.getEpochTime();
  diasFaltan = ((diasIncubar - epochTime)/86400);
  return diasFaltan;
}

//Función para gestionar el bot de telegram
//Function to manage the telegram bot
void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    String from_name = bot.messages[i].from_name;
    if (from_name == "") from_name = "Guest";

    if (text == "/Temperatura") {
      temp = dht.readTemperature();
      bot.sendMessage(chat_id, "Temperatura: " + String(temp) + " ºC");  
    }
    if (text == "/Humedad") {
      h = dht.readHumidity(); 
      bot.sendMessage(chat_id, "Humedad: " + String(h) + " %");   
    }
    if (text == "/DiasFaltan") {
      diasFaltan = DiasIncubando();
      bot.sendMessage(chat_id, "Faltan: " + String(diasFaltan) + " días");  
    }
    if (text == "/onRele") {
      digitalWrite(rele, LOW);
      bot.sendMessage(chat_id, "Calentando");  
    }
    if (text == "/offRele") {
      digitalWrite(rele, HIGH);
      bot.sendMessage(chat_id, "Rele apagado");  
    }
    if (text == "/options") {
      String keyboardJson = "[[\"/Temperatura\", \"/Humedad\"],[\"/DiasFaltan\",\"/onRele\"],[\"/offRele\"]]";
      bot.sendMessageWithReplyKeyboard(chat_id, "Elige una de estas opciones...", "", keyboardJson, true);
    }
  }
}  

//Función para el funcionamineto del servo. Tiene que realizar movimientos durante 3min cada 3h
//Function for servo operation. It has to make movements during 3min every 3h
bool Giro(void *) {
  if(pararServo<145) {                            //Esta función tiene que parar 3 días antes del final del ciclo
    if (conta < 37) { 
      while (conta < 37) {                        //37 movimientos es lo calculado para 3 min
         for (pos = 0; pos <= 180; pos += 1) { 
         myservo.write(pos);              
         delay(15);                      
         }
         for (pos = 180; pos >= 0; pos -= 1) { 
         myservo.write(pos);            
         delay(15);                        
         } 
         conta++;
      } 
    } else {
      conta = 0;
    }
  }
  pararServo++;
  return true;
}

//Función de alarma ante temperatura muy baja
//Very low temperature alarm function
void Alarma() {
  String chat_id2 = String("xxxxxxxx");
  bot.sendMessage(chat_id2, "Temperatura Incubadora muy baja!!"); 
}

//Funcion para controlar el calentamiento de la incubadora
//Function to control the heating of the incubator
void ControlTemperatura() {
  if(setTemp-temp<=0.2 || temp > 38){
    digitalWrite(rele, HIGH);
    display.setCursor(1, 32);
    display.print("LAMPARA OFF"); 
    digitalWrite(ledAzul, LOW);
    }
    
  else if(setTemp-temp>0.2 && temp<35){
    digitalWrite(rele, LOW);
    digitalWrite(ledRojo,HIGH);
    digitalWrite(ledAzul, HIGH);
    display.setCursor(1, 32);
    display.print("TEMP. MUY BAJA!!!"); 
    Alarma();   
    
  } else {  
    digitalWrite(rele, LOW);
    digitalWrite(ledAzul, HIGH);
    digitalWrite(ledRojo, LOW);
    display.setCursor(1, 32);
    display.print("CALENTANDO...");      
  }

  display.display(); 
}

void setup() {
  Serial.begin(9600);

  WiFi.begin(ssid, password);

  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
  }

  timeClient.setTimeOffset(3600);
  timeClient.begin();
  timeClient.update();
  diasIncubar = timeClient.getEpochTime() + 1814400;

  timer.every(10800000,Giro);         //Cada 3h ejectua el giro - Every 3h make void Giro

  //Configuramos la pantalla - Screen configuration
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  pinMode(rele,OUTPUT);
  pinMode(ledAzul, OUTPUT);
  pinMode(ledRojo, OUTPUT);
  myservo.attach(SERVO);
  dht.begin();
  display.display();

  digitalWrite(rele, LOW);

  //Para que funcione bien el bot es necesario el fingerprint del mismo
  bot._debug = true;
  client.setFingerprint("xx:xx:xx:xx:xx:xx");  //https://api.telegram.org/botxxxxxxx (xxx es el token) luego view page info, security, view certificate, sh1
  
}

void loop() { 

  timeClient.update();
  Serial.println(timeClient.getFormattedTime());
 
  float h = dht.readHumidity();    
  float temp = dht.readTemperature();

  if (isnan(h) || isnan(temp)) {
    Serial.println("Error obteniendo los datos del sensor DHT11");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Error obteniendo los datos del sensor DHT11");
    return;
  }  
  
  if (millis() > Bot_lasttime + Bot_mtbs)  {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while(numNewMessages) {
      Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    Bot_lasttime = millis();
  }
  
  //Obtenemos información por el serial para verificar funcionamiento
  //We get information from the serial to verify
  Serial.print("Humedad: ");
  Serial.print(h);
  Serial.println(" %\t");
  Serial.print("Temperatura: ");
  Serial.print(temp);
  Serial.println(" *C\t");
  Serial.print("Dias que faltan: ");
  Serial.println(DiasIncubando());
  
  //Ponemos todo por pantalla
  //We show the information on the screen
  display.clearDisplay();  
           
  display.setCursor(0, 0);
  display.print("Humedad: ");
  display.setCursor(75, 0);
  display.print(h);
  display.println("%");            
       
  display.print("Temperatura: ");
  display.setCursor(75, 8);
  display.print(temp);                
  display.print("C"); 
  display.display();
  
  display.setCursor(0, 16);              
  display.print("Dias que faltan: "); 
  display.setCursor(95, 16);  
  display.print(DiasIncubando());  
  display.display();
  
  ControlTemperatura();

  timer.tick();
  
  delay(5000); 

}