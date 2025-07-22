// -- Inclusão de bibliotecas
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include <string.h>
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"
#include "aht20.h"
#include "bmp280.h"
#include "ssd1306.h"
#include "font.h"
#include <math.h>
#include "pico/bootrom.h"


// -- Definição de constantes

// Credenciais da rede local
#define WIFI_SSID "-"
#define WIFI_PASS "-"

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
#define matriz_leds 7 // Matriz de LEDs GPIO 7
#define NUM_LEDS 25 // Número de LEDs na matriz
#define LED_Green 11 // LED Verde GPIO 11
#define LED_Blue 12 // LED Azul GPIO 12
#define LED_Red 13 // LED Vermelho GPIO 13
#define buzzer_A 21 // Buzzer A GPIO 21
#define buzzer_B 10 // Buzzer B GPIO 10


// -- Definição de variáveis globais

static volatile uint32_t last_time = 0; // Armazena o tempo do último clique dos botões (debounce)

volatile int32_t pressao = 0; // Armazena o valor da pressão medido pelo BMP280
volatile double altitude = 0; // Armazena o valor de altitude calculado

volatile float temperatura_final = 0; // Armazena o valor final da temperatura
volatile float pressao_final = 0; // Armazena o valor final da pressão
volatile float altitude_final = 0; // Armazena o valor final da altitude
volatile float umidade_final = 0; // Armazena o valor final da umidade

volatile float temperatura_offset = 0; // Armazena o valor do offset da temperatura
volatile float pressao_offset = 0; // Armazena o valor do offset da pressão
volatile float altitude_offset = 0; // Armazena o valor do offset da altitude
volatile float umidade_offset = 0; // Armazena o valor do offset da umidade

volatile float temperatura_min = 10.0; // Armazena o valor de temperatura mínima
volatile float temperatura_max = 35.0; // Armazena o valor de temperatura máxima
volatile float umidade_min = 30.0; // Armazena o valor de umidade mínima
volatile float umidade_max = 70.0; // Armazena o valor de umidade máxima

volatile int tela = 1; // Armazena qual a tela está ativada no momento
volatile int text_wifi = 1; // Armazena qual texto do Wi-Fi será mostrado no display

char str_ip[24];

char str_temperatura[5]; // Armazena o valor da temperatura em string
char str_pressao[6]; // Armazena o valor da pressão em string
char str_altitude[5]; // Armazena o valor da altitude em string
char str_umidade[5]; // Armazena o valor da umidade em string

char str_temperatura_min[5]; // Armazena o valor de temperatura mínima em string
char str_temperatura_max[5]; // Armazena o valor de temperatura máxima em string
char str_umidade_min[5]; // Armazena o valor de umidade mínima em string
char str_umidade_max[5]; // Armazena o valor de umidade máxima em string



// --- Inicio das funções necessárias para a manipulação do buzzer

void pwm_buzzer(uint gpio, bool active); // Esta função precisa ser declarada antes dos alarmes

// Função de callback do alarme do buzzer
int64_t alarm_callback_buzzer(alarm_id_t id, void *user_data){
    pwm_buzzer(buzzer_A, false);
    pwm_buzzer(buzzer_B, false);
    return 0;
}

// Função para definir a frequência do som do buzzer
void pwm_freq(uint gpio, uint freq) {
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint clock_div = 4; // Define o divisor do clock
    uint wrap_value = (125000000 / (clock_div * freq)) - 1; // Define o valor do Wrap

    pwm_set_clkdiv(slice, clock_div); // Define o divisor do clock
    pwm_set_wrap(slice, wrap_value); // Define o contador do PWM
    pwm_set_chan_level(slice, pwm_gpio_to_channel(gpio), wrap_value / 40); // Duty cycle (Volume)
}

// Função para ativar/desativar o buzzer
void pwm_buzzer(uint gpio, bool active){
    uint slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_enabled(slice, active);
}

// Função para beepar o buzzer
void beep_buzzer(uint time){
    pwm_buzzer(buzzer_A, true);
    pwm_buzzer(buzzer_B, true);
    add_alarm_in_ms(time, alarm_callback_buzzer, NULL, false);
}

// --- Final das funções necessárias para a manipulação do buzzer



// --- Funções necessária para a manipulação da matriz de LEDs

// Estrutura do pixel GRB (Padrão do WS2812)
struct pixel_t {
    uint8_t G, R, B; // Define variáveis de 8-bits (0 a 255) para armazenar a cor
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t; // Permite declarar variáveis utilizando apenas "npLED_t"

// Declaração da Array que representa a matriz de LEDs
npLED_t leds[NUM_LEDS];

// Variáveis para máquina PIO
PIO np_pio;
uint sm;

// Função para definir a cor de um LED específico
void cor(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
    leds[index].R = r;
    leds[index].G = g;
    leds[index].B = b;
}

// Função para desligar todos os LEDs
void desliga() {
    for (uint i = 0; i < NUM_LEDS; ++i) {
        cor(i, 0, 0, 0);
    }
}

// Função para enviar o estado atual dos LEDs ao hardware  - buffer de saída
void buffer() {
    for (uint i = 0; i < NUM_LEDS; ++i) {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
}

// Função para converter a posição da matriz para uma posição do vetor.
int getIndex(int x, int y) {
    // Se a linha for par (0, 2, 4), percorremos da esquerda para a direita.
    // Se a linha for ímpar (1, 3), percorremos da direita para a esquerda.
    if (y % 2 == 0) {
        return 24-(y * 5 + x); // Linha par (esquerda para direita).
    } else {
        return 24-(y * 5 + (4 - x)); // Linha ímpar (direita para esquerda).
    }
}

// --- Final das funções necessária para a manipulação da matriz de LEDs


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
        if(text_wifi == 1){
            ssd1306_draw_string(&ssd, "Iniciando...", 16, 32); // Desenha uma string    
        }else if(text_wifi == 2){
            ssd1306_draw_string(&ssd, "Conectando...", 12, 32); // Desenha uma string
        }else if(text_wifi == 3){
            ssd1306_draw_string(&ssd, "Falha!", 40, 32); // Desenha uma string
        }else if(text_wifi == 4){
            ssd1306_draw_string(&ssd, "Conectado!", 24, 32); // Desenha uma string
        }else{
            ssd1306_draw_string(&ssd, "Erro!", 44, 32); // Desenha uma string
        }

        ssd1306_line(&ssd, 1, 41, 126, 41, true); // Desenha uma linha horizontal

        // IP do WebServer
        ssd1306_draw_string(&ssd, "IP Web Server:", 8, 43); // Desenha uma string
        ssd1306_draw_string(&ssd, str_ip, 16, 52); // Desenha uma string

    }else if(tela == 2){ // TELA 2 - GERAL
        // Cabeçalho
        ssd1306_draw_string(&ssd, "Dados do local:", 4, 3); // Desenha uma string

        ssd1306_line(&ssd, 1, 12, 126, 12, true); // Desenha uma linha horizontal

        // Temperatura
        sprintf(str_temperatura, "%.1fC", temperatura_final);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Tem:", 4, 15); // Desenha uma string
        ssd1306_draw_string(&ssd, str_temperatura, 40, 15); // Desenha uma string

        ssd1306_line(&ssd, 1, 25, 126, 25, true); // Desenha uma linha horizontal

        // Pressão
        sprintf(str_pressao, "%.2fkPa", pressao_final);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Pre:", 4, 28); // Desenha uma string
        ssd1306_draw_string(&ssd, str_pressao, 40, 28); // Desenha uma string

        ssd1306_line(&ssd, 1, 38, 126, 38, true); // Desenha uma linha horizontal

        // Altitude
        sprintf(str_altitude, "%.0fm", altitude_final);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Alt:", 4, 41); // Desenha uma string
        ssd1306_draw_string(&ssd, str_altitude, 40, 41); // Desenha uma string

        ssd1306_line(&ssd, 1, 51, 126, 51, true); // Desenha uma linha horizontal

        // Umidade
        sprintf(str_umidade, "%.1f%%", umidade_final);  // Converte o float em string
        ssd1306_draw_string(&ssd, "Umi:", 4, 53); // Desenha uma string
        ssd1306_draw_string(&ssd, str_umidade, 40, 53); // Desenha uma string

    }else if(tela == 3){ // TELA 3 - DETALHES DA TEMPERATURA
        // Cabeçalho
        ssd1306_draw_string(&ssd, "TEMPERATURA", 20, 3); // Desenha uma string

        ssd1306_line(&ssd, 1, 12, 126, 12, true); // Desenha uma linha horizontal

        // Temperatura medida
        sprintf(str_temperatura, "%.1fC", temperatura_final);  // Converte o float em string
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
        if(temperatura_final >= temperatura_max){
            ssd1306_draw_string(&ssd, "Alerta: T > Max", 2, 53); // Desenha uma string
        }else if(temperatura_final <= temperatura_min){
            ssd1306_draw_string(&ssd, "Alerta: T < Min", 2, 53); // Desenha uma string
        }else{
            ssd1306_draw_string(&ssd, "Status: Ok", 24, 53); // Desenha uma string
        }
    }else if(tela == 4){ // TELA 4 - DETALHES DA UMIDADE
        // Cabeçalho
        ssd1306_draw_string(&ssd, "UMIDADE", 36, 3); // Desenha uma string

        ssd1306_line(&ssd, 1, 12, 126, 12, true); // Desenha uma linha horizontal

        // Umidade medida
        sprintf(str_umidade, "%.1f%%", umidade_final);  // Converte o float em string
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
        if(umidade_final >= umidade_max){
            ssd1306_draw_string(&ssd, "Alerta: U > Max", 2, 53); // Desenha uma string
        }else if(umidade_final <= umidade_min){
            ssd1306_draw_string(&ssd, "Alerta: U < Min", 2, 53); // Desenha uma string
        }else{
            ssd1306_draw_string(&ssd, "Status: Ok", 24, 53); // Desenha uma string
        }
    }
    ssd1306_send_data(&ssd); // Atualiza o display
}

// Função para atualizar a matriz de LEDs
void atualizar_matriz(bool alerta){
    desliga();
    if(alerta){
        // Frame "!"
        int frame0[5][5][3] = {
            {{0, 0, 0}, {0, 0, 0}, {150, 0, 0}, {0, 0, 0}, {0, 0, 0}},
            {{0, 0, 0}, {0, 0, 0}, {150, 0, 0}, {0, 0, 0}, {0, 0, 0}},    
            {{0, 0, 0}, {0, 0, 0}, {150, 0, 0}, {0, 0, 0}, {0, 0, 0}},
            {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
            {{0, 0, 0}, {0, 0, 0}, {150, 0, 0}, {0, 0, 0}, {0, 0, 0}}
        };
        for (int linha = 0; linha < 5; linha++){
            for (int coluna = 0; coluna < 5; coluna++){
                int posicao = getIndex(linha, coluna);
                cor(posicao, frame0[coluna][linha][0], frame0[coluna][linha][1], frame0[coluna][linha][2]);
            }
        };
        buffer();
    }else{
        // Frame "Check"
        int frame1[5][5][3] = {
            {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
            {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 150, 0}},    
            {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 150, 0}, {0, 0, 0}},
            {{0, 150, 0}, {0, 0, 0}, {0, 150, 0}, {0, 0, 0}, {0, 0, 0}},
            {{0, 0, 0}, {0, 150, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}
        };
        for (int linha = 0; linha < 5; linha++){
            for (int coluna = 0; coluna < 5; coluna++){
                int posicao = getIndex(linha, coluna);
                cor(posicao, frame1[coluna][linha][0], frame1[coluna][linha][1], frame1[coluna][linha][2]);
            }
        };
        buffer();
    }
}

void atualizar_valores(){
    temperatura_final = data.temperature + temperatura_offset;
    pressao_final = (pressao / 1000) + pressao_offset;
    altitude_final = altitude + altitude_offset;
    umidade_final = data.humidity +umidade_offset;
}

// --- Inicio das funções necessárias para a manipulação do modulo Wi-Fi

const char HTML_BODY[] =
"<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Estação Meteorológica</title>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<script src='https://cdn.jsdelivr.net/npm/chart.js'></script><style>"
"body{font:sans-serif;text-align:center;background:#f9f9f9;margin:0;padding:10px}"
".c{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:20px;padding:10px}"
".g{background:#fff;padding:10px;border-radius:10px;box-shadow:0 0 10px rgba(0,0,0,.1)}"
".m{margin-top:5px;font-weight:700}"
".v{font-weight:700;margin-bottom:5px;font-size:1.1em;color:#222}"
"canvas{width:100%!important;height:auto!important}"
".f{display:flex;flex-wrap:wrap;justify-content:center;gap:20px;margin-top:20px}"
"form{flex:1;min-width:280px;max-width:600px;background:#fff;padding:15px;border-radius:10px;text-align:left;box-shadow:0 0 10px rgba(0,0,0,.1)}"
"label{display:block;margin-top:10px;font-weight:700;color:#222}"
"input[type=number]{width:100%;padding:8px;margin-top:5px;border-radius:5px;border:1px solid #ccc;box-sizing:border-box;font-size:1em}"
"button{margin-top:15px;padding:10px 20px;font-size:1em;border:none;border-radius:8px;background:#4CAF50;color:#fff;cursor:pointer}"
"button:hover{background:#45a049}"
"</style></head><body>"

"<h1>Estação Meteorológica</h1><h2>Dados em tempo real</h2><div class='c'>"

"<div class='g'><div id='valorTemp' class='v'>Temperatura atual: -- °C</div><h3>Temperatura (°C)</h3>"
"<canvas id='chartTemp'></canvas><div id='mediaTemp' class='m'>Média: --</div></div>"

"<div class='g'><div id='valorPres' class='v'>Pressão atual: -- kPa</div><h3>Pressão (kPa)</h3>"
"<canvas id='chartPres'></canvas><div id='mediaPres' class='m'>Média: --</div></div>"

"<div class='g'><div id='valorAlt' class='v'>Altitude atual: -- m</div><h3>Altitude (m)</h3>"
"<canvas id='chartAlt'></canvas><div id='mediaAlt' class='m'>Média: --</div></div>"

"<div class='g'><div id='valorUmi' class='v'>Umidade atual: -- %</div><h3>Umidade (%)</h3>"
"<canvas id='chartUmi'></canvas><div id='mediaUmi' class='m'>Média: --</div></div>"

"</div>"

"<div class='f'>"

"<form onsubmit='return enviarLimites();'>"
"<h3>Configurar limites de Temperatura e Umidade</h3>"

"<label for='temp_min'>Temperatura mínima (°C):</label>"
"<input type='number' step='0.1' id='temp_min' value='10.0' required>"

"<label for='temp_max'>Temperatura máxima (°C):</label>"
"<input type='number' step='0.1' id='temp_max' value='35.0' required>"

"<label for='umi_min'>Umidade mínima (%):</label>"
"<input type='number' step='0.1' id='umi_min' value='30.0' required>"

"<label for='umi_max'>Umidade máxima (%):</label>"
"<input type='number' step='0.1' id='umi_max' value='70.0' required>"

"<button type='submit'>Salvar Limites</button>"
"</form>"

"<form onsubmit='return enviarOffsets();'>"
"<h3>Calibrar Sensores (Offset)</h3>"

"<label for='temp_off'>Offset Temperatura (°C):</label>"
"<input type='number' step='0.1' id='temp_off' value='0.0' required>"

"<label for='pres_off'>Offset Pressão (kPa):</label>"
"<input type='number' step='0.1' id='pres_off' value='0.0' required>"

"<label for='alt_off'>Offset Altitude (m):</label>"
"<input type='number' step='0.1' id='alt_off' value='0.0' required>"

"<label for='umi_off'>Offset Umidade (%):</label>"
"<input type='number' step='0.1' id='umi_off' value='0.0' required>"

"<button type='submit'>Salvar Offsets</button>"
"</form>"

"</div>"

"<script>"
"function enviarLimites(){"
"  const tmin=document.getElementById('temp_min').value;"
"  const tmax=document.getElementById('temp_max').value;"
"  const umin=document.getElementById('umi_min').value;"
"  const umax=document.getElementById('umi_max').value;"
"  const url=`/set_limits?temp_min=${tmin}&temp_max=${tmax}&umi_min=${umin}&umi_max=${umax}`;"
"  fetch(url).then(r=>r.text()).then(t=>alert(t)).catch(e=>alert('Erro: '+e));"
"  return false;"
"}"

"function enviarOffsets(){"
"  const toff=document.getElementById('temp_off').value;"
"  const poff=document.getElementById('pres_off').value;"
"  const aoff=document.getElementById('alt_off').value;"
"  const uoff=document.getElementById('umi_off').value;"
"  const url=`/set_offsets?temp_off=${toff}&pres_off=${poff}&alt_off=${aoff}&umi_off=${uoff}`;"
"  fetch(url).then(r=>r.text()).then(t=>alert(t)).catch(e=>alert('Erro: '+e));"
"  return false;"
"}"

"let dadosTemp=[],dadosPres=[],dadosAlt=[],dadosUmi=[],tempo=[];"
"const opcoes=l=>({responsive:true,scales:{y:{beginAtZero:false},x:{display:false}},plugins:{legend:{display:false},title:{display:true,text:l}}});"
"const criarGrafico=(id,l,c)=>new Chart(document.getElementById(id),{type:'line',data:{labels:tempo,datasets:[{label:l,data:[],borderColor:c,tension:.3,fill:false}]},options:opcoes(l)});"
"let chartTemp=criarGrafico('chartTemp','Temperatura','red'),chartPres=criarGrafico('chartPres','Pressão','blue'),chartAlt=criarGrafico('chartAlt','Altitude','green'),chartUmi=criarGrafico('chartUmi','Umidade','purple');"
"function atualizar(){fetch('/dados').then(r=>r.json()).then(d=>{const t=new Date().toLocaleTimeString();if(tempo.length>=20)tempo.shift();tempo.push(t);"
"const pushEAtualiza=(a,v,c,m)=>{if(a.length>=20)a.shift();a.push(v);c.data.labels=tempo;c.data.datasets[0].data=a;c.update();const med=(a.reduce((x,y)=>x+y,0)/a.length).toFixed(2);document.getElementById(m).innerText='Média: '+med;};"
"if(d.tem!==undefined)document.getElementById('valorTemp').innerText='Temperatura atual: '+parseFloat(d.tem).toFixed(2)+' °C';"
"if(d.pre!==undefined)document.getElementById('valorPres').innerText='Pressão atual: '+parseFloat(d.pre).toFixed(2)+' kPa';"
"if(d.alt!==undefined)document.getElementById('valorAlt').innerText='Altitude atual: '+parseFloat(d.alt).toFixed(2)+' m';"
"if(d.umi!==undefined)document.getElementById('valorUmi').innerText='Umidade atual: '+parseFloat(d.umi).toFixed(2)+' %';"
"pushEAtualiza(dadosTemp,parseFloat(d.tem),chartTemp,'mediaTemp');"
"pushEAtualiza(dadosPres,parseFloat(d.pre),chartPres,'mediaPres');"
"pushEAtualiza(dadosAlt,parseFloat(d.alt),chartAlt,'mediaAlt');"
"pushEAtualiza(dadosUmi,parseFloat(d.umi),chartUmi,'mediaUmi');});}"
"setInterval(atualizar,1000);"
"</script></body></html>";


struct http_state
{
    char response[20000];
    size_t len;
    size_t sent;
};

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;

    if (strstr(req, "GET /set_limits?")) {
        beep_buzzer(200);
        float t_min, t_max, u_min, u_max;
        // Exemplo de sscanf para pegar os valores (ajuste se necessário)
        sscanf(req, "GET /set_limits?temp_min=%f&temp_max=%f&umi_min=%f&umi_max=%f", &t_min, &t_max, &u_min, &u_max);

        // Atualize suas variáveis globais com os novos limites
        temperatura_min = t_min;
        temperatura_max = t_max;
        umidade_min = u_min;
        umidade_max = u_max;

        const char *txt = "Limites atualizados com sucesso";
        hs->len = snprintf(hs->response, sizeof(hs->response),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: %d\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "%s",
                        (int)strlen(txt), txt);
    }
    else if (strstr(req, "GET /dados"))
    {
        char json_payload[2048];
        int json_len = snprintf(json_payload, sizeof(json_payload),
                                "{\"tem\":%.1f,\"pre\":%.2f,\"alt\":%.0f,\"umi\":%.1f}\r\n",
                                temperatura_final, pressao_final, altitude_final, umidade_final);

        printf("[DEBUG] JSON: %s\n", json_payload);

        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           json_len, json_payload);
    }
    else if (strstr(req, "GET /set_offsets?")) {
        beep_buzzer(200);
        float t_off, p_off, a_off, u_off;
        sscanf(req, "GET /set_offsets?temp_off=%f&pres_off=%f&alt_off=%f&umi_off=%f", &t_off, &p_off, &a_off, &u_off);

        temperatura_offset = t_off;
        pressao_offset = p_off;
        altitude_offset = a_off;
        umidade_offset = u_off;

        const char *txt = "Offsets atualizados com sucesso";
        hs->len = snprintf(hs->response, sizeof(hs->response),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: %d\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "%s",
                        (int)strlen(txt), txt);
    }
    else
    {
        hs->len = snprintf(hs->response, sizeof(hs->response),
                           "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html\r\n"
                           "Content-Length: %d\r\n"
                           "Connection: close\r\n"
                           "\r\n"
                           "%s",
                           (int)strlen(HTML_BODY), HTML_BODY);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);

    tcp_write(tpcb, hs->response, hs->len, TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);
    return ERR_OK;
}

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}

// --- Final das funções necessárias para a manipulação do modulo Wi-Fi


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
    sleep_ms(2000);

    // Inicialização dos LEDs
    gpio_init(LED_Green);
    gpio_set_dir(LED_Green, GPIO_OUT);
    gpio_init(LED_Blue);
    gpio_set_dir(LED_Blue, GPIO_OUT);
    gpio_init(LED_Red);
    gpio_set_dir(LED_Red, GPIO_OUT);

    // PWM
    gpio_set_function(buzzer_A, GPIO_FUNC_PWM); // Define a função da porta GPIO como PWM
    gpio_set_function(buzzer_B, GPIO_FUNC_PWM); // Define a função da porta GPIO como PWM
    pwm_freq(buzzer_A, 1000); // Define a frequência do buzzer A
    pwm_freq(buzzer_B, 1000); // Define a frequência do buzzer B

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

    // Inicialização do PIO
    np_pio = pio0;
    sm = pio_claim_unused_sm(np_pio, true);
    uint offset = pio_add_program(pio0, &ws2818b_program);
    ws2818b_program_init(np_pio, sm, offset, matriz_leds, 800000);
    desliga(); // Para limpar o buffer dos LEDs
    buffer(); // Atualiza a matriz de LEDs

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

    gpio_put(LED_Green, 0);
    gpio_put(LED_Blue, 1);
    gpio_put(LED_Red, 0);

    atualizar_display(); // Atualiza o display OLED

    // Inicialização do servidor

    if (cyw43_arch_init())
    {
        text_wifi = 3;
        gpio_put(LED_Green, 0);
        gpio_put(LED_Blue, 0);
        gpio_put(LED_Red, 1);
        atualizar_display(); // Atualiza o display OLED
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    text_wifi = 2;
    gpio_put(LED_Green, 1);
    gpio_put(LED_Blue, 0);
    gpio_put(LED_Red, 1);
    atualizar_display(); // Atualiza o display OLED
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        text_wifi = 3;
        gpio_put(LED_Green, 0);
        gpio_put(LED_Blue, 0);
        gpio_put(LED_Red, 1);
        atualizar_display(); // Atualiza o display OLED
        return 1;
    }

    text_wifi = 4;
    gpio_put(LED_Green, 1);
    gpio_put(LED_Blue, 0);
    gpio_put(LED_Red, 0);
    atualizar_display(); // Atualiza o display OLED

    uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
    snprintf(str_ip, sizeof(str_ip), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    atualizar_display(); // Atualiza o display OLED

    start_http_server();

    while (true) {

        cyw43_arch_poll();
        
        ler_bmp280(); // Leitura do sensor BMP280
        ler_aht10();  // Leitura do sensor AHT10

        atualizar_valores();
        
        if(temperatura_final <= temperatura_min || temperatura_final >= temperatura_max || umidade_final <= umidade_min || umidade_final >= umidade_max){
            atualizar_matriz(true);
        }else{
            atualizar_matriz(false);
        }
        
        atualizar_display(); // Atualiza o display OLED

        sleep_ms(300); // Delay de 300ms
    }

    cyw43_arch_deinit();
    return 0;
}