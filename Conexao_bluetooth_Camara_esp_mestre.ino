#include <RTClib.h>
#include <esp_now.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include "RTClib.h"
#include <EEPROM.h>

//MAC DO ESP DESSE CÓDIGO : 58:BF:25:A1:1B:68

//Acionamento do CO2
#define CO2 32

//Pino do potenciometro
#define pinPot 33

//RTC
RTC_DS1307 rtc;
char daysOfTheWeek[7][22] = {"Domingo", "Segunda-feira", "Terça-feira", "Quarta-feira", "Quinta-feira", "Sexta-feira", "Sabado"};

//Variaveis media movel
const int numLeituras = 20;
float leituras[numLeituras];
int indexLeituras = 0;
double total = 0;
float media;
int soma_media = 0;

//Definindo timer WatchDog
#define WDT_TIMEOUT 8

//Instanciando objeto tela LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

//Variaveis para salvamento no SD
char salvamento[50] = "0000,000000,0000,000000,00:00:00,00/00/0000,";

//Variaveis e objetos RTC
int dia, mes, ano, hora, minuto, segundo;

//Definindo estrutura de dados
typedef struct struct_message {
  int identificador;
  float msgTemp;
  float msgUmi;
  double msgCo2;
  int msgDia;
  int msgMes;
  int msgAno;
  int msgHora;
  int msgMin;
  int msgSeg;
} struct_message;

//Criando objetos da estrutura
struct_message message;
struct_message msgSend;

//Endereço mac do par 0C:B8:15:C3:3F:B4
uint8_t broadcastAddress[] = {0x0C, 0xB8, 0x15, 0xC3, 0x3F, 0xB4};

//informações do par
esp_now_peer_info_t peerInfo;

//Função de callback para o envio de dados
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");

}

//Variaveis para armazenar as temperaturas e co2
float temp1, temp2, temp_backup;
float umi1, umi2, umi_backup;
double co2_1, co2_2, co2_backup;

//Variaveis do potenciometro
int pot;
int setpoint_int;
float setpoint_float;

//Variaveis do estado de co2
int tempo_de_injecao_co2;
int tempo_de_homogenizacao_do_co2;
int estadoCo2;


//Variaveis de tempo
unsigned long tempo_pot = 0;
unsigned long tempo_impressoes = 0;
unsigned long tempo_sd = 0;
unsigned long tempo_envios = 0;
unsigned long tempo_co2 = 0;

//Função de callback para recebimento de dados
void OnDataRecv(const uint8_t* mac, const uint8_t *incomingData, int len) {
  memcpy(&message, incomingData, sizeof(message));
  Serial.print("Data recieved ");
  Serial.println(len);
  Serial.print("Dados recebidos do esp ");
  Serial.println(message.identificador);
  Serial.print("Temperatura = ");
  Serial.println(message.msgTemp);
  Serial.print("CO2 = ");
  Serial.println(message.msgCo2);

  //O ESP mestre diferencia a origem da mensagem através do número identificador
  //identificador == 1 para o servo 1 e identificador == 2 para o servo 2
  if (message.identificador == 1) {
    temp1 = message.msgTemp;
    co2_1 = message.msgCo2;
  }
  else if (message.identificador == 2) {
    temp2 = message.msgTemp;
    umi2 = message.msgUmi;
    co2_2 = message.msgCo2;
    if (co2_2 != 0) {
      co2_backup = co2_2 / 10000;
    }
  }
}

void setup() {
  //Iniciando Serial e I2C
  Serial.begin(9600);
  Wire.begin();
  pinMode(CO2, OUTPUT);

  //Ativação de co2 deve começar desligada
  digitalWrite(CO2, LOW);

  //iniciando lcd
  lcd.begin();
  lcd.backlight();
  lcd.print("Iniciando");
  delay(5000);
  lcd.clear();

  //Verificando se o rtc esta conectado
  if(!rtc.begin()){
    Serial.println("Erro rtc");
    Serial.flush();
    abort();
  }

  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }
  
  //Iniciando SD
//  if (!SD.begin()) {
//    lcd.clear();
//    lcd.setCursor(0, 0);
//    lcd.print("ERRO CARTAO SD");
//    while (1) {
//    }
//  }

  //verifica a existencia do arquivo, se nao existir cria outro
  if (!SD.exists("/d0.txt")) {
    //arquivo nao existe e sera criado
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("CRIANDO ARQUIVO TXT");
    writeFile(SD, "/d0.txt", "TEMPERATURA_1, UMIDADE_1, CO2_1, TEMPERATURA_2, UMIDADE_2, CO2_2, HORA, DATA,");
    appendFile(SD, "/d0.txt", "\r\n");
  }
  else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ARQUIVO TXT");
    lcd.setCursor(0, 1);
    lcd.print("JA EXISTE");
    appendFile(SD, "/d0.txt", "NOVA LEITURA");
    appendFile(SD, "/d0.txt", "\r\n");

  }

  //WiFi para modo Station
  WiFi.mode(WIFI_STA);
  Serial.println("Wifi iniciado");

  //Iniciando ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  } else if (esp_now_init() == ESP_OK) {
    Serial.println("ESPNow Init Success");

    //Registrando as funçoes de callback
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);
  }

  //Registrando Par
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if(esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Falha ao adicionar par");
    return;
  }

  //Iniciando contadores de tempo
  tempo_impressoes = millis();
  tempo_sd = millis();
}

//Inicio do loop -----------------------------------------
void loop() {

  //Filtrando o input do potenciometro atraves de media movel
  //a filtragem e realizada para evitar ruido da entrada analogica e manter o setpoint estavel
  if (millis() - tempo_pot > 50) {
    total = total - leituras[indexLeituras];
    leituras[indexLeituras] = analogRead(33);
    soma_media++;
    total = total + leituras[indexLeituras];
    indexLeituras += 1;
    if (indexLeituras >= 20) {
      indexLeituras = 0;
    }

    media = total / 20;
    pot = media;
   // Serial.print(pot); Serial.print("\t\t");
    setpoint_float = mapFloat(pot, 0, 4095, 0, 10);
    //Serial.println(setpoint_float);
  }


  //Imprimindo os dados na tela LCD -- 
  //FORMATAÇÃO PLACEHOLDER -- 
  //IMPRIMIR A MEDIA QUANDO SERVO 1 FOR IMPLEMENTADO__
  if (millis() - tempo_impressoes > 500) {

    //rtc
    DateTime now = rtc.now();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(now.hour()); lcd.print(":");
    lcd.print(now.minute()); lcd.print(":");
    lcd.print(now.second());
    
    lcd.setCursor(10, 0);
    lcd.print(now.day()); lcd.print("/");
    lcd.print(now.month()); lcd.print("/");
    lcd.print(now.year());
    
    lcd.setCursor(0, 2);
    lcd.print("TEM="); lcd.print(temp2);
    lcd.setCursor(0, 3);
    lcd.print("UMI="); lcd.print(umi2);
    lcd.setCursor(11, 2);
    lcd.print("CO2="); lcd.print(co2_backup);
    lcd.setCursor(11, 3);
    lcd.print("SET="); lcd.print(setpoint_float);
    tempo_impressoes = millis();
  } 

  //Controle de CO2
  if(co2_backup < setpoint_float){
    verifica_estado_co2(setpoint_float, co2_backup);
    if(estadoCo2 == 0 && co2_backup < 9.5){
      //Acionamento do co2 -- descomentar quando a maquina estiver finalizada
//      digitalWrite(CO2, HIGH);
      estadoCo2 = 1;
      tempo_co2 = millis();
    }
    if((millis() - tempo_co2) > tempo_de_injecao_co2 && estadoCo2 == 1){
      digitalWrite(CO2, LOW);
      estadoCo2 = 2;
      tempo_co2 = millis();
    }
    if((millis() - tempo_co2) > tempo_de_homogenizacao_do_co2 && estadoCo2 == 2){
      estadoCo2 = 0;
    }
  }
  else{
    if((millis() - tempo_co2) > tempo_de_injecao_co2 && estadoCo2 == 1){
      digitalWrite(CO2, LOW);
      estadoCo2 = 2;
      tempo_co2 = millis();
    }
    if((millis() - tempo_co2) > tempo_de_homogenizacao_do_co2 && estadoCo2 == 2){
      estadoCo2 = 0;
    }
  }


  
  if (millis() - tempo_envios > 30000000) {
    //formatando estrutura de dados
    //Identificador configurado como 3 para ser reconhecido no envio do email
    //Código comentado PLACEHOLDER até o servo 1 ser implementado
    //msgSend.msgTemp = (temp1 + temp2) / 2;
    //msgSend.msgUmi = (umi2 + umi2) / 2;
    //msgSend.msgCo2 = (co2_1 + co2_2) / 2;
    DateTime agora = rtc.now(); 
    msgSend.msgTemp = temp2;
    msgSend.msgUmi = umi2;
    msgSend.msgCo2 = co2_2;
    msgSend.identificador = 3;
    msgSend.msgDia = agora.day();
    msgSend.msgMes = agora.month();
    msgSend.msgAno = agora.year();
    msgSend.msgHora = agora.hour();
    msgSend.msgMin = agora.minute();
    msgSend.msgSeg = agora.second();

    Serial.println(msgSend.msgHora);
    Serial.println("------*-----");
    //Enviando mensagem via ESP-NOW
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t*) &msgSend, sizeof(msgSend));

    if (result == ESP_OK) {
      Serial.println("Sending confirmed");
    }
    else {
      Serial.println("Sending error");;
    }
    tempo_envios = millis();
  }
  //salva no cartao sd
  if (millis() - tempo_sd > 10000) {
    sprintf(salvamento, "%ld,%ld,%ld,%ld,%ld,%ld,%d:%d:%d,%d/%d/%d,", temp1 , umi1, co2_1, temp2, umi2, co2_2, hora, minuto, segundo, dia, mes, ano);
    appendFile(SD, "/d0.txt", salvamento);
    appendFile(SD, "/d0.txt", "\r\n");

    tempo_sd = millis();
  }

}
//Final do loop ------------------------------------------

//funcoes do cartao sd------------------------------------
void listDir(fs::FS &fs, const
             char * dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels) {
        listDir(fs, file.name(), levels - 1);
      }
    } else {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void createDir(fs::FS &fs, const char * path) {
  Serial.printf("Creating Dir: %s\n", path);
  if (fs.mkdir(path)) {
    Serial.println("Dir created");
  } else {
    Serial.println("mkdir failed");
  }
}

void removeDir(fs::FS &fs, const char * path) {
  Serial.printf("Removing Dir: %s\n", path);
  if (fs.rmdir(path)) {
    Serial.println("Dir removed");
  } else {
    Serial.println("rmdir failed");
  }
}

void readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\n", path);

  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void renameFile(fs::FS &fs, const char * path1, const char * path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (fs.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}

void deleteFile(fs::FS &fs, const char * path) {
  Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

void testFileIO(fs::FS &fs, const char * path) {
  File file = fs.open(path);
  static uint8_t buf[512];
  size_t len = 0;
  uint32_t start = millis();
  uint32_t end = start;
  if (file) {
    len = file.size();
    size_t flen = len;
    start = millis();
    while (len) {
      size_t toRead = len;
      if (toRead > 512) {
        toRead = 512;
      }
      file.read(buf, toRead);
      len -= toRead;
    }
    end = millis() - start;
    Serial.printf("%u bytes read for %u ms\n", flen, end);
    file.close();
  } else {
    Serial.println("Failed to open file for reading");
  }


  file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  size_t i;
  start = millis();
  for (i = 0; i < 2048; i++) {
    file.write(buf, 512);
  }
  end = millis() - start;
  Serial.printf("%u bytes written for %u ms\n", 2048 * 512, end);
  file.close();
}

//Função mapeamento
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

//MediaMovel
long leituraAnalogica() {
  pot = analogRead(pinPot);
  Serial.print(F("Pot: "));
  Serial.print(pot);
}

long mediaMovel() {
  long media;
  total = total - leituras[indexLeituras];
  leituras[indexLeituras] = analogRead(pinPot);
  total = total + leituras[indexLeituras];
  indexLeituras += 1;
  if (indexLeituras >= numLeituras) {
    indexLeituras = 0;
  }
}

void verifica_estado_co2(float set_point, float leitura) {
  if ((set_point - leitura) >= 2) {
    tempo_de_injecao_co2 = 1500;
    tempo_de_homogenizacao_do_co2 = 8000;
    //return 0;
  }
  if ((set_point - leitura) >= 1.1 && (set_point - leitura) < 2) {
    tempo_de_injecao_co2 = 1200;
    tempo_de_homogenizacao_do_co2 = 10000;
    //return 1;
  }
  if ((set_point - leitura) >= 0.51 && (set_point - leitura) < 1.1) {
    tempo_de_injecao_co2 = 1000;
    tempo_de_homogenizacao_do_co2 = 15000;
    //return 2;
  }
  if ((set_point - leitura) >= 0.11 && (set_point - leitura) < 0.51) {
    tempo_de_injecao_co2 = 800;
    tempo_de_homogenizacao_do_co2 = 17000;
    //return 3;
  }
  if ((set_point - leitura) < 0.11 ) {
    tempo_de_injecao_co2 = 600;
    tempo_de_homogenizacao_do_co2 = 20000;
    //return 4;
  }
}
