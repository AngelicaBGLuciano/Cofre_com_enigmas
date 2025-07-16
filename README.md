# Cofre com Enigmas - Projeto de Sistema Embarcado com FreeRTOS

![Badge do Projeto](https://img.shields.io/badge/Projeto-Conclu%C3%ADdo-brightgreen)
![Badge da Linguagem](https://img.shields.io/badge/Linguagem-C%2B%2B%20(Arduino)-blue)
![Badge do RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-orange)

## 📖 Sobre o Projeto

Este projeto é um cofre interativo desenvolvido como trabalho para a disciplina de Sistemas Embarcados. O sistema, controlado por um Arduino Mega, utiliza o sistema operacional de tempo real **FreeRTOS** para gerenciar múltiplas tarefas concorrentes. O objetivo é resolver uma sequência de quatro enigmas temáticos para acionar a abertura do cofre, demonstrando conceitos de escalonamento, comunicação entre tarefas (filas) e gerenciamento de estado.

---

## 🎬 Demonstração

Veja o cofre em ação! O GIF abaixo demonstra a sequência de enigmas, o feedback no display OLED e a abertura final com o servo motor.

![Demo do projeto](https://drive.google.com/file/d/13VjAMwlGti3mm-U31Ctjicc1TJSl3S9S/view?usp=sharing)


---

## ✨ Funcionalidades

- **Sistema de Enigmas Sequenciais:** Quatro desafios que devem ser resolvidos em ordem.
- **Feedback em Tempo Real:** O usuário é guiado por um display OLED, LEDs de status e um buzzer.
- **Arquitetura Multitarefa:** Utiliza FreeRTOS para gerenciar 4 tarefas principais de forma concorrente e organizada.
- **Comunicação Segura entre Tarefas:** Uso de Filas (Queues) para enviar dados entre a lógica de controle, sensores e atuadores.
- **Mecanismo de Timeout:** Cada enigma possui um tempo limite, adicionando um nível de desafio.
- **Acionamento Físico:** Um servo motor simula a abertura da trava do cofre ao final dos desafios.

---

## 🛠️ Hardware e Software

### Componentes de Hardware

| Componente                  | Quantidade |
| --------------------------- | :--------: |
| Arduino Mega 2560           |     1      |
| Display OLED I2C 128x64     |     1      |
| Servo Motor SG90            |     1      |
| Buzzer                      |     1      |
| LEDs (5mm, cores diversas)  |     5      |
| Teclado Matricial 4x3       |     1      |
| Sensor de Vibração SW-420   |     1      |
| Potenciômetro 10kΩ          |     1      |
| Resistor LDR                |     1      |
| Resistores (variados)       |    ~5      |
| Protoboard e Jumpers        |     -      |

### Software e Bibliotecas

- **Ambiente:** Arduino IDE
- **Linguagem:** C++ (padrão Arduino)
- **RTOS:** FreeRTOS (via biblioteca `Arduino_FreeRTOS.h`)
- **Bibliotecas Adicionais:**
  - `Adafruit_GFX.h`
  - `Adafruit_SSD1306.h`
  - `Keypad.h`
  - `Servo.h`

---

## ⚙️ Montagem e Instalação

### Instalação do Firmware

1.  **Clone o repositório:**
    ```bash
    git clone [https://github.com/seu-usuario/seu-repositorio.git](https://github.com/seu-usuario/seu-repositorio.git)
    ```
2.  **Abra na IDE do Arduino:** Abra o arquivo `.ino` na sua Arduino IDE.
3.  **Instale as bibliotecas:** Vá em `Ferramentas > Gerenciar Bibliotecas...` e instale as cinco bibliotecas listadas na seção de software.
4.  **Conecte o Arduino:** Conecte seu Arduino Mega ao computador.
5.  **Carregue o código:** Selecione a placa "Arduino Mega or Mega 2560" e a porta COM correta, e clique em "Carregar".

---

## 🏛️ Arquitetura do Software

O sistema é orquestrado pelo FreeRTOS e dividido em quatro tarefas principais que se comunicam através de filas, garantindo desacoplamento e organização.

```mermaid
graph TD
    subgraph Hardware
        A[Sensores]
        B[Atuadores]
    end

    subgraph "FreeRTOS Tasks"
        C(Task_Palantir) -- Lê --> A
        C -- "xEventQueue" --> D(Task_ConselhoDeElrond)
        D -- "xDisplayCommandQueue" --> E(Task_EspelhoDeGaladriel)
        D -- "xActuatorCommandQueue" --> F(Task_VozDeSaruman)
        E -- Controla --> B1[Display OLED]
        F -- Controla --> B2[Buzzer & Servo]
    end
