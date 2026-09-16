import serial
import struct
import sys

MARKER = b"<{DP}>"
HEADER_LEN = len(MARKER) + 2

if len(sys.argv) < 2:
    print(f"USO: python {sys.argv[0]} <puerto>")
    exit(1)

port = sys.argv[1]

with serial.Serial(port, baudrate=115200, timeout=0.1) as com:
    buf = bytearray()
    print(f"Escuchando en {port}...")
    
    while True:
        chunk = com.read(4096)
        if not chunk:
            continue

        buf += chunk

        while True:
            start = buf.find(MARKER)

            if start < 0:
                cut = max(0, len(buf) - len(MARKER))
                del buf[:cut]
                break

            if start > 0:
                del buf[:start]

            if len(buf) < HEADER_LEN:
                break

            float_num, = struct.unpack_from("<H", buf, len(MARKER))
            data_len = float_num * 4

            if len(buf) < HEADER_LEN + data_len:
                break

            data = buf[HEADER_LEN:HEADER_LEN + data_len]
            del buf[:HEADER_LEN + data_len]

            values = struct.unpack(f"<{float_num}f", data)
            
            if float_num == 3:
                print(f"[ACELERÓMETRO] X: {values[0]:.3f} g | Y: {values[1]:.3f} g | Z: {values[2]:.3f} g")
            elif float_num == 2:
                print(f"[AMBIENTALES]  Temp: {values[0]:.1f} °C | Humedad: {values[1]:.0f} %")