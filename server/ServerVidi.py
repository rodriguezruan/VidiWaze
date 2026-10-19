"""
VidiWaze - servidor de inferencia (Flask)

Recebe o scan Wi-Fi do ESP32, monta o MESMO vetor usado no treino
e devolve a posicao estimada.

Arquivos necessarios na mesma pasta (gerados no Colab):
    modelo_rf.pkl   -> RandomForest treinado
    scaler.pkl      -> MinMaxScaler ajustado no treino
    colunas.json    -> lista de BSSIDs na ordem exata das colunas de X
"""
import json
import logging
from pathlib import Path

import joblib
import pandas as pd
from flask import Flask, jsonify, request

BASE = Path(__file__).parent
RSSI_AUSENTE = -100
MIN_APS_CONHECIDOS = 1

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
log = logging.getLogger("vidiwaze")

#carrega UMA vez, na inicializacao ────────────────────────────────────────
modelo = joblib.load(BASE / "modelo_rf.pkl")
scaler = joblib.load(BASE / "scaler.pkl")
COLUNAS = [c.upper() for c in json.loads((BASE / "colunas.json").read_text())]
log.info("Modelo carregado | %d BSSIDs | classes: %s", len(COLUNAS), list(modelo.classes_))

app = Flask(__name__)


@app.get("/health")
def health():
    """Para testar no navegador: http://IP:5000/health"""
    return jsonify({"status": "ok", "n_bssids": len(COLUNAS), "classes": list(modelo.classes_)})


@app.post("/predict")
def predict():
    dados = request.get_json(silent=True)
    if not dados or not isinstance(dados.get("bssid"), dict):
        return jsonify({"erro": 'JSON invalido. Esperado: {"bssid": {"AA:BB:...": -70}}'}), 400

    # normaliza os BSSIDs recebidos (maiusculas, como no treino)
    recebidos = {}
    for mac, rssi in dados["bssid"].items():
        try:
            recebidos[str(mac).upper()] = float(rssi)
        except (TypeError, ValueError):
            return jsonify({"erro": f"RSSI invalido para {mac}"}), 400

    conhecidos = {m: r for m, r in recebidos.items() if m in COLUNAS}
    desconhecidos = len(recebidos) - len(conhecidos)

    if len(conhecidos) < MIN_APS_CONHECIDOS:
        log.warning("Nenhum BSSID conhecido (recebidos=%d)", len(recebidos))
        return jsonify({"erro": "nenhum BSSID conhecido pelo modelo"}), 400

    vetor = {c: conhecidos.get(c, RSSI_AUSENTE) for c in COLUNAS}
    X = pd.DataFrame([vetor], columns=COLUNAS)

    X_scaled = scaler.transform(X)
    proba = modelo.predict_proba(X_scaled)[0]
    ordem = proba.argsort()[::-1]
    melhor = modelo.classes_[ordem[0]]
    confianca = round(float(proba[ordem[0]]) * 100, 2)

    log.info("-> %s (%.1f%%) | APs: %d conhecidos, %d desconhecidos",
             melhor, confianca, len(conhecidos), desconhecidos)

    return jsonify({
        "posicao": str(melhor),
        "confianca": confianca,
        "top3": [{"posicao": str(modelo.classes_[i]), "confianca": round(float(proba[i]) * 100, 2)}
                 for i in ordem[:3]],
        "aps_conhecidos": len(conhecidos),
        "aps_desconhecidos": desconhecidos,
    })


if __name__ == "__main__":
    # 0.0.0.0 = aceita conexoes de outros dispositivos da rede (o ESP32)
    app.run(host="0.0.0.0", port=5000)
