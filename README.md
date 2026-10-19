# VidiWaze

Sistema de **geolocalização indoor por Wi-Fi fingerprinting** com ESP32 e Machine Learning, pensado para auxiliar a navegação de **pessoas com deficiência visual** em ambientes internos. Projeto de Iniciação Científica no [Inatel](https://inatel.br).

> **Status:** em desenvolvimento. A coleta de dados e o treino dos modelos estão funcionando; o teste em tempo real (firmware de inferência + servidor) está em implementação.

---

## Motivação

O GPS não funciona bem dentro de edifícios. Já os pontos de acesso Wi-Fi estão presentes em praticamente todo ambiente interno, e a intensidade do sinal recebido (RSSI) de cada AP varia de forma característica conforme a posição. Esse padrão é a *impressão digital* (fingerprint) do lugar, e um modelo de ML consegue aprender a reconhecê-la.

O VidiWaze usa essa ideia para estimar em qual ponto de um corredor o usuário está, usando apenas um microcontrolador de baixo custo e a infraestrutura Wi-Fi que já existe no prédio.

## Como funciona

```
┌──────────── Modo coleta / treino ────────────┐      ┌──────── Modo inferência ────────┐
│                                              │      │                                 │
│  ESP32 ──► Google Sheets ──► Google Colab    │      │  ESP32 ──► Servidor Flask       │
│  (scan +    (armazenamento)  (limpeza, pivot,│ ───► │  (scan)    (modelo + scaler)    │
│   rótulo)                     treino)        │modelo│     ▲            │              │
│                                              │ .pkl │     └── posição ─┘              │
└──────────────────────────────────────────────┘      └─────────────────────────────────┘
```

1. **Coleta:** o ESP32 escaneia os APs da rede `WLL-Inatel` e envia, para cada AP visto, o par *BSSID + RSSI* com o rótulo do ponto (ex.: `eHealth_p01`) para uma planilha do Google Sheets.
2. **Preparação:** no Google Colab, os dados são limpos e transformados (pivot) em um vetor por amostra, com um RSSI por BSSID. APs não vistos no scan recebem o valor **-100 dBm**. Os valores são normalizados com Min-Max.
3. **Treino:** KNN e Random Forest são treinados e comparados (divisão 70/30 e validação cruzada de 5 folds).
4. **Inferência:** um firmware separado escaneia, envia o vetor para um servidor Flask, que aplica o mesmo pré-processamento do treino, roda o modelo e devolve a posição estimada e a confiança.

## Hardware

| Componente | Função |
|---|---|
| ESP32 DevKit (WROOM-32U) | Scan Wi-Fi e envio dos dados |
| Módulo TP4056 (com proteção) | Carga da bateria via USB |
| Módulo step-up MT3608 | Eleva 3,7 V da LiPo para 5 V (pino VIN) |
| Bateria LiPo 3,7 V (≥ 1000 mAh) | Alimentação portátil |
| Capacitor 470 µF | Absorve picos de corrente do rádio Wi-Fi (evita *brownout*) |
| Botões (2) + LCD 16×2 I²C | Seleção do ponto de coleta e exibição de status |

O esquemático em nível de módulos está em [`docs/esquematico_vidiwaze.pdf`](docs/esquematico_vidiwaze.pdf).

## Dados e pipeline

Cada linha enviada ao Sheets pelo firmware de coleta tem:

```
label, ssid, bssid, rssi_dbm, [snr_db], sample_id
```

Todos os APs do SSID alvo vistos em um mesmo scan compartilham o mesmo `sample_id`. O pivot transforma isso em:

```
label        | rssi_<BSSID_1> | rssi_<BSSID_2> | ... | rssi_<BSSID_N>
eHealth_p01  |      -74       |      -81       | ... |     -100
```

As colunas de SNR não entram no treino. Veja [Limitações](#limitações).

### Conjuntos coletados

| | ESP8266 | ESP32 |
|---|---|---|
| Linhas brutas | 7.513 | 24.778 |
| Amostras (scans) | 1.570 | 2.638 |
| Pontos no corredor | 3 | 5 |
| BSSIDs únicos | 13 | 22 |
| APs por amostra (média) | 4,78 | 9,39 |

## Resultados

Dados coletados com o dispositivo **parado** em cada ponto, no corredor da sala de Iniciação Científica do Inatel.

| Modelo | Métrica | ESP8266 (3 pontos) | ESP32 (5 pontos) |
|---|---|---|---|
| KNN (k=7) | Acurácia (teste 30%) | 98,51% | 96,97% |
| KNN (k=7) | CV 5-fold (média ± desvio) | 98,60% ± 1,59 | 95,30% ± 3,70 |
| Random Forest | Acurácia (teste 30%) | 100,00% | 99,87% |
| Random Forest | CV 5-fold (média ± desvio) | 99,55% ± 0,74 | 99,92% ± 0,15 |

O Random Forest foi o modelo mais estável. As confusões do KNN ocorrem principalmente entre pontos vizinhos.

## Limitações

Estes resultados devem ser lidos com cautela:

- **Poucos pontos, bem separados.** Com 3 a 5 pontos, a classificação é um problema fácil. O teste real é com mais pontos e espaçamento de 1 a 2 m.
- **Divisão aleatória das amostras.** Amostras da mesma sessão de coleta caem em treino e teste, o que tende a inflar a acurácia. Uma avaliação mais honesta usa sessões ou dias diferentes para o teste.
- **Dispositivo parado.** Com o usuário andando, o RSSI varia mais. O desempenho em movimento ainda não foi medido.
- **Uma única condição ambiental.** Não foram coletados dados com corredor cheio/vazio ou em horários diferentes.
- **Noise floor e SNR.** O piso de ruído não é medido de forma confiável no ESP32 (a função de ROM `rom_check_noise_floor` não é documentada e retornou valor praticamente constante), e no ESP8266 ele foi assumido como -95 dBm fixo. Por isso o SNR calculado é só o RSSI deslocado por uma constante e **não é usado como feature**. O RSSI por BSSID é a feature principal.

## Roadmap

- [ ] Firmware de inferência (scan → POST → posição) e servidor Flask
- [ ] Coleta em mais pontos, com espaçamento de 1 a 2 m e em condições variadas
- [ ] Avaliação com sessões/dias separados e com o usuário em movimento
- [ ] Métrica de erro de localização em metros, além da acurácia de classificação
- [ ] Comparação com SVM e Decision Tree
- [ ] Suavização temporal das predições (janela deslizante)
- [ ] Saída para o usuário final por áudio ou vibração (o LCD serve só para depuração)
- [ ] Avaliar execução do modelo embarcada no ESP32 (ou em microcomputador portátil)

## Estrutura sugerida do repositório

```
vidiwaze/
├── firmware/
│   ├── coleta/          # firmware de coleta (ESP32 → Google Sheets)
│   └── inferencia/      # firmware de inferência (em desenvolvimento)
├── apps_script/         # script do Google Apps Script que recebe os dados
├── notebooks/           # pipeline no Colab (limpeza, pivot, treino)
├── servidor/            # servidor Flask de inferência (em desenvolvimento)
├── docs/                # esquemático e diagramas
└── README.md
```

## Como reproduzir

### Coleta
1. Crie uma planilha no Google Sheets e implante um Google Apps Script como *Web App* com um `doPost` que grave as linhas recebidas (`appendRow`).
2. Copie `config.example.h` para `config.h` e preencha o SSID, a senha e a URL do Web App. **Não versione esse arquivo.**
3. Grave o firmware de coleta no ESP32, defina o ponto (por botão ou comando serial `LABEL <nome>`) e confirme (`OK`) para iniciar a gravação.

### Treino
1. Abra o notebook no Google Colab e autorize o acesso à planilha (`gspread`).
2. Execute as células na ordem: carga → limpeza → pivot → normalização → treino/avaliação.

## Referência

Attar, H. et al. *Machine learning in indoor localization prediction using Received Signal Strength Indicator and Wi-Fi network.* International Journal of Intelligent Networks, v. 6, p. 233–243, 2025. DOI: [10.1016/j.ijin.2025.10.001](https://doi.org/10.1016/j.ijin.2025.10.001)

Pré-processamento (preenchimento de APs ausentes com -100 dBm, normalização Min-Max) e modelos comparados (KNN, Random Forest) seguem a metodologia descrita nesse trabalho.

## Autor

**Ruan Rodrigues**: Engenharia de Software, [Inatel](https://inatel.br)
[LinkedIn](https://linkedin.com/in/ruan-rodrigues-rdg) · [GitHub](https://github.com/rodriguezruan)

## Licença

Defina a licença do projeto (por exemplo, MIT) antes de publicar.
