# SC4-QuantumNet

DLL Plugin per **SimCity 4 Deluxe / Rush Hour** che ricrea la meccanica **ControlNet** di *SimCity (2013) – Cities of Tomorrow*, qui chiamata **QuantumNet** (unità: **QNU** = QuantumNet Units al giorno).

È il porting nativo in C++ del vecchio motore `QUANTUMNET.lua` (basato sul NEON ENGINE di RobertaME / Ulisse Wolf). Gli script Lua di QuantumNet e la relativa sezione di `PACK_CONSTANTS.lua` non servono più: tutto è nella DLL e in un file INI opzionale.

## Come funziona

Una volta al mese di gioco la DLL:

1. conta i **produttori** (QuantumNet Data Hub) e i **consumatori** (Fusion Power Plant, …) presenti in città;
2. legge la popolazione residenziale per fascia di ricchezza (R$, R$$, R$$$) e l'EQ medio della città;
3. calcola la produzione: `capacità base × fattore socioeconomico`, dove il fattore è la media tra
   - **fattore ricchezza** = (R$·0.4 + R$$·1.0 + R$$$·2.2) / popolazione R
   - **fattore istruzione** = EQ medio / 500, limitato tra 0.5 e 2.0;
4. scambia eccedenza/deficit con la regione tramite **sc4-regional-supply-demand**;
5. incassa le esportazioni (2 §/QNU) e paga le importazioni (3 §/QNU);
6. sblocca o blocca le due ricompense:
   - **Data Hub Small**: 8.000 abitanti ed EQ medio ≥ 300
   - **Data Hub Large**: 30.000 abitanti, EQ medio ≥ 600 e 2.000 Sim R$$$;
7. pubblica le variabili `game.g_quantumnet_*` per UI e altri script.

## Installazione

1. Scarica `SC4QuantumNet.dll` e `SC4QuantumNet.ini` dagli artifact della GitHub Action **Build**.
2. Copiali nella cartella **Plugins** di SimCity 4 (non in sottocartelle).
3. Installa anche **sc4-regional-supply-demand** se vuoi lo scambio regionale; senza, QuantumNet funziona solo in locale.
4. Il log viene scritto in `SC4QuantumNet.log` accanto alla DLL.

Requisiti: SimCity 4 Deluxe/Rush Hour 1.1.641 (Windows) e i lotti/exemplar dei QuantumNet Data Hub con ID `0xBB340000` / `0xBB340001`, marcati come edifici condizionali (reward).

## Configurazione (`SC4QuantumNet.ini`)

Tutte le chiavi sono facoltative; i valori predefiniti sono identici alle costanti di `PACK_CONSTANTS.lua`.

| Sezione | Contenuto |
|---|---|
| `[General]` | `ResourceID` della risorsa regionale, attivazione ricompense / scambio / pagamenti, `LogLevel` |
| `[Ratios]` | pesi di ricchezza, riferimento e limiti dell'istruzione, prezzi di acquisto/vendita |
| `[Unlocks]` | soglie di sblocco dei due Data Hub |
| `[Rewards]` | ID degli edifici ricompensa e testo mostrato quando sono bloccati |
| `[Producers]` | `ID = QNU/giorno, costo mensile, nome` |
| `[Consumers]` | `ID = QNU/giorno, classe, nome` — qui si aggiungono Tidal, Sewage Sanitizer, Waste-to-Energy quando avranno ID reali |

Se definisci `[Producers]` o `[Consumers]`, l'elenco sostituisce quello predefinito.

## Compatibilità con NEON / AQUA / WASTE

Per non rompere gli script che leggevano QuantumNet, la DLL registra nel contesto Lua degli advisor le stesse funzioni globali:

| Funzione | Restituisce |
|---|---|
| `get_quantumnet_efficiency_multiplier()` | rapporto di soddisfazione 0–1 (moltiplicatore di efficienza dei consumatori) |
| `run_quantumnet_balance_variables(ret_type)` | `"B"` soddisfazione, `"I"` importati, `"X"` esportati, `"U"` domanda non coperta |
| `get_quantumnet_building_cost_monthly()` | costo mensile totale dei Data Hub |

Variabili disponibili come `game.<nome>`: `g_quantumnet_satisfaction`, `g_quantumnet_production`, `g_quantumnet_demand`, `g_quantumnet_imported`, `g_quantumnet_exported`, `g_quantumnet_unmet_demand`, `g_quantumnet_producer_count`, `g_quantumnet_socioeconomic_factor`, `g_quantumnet_cost_monthly`.

## Differenze rispetto alla versione Lua

- **Nessun addebito multiplo**: in Lua ogni chiamata a `run_quantumnet_balance_variables()` (anche dalle condizioni degli advisor, più volte al mese) ripeteva lo scambio e il pagamento. Ora il saldo avviene una volta al mese; la funzione Lua di compatibilità restituisce solo i valori dell'ultimo calcolo.
- **Consumatori definiti**: `quantumnet_consumer_classes` non era definita in nessun file Lua; ora i consumatori sono configurati in `[Consumers]`.
- **ID reali**: i segnaposto `XXXXXXXX` sono sostituiti dagli OG_ID `0xBB340000` / `0xBB340001` (modificabili nell'INI).
- **Senza sc4-regional-supply-demand** la DLL non va in errore: lavora solo in locale.
- **Ricompense native**: lo sblocco avviene tramite il Civic Building Simulator del gioco invece dei record `create_reward_cityplanning`; il testo di blocco è nell'INI.

Il calcolo è stato verificato confrontando i risultati della DLL con quelli di `QUANTUMNET.lua` eseguito in un interprete Lua con variabili di gioco simulate.

## Compilazione

Serve Visual Studio 2022 (o Build Tools) e CMake ≥ 3.20. SimCity 4 è a 32 bit:

```bat
git clone --recurse-submodules -b staging https://github.com/UlisseWolf/SC4-QuantumNet
cd SC4-QuantumNet
cmake -S . -B build -A Win32
cmake --build build --config Release
ctest --test-dir build -C Release
```

La GitHub Action **Build** compila la DLL a ogni push e la pubblica come artifact. I test della simulazione girano anche su Linux (`cmake -S . -B build && cmake --build build && ctest --test-dir build`).

## Struttura

```
src/QuantumNetSimulation.*     logica di simulazione (indipendente da SC4, testata)
src/QuantumNetConfig.*         lettura di SC4QuantumNet.ini
src/RegionalSupplyLuaBridge.*  ponte verso sc4-regional-supply-demand
src/QuantumNetDllDirector.cpp  integrazione con SimCity 4 (messaggi, ricompense, Lua)
config/SC4QuantumNet.ini       configurazione di esempio
tests/                         test della simulazione
vendor/gzcom-dll               SDK per DLL di SimCity 4 (submodule)
```

## Note e limiti noti

- Come nella versione Lua, il contributo della città al pool regionale riparte da zero a ogni caricamento: se il mod regionale conserva i valori tra una sessione e l'altra, quelli della sessione precedente non vengono ritirati.
- `ResourceID` (`0x514E4554`, "QNET") è ancora un segnaposto: deve coincidere con l'ID registrato da sc4-regional-supply-demand.
- La lettura della popolazione per fascia di ricchezza somma i residenti dei singoli lotti; può differire di poco dai valori `game.g_city_r*_population` mostrati dagli advisor.

## Crediti

- NEON ENGINE: RobertaME / Ulisse Wolf
- [gzcom-dll](https://github.com/nsgomez/gzcom-dll): SDK per DLL di SimCity 4 (LGPL 2.1)
