import socket
import threading
import time
import sys

# Globale Variablen für die Simulation
current_width = 16          # Standard: 16 Bit
bytes_per_sec_limit = 0.0   # 0.0 = unbegrenzt, bis der Befehl kommt

def control_server():
    """Simuliert den Steuer-Port 5000"""
    global current_width, bytes_per_sec_limit
    
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', 5000))
    server.listen(5)
    print("[Port 5000] Steuerport lauscht...")

    while True:
        try:
            client, addr = server.accept()
            # Kurz warten und Daten empfangen
            data = client.recv(1024).decode('utf-8').strip()
            client.close()

            if data.startswith("width"):
                parts = data.split()
                if len(parts) >= 2:
                    current_width = int(parts[1])
                    print(f"\n[Port 5000] DAC Width gesetzt auf: {current_width} Bit")

            elif data.startswith("rate"):
                parts = data.split()
                if len(parts) >= 2:
                    rate_mhz = float(parts[1])
                    rate_hz = rate_mhz * 1_000_000.0
                    
                    # Berechnung: Bytes pro Sekunde = Samplerate * (Bits / 8)
                    bytes_per_sec_limit = rate_hz * (current_width / 8)
                    mb_per_sec = bytes_per_sec_limit / (1024 * 1024)
                    
                    print(f"\n[Port 5000] DAC Rate gesetzt auf: {rate_mhz} MHz")
                    print(f"[Port 5000] -> Neues pv-Drossel-Limit: {mb_per_sec:.2f} MB/s")
                    # Die print-Ausgabe für den Daten-Thread wiederherstellen
                    print("[Port 1234] Schreibe Daten...", end="")
        except Exception as e:
            print(f"\n[Port 5000] Fehler: {e}")

def data_server():
    """Simuliert den Daten-Port 1234 mit eingebautem pv-Verhalten"""
    global bytes_per_sec_limit
    
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', 1234))
    server.listen(1)
    print("[Port 1234] Datenport lauscht...")

    while True:
        try:
            client, addr = server.accept()
            print(f"\n[Port 1234] Verbindung hergestellt von {addr[0]}")
            
            with open("testbench_passthrough_ci16.iq", "wb") as f:
                total_bytes = 0
                start_time = time.time()
                last_print = start_time

                while True:
                    # Wir lesen in relativ kleinen Chunks, um sauber zu drosseln
                    chunk = client.recv(8192)
                    if not chunk:
                        break # C++ Client hat Verbindung beendet

                    f.write(chunk)
                    chunk_len = len(chunk)
                    total_bytes += chunk_len

                    # === HIER PASSIERT DIE PV-DROSSELUNG ===
                    if bytes_per_sec_limit > 0:
                        # Berechne, wie lange dieser Chunk dauern SOLLTE
                        sleep_time = chunk_len / bytes_per_sec_limit
                        time.sleep(sleep_time) # TCP Puffer staut sich hier zurück!

                    # === PV-ÄHNLICHE KONSOLEN-AUSGABE ===
                    now = time.time()
                    if now - last_print > 0.5: # Alle 500ms Anzeige updaten
                        elapsed = now - start_time
                        mb_total = total_bytes / (1024 * 1024)
                        current_speed = mb_total / elapsed if elapsed > 0 else 0
                        
                        sys.stdout.write(f"\r[Port 1234] Gespeichert: {mb_total:.2f} MB | Aktuelle Rate: {current_speed:.2f} MB/s    ")
                        sys.stdout.flush()
                        last_print = now

            print("\n[Port 1234] Übertragung beendet. Datei 'testbench_passthrough_ci16.iq' geschlossen.")
            # bytes_per_sec_limit = 0.0 # Optional: Reset für nächsten Run
            
        except KeyboardInterrupt:
            print("\nBeendet durch Benutzer.")
            break
        except Exception as e:
            print(f"\n[Port 1234] Fehler: {e}")

if __name__ == "__main__":
    # Steuer-Server als Hintergrund-Thread starten
    ctrl_thread = threading.Thread(target=control_server, daemon=True)
    ctrl_thread.start()
    
    # Daten-Server im Haupt-Thread ausführen
    try:
        data_server()
    except KeyboardInterrupt:
        print("\nSkript beendet.")
        sys.exit(0)