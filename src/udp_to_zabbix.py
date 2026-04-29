from flask import Flask, request, jsonify
from datetime import datetime

app = Flask(__name__)

@app.route('/sinal', methods=['POST'])
def receber_sinal():
    dados = request.get_json()
    print(f"[{datetime.now()}] Dados recebidos: {dados}")
    return jsonify({"status": "ok"}), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8082, debug=False)