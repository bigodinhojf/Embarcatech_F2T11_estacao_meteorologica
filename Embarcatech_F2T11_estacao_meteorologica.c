// -- Inclusão de bibliotecas
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "aht20.h"
#include "bmp280.h"
#include "ssd1306.h"
#include "font.h"
#include <math.h>
#include "pico/bootrom.h"


// -- Definição de constantes

// Porta I2C que está conectado os dois sensores (I2C0 e GPIOs 0 e 1)
#define I2C_PORT i2c0 // Define a porta I2C0
#define I2C_SDA 0 // Define o pino SDA na GPIO 0
#define I2C_SCL 1 // Define o pino SCL na GPIO 1

#define SEA_LEVEL_PRESSURE 101325.0 // Pressão ao nível do mar em Pa

// Estrutura para o BMP 280
struct bmp280_calib_param params;

// Estrutura para o AHT10
AHT20_Data data;
int32_t raw_temp_bmp;
int32_t raw_pressure;

// Porta I2C que está conectado o Display OLED (I2C1 e GPIOs 14 e 15)
#define display_i2c_port i2c1 // Define a porta I2C1
#define display_i2c_sda 14 // Define o pino SDA na GPIO 14
#define display_i2c_scl 15 // Define o pino SCL na GPIO 15
#define display_i2c_endereco 0x3C // Define o endereço I2C do Display
ssd1306_t ssd; // Inicializa a estrutura do display

// GPIO
#define button_A 5 // Botão A GPIO 5
#define button_B 6 // Botão B GPIO 6
#define button_J 22 // Botão do Joystick GPIO 22


// -- Definição de variáveis globais

static volatile uint32_t last_time = 0; // Armazena o tempo do último clique dos botões (debounce)

volatile int32_t pressao = 0; // Armazena o valor da pressão medido pelo BMP280
volatile double altitude = 0; // Armazena o valor de altitude calculado

volatile float temperatura_min = 10.0; // Armazena o valor de temperatura mínima
volatile float temperatura_max = 35.0; // Armazena o valor de temperatura máxima
volatile float umidade_min = 30.0; // Armazena o valor de umidade mínima
volatile float umidade_max = 70.0; // Armazena o valor de umidade máxima

volatile int tela = 1; // Armazena qual a tela está ativada no momento

char str_temperatura[5]; // Armazena o valor da temperatura em string
char str_pressao[6]; // Armazena o valor da pressão em string
char str_altitude[5]; // Armazena o valor da altitude em string
char str_umidade[5]; // Armazena o valor da umidade em string

char str_temperatura_min[5]; // Armazena o valor de temperatura mínima em string
char str_temperatura_max[5]; // Armazena o valor de temperatura máxima em string
char str_umidade_min[5]; // Armazena o valor de umidade mínima em string
char str_umidade_max[5]; // Armazena o valor de umidade máxima em string


// -- Funções

// Função que calcula a altitude do local com base no valor da pressão
double calculo_altitude(double pressao){
    return 44330.0 * (1.0 - pow(pressao / SEA_LEVEL_PRESSURE, 0.1903));
}

// Função para fazer a leitura do sensor BMP280
void ler_bmp280(){
    // Leitura do BMP280
    bmp280_read_raw(I2C_PORT, &raw_temp_bmp, &raw_pressure);
    int32_t temperatura = bmp280_convert_temp(raw_temp_bmp, &params);
    pressao = bmp280_convert_pressure(raw_pressure, raw_temp_bmp, &params);

    // Cálculo da altitude
    altitude = calculo_altitude(pressao);

    printf("Pressao = %.3f kPa\n", pressao / 1000.0);
    printf("Temperatura BMP: = %.2f C\n", temperatura / 100.0);
    printf("Altitude estimada: %.2f m\n", altitude);  
}

// Função para fazer a leitura do sensor AHT10
void ler_aht10(){
    // Leitura do AHT20
    if(aht20_read(I2C_PORT, &data)){
        printf("Temperatura AHT: %.2f C\n", data.temperature);
        printf("Umidade: %.2f %%\n\n\n", data.humidity);
    }else{
        printf("Erro na leitura do AHT10!\n\n\n");
    }
}

// Função para atualizar as informações do display
void atualizar_display(){
    ssd1306_fill(&ssd, false); // Limpa o display
    ssd1306_rect(&ssd, 0, 0, 127, 63, true, false); // Borda principal

    if(tela == 1){ // TELA 1 - INICIAL
        // Cabeçalho
        ssd1306_draw_string(&ssd, "EMB ESTACAO", 20, 3); // Desenha uma string
        ssd1306_draw_string(&ssd, "METEOROLOGICA", 12, 12); // Desenha uma string

        ssd1306_line(&ssd, 1, 21, 126, 21, true); // Desenha uma linha horizontal

        // Status da conexão Wi-Fi
        ssd1306_draw_string(&ssd, "Conexao Wi-Fi:", 8, 23); // Desenha uma string
        ssd1306_draw_string(&ssd, "Conectando...", 12, 32); // Desenha uma string

        ssd1306_line(&ssd, 1, 41, 126, 41, true); // Desenha uma linha horizontal

        // IP do WebServer
        ssd1306_draw_string(&ssd, "IP Web Server:", 8, 43); // Desenha uma string
        ssd1306_draw_string(&ssd, "12.34.56.78", 20, 52); // Desenha uma string

    }else if(tela == 2){ // TELA 2 - GERAL
        // Cabeçalho
        ssd1306_draw_string(&ssd, "Dados do local:", 4, 3); // Desenha uma string

        ssd1306_line(&ssd, 1, 12, 126, 12, true); // Desenha uma linha horizontal

        // Temperatura
        sprintf(str_temperatura, "%.1fC", data.temperature);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Tem:", 4, 15); // Desenha uma string
        ssd1306_draw_string(&ssd, str_temperatura, 40, 15); // Desenha uma string

        ssd1306_line(&ssd, 1, 25, 126, 25, true); // Desenha uma linha horizontal

        // Pressão
        sprintf(str_pressao, "%.2fkPa", pressao / 1000.0);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Pre:", 4, 28); // Desenha uma string
        ssd1306_draw_string(&ssd, str_pressao, 40, 28); // Desenha uma string

        ssd1306_line(&ssd, 1, 38, 126, 38, true); // Desenha uma linha horizontal

        // Altitude
        sprintf(str_altitude, "%.0fm", altitude);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Alt:", 4, 41); // Desenha uma string
        ssd1306_draw_string(&ssd, str_altitude, 40, 41); // Desenha uma string

        ssd1306_line(&ssd, 1, 51, 126, 51, true); // Desenha uma linha horizontal

        // Umidade
        sprintf(str_umidade, "%.1f%%", data.humidity);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Umi:", 4, 53); // Desenha uma string
        ssd1306_draw_string(&ssd, str_umidade, 40, 53); // Desenha uma string

    }else if(tela == 3){ // TELA 3 - DETALHES DA TEMPERATURA
        // Cabeçalho
        ssd1306_draw_string(&ssd, "TEMPERATURA", 20, 3); // Desenha uma string

        ssd1306_line(&ssd, 1, 12, 126, 12, true); // Desenha uma linha horizontal

        // Temperatura medida
        sprintf(str_temperatura, "%.1fC", data.temperature);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Atual:", 4, 15); // Desenha uma string
        ssd1306_draw_string(&ssd, str_temperatura, 56, 15); // Desenha uma string

        ssd1306_line(&ssd, 1, 25, 126, 25, true); // Desenha uma linha horizontal

        // Temperatura mínima
        sprintf(str_temperatura_min, "%.1fC", temperatura_min);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Min:", 4, 28); // Desenha uma string
        ssd1306_draw_string(&ssd, str_temperatura_min, 40, 28); // Desenha uma string

        ssd1306_line(&ssd, 1, 38, 126, 38, true); // Desenha uma linha horizontal

        // Temperatura máxima
        sprintf(str_temperatura_max, "%.1fC", temperatura_max);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Max:", 4, 41); // Desenha uma string
        ssd1306_draw_string(&ssd, str_temperatura_max, 40, 41); // Desenha uma string

        ssd1306_line(&ssd, 1, 51, 126, 51, true); // Desenha uma linha horizontal

        // Status da temperatura
        if(data.temperature > temperatura_max){
            ssd1306_draw_string(&ssd, "Alerta: T > Max", 2, 53); // Desenha uma string
        }else if(data.temperature < temperatura_min){
            ssd1306_draw_string(&ssd, "Alerta: T < Min", 2, 53); // Desenha uma string
        }else{
            ssd1306_draw_string(&ssd, "Status: Ok", 24, 53); // Desenha uma string
        }
    }else if(tela == 4){ // TELA 4 - DETALHES DA UMIDADE
        // Cabeçalho
        ssd1306_draw_string(&ssd, "UMIDADE", 36, 3); // Desenha uma string

        ssd1306_line(&ssd, 1, 12, 126, 12, true); // Desenha uma linha horizontal

        // Umidade medida
        sprintf(str_umidade, "%.1f%%", data.humidity);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Atual:", 4, 15); // Desenha uma string
        ssd1306_draw_string(&ssd, str_umidade, 56, 15); // Desenha uma string

        ssd1306_line(&ssd, 1, 25, 126, 25, true); // Desenha uma linha horizontal

        // Umidade mínima
        sprintf(str_umidade_min, "%.1f%%", umidade_min);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Min:", 4, 28); // Desenha uma string
        ssd1306_draw_string(&ssd, str_umidade_min, 40, 28); // Desenha uma string

        ssd1306_line(&ssd, 1, 38, 126, 38, true); // Desenha uma linha horizontal

        // Umidade máxima
        sprintf(str_umidade_max, "%.1f%%", umidade_max);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Max:", 4, 41); // Desenha uma string
        ssd1306_draw_string(&ssd, str_umidade_max, 40, 41); // Desenha uma string

        ssd1306_line(&ssd, 1, 51, 126, 51, true); // Desenha uma linha horizontal

        // Status da temperatura
        if(data.humidity > umidade_max){
            ssd1306_draw_string(&ssd, "Alerta: U > Max", 2, 53); // Desenha uma string
        }else if(data.humidity < umidade_min){
            ssd1306_draw_string(&ssd, "Alerta: U < Min", 2, 53); // Desenha uma string
        }else{
            ssd1306_draw_string(&ssd, "Status: Ok", 24, 53); // Desenha uma string
        }
    }
    ssd1306_send_data(&ssd); // Atualiza o display
}

// Função de interrupção dos botões
void gpio_irq_handler(uint gpio, uint32_t events){
    //Debouncing
    uint32_t current_time = to_us_since_boot(get_absolute_time()); // Pega o tempo atual e transforma em us
    if(current_time - last_time > 1000000){
        last_time = current_time; // Atualização de tempo do último clique
        if(gpio == button_A){
            if(tela <= 1){
                tela = 4;
            }else{
                tela = tela - 1;
            }
            atualizar_display(); // Atualiza o display OLED
        }else if(gpio == button_B){
            if(tela >= 4){
                tela = 1;
            }else{
                tela = tela + 1;
            }
            atualizar_display(); // Atualiza o display OLED
        }else if(gpio == button_J){
            reset_usb_boot(0, 0);
        }
    }
}

// Função principal
int main(){
    stdio_init_all();

    // Inicialização dos botões
    gpio_init(button_A); // Inicia a GPIO 5 do botão A
    gpio_set_dir(button_A, GPIO_IN); // Define a direção da GPIO 5 do botão A como entrada
    gpio_pull_up(button_A); // Habilita o resistor de pull up da GPIO 5 do botão A
    gpio_init(button_B); // Inicia a GPIO 6 do botão B
    gpio_set_dir(button_B, GPIO_IN); // Define a direção da GPIO 6 do botão B como entrada
    gpio_pull_up(button_B); // Habilita o resistor de pull up da GPIO 6 do botão B
    gpio_init(button_J); // Inicia a GPIO 22 do botão do joystick
    gpio_set_dir(button_J, GPIO_IN); // Define a direção da GPIO 22 do botão do joystick como entrada
    gpio_pull_up(button_J); // Habilita o resistor de pull up da GPIO 22 do botão do joystick
    gpio_set_irq_enabled_with_callback(button_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler); // Interrupção do botão A
    gpio_set_irq_enabled_with_callback(button_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler); // Interrupção do botão B
    gpio_set_irq_enabled_with_callback(button_J, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler); // Interrupção do botão do joystick

    // Inicialização do Display OLED
    i2c_init(display_i2c_port, 400 * 1000); // Inicializa o I2C usando 400kHz
    gpio_set_function(display_i2c_sda, GPIO_FUNC_I2C); // Define o pino SDA (GPIO 14) na função I2C
    gpio_set_function(display_i2c_scl, GPIO_FUNC_I2C); // Define o pino SCL (GPIO 15) na função I2C
    gpio_pull_up(display_i2c_sda); // Ativa o resistor de pull up para o pino SDA (GPIO 14)
    gpio_pull_up(display_i2c_scl); // Ativa o resistor de pull up para o pino SCL (GPIO 15)
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, display_i2c_endereco, display_i2c_port); // Inicializa o display
    ssd1306_config(&ssd); // Configura o display
    ssd1306_send_data(&ssd); // Envia os dados para o display
    ssd1306_fill(&ssd, false); // Limpa o display
    ssd1306_send_data(&ssd); // Atualiza o display

    // Incialização do I2C dos sensores
    i2c_init(I2C_PORT, 400 * 1000); // Inicializa o I2C usando 400kHz
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // Define o pino SDA (GPIO 0) na função I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // Define o pino SCL (GPIO 1) na função I2C
    gpio_pull_up(I2C_SDA); // Ativa o resistor de pull up para o pino SDA (GPIO 0)
    gpio_pull_up(I2C_SCL); // Ativa o resistor de pull up para o pino SCL (GPIO 1)

    // Inicialização do BMP280
    bmp280_init(I2C_PORT);
    bmp280_get_calib_params(I2C_PORT, &params);

    // Inicialização do AHT20
    aht20_reset(I2C_PORT);
    aht20_init(I2C_PORT);

    while (true) {
        
        ler_bmp280(); // Leitura do sensor BMP280
        ler_aht10();  // Leitura do sensor AHT10
        
        atualizar_display(); // Atualiza o display OLED

        sleep_ms(500); // Delay de 500ms
    }
}