# Controlador Modbus NR9000 con ESP32

Proyecto basado en un **ESP32** para comunicarse mediante **Modbus RTU sobre RS-485** con un controlador de fan-coil **ISMA Controlli NR9000**.

El sistema permite consultar y modificar diferentes parámetros del controlador y mostrar la información tanto en una pantalla **Nokia 5110** como mediante una interfaz web alojada directamente en el ESP32.

## Descripción del proyecto

El ESP32 actúa como interfaz entre el controlador NR9000 y el usuario.

La comunicación se realiza mediante:

* **ESP32**
* Transceptor **MAX485**
* Bus **RS-485**
* Protocolo **Modbus RTU**
* Controlador **ISMA Controlli NR9000**
* Pantalla gráfica **Nokia 5110 / PCD8544**
* Servidor web integrado en el ESP32

El ESP32 puede leer información del NR9000 y modificar determinados registros mediante peticiones Modbus.

La interfaz web permite controlar algunos parámetros sin necesidad de utilizar directamente un software Modbus externo.

## Arquitectura del sistema

```text
                  ┌─────────────────────┐
                  │      NR9000         │
                  │  ISMA Controlli     │
                  └──────────┬──────────┘
                             │
                         RS-485
                             │
                  ┌──────────▼──────────┐
                  │       MAX485        │
                  │     RS-485 / TTL    │
                  └──────────┬──────────┘
                             │
                           UART
                             │
                  ┌──────────▼──────────┐
                  │        ESP32        │
                  │                     │
                  │  Modbus RTU         │
                  │  Servidor web       │
                  │  Control del LCD    │
                  └──────┬───────┬──────┘
                         │       │
                       SPI       │ Wi-Fi
                         │       │
              ┌──────────▼──┐    │
              │ Nokia 5110  │    │
              │   PCD8544   │    │
              └─────────────┘    │
                                  │
                           ┌──────▼──────┐
                           │ Navegador   │
                           │   Web       │
                           └─────────────┘
```

## Comunicación Modbus RTU

La comunicación entre el ESP32 y el NR9000 utiliza **Modbus RTU sobre RS-485**.

Los parámetros utilizados son:

```text
Velocidad:       9600 baudios
Bits de datos:   8
Paridad:         Ninguna
Bits de parada:  1
Formato:         8N1
Dirección:       42 decimal
Dirección HEX:   0x2A
```

En Arduino:

```cpp
RS485.begin(9600, SERIAL_8N1, RXD2, TXD2);
```

La dirección Modbus del NR9000 utilizada en este proyecto es:

```text
42 decimal = 0x2A
```

## Estructura de una trama Modbus RTU

Una petición de lectura utiliza la función `03`.

Ejemplo:

```text
2A 03 23 36 00 01 CRC
```

La estructura es:

```text
┌────────┬──────────┬────────────┬────────────┬──────────┐
│ Slave  │ Función  │ Dirección  │ Cantidad   │   CRC    │
│  1 byte│  1 byte  │   2 bytes  │   2 bytes  │ 2 bytes  │
└────────┴──────────┴────────────┴────────────┴──────────┘
```

Donde:

* **Slave**: dirección del dispositivo Modbus.
* **Función**: operación solicitada.
* **Dirección**: registro que se quiere consultar o modificar.
* **Cantidad**: número de registros solicitados.
* **CRC**: comprobación de errores de la trama.

## Lectura de registros

Para leer un registro se utiliza la función Modbus:

```text
03 - Read Holding Registers
```

La petición tiene 8 bytes:

```text
[ID] [03] [REG_H] [REG_L] [QTY_H] [QTY_L] [CRC_L] [CRC_H]
```

Para leer un único registro:

```text
QTY = 1
```

Por ejemplo, para leer el registro `9015`:

```text
9015 decimal = 0x2337
```

La petición sería conceptualmente:

```text
2A 03 23 37 00 01 CRC
```

La respuesta normal contiene:

```text
[ID] [03] [02] [DATA_H] [DATA_L] [CRC_L] [CRC_H]
```

## Escritura de registros

Para modificar un registro se utiliza la función:

```text
06 - Write Single Register
```

La petición tiene la estructura:

```text
[ID] [06] [REG_H] [REG_L] [DATA_H] [DATA_L] [CRC_L] [CRC_H]
```

Por ejemplo, para escribir el registro `9014` con el valor `2`:

```text
2A 06 23 36 00 02 CRC
```

El NR9000 responde repitiendo la petición cuando la escritura es aceptada.

## CRC Modbus

El CRC utilizado es el estándar **CRC-16 Modbus**.

El cálculo comienza con:

```text
0xFFFF
```

y utiliza el polinomio:

```text
0xA001
```

La transmisión del CRC se realiza con:

```text
CRC Low
CRC High
```

La función utilizada en el proyecto es:

```cpp
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
```

# Registros utilizados

Los siguientes registros han sido comprobados directamente sobre el **NR9000 utilizado en este proyecto**.

| Registro | Parámetro                | Acceso | Unidad | Formato / valores                          |
| -------- | ------------------------ | ------ | ------ | ------------------------------------------ |
| `9014`   | Modo de funcionamiento   | R/W    | —      | `0` Confort, `1` Eco, `2` Off              |
| `9015`   | Consigna de temperatura  | R/W    | °C     | Décimas de grado                           |
| `9019`   | Velocidad del ventilador | R/W    | —      | `0` Stop, `1` V1, `2` V2, `3` V3, `4` Auto |
| `9020`   | Temperatura ambiente     | R      | °C     | Formato específico observado en el NR9000  |

> **Nota:** los registros y formatos indicados se han obtenido mediante pruebas sobre el controlador NR9000 utilizado en este proyecto. No se deben asumir automáticamente para otros modelos o versiones del controlador.

## Registro 9014 — Modo de funcionamiento

```text
Registro: 9014
Acceso:   Lectura / Escritura
Unidad:   —
```

Valores comprobados:

```text
0 = CONFORT / ON
1 = ECO
2 = OFF
```

El valor `2` ha sido comprobado físicamente.

Al escribir:

```text
9014 = 2
```

el controlador NR9000 se apaga completamente:

* La pantalla del termostato se apaga.
* El controlador deja de funcionar en modo normal.
* Las válvulas dejan de actuar.
* El ventilador se detiene.

Por tanto, en este proyecto:

```text
9014 → 0 = Confort
9014 → 1 = Eco
9014 → 2 = Off
```

## Registro 9015 — Consigna de temperatura

```text
Registro: 9015
Acceso:   Lectura / Escritura
Unidad:   °C
Formato:  Décimas de grado
```

La consigna se representa en décimas de grado.

Ejemplos observados:

```text
20,0 °C → 200
21,0 °C → 210
24,0 °C → 240
25,0 °C → 250
25,5 °C → 255
```

A partir de `256`, la lectura del registro vuelve a comenzar desde `0` porque el valor observado se comporta como un byte de 8 bits:

```text
260 → 4
265 → 9
270 → 14
280 → 24
```

Por ejemplo:

```text
26,0 °C × 10 = 260

260 decimal = 0x0104

El NR9000 devuelve:

0x04 = 4
```

La conversión observada para la lectura es:

```cpp
float rawAConsigna(uint16_t raw)
{
  if (raw >= 100)
    return raw / 10.0;

  return (raw + 256) / 10.0;
}
```

Por ejemplo:

```text
raw 250 → 25,0 °C
raw 255 → 25,5 °C
raw   4 → 26,0 °C
raw   9 → 26,5 °C
raw  14 → 27,0 °C
raw  24 → 28,0 °C
```

### Escritura de la consigna

Para generar el valor de escritura se utiliza:

```cpp
uint16_t consignaARaw(float temperatura)
{
  return (uint16_t)round(temperatura * 10.0);
}
```

Ejemplos:

```text
20,0 °C → 200
21,0 °C → 210
25,0 °C → 250
25,5 °C → 255
26,0 °C → 260
27,0 °C → 270
28,0 °C → 280
```

Sin embargo, existe una diferencia importante entre la **representación Modbus leída** y la **aceptación física de la consigna por el controlador**.

Durante las pruebas se ha observado que el registro puede devolver valores correspondientes a temperaturas superiores a `25,5 °C`, pero todavía se está verificando si el NR9000 acepta físicamente como consigna valores superiores a:

```text
255 = 25,5 °C
```

Por este motivo, la aceptación real de consignas superiores a `25,5 °C` queda pendiente de confirmación mediante pruebas directas sobre el equipo.

## Registro 9019 — Velocidad del ventilador

```text
Registro: 9019
Acceso:   Lectura / Escritura
Unidad:   —
```

Valores comprobados:

```text
0 = STOP
1 = V1
2 = V2
3 = V3
4 = AUTO
```

Por tanto:

```text
9019 → 0 = Ventilador parado
9019 → 1 = Velocidad 1
9019 → 2 = Velocidad 2
9019 → 3 = Velocidad 3
9019 → 4 = Automático
```

## Registro 9020 — Temperatura ambiente

```text
Registro: 9020
Acceso:   Lectura
Unidad:   °C
```

Este registro corresponde a la temperatura ambiente observada en el NR9000.

Durante las pruebas se obtuvieron valores como:

```text
Raw  6 → 26,2 °C
Raw  7 → 26,3 °C
Raw 17 → 27,3 °C
Raw 31 → 28,7 °C
Raw 32 → 28,8 °C
```

El comportamiento observado corresponde a un valor de 8 bits con signo y una referencia de `25,6 °C`.

La conversión utilizada es:

```cpp
float rawATemperatura(uint16_t raw)
{
  int8_t valor = (int8_t)(raw & 0xFF);

  return 25.6 + (valor * 0.1);
}
```

Por ejemplo:

```text
Raw 6  → 25,6 + 0,6 = 26,2 °C
Raw 17 → 25,6 + 1,7 = 27,3 °C
Raw 31 → 25,6 + 3,1 = 28,7 °C
```

Este formato se considera específico del comportamiento observado en el NR9000 utilizado en las pruebas.

## Resumen de registros

```text
9014 → Modo de funcionamiento
       0 = Confort
       1 = Eco
       2 = Off

9015 → Consigna de temperatura
       Unidad: °C
       Formato: décimas de grado

9019 → Velocidad del ventilador
       0 = Stop
       1 = V1
       2 = V2
       3 = V3
       4 = Auto

9020 → Temperatura ambiente
       Unidad: °C
       Formato específico del NR9000
```

# Conexiones de hardware

## ESP32 → MAX485

El módulo MAX485 utilizado dispone de una interfaz TTL para comunicarse con el ESP32 y una interfaz diferencial RS-485 para comunicarse con el NR9000.

Conexiones:

| MAX485 | ESP32                    |
| ------ | ------------------------ |
| VCC    | 5 V                      |
| GND    | GND                      |
| DI     | GPIO 22                  |
| RO     | GPIO 17 mediante divisor |
| DE     | GPIO 21                  |
| RE     | GPIO 21                  |

Los pines `DE` y `RE` están unidos.

```text
GPIO 21 = HIGH → Transmisión
GPIO 21 = LOW  → Recepción
```

## Divisor de tensión

La salida `RO` del MAX485 puede trabajar a niveles superiores a los adecuados para la entrada del ESP32.

Por este motivo se utiliza un divisor de tensión:

```text
                 10 kΩ
MAX485 RO ─────/\/\/\/─────┬──── GPIO 17
                           │
                         20 kΩ
                           │
                          GND
```

El divisor reduce la tensión aplicada a la entrada RX del ESP32.

## UART utilizada

```text
TX2 → GPIO 22
RX2 → GPIO 17
DE/RE → GPIO 21
```

Configuración:

```cpp
#define RXD2 17
#define TXD2 22
#define DE_RE 21
```

Y:

```cpp
HardwareSerial RS485(2);

RS485.begin(9600, SERIAL_8N1, RXD2, TXD2);
```

# Conexión del NR9000

La comunicación con el NR9000 se realiza mediante el bus RS-485.

Conceptualmente:

```text
ESP32
  │
  │ UART
  ▼
MAX485
  │
  │ RS-485 A/B
  ▼
NR9000
```

La polaridad de las líneas A/B debe conectarse según la instalación y documentación del equipo.

La dirección Modbus utilizada es:

```text
42
```

## Pantalla Nokia 5110

El proyecto utiliza una pantalla Nokia 5110 basada en el controlador **PCD8544**.

Conexiones utilizadas:

| Nokia 5110 | ESP32   |
| ---------- | ------- |
| CLK        | GPIO 18 |
| DIN        | GPIO 23 |
| DC         | GPIO 16 |
| CE         | GPIO 5  |
| RST        | GPIO 4  |

Constructor utilizado:

```cpp
Adafruit_PCD8544 display(18, 23, 16, 5, 4);
```

Inicialización:

```cpp
display.begin();
display.setContrast(60);
```

La pantalla se utiliza para mostrar información del NR9000, como:

* Temperatura ambiente.
* Consigna.
* Modo de funcionamiento.
* Velocidad del ventilador.
* Estado de comunicación.

# Interfaz web

El ESP32 crea un punto de acceso Wi-Fi propio:

```text
SSID:     NR9000-ESP32
Password: 12345678
```

Una vez conectado a la red Wi-Fi creada por el ESP32, se puede acceder mediante un navegador a la interfaz web del controlador.

La interfaz permite consultar información del NR9000 y modificar determinados parámetros.

## Funciones disponibles

Entre las rutas utilizadas se encuentran:

```text
/
```

Página principal.

```text
/bajarconsigna
```

Reduce la consigna.

```text
/subirconsigna
```

Aumenta la consigna.

```text
/setconsigna
```

Establece directamente una consigna.

```text
/setmodo
```

Modifica el modo de funcionamiento.

```text
/setfan
```

Modifica la velocidad del ventilador.

La interfaz web actúa como una capa de control sobre las funciones Modbus implementadas en el ESP32.

# Flujo de funcionamiento

El funcionamiento general del sistema es:

```text
                 ┌──────────────────┐
                 │ Usuario          │
                 │ navegador móvil  │
                 │ o PC             │
                 └────────┬─────────┘
                          │
                         Wi-Fi
                          │
                 ┌────────▼─────────┐
                 │      ESP32       │
                 │                  │
                 │ Servidor web     │
                 │                  │
                 │ Control Modbus   │
                 └────────┬─────────┘
                          │
                         UART
                          │
                 ┌────────▼─────────┐
                 │      MAX485      │
                 └────────┬─────────┘
                          │
                         RS-485
                          │
                 ┌────────▼─────────┐
                 │      NR9000      │
                 └──────────────────┘
```

Al mismo tiempo, el ESP32 actualiza la pantalla Nokia 5110:

```text
NR9000
   │
   │ Modbus RTU
   ▼
 ESP32
   │
   │ SPI
   ▼
Nokia 5110
```

# Control de transmisión y recepción RS-485

El MAX485 utiliza las señales `DE` y `RE` para controlar la dirección de comunicación.

Para transmitir:

```cpp
digitalWrite(DE_RE, HIGH);
```

Se envía la trama:

```cpp
RS485.write(request, 8);
RS485.flush();
```

Después se cambia inmediatamente a recepción:

```cpp
digitalWrite(DE_RE, LOW);
```

Esto permite que el ESP32 pueda recibir la respuesta del NR9000 por el mismo bus RS-485.

# Lectura de un registro

El procedimiento general utilizado para leer un registro es:

```text
1. Limpiar el buffer serie.
2. Preparar la petición Modbus.
3. Calcular el CRC.
4. Activar transmisión.
5. Enviar la trama.
6. Esperar a que termine la transmisión.
7. Activar recepción.
8. Esperar la respuesta.
9. Comprobar la dirección Modbus.
10. Comprobar la función.
11. Comprobar la cantidad de bytes.
12. Comprobar el CRC.
13. Extraer el valor del registro.
14. Convertir el valor a la unidad correspondiente.
```

# Escritura de un registro

Para escribir un registro:

```text
1. Preparar la petición Modbus.
2. Añadir el valor.
3. Calcular el CRC.
4. Activar transmisión.
5. Enviar la trama.
6. Esperar a que termine.
7. Activar recepción.
8. Esperar la respuesta.
9. Comprobar la respuesta.
10. Verificar que coincide con la petición.
```

La función utilizada para la escritura es:

```text
06 - Write Single Register
```

# Gestión de errores

El programa comprueba diferentes situaciones de error durante la comunicación:

```text
Timeout
Respuesta incorrecta
Dirección Modbus incorrecta
Función incorrecta
Número de bytes incorrecto
CRC incorrecto
Excepción Modbus
```

Las excepciones Modbus se identifican cuando el bit más significativo de la función recibida está activado.

Por ejemplo:

```text
03 → operación normal
83 → excepción de la función 03
```

En ese caso, el siguiente byte contiene el código de excepción.

# Registros no utilizados

Durante la investigación del protocolo se han probado otros registros.

Algunos no han podido ser identificados todavía y otros devuelven error de dirección.

Por ejemplo, los registros:

```text
122
123
```

han devuelto:

```text
ERROR -102
```

que corresponde a una respuesta Modbus con excepción de dirección de datos ilegal (`0x02`).

También se han observado otros registros cuyo significado todavía no ha sido confirmado:

```text
9013
9017
9018
9031
9045
9047
9048
9001
9002
9003
9004
```

Estos registros no se incluyen como parámetros funcionales del proyecto hasta disponer de pruebas suficientes que permitan determinar su significado.

# Investigación del protocolo

Una parte importante del proyecto consiste en identificar experimentalmente el funcionamiento de los registros del NR9000.

El procedimiento utilizado es:

```text
Leer registro
     ↓
Modificar valor
     ↓
Escribir registro
     ↓
Volver a leer
     ↓
Observar comportamiento del NR9000
     ↓
Confirmar significado
```

Esto permite distinguir entre:

* Registros de lectura.
* Registros de escritura.
* Parámetros de configuración.
* Valores de temperatura.
* Modos de funcionamiento.
* Valores codificados.
* Registros no disponibles.

# Tecnologías utilizadas

```text
ESP32
Arduino IDE
C/C++
Modbus RTU
RS-485
MAX485
Nokia 5110
PCD8544
SPI
UART
Wi-Fi
WebServer
```

Bibliotecas utilizadas para la pantalla:

```cpp
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>
```

# Objetivo del proyecto

El objetivo es desarrollar una interfaz propia para el controlador **ISMA Controlli NR9000**, utilizando un ESP32 como plataforma de control y comunicación.

El proyecto combina:

```text
Electrónica
      +
Programación
      +
Comunicaciones industriales
      +
Modbus RTU
      +
RS-485
      +
ESP32
      +
Interfaz web
      +
Pantalla gráfica
```

La finalidad es disponer de una plataforma que permita estudiar el protocolo del NR9000 y, posteriormente, crear un sistema de control independiente y configurable.

# Estado actual del proyecto

Actualmente se ha conseguido:

* Comunicación física entre ESP32 y NR9000 mediante RS-485.
* Comunicación Modbus RTU a 9600 8N1.
* Identificación de la dirección Modbus `42`.
* Lectura de registros.
* Escritura de registros.
* Cálculo y comprobación del CRC Modbus.
* Control del modo de funcionamiento mediante el registro `9014`.
* Control del ventilador mediante el registro `9019`.
* Lectura de la temperatura ambiente mediante el registro `9020`.
* Lectura y escritura de la consigna mediante el registro `9015`.
* Integración de una pantalla Nokia 5110.
* Integración de una interfaz web mediante Wi-Fi.
* Control de la consigna desde la interfaz web.

Queda pendiente continuar investigando algunos registros del NR9000 y confirmar completamente el comportamiento físico de la consigna cuando se utilizan valores superiores a `25,5 °C`.

# Licencia

Proyecto personal realizado con fines de aprendizaje, experimentación y desarrollo.

La información sobre registros y formatos ha sido obtenida mediante pruebas realizadas directamente sobre el controlador NR9000 utilizado en este proyecto.
