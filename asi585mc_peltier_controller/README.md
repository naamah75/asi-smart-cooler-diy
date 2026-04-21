# ASI585MC Peltier Controller

Firmware PlatformIO per ESP32 che controlla una cella di Peltier per raffreddare una camera ZWO ASI585MC.

Target hardware attuale: `Wemos/LOLIN D32 Pro`.

## Funzioni implementate

- Controllo PWM di un MOSFET per pilotare la Peltier
- Regolazione PID sul setpoint della temperatura fredda
- Base tuning PID rapido e profili soft/normal/aggressive
- Override anti-condensa: il target non scende sotto `dew point + margine`
- Lettura NTC 10K B3950 sulla parte fredda
- Sensore NTC 10K B3950 opzionale sul lato caldo con cutoff di sicurezza
- Lettura temperatura/umidita ambiente da BME280
- Calcolo dew point
- Lettura corrente e tensione Peltier via INA219
- Calibrazioni persistenti salvate in `Preferences`
- Controllo via WiFi con pagina web e API JSON
- Controllo via BLE con servizio JSON e servizio strutturato a caratteristiche dedicate
- Log seriale periodico dei parametri principali
- Preset persistenti `estate`, `inverno`, `deep_cooling`
- Endpoint REST semplificati per integrazione con software astronomico o script custom
- Bridge Python `ASCOM Alpaca Camera` focalizzato sul cooler
- mDNS su ESP32 per raggiungere il controller come `http://<hostname>.local`
- AP fallback con captive portal per onboarding WiFi
- Menu `Settings` persistente per rete e parametri di connettivita
- Scan reti WiFi dal captive portal e dalla pagina Settings

## Pinout di default

Pin proposti per `LOLIN D32 Pro`:

- `GPIO25`: PWM gate MOSFET Peltier
- `GPIO34`: ADC NTC freddo 10K B3950
- `GPIO35`: ADC NTC caldo opzionale 10K B3950
- `GPIO21`: SDA I2C
- `GPIO22`: SCL I2C

## Assunzioni hardware

- NTC in partitore con resistenza fissa da `10k` verso `3.3V`
- NTC verso `GND`
- ADC letto sul nodo centrale del partitore
- Il sensore caldo usa lo stesso schema del sensore freddo
- INA219 sul bus I2C della Peltier
- BME280 sullo stesso bus I2C a `0x76` o `0x77`
- MOSFET logic level comandato direttamente da ESP32 con adeguato driver/pull-down se necessario

## Configurazione WiFi

1. Copia `include/secrets.example.h` in `include/secrets.h`
2. Puoi usare valori statici iniziali oppure lasciare che il primo onboarding avvenga da captive portal

Quando l'ESP32 si collega alla rete avvia anche `mDNS`, quindi il controller e raggiungibile tramite:

- `http://<HOSTNAME>.local`

Esempio con hostname di default:

- `http://asi585mc-cooler.local`

## Onboarding

Se il controller non ha credenziali WiFi valide o non riesce a collegarsi alla rete, attiva automaticamente un access point di setup.

Parametri di default del portale:

- SSID AP: `<hostname>-setup`
- IP captive portal: `192.168.4.1`

Il captive portal espone:

- pagina onboarding su `/`
- menu persistente rete su `/settings`
- API rete su `/api/network`

Dal portale puoi salvare:

- `wifi_ssid`
- `wifi_password`
- `hostname`
- `dhcp_enabled`
- `static_ip`
- `gateway`
- `subnet`
- `dns1`

Dopo il salvataggio il firmware tenta subito la riconnessione WiFi con i nuovi parametri.

Per facilitare l'onboarding, il portale e la pagina settings permettono anche la scansione delle reti WiFi visibili tramite endpoint `GET /api/wifi/scan`.

## Menu impostazioni

Il firmware ora ha due interfacce web principali:

- `/`: dashboard cooler oppure onboarding se non connesso alla rete
- `/settings`: pagina dedicata alle impostazioni di rete persistenti

Default rete statica proposta:

- IP: `10.0.0.50`
- gateway: `10.0.0.1`
- subnet: `255.255.255.0`

## API HTTP

### Stato

- `GET /api/status`

Restituisce lo stato completo in JSON.

### Configurazione

- `POST /api/config`

Body esempio:

```json
{
  "enabled": true,
  "hot_sensor_enabled": true,
  "target_c": 2.0,
  "dew_margin_c": 2.5,
  "max_duty": 0.85,
  "max_hot_side_c": 52.0,
  "pid_profile": "normal",
  "pid_base_tune": false,
  "pid": {
    "kp": 18.0,
    "ki": 0.3,
    "kd": 10.0
  },
  "calibration": {
    "cold_offset_c": -0.4,
    "hot_offset_c": 0.0,
    "ambient_offset_c": 0.2,
    "humidity_offset_pct": 3.0,
    "current_offset_a": 0.0,
    "current_scale": 1.0
  }
}
```

### Preset

- `GET /api/presets`
- `POST /api/preset/save`
- `POST /api/preset/apply`

Body esempio:

```json
{
  "name": "estate"
}
```

Preset disponibili:

- `estate`
- `inverno`
- `deep_cooling`

Ogni preset salva:

- `target_c`
- `dew_margin_c`
- `max_duty`
- `max_hot_side_c`
- `hot_sensor_enabled`

## BLE

- Servizio JSON: `9b220100-35ac-4e4e-9f6f-8ed48fc5c001`
- Status characteristic: `9b220101-35ac-4e4e-9f6f-8ed48fc5c001`
- Command characteristic: `9b220102-35ac-4e4e-9f6f-8ed48fc5c001`

La caratteristica status pubblica lo stesso JSON dell'endpoint HTTP. La caratteristica command accetta lo stesso payload JSON del `POST /api/config`.

### BLE strutturato

- Servizio strutturato: `9b220200-35ac-4e4e-9f6f-8ed48fc5c001`
- Telemetry JSON read/notify: `9b220201-35ac-4e4e-9f6f-8ed48fc5c001`
- Cooler enabled read/write/notify `uint8`: `9b220202-35ac-4e4e-9f6f-8ed48fc5c001`
- Target read/write/notify `int16` in centesimi di grado C: `9b220203-35ac-4e4e-9f6f-8ed48fc5c001`
- Preset command write string: `9b220204-35ac-4e4e-9f6f-8ed48fc5c001`
- Tune command write string: `9b220205-35ac-4e4e-9f6f-8ed48fc5c001`

Comandi previsti nel BLE strutturato:

- preset: `apply:estate`, `apply:inverno`, `apply:deep_cooling`, `save:estate`, `save:inverno`, `save:deep_cooling`
- tune: `base`, `soft`, `normal`, `aggressive`

## Endpoint aggiuntivi

- `POST /api/pid/base-tune`

Applica un tuning PID iniziale basato sul delta termico tra ambiente e target. E utile come punto di partenza, poi conviene rifinire i parametri con prove reali.

## Endpoint Astro REST

Questi endpoint non implementano l'intero stack ASCOM Alpaca o INDI, ma offrono un layer REST semplice e stabile per integrazioni custom, automazioni o bridge futuri.

- `GET /api/astro/status`
- `GET|POST /api/astro/cooler`
- `GET|POST /api/astro/setpoint`
- `GET /api/astro/power`
- `GET /api/astro/temperature`

Esempi:

```json
POST /api/astro/cooler
{
  "enabled": true
}
```

```json
POST /api/astro/setpoint
{
  "target_c": -2.0
}
```

Risposte tipiche includono:

- temperatura fredda
- setpoint richiesto
- setpoint effettivo dopo clamp dew point
- potenza cooler in percentuale
- temperatura lato caldo
- corrente e tensione della Peltier
- flag di protezione dew point e lato caldo

## Bridge ASCOM Alpaca Camera

Nella cartella `bridge/` trovi un bridge Python che espone il controller come device `Camera` Alpaca limitato alle funzioni di raffreddamento.

### Installazione

```bash
cd bridge
pip install -r requirements.txt
```

### Avvio

```bash
python ascom_alpaca_camera_bridge.py --esp32-url http://asi585mc-cooler.local --port 11111
```

Con fallback IP esplicito:

```bash
python ascom_alpaca_camera_bridge.py --esp32-url http://asi585mc-cooler.local --esp32-hostname asi585mc-cooler --esp32-fallback-url http://192.168.1.50 --port 11111
```

### Endpoint Alpaca principali

- `/management/apiversions`
- `/management/v1/description`
- `/management/v1/configureddevices`
- `/api/v1/camera/0/connected`
- `/api/v1/camera/0/cooleron`
- `/api/v1/camera/0/setccdtemperature`
- `/api/v1/camera/0/ccdtemperature`
- `/api/v1/camera/0/heatsinktemperature`
- `/api/v1/camera/0/coolerpower`

Il bridge non implementa esposizioni o acquisizione immagini: serve solo per il sottosistema cooler.

Ordine di tentativo lato bridge:

1. `--esp32-url`
2. `http://<esp32-hostname>.local`
3. IP risolto dal nome `.local`, se disponibile
4. `--esp32-fallback-url`, se impostato

## Note di taratura

- `cold_offset_c`: correzione della sonda NTC fredda
- `hot_offset_c`: correzione della sonda NTC calda opzionale
- `ambient_offset_c`: correzione temperatura BME280
- `humidity_offset_pct`: correzione umidita BME280
- `current_offset_a`: offset della corrente INA219
- `current_scale`: guadagno moltiplicativo della corrente INA219

## Sensore caldo opzionale

- Se `hot_sensor_enabled` e la temperatura lato caldo supera `max_hot_side_c`, il PWM viene azzerato
- La protezione e pensata per salvaguardare Peltier, dissipatore e camera in caso di ventilazione insufficiente
- Se il sensore non e montato, lascia `hot_sensor_enabled = false`

## Pagina web

La pagina su `/` ora include:

- stato live delle temperature, dew point, corrente e duty PWM
- gestione preset estate/inverno/deep cooling
- configurazione controllo
- tuning PID manuale
- selezione profilo PID
- pulsante di base tuning PID
- calibrazioni complete dei sensori
- vista JSON live per debug

## Build

```bash
pio run
```

## Flash

```bash
pio run -t upload
```

## Nota pratica

Per evitare condensa sulla camera conviene partire con un margine dew point di almeno `2-3 C` e limitare `max_duty` nelle prime prove, verificando corrente, dissipazione lato caldo e isolamento termico del cold finger.

Per il lato caldo conviene impostare inizialmente `max_hot_side_c` tra `45 C` e `55 C`, poi alzarlo solo dopo avere verificato dissipatore, ventola e stabilita del sistema.
