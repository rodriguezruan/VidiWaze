# VidiWaze

Sistema de **geolocalização indoor por Wi-Fi fingerprinting** com ESP32 e Machine Learning, pensado para auxiliar a navegação de **pessoas com deficiência visual** em ambientes internos. Projeto de Iniciação Científica no [Inatel](https://inatel.br).

> **Status:** em desenvolvimento.

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

A coluna de SNR não entra no treino. O piso de ruído não é medido de forma confiável pelo hardware, então o SNR calculado é só o RSSI deslocado por uma constante. A feature usada é o RSSI por BSSID.

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
