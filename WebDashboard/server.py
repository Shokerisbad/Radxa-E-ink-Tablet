import os
import shutil
import sys
import subprocess
from flask import Flask, request, jsonify, render_template, abort

app = Flask(__name__, template_folder='templates', static_folder='static')

# Determine book storage directory location dynamically
BOOKS_DIR = None
potential_dirs = [
    os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'TabletClient', 'build', 'books')),
    os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'TabletClient', 'books')),
    os.path.abspath(os.path.join(os.path.dirname(__file__), 'TabletClient', 'books')),
    os.path.abspath(os.path.join(os.path.dirname(__file__), 'books'))
]

for d in potential_dirs:
    parent = os.path.dirname(d)
    if os.path.exists(d) or os.path.exists(parent):
        BOOKS_DIR = d
        break

if not BOOKS_DIR:
    BOOKS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), 'books'))

os.makedirs(BOOKS_DIR, exist_ok=True)
print(f"[Dashboard] Storage directory resolved to: {BOOKS_DIR}")

# Allowed E-book extensions
ALLOWED_EXTENSIONS = {'.epub', '.pdf', '.txt'}

def get_allowed_file(filename):
    _, ext = os.path.splitext(filename.lower())
    return ext in ALLOWED_EXTENSIONS

def format_size(bytes_size):
    for unit in ['B', 'KB', 'MB', 'GB']:
        if bytes_size < 1024.0:
            return f"{bytes_size:.1f} {unit}"
        bytes_size /= 1024.0
    return f"{bytes_size:.1f} TB"

# Telemetry Helper Functions
def get_battery_percentage():
    # Read from Linux power supply sysfs
    paths = [
        "/sys/class/power_supply/battery/capacity",
        "/sys/class/power_supply/axp20x-battery/capacity", # common for AXP PMICs
        "/sys/class/power_supply/rk-bat/capacity"
    ]
    for p in paths:
        if os.path.exists(p):
            try:
                with open(p, 'r') as f:
                    return int(f.read().strip())
            except Exception:
                pass
    return -1 # -1 signifies running on mains or unsupported PMIC

def get_cpu_temperature():
    # Read from Linux thermal zones
    paths = [
        "/sys/class/thermal/thermal_zone0/temp",
        "/sys/class/thermal/thermal_zone1/temp"
    ]
    for p in paths:
        if os.path.exists(p):
            try:
                with open(p, 'r') as f:
                    temp_millidegrees = int(f.read().strip())
                    return round(temp_millidegrees / 1000.0, 1)
            except Exception:
                pass
    # Return mock value if running locally on development laptop (Windows/Mac)
    if sys.platform != 'linux':
        return 42.5
    return 0.0

def get_wireguard_status():
    # Check if wg0 interface exists in sysfs (extremely fast, zero process forks)
    if os.path.exists("/sys/class/net/wg0"):
        return True
    # Mock connected state if testing locally on non-Linux
    if sys.platform != 'linux':
        return True
    return False

# Routes
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/books', methods=['GET'])
def list_books():
    books = []
    try:
        for entry in os.scandir(BOOKS_DIR):
            if entry.is_file() and get_allowed_file(entry.name):
                stat = entry.stat()
                import datetime
                uploaded_time = datetime.datetime.fromtimestamp(stat.st_mtime).strftime('%Y-%m-%d %H:%M:%S')
                books.append({
                    "name": entry.name,
                    "size": format_size(stat.st_size),
                    "uploaded": uploaded_time
                })
    except Exception as e:
        return jsonify({"error": str(e)}), 500
    return jsonify(books)

@app.route('/api/upload', methods=['POST'])
def upload_book():
    if 'file' not in request.files:
        return jsonify({"error": "No file part in request"}), 400
    
    file = request.files['file']
    if file.filename == '':
        return jsonify({"error": "No selected file"}), 400
        
    if not get_allowed_file(file.filename):
        return jsonify({"error": "Unsupported file format. Only EPUB, PDF, and TXT are allowed."}), 400
        
    try:
        # Prevent path traversal attacks using basename
        filename = os.path.basename(file.filename)
        save_path = os.path.join(BOOKS_DIR, filename)
        file.save(save_path)
        return jsonify({"success": True, "filename": filename})
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/books/<filename>', methods=['DELETE'])
def delete_book(filename):
    # Prevent directory traversal
    filename = os.path.basename(filename)
    file_path = os.path.join(BOOKS_DIR, filename)
    
    if not os.path.exists(file_path):
        return jsonify({"error": "File not found"}), 404
        
    try:
        os.remove(file_path)
        return jsonify({"success": True})
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/sync_metadata', methods=['POST'])
def sync_metadata():
    import json, urllib.request, urllib.parse, os
    
    cache_path = os.path.join(BOOKS_DIR, '.cache', 'metadata.json')
    if not os.path.exists(cache_path):
        return jsonify({"success": True, "message": "No cache found to sync."})
        
    try:
        with open(cache_path, 'r') as f:
            metadata = json.load(f)
            
        changed = False
        for path, meta in metadata.items():
            genre = meta.get("genre", "")
            summary = meta.get("summary", "")
            if genre == "Unknown Genre" or genre == "" or summary == "No summary available." or summary == "":
                title = meta.get("title", "")
                author = meta.get("author", "")
                
                if not title and not author:
                    continue
                    
                import re
                clean_title = re.split(r'[:;]', title)[0].strip()
                
                def try_fetch(q):
                    url = "https://www.googleapis.com/books/v1/volumes?q=" + urllib.parse.quote(q)
                    print(f"[Dashboard Sync] Fetching {url}")
                    try:
                        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
                        with urllib.request.urlopen(req, timeout=5) as response:
                            data = json.loads(response.read().decode('utf-8'))
                            if "items" in data and len(data["items"]) > 0:
                                return data["items"][0].get("volumeInfo", {})
                    except Exception as e:
                        print(f"[Dashboard Sync] Fetch failed for {q}: {e}")
                    return None

                # 1. Strict query
                q_strict = ""
                if clean_title: q_strict += f"intitle:{clean_title}"
                if author and author != "Unknown":
                    if q_strict: q_strict += " "
                    q_strict += f"inauthor:{author}"
                
                vol = try_fetch(q_strict) if q_strict else None
                
                # 2. Open Library fallback (for genre)
                if not vol or ("categories" not in vol and genre == ""):
                    ol_q = ""
                    if clean_title: ol_q += f"title={urllib.parse.quote(clean_title)}"
                    if author and author != "Unknown":
                        if ol_q: ol_q += "&"
                        ol_q += f"author={urllib.parse.quote(author)}"
                    
                    if ol_q:
                        ol_url = f"https://openlibrary.org/search.json?{ol_q}"
                        print(f"[Dashboard Sync] Fetching (OpenLibrary) {ol_url}")
                        try:
                            req = urllib.request.Request(ol_url, headers={'User-Agent': 'Mozilla/5.0'})
                            with urllib.request.urlopen(req, timeout=5) as response:
                                ol_data = json.loads(response.read().decode('utf-8'))
                                if "docs" in ol_data and len(ol_data["docs"]) > 0:
                                    ol_doc = ol_data["docs"][0]
                                    if "subject" in ol_doc and len(ol_doc["subject"]) > 0:
                                        if genre == "Unknown Genre" or genre == "":
                                            meta["genre"] = ol_doc["subject"][0]
                                            genre = meta["genre"]
                                            changed = True
                                            print(f"[Dashboard Sync] OpenLibrary found genre: {genre}")
                        except Exception as e:
                            print(f"[Dashboard Sync] OpenLibrary fetch failed: {e}")

                # 3. Relaxed query fallback
                if not vol or ("description" not in vol and summary == "") or ("categories" not in vol and genre == ""):
                    q_relaxed = clean_title
                    if author and author != "Unknown":
                        if q_relaxed: q_relaxed += " "
                        q_relaxed += author
                    if q_relaxed:
                        vol_relaxed = try_fetch(q_relaxed)
                        if vol_relaxed:
                            vol = vol_relaxed
                
                if vol:
                    cats = vol.get("categories", [])
                    desc = vol.get("description", "")
                    
                    if cats and (genre == "Unknown Genre" or genre == ""):
                        meta["genre"] = cats[0]
                        changed = True
                    if desc and (summary == "No summary available." or summary == ""):
                        meta["summary"] = desc
                        changed = True
                    
        if changed:
            with open(cache_path, 'w') as f:
                json.dump(metadata, f, indent=4)
            return jsonify({"success": True, "message": "Metadata synced successfully. Reboot tablet to apply."})
        else:
            return jsonify({"success": True, "message": "Metadata sync complete. No updates found."})
            
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/status', methods=['GET'])
def get_status():
    client_ip = request.remote_addr
    if client_ip:
        llm_ip_path = os.path.abspath(os.path.join(BOOKS_DIR, '..', 'llm_ip.txt'))
        try:
            current_ip = ""
            if os.path.exists(llm_ip_path):
                with open(llm_ip_path, 'r') as f:
                    current_ip = f.read().strip()
            if current_ip != client_ip:
                with open(llm_ip_path, 'w') as f:
                    f.write(client_ip)
                print(f"[Dashboard] Automatically registered LLM API Server IP: {client_ip}")
        except Exception as e:
            print(f"[Dashboard] Error writing LLM IP: {e}")

    total, used, free = shutil.disk_usage(BOOKS_DIR)
    return jsonify({
        "battery": get_battery_percentage(),
        "cpu_temp": get_cpu_temperature(),
        "storage_total": format_size(total),
        "storage_free": format_size(free),
        "storage_used_percent": round((used / total) * 100, 1),
        "wireguard_connected": get_wireguard_status()
    })

@app.route('/api/reboot', methods=['POST'])
def reboot_system():
    if sys.platform != 'linux':
        print("[Dashboard] [Mock] Reboot command received (not on Linux)")
        return jsonify({"success": True, "message": "Reboot command received (mock mode)."})
    
    try:
        # Run reboot asynchronously after a 2-second delay to let the response complete
        subprocess.Popen("sleep 2 && sudo reboot", shell=True)
        return jsonify({"success": True, "message": "System is rebooting..."})
    except Exception as e:
        return jsonify({"error": f"Failed to initiate reboot: {str(e)}"}), 500

@app.route('/api/logs', methods=['GET'])
def get_logs():
    if sys.platform != 'linux':
        import time
        import random
        t_str = time.strftime('%b %d %H:%M:%S')
        mock_logs = [
            f"{t_str} radxa-tablet tablet-client[1024]: [Touch] Initialized GT911 touch controller on i2c-1",
            f"{t_str} radxa-tablet tablet-client[1024]: [Touch] Calibrated touch surface coordinate mapping (800x480)",
            f"{t_str} radxa-tablet tablet-client[1024]: [EPD] Initialized Waveshare E-Paper Display (SPI speed 4MHz)",
            f"{t_str} radxa-tablet tablet-client[1024]: [LVGL] Registered E-Paper display driver successfully",
            f"{t_str} radxa-tablet systemd[1]: Started Radxa E-Ink Tablet Client UI Daemon.",
            f"{t_str} radxa-tablet tablet-client[1024]: [Dashboard] Web interface listening on http://0.0.0.0:8080",
            f"{t_str} radxa-tablet tablet-client[1024]: [Library] Scanning 'books' folder: found 3 files",
            f"{t_str} radxa-tablet kernel: [   12.435] axp20x_battery: battery online, capacity 92%",
            f"{t_str} radxa-tablet wg-quick[620]: [#] ip link add dev wg0 type wireguard",
            f"{t_str} radxa-tablet wg-quick[620]: [#] wg setconf wg0 /etc/wireguard/wg0.conf",
            f"{t_str} radxa-tablet wg-quick[620]: [#] ip link set mtu 1420 up dev wg0",
            f"{t_str} radxa-tablet kernel: [   15.912] wireguard: wg0 created successfully",
            f"{t_str} radxa-tablet tablet-client[1024]: [EPD] E-Paper refresh triggered (Full Refresh, mode 2)",
            f"{t_str} radxa-tablet tablet-client[1024]: [Library] Successfully parsed EPUB metadata for 'Dracula.epub'",
        ]
        if random.random() > 0.3:
            mock_logs.append(f"{t_str} radxa-tablet tablet-client[1024]: [Touch] [Warning] Missed touch release event (GT911 status 0x80)")
        return jsonify({"logs": "\n".join(mock_logs)})
        
    try:
        unit = request.args.get('unit', '')
        cmd = ["journalctl", "-n", "100", "--no-pager"]
        if unit:
            cmd.extend(["-u", unit])
            
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=5)
        if res.returncode == 0:
            return jsonify({"logs": res.stdout})
        
        # Fallback if journalctl fails or doesn't exist
        for syslog_path in ["/var/log/syslog", "/var/log/messages"]:
            if os.path.exists(syslog_path):
                with open(syslog_path, 'r') as f:
                    lines = f.readlines()
                    return jsonify({"logs": "".join(lines[-100:])})
                    
        return jsonify({"error": "Logs not accessible"}), 500
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    # Listen on all interfaces (0.0.0.0) so it's accessible over VPN / LAN
    app.run(host='0.0.0.0', port=8080, debug=False)
