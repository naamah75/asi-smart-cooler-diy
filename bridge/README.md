# Alpaca Camera Bridge

Bridge Python che espone un sottoinsieme utile di `ASCOM Alpaca Camera` focalizzato sul cooler e lo traduce verso gli endpoint REST dell'ESP32.

## Dipendenze

```bash
pip install -r requirements.txt
```

## Avvio

```bash
python ascom_alpaca_camera_bridge.py --esp32-url http://asi585mc-cooler.local --port 11111
```

Con fallback IP:

```bash
python ascom_alpaca_camera_bridge.py --esp32-url http://asi585mc-cooler.local --esp32-hostname asi585mc-cooler --esp32-fallback-url http://192.168.1.50 --port 11111
```

## Risoluzione controller

Il bridge prova in ordine:

1. URL esplicito `--esp32-url`
2. hostname mDNS `http://<esp32-hostname>.local`
3. IP risolto dal nome mDNS, se disponibile
4. URL di fallback `--esp32-fallback-url`

## Endpoint Alpaca principali implementati

- `/management/apiversions`
- `/management/v1/description`
- `/management/v1/configureddevices`
- `/api/v1/camera/0/connected`
- `/api/v1/camera/0/cooleron`
- `/api/v1/camera/0/setccdtemperature`
- `/api/v1/camera/0/ccdtemperature`
- `/api/v1/camera/0/heatsinktemperature`
- `/api/v1/camera/0/coolerpower`

## Note

- Il bridge modella il controller come device `Camera` perché in Alpaca il cooler appartiene normalmente all'interfaccia camera.
- Non implementa acquisizione immagini, esposizioni o ROI.
- E pensato per client che vogliono gestire soltanto raffreddamento, setpoint e telemetria del cooler.
