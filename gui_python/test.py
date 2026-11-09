import serial
import time
import struct
import sys
from datetime import datetime 

MARKER = b"<{DP}>"
PORT = "COM3" #"/dev/ttyUSB0"
BAUDRATE = 115200


def parse_kv_items(parts):
    """Convierte ['seq=1', 'T=23.53'] en {'seq': '1', 'T': '23.53'}"""
    data = {}
    for item in parts:
        if '=' in item:
            key, value = item.split('=', 1)
            data[key] = value
    return data

with serial.Serial(PORT, baudrate=BAUDRATE, timeout=0.1) as com:
    print(f"Escuchando en {PORT}. Enviando congifuracion de prueba...")
    time.sleep(0.5)

    # Enviamos comandos de prueba al ESP32-S3
    com.write(b"SET_ACQ,2.0,1,1,1\n")
    com.write(b"SET_AVG,1,10\n")
    com.write(b"SET_PEAK,1,24.5\n")
    com.write(b"START\n")

    buf = ""

    try:
        while True:
            # Leemos datos del puerto serie
            chunk = com.read(1024).decode("utf-8", errors="replace")
            if not chunk:
                continue
            buf += chunk

            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()

                # buscamos el marcador <{DP}> para ignorar los logs de arranque de ESP-IDF
                idx = line.find(MARKER)
                if idx < 0:
                    continue

                payload = line[idx + len(MARKER):]
                parts = payload.split(",")
                msg_type = parts[0]
                kv = parse_kv_items(parts[1:])
                ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]

                if msg_type == "SAMPLE":
                    print(f"[{ts}] [MUESTRA #{kv.get('seq')}] t={kv.get('t_ms')}ms | "
                          f"T={kv.get('T', '--')}°C | H={kv.get('H', '--')}% | P={kv.get('P', '--')}hPa | "
                          f"Prom={kv.get('temp_avg_c', '--')}°C (Completa={kv.get('win_full', '0')})")
                elif msg_type == "PEAK":
                    print(f"[{ts}] 🔥 [PICO DETECTADO] t={kv.get('t_ms')}ms | "
                          f"Valor={kv.get('peak_c')}°C (Umbral={kv.get('thr_c')}°C)")
                elif msg_type == "ACK":
                    print(f"[{ts}] ✅ [ACK] Confirmado cmd={kv.get('cmd')} -> {kv}")
                elif msg_type in ("ERR", "ERROR"):
                    print(f"[{ts}] ❌ [ERROR] Rechazo en cmd={kv.get('cmd')}: {kv.get('msg')}")

    except KeyboardInterrupt:
        com.write(b"STOP\n")
        print("\nTransmicion detenida.")

            