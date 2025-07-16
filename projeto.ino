#include <Arduino_FreeRTOS.h> // Inclui a biblioteca principal do FreeRTOS
#include <semphr.h>           // Inclui funcionalidades de semáforos (para filas, mutexes, etc.)
#include <timers.h>           // Inclui funcionalidades de timers por software (xTimer)
#include <Wire.h>             // Necessário para comunicação I2C do OLED
#include <Adafruit_GFX.h>     // Biblioteca gráfica para desenho no OLED
#include <Adafruit_SSD1306.h> // Biblioteca específica para o driver SSD1306 do OLED
#include <Servo.h>            // Inclui a biblioteca Servo para controle do servo motor
#include <Keypad.h>           // Inclui a biblioteca Keypad para o teclado matricial

// --- Definições de Pinos e Constantes ---
// Pinos para os componentes (verifique as suas conexões físicas)
const int LED_PIN = 13;             // Pino do LED embutido (para teste visual geral do sistema ativo)
const int VIBRATION_SENSOR_PIN = 2; // Pino digital para o sensor de vibração (SW-420)
const int BUZZER_PIN = 8;           // Pino digital para o buzzer passivo
const int SERVO_PIN = 9;            // Pino digital para o servo motor
const int POTENTIOMETER_PIN = A0;   // Pino analógico para o potenciômetro
const int LDR_PIN = A1;             // Pino analógico para o LDR (Resistor Dependente de Luz)

// Pinos para LEDs específicos de cada enigma (AGORA APENAS 4 ENIGMAS)
const int LED_ENIGMA_1_PIN = 30; // LED para Enigma 1 (Vibração)
const int LED_ENIGMA_2_PIN = 31; // LED para Enigma 2 (LDR)
const int LED_ENIGMA_3_PIN = 32; // LED para Enigma 3 (Teclado)
const int LED_ENIGMA_4_PIN = 33; // LED para Enigma 4 (Potenciômetro)

// Definições para o display OLED 128x64
#define SCREEN_WIDTH 128    // Largura do display OLED, em pixels
#define SCREEN_HEIGHT 64    // Altura do display OLED, em pixels
#define OLED_RESET -1       // Pino de reset do OLED (ou -1 se não usado, comum para módulos I2C)

// Objeto de display SSD1306 (global para ser acessível pelas tarefas)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Objeto Servo (global para ser acessível pelas tarefas)
Servo meuServo;

// --- Configuração do Teclado Matricial 4x3 ---
const byte ROWS = 4; // Quatro linhas
const byte COLS = 3; // Três colunas (para teclado 4x3)

// Define o mapeamento das teclas do teclado 4x3
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

// Pinos do Arduino conectados às linhas do teclado (R1, R2, R3, R4)
byte rowPins[ROWS] = {22, 23, 24, 25}; // Use pinos digitais do Mega
// Pinos do Arduino conectados às colunas do teclado (C1, C2, C3)
byte colPins[COLS] = {26, 27, 28};     // Use pinos digitais do Mega (apenas 3 pinos para colunas)

// Cria o objeto Keypad
Keypad customKeypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- Definição dos Estados do Sistema (Máquina de Estados) ---
typedef enum {
  STATE_DORMANT,            // Sistema esperando a ativação (Estágio 1)
  STATE_ENIGMA_1_VIBRATION, // Estágio 1 em andamento (aguardando vibração) - ATIVAÇÃO
  STATE_ENIGMA_2_LDR,       // Estágio 2: Luz de Eärendil (LDR)
  STATE_ENIGMA_3_KEYPAD,    // Estágio 3: Enigma do Teclado
  STATE_ENIGMA_4_POTENTIOMETER, // Estágio 4: A Balança de Durin (Potenciômetro)
  STATE_SUCCESS,            // Enigmas resolvidos, cofre aberto
  STATE_FAILURE             // Falha em um enigma, sistema resetando
} SystemState_t;

// Variável de estado global (acessada pela Task_ConselhoDeElrond)
SystemState_t currentSystemState = STATE_DORMANT;

// --- Handles de Filas (Queues) ---
// As filas são usadas para comunicação assíncrona entre as tarefas.

// Estrutura para eventos dos periféricos (Palantir) para o Conselho de Elrond
typedef struct {
  enum { EVENT_VIBRATION_DETECTED, EVENT_KEYPAD_CHAR,
          EVENT_POTENTIOMETER_READING, EVENT_LDR_READING, EVENT_ENIGMA_TIMEOUT } type; // Adicionado EVENT_ENIGMA_TIMEOUT
  union {
    char keyChar;           // Para caracteres individuais do teclado
    int potentiometerValue;  // Para o enigma do potenciômetro
    int ldrValue;          // Para o enigma do LDR
  } data;
} EventData_t;
QueueHandle_t xEventQueue;

// Estrutura para comandos do Conselho de Elrond para o Espelho de Galadriel (display OLED)
typedef struct {
    enum { CMD_DISPLAY_CLEAR, CMD_DISPLAY_TEXT, CMD_DRAW_PROGRESS_BAR, CMD_DISPLAY_OFF, CMD_DISPLAY_KEYPAD_INPUT,
            CMD_DISPLAY_POTENTIOMETER_GAUGE, CMD_DISPLAY_LDR_ENIGMA } type;
    union {
        struct { char text[64]; } textCmd; // Texto a ser exibido (aumentado para caber mais)
        struct { char input[7]; int currentLength; } keypadInputCmd; // Para exibir entrada do teclado
        struct { int currentValue; int targetMin; int targetMax; } potentiometerGaugeCmd; // Para o enigma do potenciometro
        struct { int ldrCurrentValue; } ldrEnigmaCmd; // Para o enigma do LDR
    } data;
} DisplayCommand_t;
QueueHandle_t xDisplayCommandQueue;

// Estrutura para comandos do Conselho de Elrond para a Voz de Saruman (atuadores)
typedef struct {
    enum { CMD_PLAY_SOUND, CMD_MOVE_SERVO, CMD_STOP_SOUND } type;
    union {
        struct { int frequency; int duration; } soundCmd; // Frequência em Hz, Duração em ms
        struct { int angle; } servoCmd;                  // Ângulo em graus (0-180)
    } data;
} ActuatorCommand_t;
QueueHandle_t xActuatorCommandQueue;

// --- Declaração dos Timers por Software (xTimer) ---
TimerHandle_t xPotentiometerTimer; // Timer para o enigma do potenciometro
TimerHandle_t xEnigmaTimeoutTimer; // NOVO: Timer para o timeout de cada enigma

// Prototipo da funcao de callback do timer do potenciometro
void vPotentiometerTimerCallback(TimerHandle_t pxTimer);
// NOVO: Prototipo da funcao de callback do timer de timeout do enigma
void vEnigmaTimeoutCallback(TimerHandle_t pxTimer);

// --- Declaração das Tarefas (Protótipos de Função) ---
void Task_ConselhoDeElrond(void *pvParameters);   // Gerencia a máquina de estados, valida respostas
void Task_Palantir(void *pvParameters);           // Monitora os periféricos de entrada
void Task_EspelhoDeGaladriel(void *pvParameters); // Controla o display OLED
void Task_VozDeSaruman(void *pvParameters);       // Controla os atuadores (Servo e Buzzer)

// --- Configuração (setup) ---
void setup() {
  Serial.begin(9600); // Inicia a comunicação serial para depuração
  pinMode(LED_PIN, OUTPUT); // Define o pino do LED geral como saída
  pinMode(BUZZER_PIN, OUTPUT); // Define o pino do buzzer como saída

  // Configura os pinos dos LEDs de enigma como saída
  pinMode(LED_ENIGMA_1_PIN, OUTPUT);
  pinMode(LED_ENIGMA_2_PIN, OUTPUT);
  pinMode(LED_ENIGMA_3_PIN, OUTPUT);
  pinMode(LED_ENIGMA_4_PIN, OUTPUT);

  // Garante que todos os LEDs de enigma estejam apagados no início
  digitalWrite(LED_ENIGMA_1_PIN, LOW);
  digitalWrite(LED_ENIGMA_2_PIN, LOW);
  digitalWrite(LED_ENIGMA_3_PIN, LOW);
  digitalWrite(LED_ENIGMA_4_PIN, LOW);

  // Inicializa o servo motor
  meuServo.attach(SERVO_PIN);
  meuServo.write(0); // Posição inicial do servo (fechado)

  // Inicializa o display OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Endereço I2C 0x3C (ou 0x3D)
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Trava se o display não for encontrado
  }
  display.clearDisplay();
  display.display(); // Garante que o display esteja apagado no início

  // Cria as filas antes de criar as tarefas que as usarão.
  xEventQueue = xQueueCreate(5, sizeof(EventData_t));
  if (xEventQueue == NULL) { Serial.println("Erro ao criar xEventQueue"); while (1); }
  xDisplayCommandQueue = xQueueCreate(5, sizeof(DisplayCommand_t));
  if (xDisplayCommandQueue == NULL) { Serial.println("Erro ao criar xDisplayCommandQueue"); while (1); }
  xActuatorCommandQueue = xQueueCreate(5, sizeof(ActuatorCommand_t));
  if (xActuatorCommandQueue == NULL) { Serial.println("Erro ao criar xActuatorCommandQueue"); while (1); }

  // Cria o timer por software para o potenciometro
  xPotentiometerTimer = xTimerCreate("PotTimer", pdMS_TO_TICKS(3000), pdFALSE, (void *)0, vPotentiometerTimerCallback);
  if (xPotentiometerTimer == NULL) {
    Serial.println("Erro ao criar xPotentiometerTimer");
    while (1);
  }

  // NOVO: Cria o timer por software para o timeout do enigma (30 segundos)
  xEnigmaTimeoutTimer = xTimerCreate("EnigmaTimeout", pdMS_TO_TICKS(30000), pdFALSE, (void *)0, vEnigmaTimeoutCallback);
  if (xEnigmaTimeoutTimer == NULL) {
    Serial.println("Erro ao criar xEnigmaTimeoutTimer");
    while (1);
  }

  // Cria as tarefas FreeRTOS.
  // Prioridades: Conselho (3) > Palantir (2) > Display/Atuadores (1)
  xTaskCreate(Task_ConselhoDeElrond, "ConselhoDeElrond", 1024, NULL, 3, NULL); // Aumenta pilha para estados e strings
  xTaskCreate(Task_Palantir, "Palantir", 256, NULL, 2, NULL);
  xTaskCreate(Task_EspelhoDeGaladriel, "EspelhoDeGaladriel", 768, NULL, 1, NULL); // Aumenta pilha para operações de display
  xTaskCreate(Task_VozDeSaruman, "VozDeSaruman", 256, NULL, 1, NULL);

  // Inicia o scheduler do FreeRTOS.
  vTaskStartScheduler();

  // Esta linha só será alcançada se houver um erro fatal no scheduler.
  Serial.println("Erro fatal: Scheduler nao iniciado!");
  while (1);
}

// --- Loop Principal (loop) ---
void loop() {
  // FreeRTOS está controlando tudo.
}

// --- Funcao de Callback do Timer do Potenciometro ---
void vPotentiometerTimerCallback(TimerHandle_t pxTimer) {
  (void) pxTimer; // Para evitar warnings de parametro nao utilizado
  // Esta funcao e chamada quando o timer do potenciometro expira.

  // Envia um evento de sucesso para a Task_ConselhoDeElrond
  EventData_t event;
  event.type = EventData_t::EVENT_POTENTIOMETER_READING; // Reutilizamos o tipo de evento, mas com um valor especial
  event.data.potentiometerValue = -1; // Valor especial para indicar "timer expirou"
  if (xQueueSend(xEventQueue, &event, 0) != pdPASS) { // Nao bloqueia se a fila estiver cheia
    Serial.println("Timer Callback: Erro ao enviar evento de sucesso do potenciometro.");
  } else {
    Serial.println("Timer Callback: Evento de sucesso do potenciometro enviado.");
  }
}

// NOVO: Funcao de Callback do Timer de Timeout do Enigma
void vEnigmaTimeoutCallback(TimerHandle_t pxTimer) {
  (void) pxTimer; // Para evitar warnings de parametro nao utilizado
  // Esta funcao e chamada quando o timer de timeout de um enigma expira.

  Serial.println("Enigma Timeout: Tempo esgotado para o enigma atual!");
  EventData_t event;
  event.type = EventData_t::EVENT_ENIGMA_TIMEOUT; // Novo tipo de evento para timeout
  if (xQueueSend(xEventQueue, &event, 0) != pdPASS) { // Nao bloqueia se a fila estiver cheia
    Serial.println("Enigma Timeout Callback: Erro ao enviar evento de timeout.");
  }
}

// --- Implementação das Tarefas ---

// Tarefa: Task_ConselhoDeElrond
// Responsabilidade Principal: Gerenciar a máquina de estados, valida respostas e orquestrar outras tarefas.
void Task_ConselhoDeElrond(void *pvParameters) {
  (void) pvParameters;
  EventData_t receivedEvent;
  DisplayCommand_t displayCommand;
  ActuatorCommand_t actuatorCommand;

  // Variáveis para o Enigma 2 (LDR)
  const int LDR_THRESHOLD = 700; // Limiar de luz para o LDR (ajuste conforme o seu LDR e ambiente)

  // Variáveis para o Enigma 3 (Teclado)
  static char keypadInputBuffer[7]; // Buffer para armazenar a entrada do teclado (6 dígitos + null terminator)
  static int keypadInputIndex = 0;  // Índice da próxima posição livre no buffer
  const char* MELLON_PASSWORD = "635566"; // Senha correta para "Mellon" no T9

  // Variáveis para o Enigma 4 (Potenciômetro)
  const int POT_TARGET_MIN = 400; // Valor mínimo da faixa alvo do potenciômetro (0-1023)
  const int POT_TARGET_MAX = 600; // Valor máximo da faixa alvo do potenciômetro
  static bool potentiometerInTarget = false; // Flag para saber se o potenciometro esta na faixa alvo

  // Início no estado DORMANT
  currentSystemState = STATE_DORMANT;
  displayCommand.type = DisplayCommand_t::CMD_DISPLAY_OFF; // Comando para apagar o display
  xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
  Serial.println("ConselhoDeElrond: Sistema no estado DORMANT.");

  for (;;) {
    // Espera por um evento da Task_Palantir (bloqueia indefinidamente até receber um evento)
    if (xQueueReceive(xEventQueue, &receivedEvent, portMAX_DELAY) == pdPASS) {
      // Se um timeout de enigma ocorrer, independentemente do estado, vá para STATE_FAILURE
      if (receivedEvent.type == EventData_t::EVENT_ENIGMA_TIMEOUT) {
        Serial.println("ConselhoDeElrond: Recebeu evento de TIMEOUT do enigma. Forçando FALHA.");
        xTimerStop(xPotentiometerTimer, 0); // Garante que o timer do potenciometro seja parado
        currentSystemState = STATE_FAILURE;
        // Não há break aqui, o switch case abaixo lidará com STATE_FAILURE
      }

      switch (currentSystemState) {
        case STATE_DORMANT:
          if (receivedEvent.type == EventData_t::EVENT_VIBRATION_DETECTED) {
            Serial.println("Vibracao detectada no estado DORMANT. Avancando para Enigma 1 (Vibracao).");
            currentSystemState = STATE_ENIGMA_1_VIBRATION; // AGORA AVANÇA PARA O ENIGMA 1
            digitalWrite(LED_PIN, HIGH); // Acende o LED geral do sistema

            // Envia comandos para o display (Mensagem do Enigma 1)
            displayCommand.type = DisplayCommand_t::CMD_DISPLAY_CLEAR;
            xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
            /*
            displayCommand.type = DisplayCommand_t::CMD_DISPLAY_TEXT;
            strcpy(displayCommand.data.textCmd.text, "Enigma 1: Vibracao");
            xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
            */

            
            // Inicia o timer de timeout para o Enigma 1
            xTimerStart(xEnigmaTimeoutTimer, 0);

          } else if (receivedEvent.type != EventData_t::EVENT_ENIGMA_TIMEOUT) { // Ignora timeout no DORMANT
            Serial.println("Evento inesperado no estado DORMANT. Ignorando.");
          }
          break;

        case STATE_ENIGMA_1_VIBRATION: // NOVO: Enigma 1 (Vibração)
          Serial.println("No Enigma 1 (Vibracao).");
          if (receivedEvent.type == EventData_t::EVENT_VIBRATION_DETECTED) {
            Serial.println("Vibracao detectada no Enigma 1. Avancando para Enigma 2 (LDR).");
            currentSystemState = STATE_ENIGMA_2_LDR; // Avanca para o proximo estagio

            // Acende o LED do Enigma 1 (indicando que foi resolvido)
            digitalWrite(LED_ENIGMA_1_PIN, HIGH);  

            // Para o timer de timeout do enigma anterior e inicia o novo
            xTimerStop(xEnigmaTimeoutTimer, 0);
            xTimerStart(xEnigmaTimeoutTimer, 0);

            // Envia comandos para o display (Mensagem do Enigma 2 LDR)
            displayCommand.type = DisplayCommand_t::CMD_DISPLAY_CLEAR;
            xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);

            displayCommand.type = DisplayCommand_t::CMD_DISPLAY_TEXT;
            strcpy(displayCommand.data.textCmd.text, "Luz de Earendil");
            xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
            
            displayCommand.type = DisplayCommand_t::CMD_DISPLAY_LDR_ENIGMA; // Exibe a barra de luz
            displayCommand.data.ldrEnigmaCmd.ldrCurrentValue = analogRead(LDR_PIN); // Leitura inicial
            xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);

            // Toca melodia de sucesso para o Enigma 1
            actuatorCommand.type = ActuatorCommand_t::CMD_PLAY_SOUND;
            actuatorCommand.data.soundCmd.frequency = 1000; // Tom alto
            actuatorCommand.data.soundCmd.duration = 150;
            xQueueSend(xActuatorCommandQueue, &actuatorCommand, portMAX_DELAY);
            vTaskDelay(pdMS_TO_TICKS(150));
            actuatorCommand.data.soundCmd.frequency = 1200; // Tom ainda mais alto
            xQueueSend(xActuatorCommandQueue, &actuatorCommand, portMAX_DELAY);

          } else if (receivedEvent.type != EventData_t::EVENT_ENIGMA_TIMEOUT) {
            Serial.println("Evento inesperado no Enigma 1. Resetando para DORMANT.");
            currentSystemState = STATE_FAILURE; // Aciona falha
          }
          break;

        case STATE_ENIGMA_2_LDR: // Enigma 2 (LDR)
            if (receivedEvent.type == EventData_t::EVENT_LDR_READING) {
                int ldrValue = receivedEvent.data.ldrValue;

                // REMOVIDO: xTimerReset(xEnigmaTimeoutTimer, 0);
                // O timer de timeout para o LDR agora só será parado/reiniciado ao resolver o enigma.

                // Envia o valor atual do LDR para o display
                displayCommand.type = DisplayCommand_t::CMD_DISPLAY_LDR_ENIGMA;
                displayCommand.data.ldrEnigmaCmd.ldrCurrentValue = ldrValue;
                xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);

                // Verifica se o LDR esta suficientemente iluminado
                if (ldrValue >= LDR_THRESHOLD) { // Se o LDR receber luz suficiente
                    Serial.println("LDR iluminado! Avancando para Enigma 3 (Teclado).");
                    currentSystemState = STATE_ENIGMA_3_KEYPAD; // Avanca para o proximo estagio

                    // Para o timer de timeout do enigma anterior e inicia o novo
                    xTimerStop(xEnigmaTimeoutTimer, 0);
                    xTimerStart(xEnigmaTimeoutTimer, 0);

                    // Acende o LED do Enigma 2
                    digitalWrite(LED_ENIGMA_2_PIN, HIGH);

                    // Feedback de sucesso no display e som
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_CLEAR;
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_TEXT;
                    strcpy(displayCommand.data.textCmd.text, "As Portas de Durin,"); // Mensagem do teclado
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_TEXT; // Segunda linha
                    strcpy(displayCommand.data.textCmd.text, "Senhor de Moria.");
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_TEXT; // Terceira linha
                    strcpy(displayCommand.data.textCmd.text, "Fala, amigo, e entra.");
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
                    // Envia o campo de entrada inicial (______) para o display
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_KEYPAD_INPUT;
                    keypadInputIndex = 0; // Reinicia o buffer do teclado
                    memset(keypadInputBuffer, '_', 6);
                    keypadInputBuffer[6] = '\0';
                    strcpy(displayCommand.data.keypadInputCmd.input, keypadInputBuffer);
                    displayCommand.data.keypadInputCmd.currentLength = keypadInputIndex;
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);

                    actuatorCommand.type = ActuatorCommand_t::CMD_PLAY_SOUND;
                    actuatorCommand.data.soundCmd.frequency = 1600; // Som de sucesso LDR
                    actuatorCommand.data.soundCmd.duration = 300;
                    xQueueSend(xActuatorCommandQueue, &actuatorCommand, portMAX_DELAY);
                    vTaskDelay(pdMS_TO_TICKS(500)); // Pequeno delay para a mensagem e som

                }
            } else if (receivedEvent.type != EventData_t::EVENT_ENIGMA_TIMEOUT) {
                Serial.println("Evento inesperado no Estagio 2. Resetando para DORMANT.");
                currentSystemState = STATE_FAILURE; // Aciona falha
            }
            break;

        case STATE_ENIGMA_3_KEYPAD: // Enigma 3 (Teclado)
          Serial.println("No Estagio 3 (Teclado).");
          if (receivedEvent.type == EventData_t::EVENT_KEYPAD_CHAR) {
              char key = receivedEvent.data.keyChar;
              Serial.print("Teclado digitado: ");
              Serial.println(key);

              // Reinicia o timer de timeout a cada tecla pressionada (para dar mais tempo)
              xTimerReset(xEnigmaTimeoutTimer, 0);

              if (key == '#') { // Tecla '#' pressionada, tentar validar a senha
                keypadInputBuffer[keypadInputIndex] = '\0'; // Finaliza a string
                Serial.print("Senha digitada: ");
                Serial.println(keypadInputBuffer);

                if (strcmp(keypadInputBuffer, MELLON_PASSWORD) == 0) {
                    Serial.println("Senha CORRETA! Avancando para Enigma 4 (Potenciometro).");
                    currentSystemState = STATE_ENIGMA_4_POTENTIOMETER; // Avanca para o proximo estagio

                    // Para o timer de timeout do enigma anterior e inicia o novo
                    xTimerStop(xEnigmaTimeoutTimer, 0);
                    xTimerStart(xEnigmaTimeoutTimer, 0);

                    // Acende o LED do Enigma 3
                    digitalWrite(LED_ENIGMA_3_PIN, HIGH);

                    // Feedback de sucesso no display e som
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_CLEAR;
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_TEXT;
                    strcpy(displayCommand.data.textCmd.text, "A Balanca de Durin"); // Mensagem do potenciometro
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
                    
                    // Envia o estado inicial do potenciometro para o display
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_POTENTIOMETER_GAUGE;
                    displayCommand.data.potentiometerGaugeCmd.currentValue = analogRead(POTENTIOMETER_PIN); // Leitura inicial
                    displayCommand.data.potentiometerGaugeCmd.targetMin = POT_TARGET_MIN;
                    displayCommand.data.potentiometerGaugeCmd.targetMax = POT_TARGET_MAX;
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);

                    actuatorCommand.type = ActuatorCommand_t::CMD_PLAY_SOUND;
                    actuatorCommand.data.soundCmd.frequency = 1500; // Som de sucesso
                    actuatorCommand.data.soundCmd.duration = 300;
                    xQueueSend(xActuatorCommandQueue, &actuatorCommand, portMAX_DELAY);
                    vTaskDelay(pdMS_TO_TICKS(500)); // Pequeno delay para a mensagem e som
                    
                } else {
                    Serial.println("Senha INCORRETA! Resetando para DORMANT.");
                    currentSystemState = STATE_FAILURE; // Aciona falha
                }
                // Limpa o buffer para a proxima tentativa ou novo enigma
                keypadInputIndex = 0;
                memset(keypadInputBuffer, '_', 6);
                keypadInputBuffer[6] = '\0';

              } else if (key == '*') { // Tecla '*' pressionada, apagar o ultimo caractere
                  if (keypadInputIndex > 0) {
                      keypadInputIndex--; // Decrementa o índice
                      keypadInputBuffer[keypadInputIndex] = '_'; // Substitui o caractere por '_'
                      keypadInputBuffer[keypadInputIndex + 1] = '\0'; // Garante o terminador nulo

                      // Atualiza o display com a entrada atualizada
                      displayCommand.type = DisplayCommand_t::CMD_DISPLAY_KEYPAD_INPUT;
                      strcpy(displayCommand.data.keypadInputCmd.input, keypadInputBuffer);
                      displayCommand.data.keypadInputCmd.currentLength = keypadInputIndex;
                      xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
                      Serial.println("Caractere apagado.");
                  } else {
                      Serial.println("Buffer vazio, nada para apagar.");
                  }
              } else if (keypadInputIndex < 6) { // Adiciona o digito ao buffer, se houver espaco
                  keypadInputBuffer[keypadInputIndex++] = key;
                  keypadInputBuffer[keypadInputIndex] = '\0'; // Mantem o buffer como string valida

                  // Atualiza o display com a entrada atual
                  displayCommand.type = DisplayCommand_t::CMD_DISPLAY_KEYPAD_INPUT;
                  strcpy(displayCommand.data.keypadInputCmd.input, keypadInputBuffer);
                  displayCommand.data.keypadInputCmd.currentLength = keypadInputIndex;
                  xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
              } else {
                  Serial.println("Buffer do teclado cheio. Pressione # para confirmar ou aguarde o reset.");
              }
          } else if (receivedEvent.type != EventData_t::EVENT_ENIGMA_TIMEOUT) {
              Serial.println("Evento inesperado no Estagio 3. Resetando para DORMANT.");
              currentSystemState = STATE_FAILURE; // Aciona falha
          }
          break;

        case STATE_ENIGMA_4_POTENTIOMETER: // Enigma 4 (Potenciometro)
            Serial.println("No Estagio 4 (Potenciometro).");
            if (receivedEvent.type == EventData_t::EVENT_POTENTIOMETER_READING) {
                int potValue = receivedEvent.data.potentiometerValue;

                // Reinicia o timer de timeout do enigma a cada leitura do potenciometro
                xTimerReset(xEnigmaTimeoutTimer, 0);

                if (potValue == -1) { // Sinal de que o timer do potenciometro expirou (sucesso)
                    Serial.println("Potenciometro: Tempo na posicao correta! Cofre Aberto!");
                    currentSystemState = STATE_SUCCESS; // Avanca para o estado de sucesso final

                    // Para o timer de timeout do enigma
                    xTimerStop(xEnigmaTimeoutTimer, 0);

                    // Acende o LED do Enigma 4
                    digitalWrite(LED_ENIGMA_4_PIN, HIGH);

                    // Feedback de sucesso final no display e som
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_CLEAR;
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_TEXT;
                    strcpy(displayCommand.data.textCmd.text, "Cofre Aberto!");
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);

                    actuatorCommand.type = ActuatorCommand_t::CMD_PLAY_SOUND;
                    actuatorCommand.data.soundCmd.frequency = 2500; // Som de sucesso final
                    actuatorCommand.data.soundCmd.duration = 1000;
                    xQueueSend(xActuatorCommandQueue, &actuatorCommand, portMAX_DELAY);
                    
                    actuatorCommand.type = ActuatorCommand_t::CMD_MOVE_SERVO; // Abre o cofre
                    actuatorCommand.data.servoCmd.angle = 90; // Angulo para abrir
                    xQueueSend(xActuatorCommandQueue, &actuatorCommand, portMAX_DELAY);
                    vTaskDelay(pdMS_TO_TICKS(2000)); // Espera a animacao do servo
                    
                    // Reseta o sistema apos o sucesso
                    currentSystemState = STATE_FAILURE; // Aciona falha para resetar o sistema
                } else { // Leitura normal do potenciometro
                    // Envia o valor atual para o display
                    displayCommand.type = DisplayCommand_t::CMD_DISPLAY_POTENTIOMETER_GAUGE;
                    displayCommand.data.potentiometerGaugeCmd.currentValue = potValue;
                    displayCommand.data.potentiometerGaugeCmd.targetMin = POT_TARGET_MIN;
                    displayCommand.data.potentiometerGaugeCmd.targetMax = POT_TARGET_MAX;
                    xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);

                    // Verifica se o potenciometro esta na faixa alvo
                    if (potValue >= POT_TARGET_MIN && potValue <= POT_TARGET_MAX) {
                        if (!potentiometerInTarget) { // Entrou na faixa alvo
                            potentiometerInTarget = true;
                            Serial.println("Potenciometro: ENTROU na faixa alvo. Iniciando timer...");
                            xTimerStart(xPotentiometerTimer, 0); // Inicia o timer
                        }
                    } else { // Fora da faixa alvo
                        if (potentiometerInTarget) { // Saiu da faixa alvo
                            potentiometerInTarget = false;
                            Serial.println("Potenciometro: SAIU da faixa alvo. Parando/Resetando timer...");
                            xTimerStop(xPotentiometerTimer, 0); // Para o timer
                        }
                    }
                }
            } else if (receivedEvent.type != EventData_t::EVENT_ENIGMA_TIMEOUT) {
                Serial.println("Evento inesperado no Estagio 4. Resetando para DORMANT.");
                // Se um evento nao relacionado ao potenciometro for recebido, falha.
                currentSystemState = STATE_FAILURE;
            }
            break;
            
        case STATE_FAILURE:
          Serial.println("ConselhoDeElrond: Entrou em estado de FALHA.");
          // Para o timer de timeout do enigma, se estiver ativo
          xTimerStop(xEnigmaTimeoutTimer, 0);
          xTimerStop(xPotentiometerTimer, 0); // Garante que o timer do potenciometro seja parado

          // Toca melodia de falha
          actuatorCommand.type = ActuatorCommand_t::CMD_PLAY_SOUND;
          actuatorCommand.data.soundCmd.frequency = 200; // Tom grave
          actuatorCommand.data.soundCmd.duration = 500;
          xQueueSend(xActuatorCommandQueue, &actuatorCommand, portMAX_DELAY);
          vTaskDelay(pdMS_TO_TICKS(500)); // Espera a melodia tocar

          // Reseta para o estado DORMANT
          currentSystemState = STATE_DORMANT;
          digitalWrite(LED_PIN, LOW); // Apaga o LED geral do sistema
          // Apaga todos os LEDs dos enigmas
          digitalWrite(LED_ENIGMA_1_PIN, LOW);
          digitalWrite(LED_ENIGMA_2_PIN, LOW);
          digitalWrite(LED_ENIGMA_3_PIN, LOW);
          digitalWrite(LED_ENIGMA_4_PIN, LOW);

          displayCommand.type = DisplayCommand_t::CMD_DISPLAY_OFF; // Apaga o display novamente
          xQueueSend(xDisplayCommandQueue, &displayCommand, portMAX_DELAY);
          Serial.println("ConselhoDeElrond: Retornando ao estado DORMANT.");
          break;

        case STATE_SUCCESS:
            // Este estado é transitório e deve levar a STATE_FAILURE para resetar
            Serial.println("ConselhoDeElrond: Entrou em estado de SUCESSO. Resetando para DORMANT.");
            // Para o timer de timeout do enigma, se estiver ativo
            xTimerStop(xEnigmaTimeoutTimer, 0);
            xTimerStop(xPotentiometerTimer, 0); // Garante que o timer do potenciometro seja parado
            currentSystemState = STATE_FAILURE;
            break;

        default:
          Serial.println("ConselhoDeElrond: Estado desconhecido ou nao tratado. Resetando.");
          currentSystemState = STATE_FAILURE; // Retorna para falha se o estado for inválido
          break;
      }
    }
  }
}

// Tarefa: Task_Palantir
// Responsabilidade Principal: Monitorar todos os periféricos de entrada.
void Task_Palantir(void *pvParameters) {
  (void) pvParameters;
  pinMode(VIBRATION_SENSOR_PIN, INPUT_PULLUP); // Configura o pino do sensor de vibração

  // Permite que o sensor estabilize e inicializa previousVibrationState com a leitura real.
  vTaskDelay(pdMS_TO_TICKS(100)); // Pequeno atraso para o sensor estabilizar após o boot
  int previousVibrationState = digitalRead(VIBRATION_SENSOR_PIN); // Inicializa com o estado atual do sensor

  for (;;) {
    // --- Monitoramento do Sensor de Vibração ---
    // Somente envia evento de vibração se o sistema estiver no estado DORMANT ou ENIGMA_1_VIBRATION
    if (currentSystemState == STATE_DORMANT || currentSystemState == STATE_ENIGMA_1_VIBRATION) {
      int currentVibrationState = digitalRead(VIBRATION_SENSOR_PIN);
      if (currentVibrationState == LOW && previousVibrationState == HIGH) {
        EventData_t event;
        event.type = EventData_t::EVENT_VIBRATION_DETECTED;
        if (xQueueSend(xEventQueue, &event, pdMS_TO_TICKS(10)) != pdPASS) {
          Serial.println("Palantir: Erro ao enviar evento de vibracao para a fila.");
        }  
        vTaskDelay(pdMS_TO_TICKS(500)); // Pequeno atraso para evitar múltiplas detecções de uma única batida
      }
      previousVibrationState = currentVibrationState;
    } else {
      // Se não estiver no estado DORMANT ou ENIGMA_1_VIBRATION, reseta o estado da vibração para evitar falsos positivos
      previousVibrationState = HIGH;  
    }

    // --- Monitoramento do LDR (Enigma 2) ---
    if (currentSystemState == STATE_ENIGMA_2_LDR) { // Somente monitora o LDR no estado Enigma 2
        int ldrValue = analogRead(LDR_PIN);
        EventData_t event;
        event.type = EventData_t::EVENT_LDR_READING;
        event.data.ldrValue = ldrValue;
        xQueueSend(xEventQueue, &event, 0);
    }

    // --- Monitoramento do Teclado Matricial (Enigma 3) ---
    if (currentSystemState == STATE_ENIGMA_3_KEYPAD) { // Somente monitora o teclado no estado Enigma 3
      char key = customKeypad.getKey(); // Obtém a tecla pressionada

      if (key != NO_KEY) { // Se uma tecla foi pressionada
        EventData_t event;
        event.type = EventData_t::EVENT_KEYPAD_CHAR;
        event.data.keyChar = key;
        if (xQueueSend(xEventQueue, &event, pdMS_TO_TICKS(10)) != pdPASS) {
          Serial.println("Palantir: Erro ao enviar evento de teclado para a fila.");
        }  
      }
    }

    // --- Monitoramento do Potenciômetro (Enigma 4) ---
    if (currentSystemState == STATE_ENIGMA_4_POTENTIOMETER) { // Somente monitora o potenciometro no estado Enigma 4
      int potValue = analogRead(POTENTIOMETER_PIN);
      EventData_t event;
      event.type = EventData_t::EVENT_POTENTIOMETER_READING;
      event.data.potentiometerValue = potValue;
      xQueueSend(xEventQueue, &event, 0);  
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // Cede o tempo para outras tarefas a cada 50ms
  }
}

// Tarefa: Task_EspelhoDeGaladriel
// Responsabilidade Principal: Controlar exclusivamente o display OLED.
void Task_EspelhoDeGaladriel(void *pvParameters) {
  (void) pvParameters;
  DisplayCommand_t receivedCommand;

  for (;;) {
    if (xQueueReceive(xDisplayCommandQueue, &receivedCommand, portMAX_DELAY) == pdPASS) {
      display.clearDisplay(); // Limpa o buffer antes de desenhar qualquer coisa nova

      switch (receivedCommand.type) {
        case DisplayCommand_t::CMD_DISPLAY_CLEAR:
          display.display();
          Serial.println("EspelhoDeGaladriel: Display limpo.");
          break;
        case DisplayCommand_t::CMD_DISPLAY_TEXT:
          display.ssd1306_command(SSD1306_DISPLAYON); // LIGA o display fisicamente
          display.setTextSize(1);
          display.setTextColor(SSD1306_WHITE);
          display.setCursor(0, 0);
          display.println(receivedCommand.data.textCmd.text);
          display.display();
          Serial.print("EspelhoDeGaladriel: Exibindo: ");
          Serial.println(receivedCommand.data.textCmd.text);
          break;
        case DisplayCommand_t::CMD_DISPLAY_KEYPAD_INPUT:
          display.ssd1306_command(SSD1306_DISPLAYON); // LIGA o display fisicamente
          display.setTextSize(1);
          display.setTextColor(SSD1306_WHITE);
          display.setCursor(0, 0); // Posição para a mensagem principal (Fale 'Amigo')
          display.println("As Portas de Durin,"); // Mensagem do teclado
          display.setCursor(0, 10);
          display.println("Senhor de Moria.");
          display.setCursor(0, 20);
          display.println("Fala, amigo, e entra.");

          // Posição para o campo de entrada da senha
          display.setCursor(0, 40); // Ajuste a posição Y conforme necessário
          
          char displayBuffer[15]; // Buffer para formatar a string de exibição
          // Copia a parte digitada e preenche o resto com '_'
          for (int i = 0; i < 6; i++) {
              if (i < receivedCommand.data.keypadInputCmd.currentLength) {
                  displayBuffer[i] = receivedCommand.data.keypadInputCmd.input[i];
              } else {
                  displayBuffer[i] = '_';
              }
          }
          displayBuffer[6] = '\0'; // Finaliza a string

          display.print(displayBuffer); // Exibe o campo de entrada

          display.display();
          Serial.print("EspelhoDeGaladriel: Campo de entrada: ");
          Serial.println(displayBuffer);
          break;
        case DisplayCommand_t::CMD_DISPLAY_POTENTIOMETER_GAUGE:
          display.ssd1306_command(SSD1306_DISPLAYON); // LIGA o display fisicamente
          display.setTextSize(1);
          display.setTextColor(SSD1306_WHITE);
          display.setCursor(0, 0);
          display.println("A Balanca de Durin"); // Mensagem do potenciometro

          // Desenha a barra do potenciometro
          int barWidth = SCREEN_WIDTH - 20; // Largura da barra, com margens
          int barHeight = 10;
          int barX = 10;
          int barY = 30; // Posicao Y da barra

          // Desenha o contorno da barra
          display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);

          // Calcula a posicao do marcador (valor atual) na barra
          // Mapeia o valor do potenciometro (0-1023) para a largura da barra
          int markerX = map(receivedCommand.data.potentiometerGaugeCmd.currentValue, 0, 1023, barX, barX + barWidth - 2); // -2 para o marcador nao sair do limite
          display.fillRect(markerX, barY + 1, 3, barHeight - 2, SSD1306_WHITE); // Desenha o marcador

          // Desenha a faixa alvo (zona de equilibrio)
          int targetMinX = map(receivedCommand.data.potentiometerGaugeCmd.targetMin, 0, 1023, barX, barX + barWidth);
          int targetMaxX = map(receivedCommand.data.potentiometerGaugeCmd.targetMax, 0, 1023, barX, barX + barWidth);
          display.drawRect(targetMinX, barY - 2, (targetMaxX - targetMinX), barHeight + 4, SSD1306_WHITE); // Desenha um retangulo ao redor da faixa alvo

          display.display();
          Serial.print("EspelhoDeGaladriel: Medidor Potenciometro: ");
          Serial.println(receivedCommand.data.potentiometerGaugeCmd.currentValue);
          break;
        case DisplayCommand_t::CMD_DISPLAY_LDR_ENIGMA: // CASE para o LDR
          display.ssd1306_command(SSD1306_DISPLAYON); // LIGA o display fisicamente
          display.setTextSize(1);
          display.setTextColor(SSD1306_WHITE);
          display.setCursor(0, 0);
          display.println("Luz de Earendil");
          display.setCursor(0, 16);
          display.println("Ilumine o sensor!");

          // Desenha uma barra que se enche com a luz
          int ldrBarWidth = map(receivedCommand.data.ldrEnigmaCmd.ldrCurrentValue, 0, 1023, 0, SCREEN_WIDTH - 20);
          display.drawRect(10, 40, SCREEN_WIDTH - 20, 10, SSD1306_WHITE); // Contorno da barra
          display.fillRect(10, 40, ldrBarWidth, 10, SSD1306_WHITE); // Preenchimento da barra

          display.display();
          Serial.print("EspelhoDeGaladriel: Medidor LDR: ");
          Serial.println(receivedCommand.data.ldrEnigmaCmd.ldrCurrentValue);
          break;
        case DisplayCommand_t::CMD_DISPLAY_OFF:
          display.clearDisplay();
          display.display(); // Garante que o display esteja apagado
          display.ssd1306_command(SSD1306_DISPLAYOFF); // Desliga o display fisicamente
          Serial.println("EspelhoDeGaladriel: Display desligado.");
          break;
        default:
          Serial.println("EspelhoDeGaladriel: Comando de display desconhecido.");
          break;
      }
    }
  }
}

// Tarefa: Task_VozDeSaruman
// Responsabilidade Principal: Controlar exclusivamente os atuadores (Servo Motor e Buzzer Passivo).
void Task_VozDeSaruman(void *pvParameters) {
  (void) pvParameters;
  ActuatorCommand_t receivedCommand;

  for (;;) {
    if (xQueueReceive(xActuatorCommandQueue, &receivedCommand, portMAX_DELAY) == pdPASS) {
      switch (receivedCommand.type) {
        case ActuatorCommand_t::CMD_PLAY_SOUND:
          tone(BUZZER_PIN, receivedCommand.data.soundCmd.frequency, receivedCommand.data.soundCmd.duration);
          Serial.print("VozDeSaruman: Tocando som: Freq=");
          Serial.print(receivedCommand.data.soundCmd.frequency);
          Serial.print("Hz, Duracao=");
          Serial.print(receivedCommand.data.soundCmd.duration);
          Serial.println("ms");
          break;
        case ActuatorCommand_t::CMD_STOP_SOUND:
          noTone(BUZZER_PIN);
          Serial.println("VozDeSaruman: Parando som.");
          break;
        case ActuatorCommand_t::CMD_MOVE_SERVO:
          meuServo.write(receivedCommand.data.servoCmd.angle);
          Serial.print("VozDeSaruman: Movendo servo para ");
          Serial.print(receivedCommand.data.servoCmd.angle);
          Serial.println(" graus.");
          break;
        default:
          Serial.println("VozDeSaruman: Comando de atuador desconhecido.");
          break;
      }
    }
  }
}
