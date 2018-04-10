#include <SPI.h>
#include <Ethernet.h>
#include <HttpClient.h>
#include <EEPROM.h>
#include <ThingSpeak.h>
#include <Adafruit_Sensor.h>
#include <DHT_U.h>
//#include <DHT.h>
#include <avr/wdt.h>

#define LED_PIN 5       // пин светодиодной ленты
#define PSENS_PIN A4    // аналоговый пин датчика освещенности
#define DHT22_PIN 7     // пин датчика температуры и влажности
#define PIR1_PIN 8      // пин PIR1 датчика
#define PIR2_PIN 9      // пин PIR2 датчика

DHT_Unified dht(DHT22_PIN, DHT22);  // создание объекта для датчика DHT

//int fadeInCurve = -2;            // кривая изменения яркости подсветки
//int fadeOutCurve = 0.5;          // кривая изменения яркости подсветки
unsigned long fadeLastUpdate = 0;   // переменная для хранения последнего времени обновления статуса подсветки

unsigned long lastLightOn = 0;  // переменная для хранения последнего времени включения подсветки
boolean l_on = false;           // флаг включения основной подсветки
boolean sl_on = false;          // флаг включения мягкой подсветки
uint8_t currLLevel = 0;         // текущий уровень подсветки
float temperature = -50;        // температура
float humidity = -1;            // влажность
uint32_t sensorDelay = 2000;    // частота опроса датчика DHT, миллисекунды
unsigned long sensorPreviousMillis = 0; // временная переменная, последнее время опроса датчика DHT
unsigned long currentMillis;

//boolean debug = true;            // флаг отладки
uint8_t params_count = 5;        // (количество параметров - 1)
boolean ethernet_ready = false;  // флаг готовности ethernet
uint8_t light_level = 0;         // переменная для хранения уровня освещения
boolean IsAJAX = false;          // переменная для хранения признака, что http запрос запрашивает ajax
boolean IsFavicon = false;       // переменная для хранения признака, что http запрос запрашивает favicon.ico

unsigned long pirPreviousMillis = 0;  // переменная для хранения последнего времени передачи срабатывания pir датчика на thingspeak.com
boolean prevPir1_trg = true;          //предыдущее значение pir1_trg

// структура параметров
struct Params
{
  char* pName;          // название
  char* pDescription;   // описание
  char* htmlInputType;  // html тег
  uint8_t   value;      // значение
  uint8_t   minValue;   // минимальное значение
  uint8_t   maxValue;   // максимальное значение
  boolean updated;      // признак обновления
};

// массив структур параметров
Params paramsArray[6] =
{
  // уровень основной подсветки
  {"mll", "main light level", "range", 255, 100, 255, false},
  
  // порог срабатывания по датчику освещенности
  {"lt", "light sensor threshold, %", "range", 0, 0, 100, false},
  
  // уровень комфортной подсветки
  {"sll", "soft light level", "range", 0, 0, 100, false},
  
  // скорость затухания подсветки
  {"fs", "light fade speed", "range", 5, 0, 60, false},
  
  // задержка выключения подсветки, в секундах
  {"fd", "main light off delay, s", "range", 0, 0, 120, false},
  
  // статус включено выключено
  {"chbOn", "light enabled", "checkbox", 1, 0, 1, false}
};

/*
int use_bitmask = 0; // битовая маска для флагов разрешений
boolean use_light_sensor = false; // флаг разрешения работы от датчика освещенности
boolean use_uss1_sensor = false; // флаг разрешения работы от датчика расстояния 1
boolean use_uss2_sensor = false; // флаг разрешения работы от датчика расстояния 2
*/

// Enter a MAC address and IP address for controller
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Initialize the Ethernet server library
EthernetServer server(80);

//thingspeak.com
const unsigned long thChannelNumber = 177346;
const char *  thWriteAPIKey = "YS4GRWYHJQXCC2CJ";
long thpreviousMillis = 0;                  // временная переменная
long thinterval = 1;                        // период логгирования показаний в thingspeak.com, в минутах
const uint8_t thtemperatureId = 1;          // id полей
const uint8_t thhumidityId = 2;             // id полей
const uint8_t thlightId = 3;                // id полей
const uint8_t pir1Id = 4;                   // id полей
EthernetClient thclient;                    // ethernet клиент

const char htmlHead[] PROGMEM = 
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/html\r\n"
  "Pragma: no-cache\r\n"
  "Connnection: close\r\n\r\n"
  "<!DOCTYPE HTML>\r\n"
  "<html>\r\n"
  "<head>\r\n"
  "<title>LedBed ver. 0.6.0</title>\r\n"
  "<meta http-equiv=Content-Type content=text/html; charset=utf-8>\r\n"
  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>\r\n"
  "<script>"
  "function GetState() {"
  " var request = new XMLHttpRequest();"
  " request.onreadystatechange = function() {"
  " if (this.readyState == 4) {"
  " if (this.status == 200) {"
  " if (this.responseText != null) {"
  " document.getElementById(\"state_div\").innerHTML = this.responseText;"
  " }}}};"
  " request.open(\"GET\", \"ajax\", true);"
  " request.send(null);"
  " setTimeout(function(){GetState()}, 3000);"
  " }\r\n"
  "</script>\r\n"
  "</head>\r\n";
  
const char br[] PROGMEM = "\r\n<br>\r\n";

// процедура мигания светодиодом, используется для визуального определения ошибки сети
void Alert() {
  for(uint8_t i = 1 ; i <= 3; i++) {
    analogWrite(LED_PIN, 255);
    delay(500);
    analogWrite(LED_PIN, 0);
    delay(500);
  }
}

/*
// функция вычисления значения по логарифмической шкале
float fscale(float originalMin, float originalMax, float newBegin, float newEnd, float inputValue, float curve){
  float OriginalRange = 0;
  float NewRange = 0;
  float zeroRefCurVal = 0;
  float normalizedCurVal = 0;
  float rangedValue = 0;
  boolean invFlag = 0;

  // condition curve parameter
  // limit range
  if (curve > 10) curve = 10;
  if (curve < -10) curve = -10;
  curve = (curve * -.1) ; // - invert and scale - this seems more intuitive - postive numbers give more weight to high end on output
  curve = pow(10, curve); // convert linear scale into lograthimic exponent for other pow function

  // Check for out of range inputValues
  if (inputValue < originalMin) {
    inputValue = originalMin;
  }
  if (inputValue > originalMax) {
    inputValue = originalMax;
  }

  // Zero Refference the values
  OriginalRange = originalMax - originalMin;
  if (newEnd > newBegin){
    NewRange = newEnd - newBegin;
  } else {
    NewRange = newBegin - newEnd;
    invFlag = 1;
  }
  zeroRefCurVal = inputValue - originalMin;
  normalizedCurVal  =  zeroRefCurVal / OriginalRange;   // normalize to 0 - 1 float

  // Check for originalMin > originalMax  - the math for all other cases i.e. negative numbers seems to work out fine
  if (originalMin > originalMax ) {
    return 0;
  }
  if (invFlag == 0){
    rangedValue =  (pow(normalizedCurVal, curve) * NewRange) + newBegin;
  } else {
    // invert the ranges
    rangedValue =  newBegin - (pow(normalizedCurVal, curve) * NewRange);
  }
  return rangedValue;
}
*/

// класс управления яркостью светодиодной ленты
class Fader
{
  uint8_t ledPin;             // the number of the LED pin
  //int value;                // calculate value
  uint8_t fadeValue;          // current value
  int updateInterval;         // interval between updates
  unsigned long lastUpdate;   // last update

public: 
  Fader(uint8_t pin)
  {
    ledPin = pin;
    //pinMode(ledPin, OUTPUT);
  }
  
  void Update(uint8_t fromValue, uint8_t toValue, uint8_t interval)
  {
    //int scaledResult;
    updateInterval = interval;    
    if((millis() - lastUpdate) > updateInterval)  // time to update
    {
      lastUpdate = millis();

      if (currLLevel != toValue)
      {
        if (fromValue != toValue)
        {
          if (fromValue < toValue)
          {
            fadeValue++;
            //value = fscale(fromValue, toValue, fromValue, toValue, fadeValue, fadeInCurve);
            //analogWrite(ledPin, value);
            analogWrite(ledPin, fadeValue);
          } else {
            fadeValue--;
            //value = fscale(toValue, fromValue, toValue, fromValue, fadeValue, fadeOutCurve);
            //analogWrite(ledPin, value);
            analogWrite(ledPin, fadeValue);
          }
          currLLevel = fadeValue;
        }
      }
    }
  }
};
Fader fader1(LED_PIN);


//=================================================================
void setup() {
  //Serial.begin(9600);
  //Serial.print("start...");

  // сконфигурировать пины
  pinMode(LED_PIN, OUTPUT);
  pinMode(PSENS_PIN, INPUT);
  pinMode(PIR1_PIN, INPUT);
  pinMode(PIR2_PIN, INPUT);

  dht.begin();

  // чтение значений из внутренней памяти
  for (uint8_t i = 0; i <= params_count; i++) 
  {
    Params &params = paramsArray[i];
    params.value = EEPROM.read(i);
  };

  // чтение флагов разрешения датчиков из внутренней памяти
/*  use_bitmask = EEPROM.read(4); // битовая маска
  use_light_sensor = bitRead(use_bitmask, 0);
  use_uss1_sensor = bitRead(use_bitmask, 1);
  use_uss2_sensor = bitRead(use_bitmask, 2);
*/

  // Set the static IP address to use if the DHCP fails to assign
  //IPAddress ip(192,168,1,100);
  //IPAddress gateway(192,168,1,254);	
  //IPAddress subnet(255, 255, 255, 0);
  //Ethernet.begin(mac, ip);
  
  // получение адреса через DHCP
  if (Ethernet.begin(mac) == 0) 
  {
    //Serial.println("Failed to configure Ethernet using DHCP");
    Alert();
  } else ethernet_ready = true;

  // запуск сервера если сетевое соединение активно
  if (ethernet_ready){
    //Serial.print("IP address: ");
    //Serial.println(Ethernet.localIP());
    server.begin();
  }
  
  ThingSpeak.begin(thclient);
  wdt_enable(WDTO_8S);
}

//=================================================================
void loop() {
  wdt_reset();
  uint16_t cntchars = 70;
  uint16_t tmp = 0;
  currentMillis = millis();
  String clientRead = "";
  
  boolean pir1_trg = digitalRead(PIR1_PIN);
  boolean pir2_trg = digitalRead(PIR2_PIN);
  light_level = map(analogRead(PSENS_PIN), 1023, 100, paramsArray[1].minValue, paramsArray[1].maxValue);
  
  //if (debug){
        /*
        Serial.print("light_level=");
        Serial.println(light_level);
        Serial.print("llt=");
        Serial.println(values[1]);
        Serial.println("==============");
        */
        
        //Serial.print("pir1_trg=");
        //Serial.println(pir1_trg);
        //Serial.println("==============");
        //delay(100);

        /*Serial.print("uss2_level=");
        Serial.println(uss2_level);
        Serial.print("uss2t=");
        Serial.println(values[3]);
        Serial.println("==============");
        */
  //}
  
  // установка флага включения подсветки по уровню датчика освещенности
  if (light_level <= paramsArray[1].value)
  {
    sl_on = true;
  } 
  else 
  {
    //гистерезис включения подсветки в 2 пункта
    if (light_level > paramsArray[1].value + 2)
    {
      sl_on = false;
    }
  }
  
  // проверка pir датчика
  l_on = (pir1_trg == 1) ? true : false;
  /*if (pir1_trg == 1)
    l_on = true;
  else
    l_on = false;*/
  
  // включение/выключение подсветки
  if (paramsArray[5].value == 1) // если подсветка разрешена настройкой
  {
    unsigned long fade_delay = paramsArray[4].value * 1000;
    if (sl_on) // если подсветка разрешена уровнем яркости
    {
      if (!l_on) // если основная подсветка запрещена
      {
        //включаем мягкую подсветку с задержкой от выключения основной
        if((lastLightOn + fade_delay) < millis())
          fader1.Update(currLLevel, paramsArray[2].value, paramsArray[3].value);
      }
      else
      {
        // если основная подсветка разрешена, то включаем основную подсветку
        fader1.Update(currLLevel, paramsArray[0].value, paramsArray[3].value);
        lastLightOn = millis();
      }
    } 
    else 
    {
        // выключение подсветки
        fader1.Update(currLLevel, 0, paramsArray[3].value);
    };
  } else {
    // выключение подсветки
    //Fade(false, false);
    fader1.Update(currLLevel, 0, paramsArray[3].value);
  };
  
  
  /*
  if (debug){
     if (l_on){
        Serial.println("light on");
     } else {Serial.println("light off");}
     if (sl_on){
        Serial.println("soft light on");
     } else {Serial.println("soft light off");}
    delay(1000);
  }
  */
 
  if (ethernet_ready)
  {
    // listen for incoming clients
    EthernetClient client = server.available();
    if (client)
    {
      //if (debug){Serial.println("new client");}
      // an http request ends with a blank line
      boolean currentLineIsBlank = true;
      while (client.connected()) 
      {
        if (client.available()) 
        {
          char c = client.read();
          // ограничение длины строки с данными от клиента
          if (tmp < cntchars) clientRead = clientRead + c;
          ++tmp;
  
          // if you've gotten to the end of the line (received a newline
          // character) and the line is blank, the http request has ended,
          // so you can send a reply
          //Serial.println(clientRead);
          if (c == '\n' && currentLineIsBlank)
          {
            /*
            Serial.print("IsAJAX=");
            Serial.println(IsAJAX);
            Serial.print("IsFavicon=");
            Serial.println(IsFavicon);
            */
            if (IsAJAX) 
            {
              IsAJAX = false;
              SendAjax(client);
              //Serial.println("SendAjax");
            }
            else 
            {
              if (IsFavicon)
              {
                IsFavicon = false;
                SendFavicon(client);
              } 
              else
              {
                SendHtml(client);
                //Serial.println("SendHtml");
              }
            }
            break;
          }
          // if HTTP request has ended
          if (c == '\n') 
          {
            //Serial.println(clientRead);
            //Serial.println("-------------");
            if (clientRead.indexOf("GET") > -1)
            {
              if (clientRead.indexOf("ajax") > -1)
              {
                IsAJAX = true;
              }
              if (clientRead.indexOf("favicon.ico") > -1)
              {
                IsFavicon = true;
              }
              if (!IsAJAX && !IsFavicon)
              {
                if(clientRead.indexOf("?") > -1)
                {
                  //if (debug){Serial.println(clientRead);}
                  String tmpstr = "";
                  String tmpparam = "";
                  String tmpvalue = "";
                  uint16_t lastidxp = clientRead.indexOf("?") + 1;
                  for(uint16_t i = lastidxp; i <= clientRead.length(); i++)
                  {
                    if ((clientRead.substring(i, i + 1) == "&")||(clientRead.substring(i, i + 1) == " "))
                    {
                      //сбрасываем признак обновления параметра
                      for (uint8_t k = 0; k <= params_count; k++) paramsArray[k].updated = false;
                      
                      // если несколько параметров, разделенных "&" или " "
                      tmpstr = clientRead.substring(lastidxp, i);
                      for(uint16_t j = 0; j <= tmpstr.length(); j++)
                      {
                        if (tmpstr.substring(j, j + 1) == "=")
                        {
                          tmpparam = tmpstr.substring(0, j);
                          tmpvalue = tmpstr.substring(j + 1, tmpstr.length());
                            
                          if (tmpvalue == "on" || tmpvalue == "checked")
                          {
                            SetArrayValue(tmpparam, 1);
                          }
                          else 
                          {
                            if (tmpvalue == "tgl") 
                            {
                              SetArrayValue(tmpparam, (GetArrayValueByName(tmpparam) == 0)? 1 : 0);
                            } else 
                            SetArrayValue(tmpparam, tmpvalue.toInt());
                          }
                                                    
                          //костыль для checkbox, все необновленные checkbox сбрасываем в 0
                          for (uint8_t k = 0; k <= params_count; k++)
                            if (paramsArray[k].htmlInputType == "checkbox" && !paramsArray[k].updated)
                              SetArrayValue(paramsArray[k].pName, 0);
                        }
                      }
                      lastidxp = i + 1;
                    }
                  }
                }
              }
            }
            currentLineIsBlank = true;
            clientRead = "";
          }
          else if (c != '\r') 
          {
            // you've gotten a character on the current line
            currentLineIsBlank = false;
          }
        }
      }
      // give the web browser time to receive the data
      delay(20);
      // close the connection:
      client.stop();
      //if (debug){Serial.println("client disconnected");}
    
    }
    
    //thingspeak.com
    if(currentMillis - thpreviousMillis > thinterval * 1000 * 60) 
    {
      thpreviousMillis = currentMillis;
      /*uint8_t dht22chk = sensor.read22(DHT22_PIN);
      if (dht22chk == DHTLIB_OK)
      {
        float t = sensor.temperature;
        float h = sensor.humidity;*/
        
        sensorRead();
        ThingSpeak.setField(thtemperatureId, temperature);
        ThingSpeak.setField(thhumidityId, humidity);
        ThingSpeak.setField(thlightId, light_level);
        
        ThingSpeak.setField(pir1Id, pir1_trg);
        prevPir1_trg = pir1_trg;
        ThingSpeak.writeFields(thChannelNumber, thWriteAPIKey);
        
      //}
    }
    
    //логгирование при изменении состояния датчика движения
    if (pir1_trg != prevPir1_trg)
    {
      if(thpreviousMillis + (thinterval * 1000 * 60) - currentMillis < (1000 * 20))
      {
        //нельзя передавать данные чаще, чем 20 секунд
        //смещаем время передачи данных, если оно ближе, чем 20 секунд к основной передаче
        pirPreviousMillis = currentMillis;
      }
      
      if(currentMillis - pirPreviousMillis > 1000 * 30)
      {
        /*Serial.print("pir1_trg=");
        Serial.println(pir1_trg);
        Serial.print("prevPir1_trg=");
        Serial.println(prevPir1_trg);
        Serial.println("==============");*/
    
        pirPreviousMillis = currentMillis;
        prevPir1_trg = pir1_trg;
          
        ThingSpeak.setField(pir1Id, pir1_trg);
        ThingSpeak.writeFields(thChannelNumber, thWriteAPIKey);
      }
    }
  } 
}

void SendHtml(EthernetClient client) {
  // ответ контентом на запрос браузера
  ClientPrint_P(htmlHead, client);
  ClientPrint_P(PSTR("<body onload=\"GetState()\">\r\n"), client);
  ClientPrint_P(PSTR("<form action='' method=get>\r\n"), client);
  ClientPrint_P(PSTR("<fieldset style='display: inline-block'>\r\n"), client);
  ClientPrint_P(PSTR("<legend>Settings</legend>\r\n"), client);
  ClientPrint_P(PSTR("<table>\r\n"), client);
  for (int i = 0; i <= params_count; i++) {
    ClientPrint_P(PSTR("<tr>\r\n <td>"), client);
    client.print(paramsArray[i].pDescription);
    ClientPrint_P(PSTR("</td>\r\n <td align=center>"), client);
    ClientPrint_P(PSTR("<input "), client);
    ClientPrint_P(PSTR("type="), client);
    client.print(paramsArray[i].htmlInputType);
    ClientPrint_P(PSTR(" onchange=this.form.submit() name="), client);
    client.print(paramsArray[i].pName);
    if (paramsArray[i].htmlInputType == "checkbox")
    {
      if (paramsArray[i].value == 1)
      {
        ClientPrint_P(PSTR(" checked"), client);
      }
    }
    else
    {
      ClientPrint_P(PSTR(" min="), client);
      client.print(paramsArray[i].minValue);
      ClientPrint_P(PSTR(" max="), client);
      client.print(paramsArray[i].maxValue);
      ClientPrint_P(PSTR(" value="), client);
      client.print(paramsArray[i].value);
    }
    ClientPrint_P(PSTR("></td>\r\n <td>"), client);
    client.print(paramsArray[i].value);
    ClientPrint_P(PSTR("</td>\r\n</tr>\r\n"), client);
  };
  ClientPrint_P(PSTR("</table>\r\n"), client);
  //ClientPrint_P(PSTR("<input type=submit value=Save>\r\n"), client);
  ClientPrint_P(PSTR("</fieldset>\r\n"), client);
  ClientPrint_P(PSTR("</form>\r\n"), client);
  /*ClientPrint_P(PSTR("<input type=button value=reload onclick=window.location.replace('http://"), client);
  client.print(Ethernet.localIP());
  ClientPrint_P(PSTR("')>"), client);*/
  ClientPrint_P(br, client);
  ClientPrint_P(PSTR("<div id=state_div></div>\r\n"), client);
  ClientPrint_P(PSTR("</body>\r\n"), client);
  ClientPrint_P(PSTR("</html>"), client);
}

void SendAjax(EthernetClient client) {
  //sensor.read22(DHT22_PIN);
  sensorRead();
  
  // ответ контентом на AJAX запрос браузера
  ClientPrint_P(PSTR("<table><tr><td>"), client);
  ClientPrint_P(PSTR("Temperature"), client);
  ClientPrint_P(PSTR("</td><td>"), client);
  //client.print(sensor.temperature);
  client.print(temperature);
  ClientPrint_P(PSTR(" C"), client);
  ClientPrint_P(PSTR("</td></tr>"), client);
  //ClientPrint_P(br, client);

  ClientPrint_P(PSTR("<tr><td>"), client);
  ClientPrint_P(PSTR("Humidity"), client);
  ClientPrint_P(PSTR("</td><td>"), client);
  //client.print(sensor.humidity);
  client.print(humidity);
  ClientPrint_P(PSTR(" %"), client);
  ClientPrint_P(PSTR("</td></tr>"), client);
  //ClientPrint_P(br, client);

  ClientPrint_P(PSTR("<tr><td>"), client);
  ClientPrint_P(PSTR("Light level"), client);
  ClientPrint_P(PSTR("</td><td>"), client);
  client.print(light_level);
  ClientPrint_P(PSTR(" %"), client);
  ClientPrint_P(PSTR("</td></tr>"), client);
  //ClientPrint_P(br, client);

  ClientPrint_P(PSTR("<tr><td>"), client);
  ClientPrint_P(PSTR("Free RAM"), client);
  ClientPrint_P(PSTR("</td><td>"), client);
  client.print(freeRam());
  ClientPrint_P(PSTR("</td></tr>"), client);
  //ClientPrint_P(br, client);
  
  ClientPrint_P(PSTR("<tr><td>"), client);
  ClientPrint_P(PSTR("ATmega328P temperature"), client);
  ClientPrint_P(PSTR("</td><td>"), client);
  client.print(getAtmegaTemp());
  ClientPrint_P(PSTR("</td></tr></table>"), client);
}

void SendFavicon(EthernetClient client) 
{
  ClientPrint_P(PSTR("\r\n"), client);
}

// процедура обновления значения в массиве значений
void SetArrayValue(String parameter, uint8_t value) {
  // поиск параметра в массиве названий по названию
  // методом перебора
  for (uint8_t k = 0; k <= params_count; k++) {
    String param = String(paramsArray[k].pName);
    if (parameter.equalsIgnoreCase(param)){
      // обновление значения в массиве значений
      paramsArray[k].value = value;
      paramsArray[k].updated = true;
      // запись значения во внутреннюю память
      EEPROM.write(k, value);
    }
  }
}

// функция получения значения массива
int GetArrayValueByName(String pname) {
  int result = -1;
  // поиск параметра в массиве названий по названию
  // методом перебора
  for (uint8_t k = 0; k <= params_count; k++) 
  {
    //String param = String(paramsArray[k].pName);   
    if (pname.equalsIgnoreCase(paramsArray[k].pName)) result = paramsArray[k].value;
  }
  return result;
}

// процедура чтения переменной из flash и передачи в EthernetClient
void ClientPrint_P(PGM_P str, EthernetClient eclient) 
{
  for (uint8_t c; (c = pgm_read_byte(str)); str++) eclient.write(c);
}

// функция подсчета количества свободной оперативной памяти
static int freeRam() 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

// функция получения внутренней температуры atmega328
double getAtmegaTemp(void)
{
  unsigned int wADC;
  double t;

  // The internal temperature has to be used
  // with the internal reference of 1.1V.
  // Channel 8 can not be selected with
  // the analogRead function yet.

  // Set the internal reference and mux.
  ADMUX = (_BV(REFS1) | _BV(REFS0) | _BV(MUX3));
  ADCSRA |= _BV(ADEN);  // enable the ADC

  delay(20);            // wait for voltages to become stable.

  ADCSRA |= _BV(ADSC);  // Start the ADC

  // Detect end-of-conversion
  while (bit_is_set(ADCSRA,ADSC));

  // Reading register "ADCW" takes care of how to read ADCL and ADCH.
  wADC = ADCW;

  // The offset of 324.31 could be wrong. It is just an indication.
  t = (wADC - 324.31 ) / 1.22;

  // The returned temperature is in degrees Celcius.
  return (t);
}

void sensorRead()
{
  // учитываем минимальную частоту опроса датчика
  if(currentMillis - sensorPreviousMillis > sensorDelay)
  {
    sensorPreviousMillis = currentMillis;
    sensors_event_t event;
    
    dht.temperature().getEvent(&event);
    temperature = (isnan(event.temperature)) ? -50 : event.temperature;
    
    dht.humidity().getEvent(&event);
    humidity = (isnan(event.relative_humidity)) ? -1 : event.relative_humidity;
  }
}

