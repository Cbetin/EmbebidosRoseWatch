# RoseWatch 🌹📡  
Sistema de monitoreo agroclimático con LoRa y Milk-V Duo 256M

Proyecto desarrollado en el curso de **Sistemas Embebidos** de la  
**Universidad Nacional de Colombia – Sede Bogotá**.

RoseWatch es un prototipo de sistema embebido para el monitoreo agroclimático en
invernaderos de rosas de exportación. El sistema mide temperatura, humedad del aire,
humedad del suelo e iluminancia, y transmite estas variables mediante un enlace LoRa
punto a punto hacia una pasarela basada en **Milk-V Duo 256M**, sin necesidad de
conectividad a Internet.

---

## 🚜 Contexto y motivación

Los cultivos de rosas para exportación son altamente sensibles a las **heladas** y a
variaciones bruscas de las condiciones ambientales. En muchos casos, el monitoreo se
realiza de forma manual y sin registro histórico, lo que dificulta:

- Detectar de forma temprana el riesgo de heladas.
- Trazar la evolución de las variables agroclimáticas en el tiempo.
- Tomar decisiones preventivas (cierre de ventanas, activación de riego, uso de
  cortinas térmicas, etc.).

RoseWatch nace de la necesidad real de un productor de rosas, que no contaba con un
sistema práctico para monitorear sus invernaderos durante la madrugada, cuando suelen
presentarse heladas.

---

## 🎯 Objetivo del proyecto

Diseñar y implementar un **prototipo de sistema embebido** que:

1. **Mida** variables agroclimáticas relevantes en un invernadero de rosas:
   - Temperatura del aire.
   - Humedad relativa del aire.
   - Humedad del suelo.
   - Iluminancia.
2. **Transmita** estos datos mediante un enlace **LoRa** de largo alcance.
3. **Reciba y registre** los datos en una pasarela basada en **Milk-V Duo 256M**,
   generando un historial en formato CSV.
4. Siente las bases para, en el futuro:
   - Soportar múltiples nodos de sensado.
   - Implementar una interfaz gráfica de visualización.
   - Desarrollar modelos matemáticos de detección de riesgo de heladas.

---

## 🧱 Arquitectura general

La arquitectura actual del sistema está compuesta por dos bloques principales:

1. **Nodo de sensado (transmisor LoRa)**  
   - Microcontrolador: **Arduino UNO**.
   - Sensores:
     - **DHT11** → temperatura y humedad relativa del aire.
     - **BH1750** → iluminancia (lux) vía I²C.
     - **HW-080** → sensor de humedad de suelo (salida analógica).
   - Comunicación: módulo **LoRa SX1278/RA-02** a 433 MHz.
   - El firmware construye una trama CSV:  
     `temp_C,humAmb_pct,humSuelo_pct,lux` y la envía periódicamente vía LoRa.

2. **Pasarela / Gateway (receptor LoRa)**  
   - Plataforma principal: **Milk-V Duo 256M** (RISC-V, Linux embebido).
   - Módulo **LoRa SX1278/RA-02** conectado por **SPI** (`/dev/spidev0.0`).
   - Programa en **C** (`lora_rx_log_v2.c`) que:
     - Inicializa SPI y el SX1278.
     - Detecta automáticamente modo y velocidad SPI correctos.
     - Recibe paquetes en modo RX continuo.
     - Decodifica la trama CSV.
     - Imprime las variables con etiquetas (estilo monitor serial).
     - Registra los datos en un archivo `lora_datos.csv` con:
       - Fecha y hora.
       - Temperatura, humedad ambiente, humedad de suelo e iluminancia.
       - RSSI (dBm) y SNR (dB).

---

## 🛠️ Configuración y compilación del SDK para Milk-V Duo 256M (PRINCIPAL)

Esta sección detalla los pasos **esenciales** para configurar y compilar el SDK de la  
**Milk-V Duo 256M**, así como para habilitar y probar el bus SPI utilizado por el
módulo LoRa. Es la parte clave para reproducir el entorno de ejecución del proyecto.

### 1. Clonación del SDK

Primero se clona el SDK de Buildroot desde el repositorio oficial:

    git clone https://github.com/milkv-duo/duo-buildroot-sdk.git

Este SDK contiene todos los archivos y configuraciones necesarias para compilar el
sistema.

### 2. Activación de las variables de entorno

Una vez clonado el SDK, se activan las variables de entorno necesarias:

    cd duo-buildroot-sdk
    source build/cvisetup.sh

Esto configura las herramientas y rutas necesarias para facilitar la compilación del
sistema.

### 3. Selección de la placa objetivo

Se selecciona la configuración específica para la placa **Milk-V Duo 256M**:

    defconfig cv1812cp_milkv_duo256m_sd

De esta forma se aplican las configuraciones adecuadas para esta tarjeta en particular.

### 4. Configuración de U-Boot

Se configura **U-Boot** para habilitar:

- Soporte de arranque desde SD/EMMC.
- Almacenamiento del entorno en un sistema de archivos FAT.

Ejemplo de opciones a habilitar en la configuración de U-Boot:

    [*] Support for booting from SD/EMMC
    [*] Environment is in a FAT filesystem
    (0:1) Device and partition for where to store the environment in FAT
    (uboot.env) Name of the FAT file to use for the environment

Esto permite que U-Boot cargue el sistema correctamente desde la tarjeta SD y mantenga
las variables de entorno en un archivo FAT.

### 5. Configuración del kernel

Se accede al menú de configuración del kernel para habilitar las opciones de arranque y
las relacionadas con sistemas de archivos FAT, SPI y otros periféricos:

    menuconfig_kernel

En este menú se pueden habilitar drivers adicionales según las necesidades del sistema.

### 6. Compilación del sistema

Para compilar todo el sistema se utiliza:

    build_all

Si ocurre algún error específico en el kernel, se puede forzar su compilación con:

    build_kernel

Esto genera la imagen de Linux, el DTB y los binarios necesarios para arrancar la
Milk-V Duo 256M.

### 7. Configuración de U-Boot para arrancar el sistema

Para sobrescribir el proceso de arranque y asegurarse de que se cargan los archivos
generados, se configuran las variables de entorno de U-Boot, por ejemplo:

    setenv cain_boot 'setenv bootargs ${reserved_mem} ${root} ${mtdparts} console=$consoledev,$baudrate $othbootargs;mmc dev ${sddev} && fatload mmc ${sddev} ${uImage_addr} Image; fatload mmc ${sddev} ${dtb_addr} cv1812cp_milkv_duo256m_sd.dtb; booti ${uImage_addr} - ${dtb_addr}'
    setenv dtb_addr 0x8b300000
    setenv sddev 0
    setenv bootcmd run_cainboot

Con esto se indica a U-Boot que cargue el kernel (`Image`) y el DTB específico de la
Milk-V Duo 256M desde la SD.

---

### ⚙️ Configuración y prueba de SPI

Una vez configurado el sistema base, el siguiente paso es asegurar la correcta
comunicación por SPI entre el módulo LoRa y la Milk-V Duo 256M.

#### 1. Configuración del archivo DTS (Device Tree Source)

El archivo **DTS** describe el hardware de la placa y es crucial para que el sistema
reconozca los dispositivos durante el arranque. Se modificó el archivo:

    linux_5.10/build/cv1812cp_milkv_duo256m_sd/arch/riscv/boot/dts/cvitek/cv1812cp_milkv_duo256m_sd.dts

En este archivo se configura el bus SPI utilizado por el módulo LoRa y otros
periféricos (habilitar nodo SPI, pines, `spidev`, etc.).

#### 2. Compilación de U-Boot con soporte SPI

Después de modificar el DTS, se configura y compila U-Boot para asegurarse de que el
SPI sea soportado correctamente. Por ejemplo:

    make menuconfig
    (habilitar SPI support, boot from SD/EMMC, etc.)
    make build_uboot

#### 3. Prueba de comunicación SPI con `spidev_test`

Una vez configurado el sistema, se verifica que el bus SPI está operativo mediante la
herramienta `spidev_test`. Por ejemplo:

    spidev_test -D /dev/spidev1.0 -v

Este comando envía y recibe datos a través del bus SPI y permite comprobar que la
comunicación es correcta.

#### 4. Prueba de SPI desde espacio de usuario (programa en C)

Finalmente, se creó un programa simple en C (`spi.c`) para comprobar el estado del SPI
desde espacio de usuario. El código realiza las siguientes acciones:

1. Apertura del dispositivo SPI (por ejemplo, `/dev/spidev1.0`).
2. Configuración del modo SPI (por ejemplo, `SPI_MODE_0`) mediante `ioctl`.
3. Definición de una transferencia SPI con buffers de transmisión y recepción.
4. Ejecución de la transferencia y verificación de los datos recibidos.
5. Cierre del dispositivo al finalizar.

La compilación del programa se realizó con:

    ~/duo-buildroot-sdk/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-gcc \
      -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -O2 -g0 \
      spi.c -o /tmp/spi

Una vez compilado, se ejecuta `/tmp/spi` y se verifica que la transferencia de datos no
presente errores, confirmando que el sistema está correctamente configurado para
utilizar el bus SPI con el módulo LoRa.

---


## 🔩 Hardware

### Nodo de sensado (Arduino UNO)

- **Placa**: Arduino UNO.
- **Sensores**:
  - `DHT11` (digital) → pin digital configurado como entrada.
  - `BH1750` (I²C) → SDA/SCL en A4/A5.
  - `HW-080` (humedad de suelo) → entrada analógica A2.
- **LoRa TX**:
  - Módulo SX1278/RA-02.
  - Conectado por SPI (CS, RST, DIO0 según el diseño del nodo).
- Alimentación a 5 V (nivelado donde sea necesario para el LoRa si el módulo lo requiere).

### Pasarela (Milk-V Duo 256M + LoRa)

- **Placa principal**: Milk-V Duo 256M.
- **Módulo LoRa SX1278/RA-02** (3,3 V).
- Conexiones típicas (ejemplo, según cabecera usada):

| Pin LoRa | Señal           | Pin Milk-V Duo 256M | Comentario                          |
|----------|-----------------|---------------------|--------------------------------------|
| VCC      | 3,3 V           | 3V3_OUT             | Alimentación del SX1278             |
| GND      | GND             | GND                 | Referencia común                    |
| SCK      | Reloj SPI       | SPI0_SCK            | Bus SPI0                            |
| MOSI     | Datos M→E       | SPI0_MOSI           | Milk-V → LoRa                       |
| MISO     | Datos E→M       | SPI0_MISO           | LoRa → Milk-V                       |
| NSS/CS   | Chip Select     | SPI0_CS0            | Asociado a `/dev/spidev0.0`        |
| RST      | Reset           | 3,3 V (fijo)        | Reset por hardware siempre alto     |
| DIO0     | IRQ/RxDone (opt)| GPIO libre          | Reservado para futuras mejoras      |

> **Nota:** en el firmware actual se usan *flags* por SPI para detectar paquetes; DIO0
> se deja listo para una futura implementación basada en interrupciones.

---

## 💻 Software

### 1. Firmware de transmisión (Arduino)

Características principales del código de transmisión:

- Librerías utilizadas:
  - `SPI.h`
  - `LoRa.h` (sandeepmistry)
  - `DHT.h`
  - `Wire.h`
  - `BH1750.h`
- Secuencia típica en `loop()`:
  1. Leer temperatura y humedad del aire (`DHT11`).
  2. Leer nivel analógico del sensor HW-080 y convertir a porcentaje de humedad de suelo.
  3. Leer iluminancia en lux con el `BH1750`.
  4. Construir una cadena CSV:

        temp_C,humAmb_pct,humSuelo_pct,lux

  5. Enviar la trama por LoRa:

        LoRa.beginPacket();
        LoRa.print(mensaje);
        LoRa.endPacket();

  6. Esperar algunos segundos y repetir.

### 2. Firmware de recepción y logging (Milk-V Duo 256M)

Archivo principal: **`lora_rx_log_v2.c`**

- **Auto-detección SPI**:
  - Prueba distintas combinaciones de modo SPI (0–3) y velocidades (50–500 kHz).
  - Lee `REG_VERSION` y realiza un test de escritura/lectura sobre `REG_SYNC_WORD`.
  - Si la lectura coincide, guarda `working_mode` y `working_speed` y continúa.

- **Inicialización LoRa (`lora_init`)**:
  - Pone el SX1278 en `SLEEP` y luego en `STDBY`.
  - Configura:
    - Frecuencia: 433 MHz.
    - SF: 12.
    - BW: 62,5 kHz.
    - CR: 4/8.
    - Sync word: `0x12`.
    - CRC desactivado (para coincidir con `LoRa.h` en Arduino).
    - LNA Boost + optimización para bajas tasas de datos (LDRO).

- **Recepción de paquetes**:
  - Entra en `MODE_RX_CONTINUOUS`.
  - Espera a que se active `IRQ_RX_DONE`.
  - Comprueba si hay error de CRC (descarta en ese caso).
  - Lee tamaño de paquete, posición del FIFO, extrae el payload.
  - Calcula RSSI y SNR desde los registros del SX1278.
  - Devuelve longitud y métricas al `main`.

- **Parsing y logging**:
  - El programa espera tramas en formato:

        temp_C,humAmb_pct,humSuelo_pct,lux

  - Usa `sscanf` para separar los 4 campos.
  - Obtiene la fecha y hora actual con resolución de milisegundos.
  - Muestra la información en consola en un formato legible.
  - Abre/crea un archivo CSV (`lora_datos.csv` o `/tmp/lora_datos.csv`) y agrega:

        fecha_hora,temp_C,humAmb_pct,humSuelo_pct,lux,rssi_dBm,snr_dB

### 3. Imagen y entorno Linux en la Milk-V

En el desarrollo del proyecto se trabajó con herramientas como:

- Repositorios de compilación y SDK de Milk-V / LicheeRV.
- Documentación de U-Boot y del kernel para habilitar:
  - Consola serie.
  - SPI de usuario (`spidev`).
  - Ajuste de pines mediante `devmem`.

> La idea es disponer de una imagen Linux mínima donde:
> - Exista `/dev/spidev0.0`.
> - Se pueda compilar el código C directamente en la placa o usar cross-compile.

---


## 🧪 Pruebas realizadas

El plan de pruebas incluye:

1. **Pruebas unitarias de sensores**:
   - DHT11: cambios de temperatura y humedad generados de forma controlada.
   - BH1750: variaciones de iluminación (ambiente / linterna / sensor cubierto).
   - HW-080: cambios en el contenido de agua del sustrato.

2. **Pruebas del enlace LoRa**:
   - Envío de tramas de prueba con valores conocidos.
   - Distintas distancias en interiores y espacios abiertos.

3. **Pruebas de integración nodo–pasarela**:
   - Verificación de que la trama CSV enviada por Arduino es recibida, parseada y
     etiquetada correctamente en la Milk-V.
   - Confirmación de la generación del archivo `lora_datos.csv`.

4. **Pruebas de visualización básica**:
   - Observación del flujo de datos en la consola de la Milk-V.
   - Coherencia entre los valores medidos y las condiciones reales de entorno.

---

## 🚀 Trabajo futuro

Algunas líneas de mejora propuestas:

- **Soporte para múltiples nodos de sensado**:
  - Incluir identificadores de nodo en la trama.
  - Definir un esquema simple de acceso al medio y manejo de colisiones.

- **Interfaz de visualización web**:
  - Panel web en la Milk-V para:
    - Visualizar datos en tiempo real.
    - Consultar históricos y gráficos.
    - Configurar umbrales de alerta desde un navegador.

- **Modelo matemático de riesgo de heladas**:
  - Integrar todas las variables (temperatura, humedad, humedad de suelo, luz).
  - Implementar índices de riesgo basados en histórico de datos.
  - Generar alertas más precisas y anticipadas.

- **Mejoras adicionales**:
  - Integración con base de datos ligera (SQLite, etc.).
  - Alimentación autónoma de nodos con energía solar.
  - Carcasas robustas y aptas para ambiente de invernadero.
  - Exploración de LoRaWAN u otras tecnologías para acceso remoto a los datos.

---

## 📂 Estructura sugerida del repositorio

Puedes organizar el repositorio de la siguiente forma (ajústalo a tu gusto):

    .
    ├── arduino_tx/           # Código Arduino del nodo de sensado (LoRa TX + sensores)
    ├── milkv_rx/             # Código C de recepción y logging en la Milk-V Duo
    │   └── lora_rx_log_v2.c
    ├── docs/                 # Informe en PDF, esquemas, diagramas, etc.
    ├── images/               # Fotos del montaje, capturas de consola, diagramas
    └── README.md             # Este archivo
