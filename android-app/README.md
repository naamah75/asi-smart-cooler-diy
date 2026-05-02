# Android App Draft

Bozza iniziale dell'app Android per il controller cooler.

Stato attuale:

- progetto Kotlin + Jetpack Compose
- drawer hamburger con schermate `Dashboard`, `Debug`, `About`
- dashboard principale con i riquadri della web UI, senza grafico
- sezione debug separata per dettagli e diagnostica
- sezione about per versione, link GitHub e metadati progetto

Prossimi passi suggeriti:

1. Aprire `android-app` in Android Studio.
2. Aggiungere il Gradle Wrapper dall'IDE se manca nel repo locale.
3. Collegare `/api/status` con un client HTTP.
4. Mappare i dati JSON reali ai riquadri Compose.
5. Aggiungere schermate di configurazione e controllo.

API gia' utili dal firmware:

- `GET /api/status`
- `POST /api/config`
- `GET /api/network`
- `POST /api/network`
