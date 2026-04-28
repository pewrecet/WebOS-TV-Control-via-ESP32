# LG + КИОН авто-запуск на ESP32

Прошивка для ESP32, которая заменяет автоматизацию из Home Assistant:
детектит включение телевизора LG по сети, подключается к нему по WebOS
SSAP (WebSocket), запускает приложение **КИОН** и проигрывает
заданную последовательность кнопок пульта (стрелки, OK, и т.п.).

## Железо

Рекомендуется **ESP32-S3 DevKitC-1** (~500₽ на AliExpress).
Обычная ESP32 DevKit тоже подойдёт — в `platformio.ini` есть отдельный env.

Питание: USB 5 В, потребление мизерное.

## Структура проекта

```
lg-kion-auto/
├── platformio.ini          # конфиг сборки
├── include/
│   └── config.h            # ← ВСЕ твои параметры (Wi-Fi, IP TV, ключ, кнопки)
├── src/
│   ├── main.cpp            # конечный автомат: детект → коннект → launch → sequence
│   ├── WebOSClient.{h,cpp} # SSAP клиент + pointer socket для кнопок
│   ├── TvDetector.{h,cpp}  # ping / TCP-probe
│   └── KeyStore.h          # client-key в NVS
└── tools/
    └── list_apps.py        # вспомогательный скрипт для получения ID КИОН
```

## Подготовка (один раз)

### 1. Установи PlatformIO

Проще всего — расширение PlatformIO IDE в VS Code. На Mac:

```bash
brew install --cask visual-studio-code
# затем в VS Code установи расширение "PlatformIO IDE"
```

Либо CLI:

```bash
brew install platformio
```

### 2. Узнай IP телевизора

В роутере в списке клиентов найди LG. **Обязательно зарезервируй IP по MAC**,
иначе после перезагрузки TV может получить другой адрес, и автоматизация
перестанет работать.

### 3. Получи `client-key` и ID приложения КИОН

Есть два варианта.

**Вариант А: взять готовое из Home Assistant.**
`client-key` лежит в `.storage/core.config_entries` (ищи по `webostv`).
ID КИОН — через Developer Tools → Services:

```yaml
service: webostv.command
data:
  entity_id: media_player.lg
  command: ssap://com.webos.applicationManager/listApps
```

Ответ увидишь в логах HA.

**Вариант Б: скриптом (TV должен быть включён).**

```bash
pip3 install pywebostv
python3 tools/list_apps.py 192.168.1.50
```

При первом запуске TV покажет запрос на сопряжение — подтверди пультом.
Скрипт сохранит ключ в `~/.lg_client_key.json` и распечатает его вместе
со списком приложений. Найди строку с "КИОН" / "KION" / "МТС ТВ" — там будет ID
вида `ru.mts.mtstv` или похожий.

### 4. Заполни `include/config.h`

Подставь:
- `WIFI_SSID` / `WIFI_PASSWORD`
- `TV_IP`
- `TV_CLIENT_KEY` (если получил)
- `APP_ID_KION`
- `REMOTE_SEQUENCE[]` — свою последовательность кнопок с задержками

Если `TV_CLIENT_KEY` оставить пустым — при первом включении ESP32 выдаст
на TV запрос pairing. После подтверждения ключ будет автоматически сохранён
в NVS, и в дальнейшем запросы не появятся.

### 5. Собери и прошей

Воткни ESP32 по USB и:

```bash
cd lg-kion-auto
pio run -t upload              # для ESP32-S3 (по умолчанию)
# или
pio run -e esp32dev -t upload  # для обычной ESP32

pio device monitor              # смотреть логи
```

## Как это работает

1. Raspberry Pi больше не нужен. ESP32 сама пингует TV каждые 3 секунды.
2. Когда TV переходит из OFF в ON, ждём `TV_BOOT_DELAY_MS` (чтобы WebOS
   успел загрузиться), затем подключаемся по `ws://TV:3000`.
3. Отправляем `register`-сообщение с client-key. TV отвечает `registered`.
4. Сразу запрашиваем `ssap://com.webos.service.networkinput/getPointerInputSocket`.
   TV возвращает URL отдельного WebSocket — это "виртуальный пульт".
5. Отправляем `ssap://system.launcher/launch` с ID приложения КИОН.
6. По таймерам из `REMOTE_SEQUENCE` шлём в pointer socket строки вида
   `type:button\nname:DOWN\n\n`.
7. Закрываем соединения, переходим в COOLDOWN — ждём, пока TV выключат,
   чтобы не зациклиться.

## Что если TV отвечает на ping даже выключенный?

У некоторых LG (OLED последних лет) в режиме Standby держится Wi-Fi
для WoL и прочих плюшек, ICMP отвечает всегда. Тогда поменяй в `config.h`:

```c
#define DETECT_METHOD_PING      0   // теперь TCP-probe на порт 3000
```

WebOS API (порт 3000) слушает только когда TV реально включён.

## Отладка

Открой Serial Monitor (115200). Полезные строки:

- `[TV] State: OFF -> ON` — сработал триггер
- `[WebOS] WS connected` — TCP/WebSocket есть
- `[WebOS] Please confirm pairing on TV screen!` — нужен первичный pairing
- `[WebOS] Registered! client-key=...` — регистрация прошла
- `[Pointer] connected` — pointer socket открыт, можно слать кнопки
- `[Seq] Done!` — последовательность отработала

Если `launchApp` срабатывает, но кнопки не доходят — скорее всего не успел
подняться pointer socket. Увеличь задержку в `onReady` (сейчас 1500 мс)
или первый шаг в `REMOTE_SEQUENCE` с `nullptr` и паузой побольше.

## Список доступных кнопок

`UP`, `DOWN`, `LEFT`, `RIGHT`, `ENTER`, `BACK`, `EXIT`, `HOME`,
`PLAY`, `PAUSE`, `STOP`, `REWIND`, `FASTFORWARD`,
`CHANNELUP`, `CHANNELDOWN`,
`RED`, `GREEN`, `YELLOW`, `BLUE`,
`MENU`, `INFO`, `ASTERISK`,
`0`..`9`.

Для громкости и mute в WebOS отдельные SSAP-команды (не через pointer),
сейчас они не реализованы — если нужны, допишу.

## Возможные улучшения

- WebUI на ESP32 (AsyncWebServer) — правка последовательности без перепрошивки
- Несколько сценариев (по времени суток)
- OTA-обновления
- Интеграция с MQTT (если решишь вернуть часть HA)

Скажи, что из этого нужно — добавлю.
