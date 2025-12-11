// lora_rx_log_v2.c
// Receptor LoRa + log a CSV (Milk-V Duo 256M)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>

// ==================== REGISTROS SX1278 ====================
#define REG_FIFO                0x00
#define REG_OP_MODE             0x01
#define REG_FRF_MSB             0x06
#define REG_FRF_MID             0x07
#define REG_FRF_LSB             0x08
#define REG_PA_CONFIG           0x09
#define REG_LNA                 0x0C
#define REG_FIFO_ADDR_PTR       0x0D
#define REG_FIFO_TX_BASE_ADDR   0x0E
#define REG_FIFO_RX_BASE_ADDR   0x0F
#define REG_FIFO_RX_CURRENT     0x10
#define REG_IRQ_FLAGS           0x12
#define REG_RX_NB_BYTES         0x13
#define REG_PKT_SNR_VALUE       0x19
#define REG_PKT_RSSI_VALUE      0x1A
#define REG_MODEM_CONFIG_1      0x1D
#define REG_MODEM_CONFIG_2      0x1E
#define REG_MODEM_CONFIG_3      0x26
#define REG_SYNC_WORD           0x39
#define REG_VERSION             0x42

// Modos
#define MODE_LONG_RANGE_MODE    0x80
#define MODE_SLEEP              0x00
#define MODE_STDBY              0x01
#define MODE_RX_CONTINUOUS      0x05

// IRQ
#define IRQ_RX_DONE             0x40
#define IRQ_CRC_ERROR           0x20

static int spi_fd = -1;
static volatile int running = 1;
static uint32_t working_speed = 0;
static uint8_t working_mode = 0;

// Log a archivo
static FILE *log_file = NULL;

// ==================== MANEJO DE SEÑALES ====================
void signal_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\n[INFO] Saliendo...\n");
}

// ==================== SPI BÁSICO ====================
int spi_open_with_config(const char *device, uint8_t mode, uint32_t speed) {
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        return -1;
    }

    uint8_t bits = 8;
    uint8_t lsb = 0;  // MSB first

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0) {
        close(fd);
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        close(fd);
        return -1;
    }

    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        close(fd);
        return -1;
    }

    // MSB first
    ioctl(fd, SPI_IOC_WR_LSB_FIRST, &lsb);

    return fd;
}

uint8_t spi_read_register(uint8_t reg, uint32_t speed) {
    usleep(200);
    uint8_t tx[2] = {reg & 0x7F, 0x00};
    uint8_t rx[2] = {0, 0};

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 2,
        .speed_hz = speed,
        .bits_per_word = 8,
        .delay_usecs = 100,
        .cs_change = 0,
    };

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        return 0;
    }

    usleep(200);
    return rx[1];
}

void spi_write_register(uint8_t reg, uint8_t value, uint32_t speed) {
    usleep(200);
    
    uint8_t tx[2] = {reg | 0x80, value};
    uint8_t rx[2] = {0, 0};

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 2,
        .speed_hz = speed,
        .bits_per_word = 8,
        .delay_usecs = 100,
        .cs_change = 0,
    };

    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    usleep(200);
}

// ==================== AUTO-DETECCIÓN SPI ====================
int auto_detect_spi_config(void) {
    printf("\n[INFO] Buscando configuración correcta de SPI...\n");
    printf("       Esto puede tardar unos segundos...\n\n");

    uint32_t speeds[] = {50000, 100000, 200000, 500000};
    uint8_t modes[] = {SPI_MODE_0, SPI_MODE_1, SPI_MODE_2, SPI_MODE_3};

    for (int m = 0; m < 4; m++) {
        for (int s = 0; s < 4; s++) {
            if (spi_fd >= 0) {
                close(spi_fd);
            }

            spi_fd = spi_open_with_config("/dev/spidev0.0", modes[m], speeds[s]);
            if (spi_fd < 0) {
                continue;
            }

            uint8_t version = spi_read_register(REG_VERSION, speeds[s]);

            if (version == 0x12 || version == 0x11 || version == 0x24) {
                // Test de escritura
                spi_write_register(REG_SYNC_WORD, 0x55, speeds[s]);
                usleep(1000);
                uint8_t test_read = spi_read_register(REG_SYNC_WORD, speeds[s]);

                if (test_read == 0x55) {
                    printf("[OK] ¡Configuración encontrada!\n");
                    printf("     Modo SPI: %d\n", modes[m]);
                    printf("     Velocidad: %u Hz\n", speeds[s]);
                    printf("     Versión chip: 0x%02X\n", version);
                    
                    working_mode = modes[m];
                    working_speed = speeds[s];
                    
                    // Restaurar SYNC_WORD por defecto
                    spi_write_register(REG_SYNC_WORD, 0x12, speeds[s]);
                    return 0;
                }
            }
        }
    }

    printf("[ERROR] No se encontró configuración válida\n");
    return -1;
}

// ==================== LORA ====================
void lora_set_mode(uint8_t mode) {
    spi_write_register(REG_OP_MODE, MODE_LONG_RANGE_MODE | mode, working_speed);
    usleep(10000);
}

void lora_set_frequency(long frequency) {
    uint64_t frf = ((uint64_t)frequency << 19) / 32000000;
    
    spi_write_register(REG_FRF_MSB, (uint8_t)(frf >> 16), working_speed);
    spi_write_register(REG_FRF_MID, (uint8_t)(frf >> 8), working_speed);
    spi_write_register(REG_FRF_LSB, (uint8_t)(frf >> 0), working_speed);
    
    printf("[OK] Frecuencia: %ld Hz\n", frequency);
}

void lora_set_spreading_factor(int sf) {
    if (sf < 6 || sf > 12) sf = 7;
    
    uint8_t config2 = spi_read_register(REG_MODEM_CONFIG_2, working_speed);
    config2 = (config2 & 0x0F) | ((sf << 4) & 0xF0);
    spi_write_register(REG_MODEM_CONFIG_2, config2, working_speed);
    
    printf("[OK] SF: %d\n", sf);
}

void lora_set_bandwidth(long bw) {
    int bw_val;
    
    if (bw <= 7800) bw_val = 0;
    else if (bw <= 10400) bw_val = 1;
    else if (bw <= 15600) bw_val = 2;
    else if (bw <= 20800) bw_val = 3;
    else if (bw <= 31250) bw_val = 4;
    else if (bw <= 41700) bw_val = 5;
    else if (bw <= 62500) bw_val = 6;
    else if (bw <= 125000) bw_val = 7;
    else if (bw <= 250000) bw_val = 8;
    else bw_val = 9;
    
    uint8_t config1 = spi_read_register(REG_MODEM_CONFIG_1, working_speed);
    config1 = (config1 & 0x0F) | (bw_val << 4);
    spi_write_register(REG_MODEM_CONFIG_1, config1, working_speed);
    
    printf("[OK] BW: %ld Hz\n", bw);
}

void lora_set_coding_rate(int cr) {
    if (cr < 5) cr = 5;
    if (cr > 8) cr = 8;
    
    int cr_val = cr - 4;
    
    uint8_t config1 = spi_read_register(REG_MODEM_CONFIG_1, working_speed);
    config1 = (config1 & 0xF1) | (cr_val << 1);
    spi_write_register(REG_MODEM_CONFIG_1, config1, working_speed);
    
    printf("[OK] CR: 4/%d\n", cr);
}

int lora_init(void) {
    printf("\n[INFO] Inicializando LoRa...\n");
    
    lora_set_mode(MODE_SLEEP);
    
    // Config igual que el Arduino
    lora_set_frequency(433000000);      // 433 MHz
    lora_set_spreading_factor(12);      // SF12
    lora_set_bandwidth(62500);          // 62.5 kHz
    lora_set_coding_rate(8);            // CR 4/8
    
    // MODEM_CONFIG_3: AGC + LowDataRateOptimize
    spi_write_register(REG_MODEM_CONFIG_3, 0x0C, working_speed);
    printf("[OK] MODEM_CONFIG_3: AGC + LowDataRateOptimize\n");

    // CRC OFF para coincidir con Arduino (LoRa.h por defecto sin CRC)
    uint8_t config2 = spi_read_register(REG_MODEM_CONFIG_2, working_speed);
    config2 &= ~0x04;  // RxPayloadCrcOn = 0
    spi_write_register(REG_MODEM_CONFIG_2, config2, working_speed);
    printf("[OK] CRC: OFF\n");
    
    // Sync word
    spi_write_register(REG_SYNC_WORD, 0x12, working_speed);
    
    // LNA boost
    uint8_t lna = spi_read_register(REG_LNA, working_speed);
    lna |= 0x03;
    spi_write_register(REG_LNA, lna, working_speed);
    
    // FIFO base
    spi_write_register(REG_FIFO_TX_BASE_ADDR, 0x00, working_speed);
    spi_write_register(REG_FIFO_RX_BASE_ADDR, 0x00, working_speed);
    
    lora_set_mode(MODE_STDBY);
    
    printf("[OK] LoRa inicializado\n\n");
    return 0;
}

// NUEVO: devolvemos también RSSI y SNR
int lora_receive_packet(uint8_t *buf, int max_len, int *out_rssi, int *out_snr) {
    spi_write_register(REG_IRQ_FLAGS, 0xFF, working_speed);
    lora_set_mode(MODE_RX_CONTINUOUS);
    
    for (int i = 0; i < 50 && running; i++) {
        uint8_t irq = spi_read_register(REG_IRQ_FLAGS, working_speed);
        
        if (irq & IRQ_RX_DONE) {
            if (irq & IRQ_CRC_ERROR) {
                printf("[WARN] CRC error - descartando paquete\n");
                spi_write_register(REG_IRQ_FLAGS, 0xFF, working_speed);
                return -1;
            }
            
            int len = spi_read_register(REG_RX_NB_BYTES, working_speed);
            uint8_t fifo_addr = spi_read_register(REG_FIFO_RX_CURRENT, working_speed);
            
            if (len > max_len) {
                printf("[WARN] Paquete demasiado grande: %d bytes (max: %d)\n", len, max_len);
                len = max_len;
            }
            
            spi_write_register(REG_FIFO_ADDR_PTR, fifo_addr, working_speed);
            usleep(1000);
            
            uint8_t tx[256] = {REG_FIFO & 0x7F};
            uint8_t rx[256] = {0};
            
            struct spi_ioc_transfer tr = {
                .tx_buf = (unsigned long)tx,
                .rx_buf = (unsigned long)rx,
                .len = len + 1,
                .speed_hz = working_speed,
                .bits_per_word = 8,
                .delay_usecs = 100,
                .cs_change = 0,
            };
            
            if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
                printf("[ERROR] Fallo al leer FIFO\n");
                spi_write_register(REG_IRQ_FLAGS, 0xFF, working_speed);
                return -1;
            }
            
            memcpy(buf, &rx[1], len);
            
            int rssi = spi_read_register(REG_PKT_RSSI_VALUE, working_speed) - 164;
            int snr  = (int8_t)spi_read_register(REG_PKT_SNR_VALUE, working_speed) / 4;
            
            if (out_rssi) *out_rssi = rssi;
            if (out_snr)  *out_snr  = snr;
            
            printf("[RX] %d bytes | RSSI: %d dBm | SNR: %d dB\n", len, rssi, snr);
            
            spi_write_register(REG_IRQ_FLAGS, 0xFF, working_speed);
            return len;
        }
        
        usleep(100000);
    }
    
    return 0;  // timeout
}

// ==================== LOG A CSV ====================
int open_log_file(void) {
    const char *paths[] = {
        "lora_datos.csv",       // 1) Directorio actual
        "/tmp/lora_datos.csv",  // 2) Carpeta temporal (si lo anterior falla)
        NULL
    };

    for (int i = 0; paths[i] != NULL; i++) {
        const char *path = paths[i];
        int exists = 0;

        FILE *test = fopen(path, "r");
        if (test) {
            exists = 1;
            fclose(test);
        }

        log_file = fopen(path, "a");
        if (!log_file) {
            // No matamos el programa, probamos con la siguiente ruta
            perror("[WARN] No se pudo abrir/crear archivo de log");
            continue;
        }

        if (!exists) {
            // Cabecera CSV
            fprintf(log_file, "fecha_hora,temp_C,humAmb_pct,humSuelo_pct,lux,rssi_dBm,snr_dB\n");
            fflush(log_file);
        }

        printf("[LOG] Guardando datos en: %s\n", path);
        return 0; // Éxito
    }

    // Si llegamos aquí, ninguna ruta funcionó
    fprintf(stderr,
            "[WARN] No se pudo crear archivo de log en ninguna ruta.\n"
            "       Se continuará solo con salida por consola.\n");
    log_file = NULL;
    return -1;
}

// ==================== MAIN ====================
int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=================================================\n");
    printf("  Receptor LoRa Ra-02 + Log CSV\n");
    printf("  Milk-V Duo 256M\n");
    printf("=================================================\n");
    
    if (auto_detect_spi_config() != 0) {
        fprintf(stderr, "\n[ERROR] No se pudo detectar el chip LoRa\n");
        return 1;
    }
    
    if (lora_init() != 0) {
        close(spi_fd);
        return 1;
    }

    if (open_log_file() != 0) {
    // seguimos sin archivo de log
    fprintf(stderr, "[WARN] Continuando sin registro en CSV (solo consola).\n");
    }

    printf("[INFO] Esperando paquetes... (Ctrl+C para salir)\n\n");
    
    uint8_t buffer[256];
    int count = 0;
    
    while (running) {
        int rssi = 0, snr = 0;
        int len = lora_receive_packet(buffer, sizeof(buffer) - 1, &rssi, &snr);
        
        if (len > 0) {
            buffer[len] = '\0';
            count++;

            // 1) Ver el string crudo
            printf("[DATO] %s\n", buffer);

            // 2) Parsear: temp,humAmb,humSuelo,lux
            float temp = 0.0f;
            float humAmb = 0.0f;
            int humSuelo = 0;
            int lux = 0;

            int parsed = sscanf((char *)buffer, "%f,%f,%d,%d",
                                &temp, &humAmb, &humSuelo, &lux);

            if (parsed == 4) {
                struct timeval tv;
                gettimeofday(&tv, NULL);
                struct tm *tm_info = localtime(&tv.tv_sec);

                int h = tm_info->tm_hour;
                int m = tm_info->tm_min;
                int s = tm_info->tm_sec;
                long ms = tv.tv_usec / 1000;

                char fecha[64];
                strftime(fecha, sizeof(fecha), "%Y-%m-%d %H:%M:%S", tm_info);

                // 3) Imprimir estilo “pestaña Serial Arduino”
                printf("%02d:%02d:%02d.%03ld -> Temperatura: %.1f °C\n",
                       h, m, s, ms, temp);
                printf("%02d:%02d:%02d.%03ld -> Humedad ambiente: %.1f %%\n",
                       h, m, s, ms, humAmb);
                printf("%02d:%02d:%02d.%03ld -> Humedad suelo: %d %%\n",
                       h, m, s, ms, humSuelo);
                printf("%02d:%02d:%02d.%03ld -> Iluminancia: %d lx\n\n",
                       h, m, s, ms, lux);

                // 4) Guardar en CSV
                if (log_file) {
                    fprintf(log_file, "%s,%.1f,%.1f,%d,%d,%d,%d\n",
                            fecha, temp, humAmb, humSuelo, lux, rssi, snr);
                    fflush(log_file);
                }
            } else {
                printf("[WARN] No se pudieron parsear los 4 campos. Cadena: '%s'\n", buffer);
            }

            printf("[INFO] Total: %d paquetes\n\n", count);
        }
        
        static int status = 0;
        if (++status >= 10) {
            printf("[STATUS] Escuchando... (%d recibidos)\n", count);
            status = 0;
        }
    }
    
    printf("\n[INFO] Cerrando...\n");
    lora_set_mode(MODE_SLEEP);
    close(spi_fd);
    if (log_file) fclose(log_file);
    printf("[OK] Cerrado\n");
    
    return 0;
}
