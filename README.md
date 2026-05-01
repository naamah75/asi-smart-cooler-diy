# ASI585MC Peltier Controller

Firmware PlatformIO per ESP32 che controlla una cella di Peltier per raffreddare una camera ZWO ASI585MC.

Target hardware attuale: `Wemos/LOLIN S2 Pico`, con supporto anche per `Wemos/LOLIN D32 Pro`.

## Funzioni implementate

- Controllo PWM di un MOSFET per pilotare la Peltier
- Regolazione PID sul setpoint della temperatura fredda
- Base tuning PID rapido e profili soft/normal/aggressive
- Override anti-condensa: il target non scende sotto `dew point + margine`
- Lettura NTC 10K B3950 sulla parte fredda
- Sensore NTC 10K B3950 opzionale sul lato caldo con cutoff di sicurezza
- Lettura temperatura/umidita ambiente da SHT30/SHT31 o BME280
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
- Display OLED SSD1306 128x32 integrato sulla versione `LOLIN S2 Pico`

## Pinout di default

Pin attuali per `LOLIN S2 Pico`:

- `GPIO11`: PWM gate MOSFET Peltier
- `GPIO10`: LED stato active-low
- `GPIO1`: ADC NTC freddo 10K B3950
- `GPIO2`: ADC NTC caldo opzionale 10K B3950
- `GPIO3`: ADC sensore corrente ACS71x quando si usa ADC interno ESP32-S2
- `GPIO8`: SDA I2C
- `GPIO9`: SCL I2C
- `GPIO18`: reset OLED SSD1306

Pin proposti per `LOLIN D32 Pro`:

- `GPIO25`: PWM gate MOSFET Peltier
- `GPIO5`: LED stato active-low
- `GPIO33`: ADC NTC freddo 10K B3950
- `GPIO36` / `VP`: ADC NTC caldo opzionale 10K B3950
- `GPIO32`: ADC sensore corrente ACS71x quando si usa ADC interno ESP32
- `GPIO21`: SDA I2C
- `GPIO22`: SCL I2C

## Selezione hardware in PlatformIO

Il progetto ora prevede piu combinazioni selezionabili prima della compilazione e dell'upload, cambiando environment in `platformio.ini`.

Environment disponibili:

- `lolin_d32_pro_espadc_ina219`
- `lolin_d32_pro_espadc_acs71x`
- `lolin_d32_pro_ads1115_ina219`
- `lolin_d32_pro_ads1115_acs71x`
- `lolin_d32_pro_espadc_ina219_classic`
- `lolin_d32_pro_espadc_acs71x_classic`
- `lolin_d32_pro_ads1115_ina219_classic`
- `lolin_d32_pro_ads1115_acs71x_classic`
- `lolin_s2_pico_espadc_ina219`
- `lolin_s2_pico_espadc_ina219_bme280`

Configurazione consigliata di default per partire:

- `lolin_s2_pico_espadc_ina219`

Questa e anche la configurazione attualmente impostata come `default_envs` nel progetto.

Tema dashboard di default:

- tema retrò ambra stile monitor DOS/CRT

Per tornare al look classico usa gli environment con suffisso `_classic`.

Significato:

- `espadc`: NTC e ingressi analogici letti dall'ADC interno ESP32
- `ads1115`: NTC e ingressi analogici letti tramite ADC I2C esterno ADS1115
- `ina219`: corrente letta via INA219 su I2C
- `acs71x`: corrente letta con sensore Hall analogico tipo ACS71x
- `USE_SHT30=1`: sensore ambiente SHT30/SHT31 su I2C, default attuale
- `USE_SHT30=0`: sensore ambiente BME280 su I2C

Nota importante:

- se usi `ADS1115 + ACS71x`, tutte le letture analogiche del progetto passano da ADS1115
- quindi NTC freddo, NTC caldo e sensore corrente ACS71x vengono tutti letti dal convertitore I2C
- se usi `ADS1115 + INA219`, ADS1115 legge solo le sonde NTC, mentre la corrente resta letta da INA219
- l'environment `lolin_s2_pico_espadc_ina219` usa SHT30/SHT31 come sensore ambiente di default
- l'environment `lolin_s2_pico_espadc_ina219_bme280` mantiene la variante con BME280

### Valori tipici ACS71x

Per i sensori Hall analogici della famiglia ACS71x il parametro chiave e la sensibilita in `mV/A`.

Valori tipici da usare come punto di partenza:

- circa `185.0 mV/A` per modelli intorno a `5 A`
- circa `100.0 mV/A` per modelli intorno a `20 A`
- circa `66.0 mV/A` per modelli intorno a `30 A`

Nel file `platformio.ini` puoi regolare:

- `ACS_CURRENT_SENS_MV_PER_A`
- `ACS_ZERO_V`

In molti moduli analogici alimentati a `3.3V`, `ACS_ZERO_V` puo partire da circa `1.65V`, ma conviene sempre rifinirlo con calibrazione reale a corrente zero.

## Assunzioni hardware

- NTC in partitore con resistenza fissa da `10k` verso `3.3V`
- NTC verso `GND`
- ADC letto sul nodo centrale del partitore
- Il sensore caldo usa lo stesso schema del sensore freddo
- INA219 sul bus I2C della Peltier
- SHT30/SHT31 sullo stesso bus I2C a `0x44` o `0x45`, oppure BME280 a `0x76` o `0x77` se compilato con `USE_SHT30=0`
- MOSFET logic level comandato direttamente da ESP32 con adeguato driver/pull-down se necessario

## Taratura NTC e ADC ESP32-S2

La versione `LOLIN S2 Pico` usa l'ADC interno dell'ESP32-S2. Con attenuazione `ADC_11db` il range utile documentato per ESP32-S2 e circa `0-2.5V`, quindi il firmware non scala il raw ADC sulla tensione di alimentazione `3.3V`.

Parametri attuali in `platformio.ini` per `lolin_s2_pico_espadc_ina219`:

- `NTC_SERIES_RESISTOR_OHM=9630.0f`: resistenza reale del partitore misurata, `9.63 kOhm`
- `NTC_SUPPLY_VOLTAGE=3.28f`: tensione reale tra `3V3` e `GND`
- `NTC_ADC_FULL_SCALE_V=2.50f`: scala ADC raw ESP32-S2 a `ADC_11db`
- `NTC_COLD_TEMP_OFFSET_C=0.0f`: offset temperatura sonda fredda
- `NTC_HOT_TEMP_OFFSET_C=0.0f`: offset temperatura sonda calda

Misure di riferimento usate per la taratura iniziale:

- tensione `3V3-GND`: `3.28V`
- tensione `ADC NTC-GND`: `1.635V`
- resistenza fissa partitore: `9.63 kOhm`

La formula del partitore e:

```text
Rntc = Rserie * Vadc / (Vsupply - Vadc)
```

Gli offset temperatura possono essere modificati in due modi:

- a build time da `platformio.ini` con `NTC_COLD_TEMP_OFFSET_C` e `NTC_HOT_TEMP_OFFSET_C`
- a runtime dalla pagina web, sezione `Calibrazioni`, dove vengono salvati in flash tramite `Preferences`

Se un offset e gia stato salvato dalla UI, il valore salvato in flash prevale sul default compilato in `platformio.ini`.

## Modello 3D

Per l'involucro della camera, della Peltier e del dissipatore con ventola e disponibile un modello stampabile in 3D su Thingiverse:

- https://www.thingiverse.com/thing:5873931

## Documentazione hardware

La cartella `docs/` contiene i file di supporto per realizzare il cablaggio e il contenitore:

- `docs/wiring_esp32s2.svg`: schema dei collegamenti per LOLIN S2 Pico, sensori NTC, bus I2C, INA219, sensore ambiente, OLED e pilotaggio MOSFET della Peltier.
- `docs/asi-smart-cooler-diy.fzz`: progetto Fritzing dello schema hardware, utile per consultare o modificare il cablaggio elettronico.
- `docs/scatola_rugged_peltier_v6.scad`: modello OpenSCAD del contenitore rugged stampabile in 3D, con corpo e coperchio separati.

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
- `ambient_offset_c`: correzione temperatura del sensore ambiente SHT30/SHT31 o BME280
- `humidity_offset_pct`: correzione umidita del sensore ambiente SHT30/SHT31 o BME280
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
