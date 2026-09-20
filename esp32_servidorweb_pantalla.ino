/*
  ============================================================
  Controlador Modbus NR9000 con ESP-32
  ============================================================

  Autor: Esteban Porqueras Araque

  Descripción:

  Programa para controlar y supervisar el controlador ISMA
  Controlli NR9000 mediante comunicación Modbus RTU sobre
  RS485.

  Permite consultar la temperatura ambiente y la consigna,
  así como controlar el modo de funcionamiento y la velocidad
  del ventilador.

  Los datos se muestran en una pantalla Nokia 5110.

  El ESP32 proporciona además una interfaz web mediante Wi-Fi
  desde la que se puede consultar el estado del NR9000 y
  modificar la consigna de temperatura.

  La consigna de temperatura puede aumentarse o reducirse
  directamente desde la interfaz web del ESP32.

  ============================================================
  CONEXIONES DE HARDWARE
  ============================================================

  ESP32 <-> MAX485
  ----------------

  ESP32 5V       -> MAX485 VCC
  ESP32 GND      -> MAX485 GND
  GPIO22 (TX2)   -> MAX485 DI
  GPIO17 (RX2)   -> MAX485 RO
  GPIO21         -> MAX485 DE + RE

  Control de dirección RS485:

  GPIO21 HIGH -> Transmisión
  GPIO21 LOW  -> Recepción

  Divisor de tensión para la entrada RX:

  MAX485 RO -> 10 kΩ -> GPIO17
                         |
                        20 kΩ
                         |
                        GND

  El divisor adapta la señal de salida del MAX485 al nivel
  de entrada de 3,3 V del ESP32.

  ============================================================
  ESP32 <-> NOKIA 5110 (PCD8544)
  ============================================================

  Nokia 5110 CLK -> GPIO18
  Nokia 5110 DIN -> GPIO23
  Nokia 5110 DC  -> GPIO16
  Nokia 5110 CE  -> GPIO5
  Nokia 5110 RST -> GPIO4

  Contraste de pantalla: 60

  ============================================================
  COMUNICACIÓN MODBUS RTU
  ============================================================

  Controlador: ISMA Controlli NR9000
  Dirección esclavo: 42 (0x2A)
  Interfaz física: RS485
  Velocidad: 9600 baudios
  Formato: 8N1
  UART utilizada: Serial2

  Registros utilizados:

    9014 -> Modo de funcionamiento
    9015 -> Consigna de temperatura
    9019 -> Velocidad / modo del ventilador
    9020 -> Temperatura ambiente

  ============================================================
  FUNCIONES PRINCIPALES
  ============================================================

  - Lectura de la temperatura ambiente.
  - Lectura de la consigna de temperatura.
  - Modificación de la consigna.
  - Selección del modo de funcionamiento.
  - Selección de la velocidad del ventilador.
  - Visualización de datos en pantalla Nokia 5110.
  - Comunicación Modbus RTU mediante RS485.
  - Servidor web integrado en el ESP32.
  - Control y supervisión desde un teléfono móvil
    conectado al punto de acceso Wi-Fi del ESP32.

  ============================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

// ============================================================
// WIFI
// ============================================================

const char* ssid = "NR9000-ESP32";
const char* password = "12345678";

WebServer server(80);

// ============================================================
// NOKIA 5110
// CLK, DIN, DC, CE, RST
// ============================================================

Adafruit_PCD8544 display(18, 23, 16, 5, 4);

// ============================================================
// RS485 / MAX485
// ============================================================

#define RXD2 17
#define TXD2 22
#define DE_RE 21

HardwareSerial RS485(2);

// ============================================================
// NR9000
// ============================================================

#define NR9000_ID 42

#define REG_MODO       9014
#define REG_CONSIGNA   9015
#define REG_VENTILADOR 9019
#define REG_TEMPERATURA 9020

// ============================================================
// VARIABLES
// ============================================================

float temperaturaAmbiente = 0.0;
float consigna = 25.0;

int modo = 0;
int ventilador = 4;

unsigned long ultimaLectura = 0;

// ============================================================
// CRC MODBUS
// ============================================================

uint16_t modbusCRC(uint8_t *buffer, uint8_t length)
{
  uint16_t crc = 0xFFFF;

  for (uint8_t pos = 0; pos < length; pos++)
  {
    crc ^= buffer[pos];

    for (uint8_t i = 8; i != 0; i--)
    {
      if (crc & 0x0001)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }

  return crc;
}

// ============================================================
// LEER REGISTRO MODBUS
// ============================================================

int leerRegistro(uint16_t registro)
{
  uint8_t request[8];

  request[0] = NR9000_ID;
  request[1] = 0x03;

  request[2] = registro >> 8;
  request[3] = registro & 0xFF;

  request[4] = 0x00;
  request[5] = 0x01;

  uint16_t crc = modbusCRC(request, 6);

  request[6] = crc & 0xFF;
  request[7] = crc >> 8;

  while (RS485.available())
    RS485.read();

  digitalWrite(DE_RE, HIGH);

  delayMicroseconds(500);

  RS485.write(request, 8);
  RS485.flush();

  delayMicroseconds(1500);

  digitalWrite(DE_RE, LOW);

  unsigned long inicio = millis();

  while (RS485.available() < 5)
  {
    if (millis() - inicio > 1000)
      return -1;
  }

  uint8_t response[7];

  for (int i = 0; i < 5; i++)
    response[i] = RS485.read();

  // Excepción Modbus
  if (response[1] & 0x80)
  {
    uint8_t codigoError = response[2];

    while (RS485.available())
      RS485.read();

    return -100 - codigoError;
  }

  while (RS485.available() < 2)
  {
    if (millis() - inicio > 1000)
      return -1;
  }

  response[5] = RS485.read();
  response[6] = RS485.read();

  if (response[0] != NR9000_ID)
    return -2;

  if (response[1] != 0x03)
    return -3;

  if (response[2] != 2)
    return -4;

  uint16_t crcRecibido =
    response[5] |
    ((uint16_t)response[6] << 8);

  uint16_t crcCalculado =
    modbusCRC(response, 5);

  if (crcRecibido != crcCalculado)
    return -5;

  uint16_t valor =
    ((uint16_t)response[3] << 8) |
    response[4];

  delay(80);

  return valor;
}

// ============================================================
// ESCRIBIR REGISTRO MODBUS
// ============================================================

bool escribirRegistro(uint16_t registro, uint16_t valor)
{
  uint8_t request[8];

  request[0] = NR9000_ID;
  request[1] = 0x06;

  request[2] = registro >> 8;
  request[3] = registro & 0xFF;

  request[4] = valor >> 8;
  request[5] = valor & 0xFF;

  uint16_t crc = modbusCRC(request, 6);

  request[6] = crc & 0xFF;
  request[7] = crc >> 8;

  while (RS485.available())
    RS485.read();

  digitalWrite(DE_RE, HIGH);

  delayMicroseconds(500);

  RS485.write(request, 8);
  RS485.flush();

  delayMicroseconds(1500);

  digitalWrite(DE_RE, LOW);

  unsigned long inicio = millis();

  while (RS485.available() < 5)
  {
    if (millis() - inicio > 1000)
      return false;
  }

  uint8_t response[8];

  for (int i = 0; i < 5; i++)
    response[i] = RS485.read();

  // Excepción Modbus
  if (response[1] & 0x80)
  {
    while (RS485.available())
      RS485.read();

    return false;
  }

  while (RS485.available() < 3)
  {
    if (millis() - inicio > 1000)
      return false;
  }

  response[5] = RS485.read();
  response[6] = RS485.read();
  response[7] = RS485.read();

  for (int i = 0; i < 8; i++)
  {
    if (response[i] != request[i])
      return false;
  }

  return true;
}

// ============================================================
// CONVERSION TEMPERATURA AMBIENTE
// REGISTRO 9020
// ============================================================

float rawATemperatura(uint16_t raw)
{
  int8_t valor = (int8_t)(raw & 0xFF);

  return 25.6 + (valor * 0.1);
}

// ============================================================
// CONVERSION CONSIGNA
// REGISTRO 9015
// ============================================================

float rawAConsigna(uint16_t raw)
{
  if (raw >= 100)
    return raw / 10.0;

  return (raw + 256) / 10.0;
}

uint16_t consignaARaw(float temperatura)
{
  return (uint16_t)round(temperatura * 10.0);
}

// ============================================================
// APLICAR CONSIGNA
// ============================================================

bool aplicarConsigna(float nuevaConsigna)
{
  if (nuevaConsigna < 10.0)
    nuevaConsigna = 10.0;

  if (nuevaConsigna > 30.0)
    nuevaConsigna = 30.0;

  nuevaConsigna = round(nuevaConsigna * 2.0) / 2.0;

  uint16_t raw = consignaARaw(nuevaConsigna);

  bool resultado =
    escribirRegistro(REG_CONSIGNA, raw);

  if (resultado)
  {
    consigna = nuevaConsigna;
  }

  return resultado;
}

// ============================================================
// ACTUALIZAR DATOS DEL NR9000
// ============================================================

void actualizarDatos()
{
  int rawTemperatura =
    leerRegistro(REG_TEMPERATURA);

  if (rawTemperatura >= 0)
  {
    temperaturaAmbiente =
      rawATemperatura(rawTemperatura);
  }

  int rawConsigna =
    leerRegistro(REG_CONSIGNA);

  if (rawConsigna >= 0)
  {
    consigna =
      rawAConsigna(rawConsigna);
  }

  int rawModo =
    leerRegistro(REG_MODO);

  if (rawModo >= 0)
  {
    modo = rawModo;
  }

  int rawVentilador =
    leerRegistro(REG_VENTILADOR);

  if (rawVentilador >= 0)
  {
    ventilador = rawVentilador;
  }
}

// ============================================================
// MOSTRAR EN NOKIA 5110
// ============================================================

void actualizarPantalla()
{
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(BLACK);

  display.setCursor(0, 0);
  display.print("NR9000");

  display.setCursor(0, 10);
  display.print("Temp: ");

  display.print(temperaturaAmbiente, 1);
  display.print(" C");

  display.setCursor(0, 20);
  display.print("Cons: ");

  display.print(consigna, 1);
  display.print(" C");

  display.setCursor(0, 30);
  display.print("Modo: ");

  if (modo == 0)
    display.print("CONF");

  else if (modo == 1)
    display.print("ECO");

  else if (modo == 2)
    display.print("OFF");

  else
    display.print(modo);

  display.setCursor(0, 40);
  display.print("Fan: ");

  if (ventilador == 0)
    display.print("STOP");

  else if (ventilador == 1)
    display.print("V1");

  else if (ventilador == 2)
    display.print("V2");

  else if (ventilador == 3)
    display.print("V3");

  else if (ventilador == 4)
    display.print("AUTO");

  else
    display.print(ventilador);

  display.display();
}

// ============================================================
// PAGINA WEB
// ============================================================

void paginaPrincipal()
{
  String html;

  html += "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";

  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";

  html += "<meta charset='UTF-8'>";

  html += "<title>NR9000</title>";

  html += "<style>";

  html += "body{font-family:Arial;text-align:center;background:#f2f2f2;margin:20px;}";

  html += ".caja{background:white;padding:20px;border-radius:15px;max-width:400px;margin:auto;}";

  html += "button{font-size:20px;padding:12px;margin:5px;border-radius:10px;}";

  html += ".valor{font-size:32px;margin:15px;}";

  html += "</style>";

  html += "</head>";

  html += "<body>";

  html += "<div class='caja'>";

  html += "<h1>NR9000</h1>";

  html += "<p>Temperatura ambiente</p>";

  html += "<div class='valor'>";

  html += String(temperaturaAmbiente, 1);

  html += " &deg;C</div>";

  html += "<p>Consigna</p>";

  html += "<div class='valor'>";

  html += String(consigna, 1);

  html += " &deg;C</div>";

  html += "<a href='/bajarconsigna'>";

  html += "<button>-</button>";

  html += "</a>";

  html += "<a href='/subirconsigna'>";

  html += "<button>+</button>";

  html += "</a>";

  html += "<hr>";

  html += "<h3>Modo</h3>";

  html += "<a href='/setmodo?valor=0'>";

  html += "<button>CONFORT</button>";

  html += "</a>";

  html += "<a href='/setmodo?valor=1'>";

  html += "<button>ECO</button>";

  html += "</a>";

  html += "<a href='/setmodo?valor=2'>";

  html += "<button>OFF</button>";

  html += "</a>";

  html += "<hr>";

  html += "<h3>Ventilador</h3>";

  html += "<a href='/setfan?valor=0'>";

  html += "<button>STOP</button>";

  html += "</a>";

  html += "<a href='/setfan?valor=1'>";

  html += "<button>V1</button>";

  html += "</a>";

  html += "<a href='/setfan?valor=2'>";

  html += "<button>V2</button>";

  html += "</a>";

  html += "<a href='/setfan?valor=3'>";

  html += "<button>V3</button>";

  html += "</a>";

  html += "<a href='/setfan?valor=4'>";

  html += "<button>AUTO</button>";

  html += "</a>";

  html += "</div>";

  html += "</body>";

  html += "</html>";

  server.send(200, "text/html; charset=utf-8", html);
}

// ============================================================
// BAJAR CONSIGNA
// ============================================================

void bajarConsigna()
{
  aplicarConsigna(consigna - 0.5);

  server.sendHeader("Location", "/");

  server.send(303);
}

// ============================================================
// SUBIR CONSIGNA
// ============================================================

void subirConsigna()
{
  aplicarConsigna(consigna + 0.5);

  server.sendHeader("Location", "/");

  server.send(303);
}

// ============================================================
// ESTABLECER CONSIGNA DESDE URL
// ============================================================

void setConsigna()
{
  if (server.hasArg("valor"))
  {
    float valor =
      server.arg("valor").toFloat();

    aplicarConsigna(valor);
  }

  server.sendHeader("Location", "/");

  server.send(303);
}

// ============================================================
// CAMBIAR MODO
// ============================================================

void setModo()
{
  if (server.hasArg("valor"))
  {
    int valor =
      server.arg("valor").toInt();

    if (valor >= 0 && valor <= 2)
    {
      if (escribirRegistro(REG_MODO, valor))
      {
        modo = valor;
      }
    }
  }

  server.sendHeader("Location", "/");

  server.send(303);
}

// ============================================================
// CAMBIAR VENTILADOR
// ============================================================

void setFan()
{
  if (server.hasArg("valor"))
  {
    int valor =
      server.arg("valor").toInt();

    if (valor >= 0 && valor <= 4)
    {
      if (escribirRegistro(REG_VENTILADOR, valor))
      {
        ventilador = valor;
      }
    }
  }

  server.sendHeader("Location", "/");

  server.send(303);
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  // -----------------------------
  // MAX485
  // -----------------------------

  pinMode(DE_RE, OUTPUT);

  digitalWrite(DE_RE, LOW);

  RS485.begin(
    9600,
    SERIAL_8N1,
    RXD2,
    TXD2
  );

  RS485.setTimeout(1000);

  // -----------------------------
  // NOKIA 5110
  // -----------------------------

  display.begin();

  display.setContrast(60);

  display.clearDisplay();

  display.setTextSize(1);

  display.setTextColor(BLACK);

  display.setCursor(0, 0);

  display.println("NR9000");

  display.println();

  display.println("Iniciando...");

  display.display();

  delay(1000);

  // -----------------------------
  // WIFI
  // -----------------------------

  WiFi.mode(WIFI_AP);

  WiFi.softAP(
    ssid,
    password
  );

  IPAddress ip =
    WiFi.softAPIP();

  Serial.println();

  Serial.println("================================");
  Serial.println("NR9000 ESP32");
  Serial.println("================================");

  Serial.print("IP: ");
  Serial.println(ip);

  Serial.println();

  // -----------------------------
  // SERVIDOR WEB
  // -----------------------------

  server.on("/", paginaPrincipal);

  server.on(
    "/bajarconsigna",
    bajarConsigna
  );

  server.on(
    "/subirconsigna",
    subirConsigna
  );

  server.on(
    "/setconsigna",
    setConsigna
  );

  server.on(
    "/setmodo",
    setModo
  );

  server.on(
    "/setfan",
    setFan
  );

  server.begin();

  Serial.println("Servidor web iniciado");

  // -----------------------------
  // PRIMERA LECTURA
  // -----------------------------

  actualizarDatos();

  actualizarPantalla();
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  server.handleClient();

  // Actualizar datos cada 2 segundos

  if (millis() - ultimaLectura >= 2000)
  {
    ultimaLectura = millis();

    actualizarDatos();

    actualizarPantalla();

    Serial.print("Temperatura: ");
    Serial.print(temperaturaAmbiente, 1);

    Serial.print(" C | Consigna: ");
    Serial.print(consigna, 1);

    Serial.print(" C | Modo: ");
    Serial.print(modo);

    Serial.print(" | Fan: ");
    Serial.println(ventilador);
  }
}


