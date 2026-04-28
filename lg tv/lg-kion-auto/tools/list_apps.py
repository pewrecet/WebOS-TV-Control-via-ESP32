#!/usr/bin/env python3
"""
Вспомогательный скрипт: подключается к LG WebOS TV и выводит список
установленных приложений с их id.

Использование:
    pip install pywebostv
    python3 list_apps.py 192.168.1.50

При первом запуске TV спросит разрешение — подтверди на экране пультом.
Ключ сохранится в ~/.lg_client_key.json, чтобы дальше не переспрашивало.
"""
import json
import os
import sys
from pathlib import Path

from pywebostv.connection import WebOSClient
from pywebostv.controls import ApplicationControl

KEY_FILE = Path.home() / ".lg_client_key.json"

def main():
    if len(sys.argv) < 2:
        print("Usage: list_apps.py <TV_IP>")
        sys.exit(1)

    ip = sys.argv[1]
    store = {}
    if KEY_FILE.exists():
        store = json.loads(KEY_FILE.read_text())

    client = WebOSClient(ip)
    client.connect()
    for status in client.register(store):
        if status == WebOSClient.PROMPTED:
            print("Подтверди pairing на экране TV...")
        elif status == WebOSClient.REGISTERED:
            print("Зарегистрированы.")

    KEY_FILE.write_text(json.dumps(store))
    print(f"client-key сохранён в {KEY_FILE}")
    print(f"client-key: {store.get('client_key')}\n")

    app = ApplicationControl(client)
    apps = app.list_apps()
    print(f"{'ID':<50} Название")
    print("-" * 80)
    for a in apps:
        print(f"{a['id']:<50} {a.get('title','')}")

if __name__ == "__main__":
    main()
