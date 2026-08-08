# Architecture Decisions

> Format per `CLAUDE.md` (Softwarearchitekt-Modus), Abschnitt 2. Entries
> marked "[zu treffen — Empfehlung siehe unten]" are proposals awaiting
> ijon's decision, not finalized. Newest first once more than one entry
> exists.

---

### AD-001: SW0 — Software-Basis für die Maschinen-Software (Z1)

- **Kontext:** Z1 umfasst die gesamte Software, die auf/für die Maschine
  selbst läuft: Touchscreen-UI, RPi-seitige Steuerung, Firmware-Anbindung.
  Reframed (Chat 2026-07-26): Z2 (VM-Post-Processing) und Z3
  (VM-Web-Portal) haben **keinen Wiederverwendungs-Kandidaten** aus den
  fünf Alt-Repos — `alexandria` (16 Commits, 2020-10-02 bis 2020-10-20)
  entstand, **bevor** die VM-Idee überhaupt existierte (frühestens
  2026-07-01), war also als in sich geschlossenes Ein-Geräte-System
  gedacht, nicht aufgeteilt in Maschine+VM. SW0 reduziert sich damit auf
  eine einzige Frage: Womit bauen wir die Maschinen-Software?
  - ~~**Hardware-Entscheidung (ijon, 2026-07-26): BMP180-Drucksensor
    wandert an den RPi (direkt), Arduino gibt ihn ab.**~~ **Zurückgenommen
    (ijon, 2026-08-01): BMP180 bleibt am Arduino.** Begründung: der
    Arduino hat sonst kaum Aufgaben (nur Relais/BOX-LIGHT-FLIP-Protokoll,
    s.u.), eine Umverkabelung würde sich kaum lohnen — und physisch ist
    er ohnehin schon dort angeschlossen (A4/A5, seit 2026-07-25), nie
    umgesteckt worden. **Für die PM-Seite ergibt sich daraus nichts
    nachzutragen** — der dortige Stand (`project-management/todo.md`,
    Eintrag "Drucksensor (BMP180, A8) angeschlossen") beschrieb ohnehin
    nur die physische Verkabelung, nie einen Umzugsplan; die
    ursprüngliche "für PM-Seite nachzutragen"-Notiz unten war also
    gegenstandslos, bevor sie umgesetzt wurde. MKS-ESP32-FOC-Treiber
    bleibt unverändert direkt am RPi, davon nicht betroffen. **Neue
    Konsequenz für die Pickup-Erkennung** (die exakt die Begründung
    unten war, RPi-direkt zu wählen): die Cross-Device-
    Korrelationsfrage, die durch RPi-direkt vermieden werden sollte, ist
    damit wieder da — gelöst über ein Zwei-Modi-Protokoll (`PRESS?`
    Einzelmessung, `PRESS START`/`STOP` kontinuierliches Streaming, RPi
    entscheidet je nach Bedarf) statt eines eigenen I2C-Treibers am RPi.
    Volle Protokoll-/Client-Spezifikation: `monospace.md` §5/§6,
    `sans-serif.md` §1.2 (inkl. Korrelations-Hinweis dort). Nebeneffekt:
    der in `monospace` bereits vorbereitete SFE_BMP180-Treiber (unten als
    "wird ungenutzt" abgeschrieben) wird jetzt doch weiterverwendet —
    passt zu ijons Wiederverwendungs-Präferenz (`CLAUDE.md` Abschnitt
    2.2). Offene Punkte (BMP180-Abtastrate, Sample-Anzahl pro
    gemittelter Messung, erreichbare Streaming-Rate) sind unten in
    `monospace.md` §6 als empirisch zu klärend vermerkt, nicht hier
    dupliziert.
  - *(Ursprüngliche, jetzt zurückgenommene Begründung, zur Historie
    stehengelassen):* War aktuell am Arduino (physisch so verkabelt seit
    2026-07-25); hrmny hatte RPi-direkt vorgeschlagen, ijon war es egal
    — mit der software-architektonischen Begründung unten damals final
    so entschieden. MKS-ESP32-FOC-Treiber hängt weiterhin direkt am RPi.
  - **Begründung (Timing-Korrelation der Pickup-Fehlererkennung):**
    Die Pickup-Fehlererkennung läuft auf **jeder einzelnen Seite
    während des gesamten Auto-Scan-Loops** (`scan-process.md`, Schritt
    8.9: Box fährt auf 30% der Seitenbreite, vergleicht Luftdruck mit
    Ausgangswert). Diese Prüfung korreliert zwei Messwerte, die
    zeitlich eng zusammenpassen müssen: Box-Position (vom FOC-Treiber)
    und Luftdruck. ~~bereits RPi-direkt~~ — **Korrektur (Claude,
    2026-07-26, nach Prüfung von `project-management/backlog.md`
    P1b und `project-management/planning/todo.md`): das war
    faktisch falsch, nicht "bereits" entschieden.** Die Verkabelung
    des MKS-ESP32-FOC-Boards war laut PM-Stand offen — zwei Optionen
    ohne bisherige Präferenz: **(A)** über den Arduino als Hub, oder
    **(B)** direkt an den RPi. **Jetzt entschieden (ijon, 2026-07-26):
    Option (B), direkt an den RPi.** Damit gilt die Begründung unten
    (keine Cross-Device-Korrelation nötig) uneingeschränkt — Box-
    Position und Luftdruck laufen beide über RPi-direkte Verbindungen,
    kein Arduino-Relay dazwischen. **Zusätzlich (ijon, 2026-07-26): das
    MKS-ESP32-FOC-Board hat noch keine eigene Firmware** — SimpleFOC-
    Konfiguration steht noch aus (deckt sich mit
    `project-management/planning/todo.md`: "SimpleFOC-Konfiguration
    für das MKS-ESP32-FOC-Board (Motor-Kalibrierung, Torque-Control-
    Modus)", unerledigt). Damit ist auch das serielle Protokoll
    RPi↔FOC-Board **nicht vorgegeben** — es muss zusammen mit der
    Firmware neu entworfen werden, ähnlich wie `monospace` +
    `sans-core::hardware` seinerzeit als zusammenpassendes Paar
    entstanden. **Für PM-Seite nachzutragen** (analog zur BMP180-
    Verkabelung oben): diese A/B-Entscheidung gehört in
    `project-management/backlog.md`/`todo.md` (P1b) eingetragen —
    trage ich hier nicht selbst ein, PM-Modus-Territorium. Ursprüngliche
    Begründung: Mit Sensor am Arduino müsste der RPi die Korrelation auf jeder
    Seite über zwei unabhängig getaktete serielle Verbindungen
    herstellen — eine wiederkehrende Fehlerquelle. Direkt am RPi liest
    derselbe Prozess, der auch die Motorposition kommandiert, den Druck
    mit — keine Cross-Device-Korrelation nötig. Kosten: `monospace`s
    fertiger SFE_BMP180-Treiber wird ungenutzt, RPi-seitig braucht es
    einen eigenen, aber einfachen I2C-Treiber (`smbus2`+BMP180-Auslesung
    ist Standard-RPi-Python-Terrain, geringer Aufwand). **Korrektur
    (ijon, 2026-08-01): dieser Kostenpunkt gilt nicht mehr** — BMP180
    bleibt am Arduino (Rücknahme oben), der SFE_BMP180-Treiber wird also
    doch weiterverwendet, kein neuer RPi-seitiger I2C-Treiber nötig.
    Betrifft nur den BMP180-Teil dieser Begründung — die RPi-direkt-
    Entscheidung fürs FOC-Board oben bleibt unverändert gültig. Arduino bleibt
    laut P1b-Endstand (2026-07-21) weiterhin zuständig für Relais und
    Pneumatik-Sicherheitssensor. **Korrektur (2026-07-29): der Endstop
    zählt hier nicht mehr dazu** — er wanderte am 2026-07-28 vom Arduino
    zum ESP32-FOC-Board (`project-management/planning/todo.md`, M2;
    `tasks.md` T20/T32), nach dieser AD-001-Eintragung entschieden. Alle
    weiteren "Relais/Endstop/Pneumatik"-Aufzählungen weiter unten in
    diesem Eintrag sind entsprechend zu lesen. **Weitere Korrektur
    (ijon, 2026-07-31, siehe AD-006):** der Pneumatik-Sicherheitssensor
    war hier zu fest als bereits zugewiesene Arduino-Zuständigkeit
    dargestellt — tatsächlich ist er optional, nur relevant bei einem
    noch nicht beschlossenen Pneumatik-Umbau. Aktuell nicht im
    Arduino-Protokoll (`monospace.md`). **Final (ijon, 2026-08-01): für
    jetzt fallengelassen, keine Priorität.** Der Umbau (Trennfächer-/
    Blower-Fan ersetzen durch Druckluft-Düsen + Magnetventile) wird für
    die MVP-Phase nicht verfolgt — Ziel ist erstmal, die Maschine mit den
    vorhandenen Fans/Blowern zum Laufen zu bringen. Der Sensor (ein
    separates analoges Bauteil mit eigenem I2C-ADC, z.B. ADS1115 — nicht
    zu verwechseln mit dem BMP180, der als eigenständiger I2C-Sensor
    innerhalb der Saugbox sitzt und unverändert am Arduino bleibt, s.o.)
    ist damit aus allen MVP-Zielzustandsdokumenten entfernt
    (`monospace.md`, `sans-serif.md`, `tasks.md`,
    `reference/z1-components.md`). Kann bei Bedarf wieder aufgenommen
    werden, falls der Pneumatik-Umbau tatsächlich beschlossen wird — dann
    hier und in den Zielzustandsdokumenten neu einzutragen, nicht aus
    dieser Historie zu rekonstruieren.
- **Alternativen:**
  - **(a) `alexandria` (Node.js/Moleculer/NATS + React/Preact-Frontend)**
    komplett übernehmen und anpassen.
  - **(b) `sans` + `serif` + `monospace`** (Rust-Backend + Ember-Frontend
    + Arduino-Firmware) komplett übernehmen und anpassen.
  - **(c) Neubau**, ggf. mit gezielter Teilübernahme aus (a) oder (b).
- **Bestandsaufnahme, was für die *tatsächlich noch offenen* Z1-Teile
  wiederverwendbar ist** (nicht die generelle Vollständigkeits-Einschätzung
  aus `docs/software/code-review.md`, sondern zugeschnitten auf das, was
  nach der MVP-Abspeckung noch zu bauen ist — Kalibrierungs-Setup mit
  Tap-Interaktion, Live-Vorschau, Seitenzahl-Crop+tesseract,
  Barcode-Bildverarbeitung, FOC-Serial, direktes I2C zum BMP180):

  | Baustein | `alexandria` | `sans`/`serif`/`monospace` |
  |---|---|---|
  | Kamera-Aufnahme (2× UVC) | **echt, funktioniert** (`services/camera`, v4l2/node-webcam) | **echt, funktioniert** (`sans-core::camera`, v4l2/rscam), aber `capture_video`/Liveview nur Stub |
  | Serieller Arduino-Protokoll-Handler (Relais/Pneumatik-Sensor — Endstop inzwischen am ESP32, s.o.) | **existiert nicht** — alexandria ging nie von einem Arduino-in-the-loop aus | **Korrektur (2026-07-29, nach vollständiger Code-Lektüre, siehe `reference/sans-code-reusability-review.md`): passt zu `monospace`, aber nur für ein grobes BOX/LIGHT/FLIP-Protokoll** — kein granulares Vakuum/Fan/Blower/Licht-Auto/Drucksensor-Kommando existiert, `FlipPage()` fährt den kompletten Ablauf autonom auf dem Arduino, unlösbar an die alte direkte Schrittmotor-Ansteuerung gebunden. Wiederverwendbar sind nur die Relais-Pin-/Polaritäts-Fakten und das generische Byte-Stream-Framing, nicht das Kommando-Vokabular. |
  | I2C zum BMP180 direkt vom RPi | **leere Datei** (`services/i2c`, 0 Byte) | **existiert nicht** in `sans` (BMP180-Handling war immer Arduino-seitig in `monospace`/SFE_BMP180) — mit der neuen Direct-RPi-Verkabelung wird das ohnehin für beide Seiten Neuland |
  | Serial zum MKS-ESP32-FOC-Board | existiert nirgendwo (Board gibt's erst seit P1, 2026-07-04) | existiert nirgendwo |
  | Motor-Sequenzierung (Prozent-Wegpunkte, Pickup-Erkennung) | nicht vorhanden | **als Konzept/Referenz in `docs/process/scan-process.md` wertvoll**, aber die *Arduino-Implementierung* in `monospace` steuerte den alten Stepper-Treiber direkt — mit FOC-Board+RPi-Serial überholt, nicht 1:1 übernehmbar |
  | Touchscreen-UI-Grundgerüst | **leere Seiten** (`jobs.jsx`/`export.jsx`/`settings.jsx`/`root.jsx` sind nur `<section><legend>`), keine der neuen Interaktionen (Tap-Kalibrierung, Live-Vorschau, Barcode-Flow) vorhanden | **funktionierender 4-Schritte-Tutorial-Flow** (Ember.js, getestet) — aber ebenfalls keine der neuen Interaktionen, keine Backend-Anbindung, unbehobener ISBN-Copy-Paste-Bug (aktuell unerreichbar, aber vorhanden) |
  | Config-Handling | vorhanden (`.env`-Mixin) | **vollständig, sauber** (`sans-core::config`, TOML) |
  | Architektur-Fit fürs Zielbild (ein Gerät, ein Job gleichzeitig) | **Microservices + NATS-Message-Broker** — Betriebsaufwand (Broker-Prozess, Service-Discovery) für ein Einzelgerät mit einem Job auf einmal vermutlich unnötige Komplexität | einfachere direkte Module (Rust-Crates), kein Broker — passt strukturell besser zu "ein Gerät, ein Job" |

- **Zwischenfazit (Beobachtung, keine Vorgabe) — korrigiert 2026-07-29
  nach vollständiger Code-Lektüre von `sans`
  (`reference/sans-code-reusability-review.md`):** Für die *tatsächlich
  neu zu bauenden* Teile (Kalibrierungs-UI mit Tap-Interaktion,
  FOC-Serial, direktes I2C, Seitenzahl-Crop+tesseract, Barcode-Erkennung)
  hilft **keines** der beiden Sets nennenswert — beide sind dafür
  gleich weit von "fertig" entfernt. **Auch der zuvor als "einziger
  echter, sofort einsetzbarer Gewinn" gelistete Punkt hält nicht ganz:**
  `monospace` (Firmware) + `sans-core::hardware` (Serial-Protokoll) sind
  zwar zueinander kompatibel, aber nur für ein grobes Drei-Kommando-
  Protokoll (Box hoch/runter, Licht an/aus, ein autonomer, monolithischer
  "FlipPage"-Ablauf) — kein granulares Relais/Pneumatik-Sensor-Kommando
  existiert, wie es für die neue Architektur gebraucht wird. Echter
  Gewinn bleiben nur die Relais-Pin-/Polaritäts-Fakten und das generische
  Byte-Stream-Framing aus `sans-core::hardware`, nicht das fertige
  Protokoll selbst. `alexandria` bietet dafür keinen
  Gegenwert, weil es nie mit einem Arduino kommuniziert hat.
- **Entscheidung (ijon, 2026-07-26 — Tendenz, wie von ijon selbst
  formuliert ["ich tendiere dazu"], hier als Stand festgehalten, aber
  nicht als unumstößlich final markiert):**
  - **`alexandria` komplett verworfen.** Kein Code, kein Muster, keine
    Infrastruktur davon wird übernommen — deckt sich mit der
    Bestandsaufnahme oben (Microservices/NATS-Overhead passt strukturell
    nicht, kein Baustein bietet einen Vorsprung, den `sans`/`monospace`
    nicht auch hätten).
  - **`monospace` bleibt Basis für den Arduino-Teil** — aber nur für das,
    was der Arduino nach P1b tatsächlich noch behält (Relais, BMP180,
    BOX/LIGHT/FLIP-Protokoll — Endstop inzwischen am ESP32, s.o.;
    Pneumatik-Sicherheitssensor **gestrichen, 2026-08-01, s.o.**). Die alte
    Stepper-Motor-Ansteuerung in `monospace` wird **nicht** mitgenommen
    — die ist durch das FOC-Board (T20, eigenes neues Protokoll)
    ohnehin überholt, wie in der Bestandsaufnahme oben schon
    festgehalten ("nicht 1:1 übernehmbar").
  - **Teile von `sans` als Basis für die neue RPi-App** — konkret die
    beiden bereits als "echt, funktioniert" bewerteten Module
    `sans-core::hardware` (Serial-Client, kompatibel zu `monospace`)
    und `sans-core::config` (TOML, vollständig, sauber). **Löst damit
    auch den zweiten offenen Unterpunkt unten (Sprach-/Framework-Wahl):
    Rust**, weil `sans-core` Rust ist. Die übrigen `sans`-Crates
    (`sans-server`, `sans-worker`, `sans-ctrl`, `sans-types`,
    `sans-processing`) bleiben laut Bestandsaufnahme oben Stubs/
    unvollständig — "Teile von sans" heißt also konkret diese zwei
    Module, nicht den ganzen Workspace unverändert übernehmen.
  - **`serif` wird entkernt und durch eine neue, passende Rust-UI
    ersetzt, behält aber den Namen `serif`.** D.h. der bestehende
    Ember.js-Code wird nicht als Code weiterverwendet (Sprachwechsel zu
    Rust, konsistent mit dem `sans`-Teil oben) — nur die
    Positionierung/der Name bleibt. **Hinweis, nicht Einwand:** die
    bestehende `serif`-Historie enthält laut
    `docs/project/target-state-requirements.md` (Transparenz-Ausnahme)
    zwei Mitwirkende, die bei künftiger Veröffentlichung nicht über
    Git-Metadaten sichtbar werden dürfen (`docs/persona-non-grata.md`)
    — wie das mit "entkernen, aber denselben Namen/Repo behalten" genau
    zusammengeht (bestehende Historie fortführen vs. sauberer Neuanfang
    unter demselben Namen), ist eine Frage der Umsetzungsmechanik zum
    Zeitpunkt der Umsetzung, keine Software-Architektur-Entscheidung —
    hier nur als Erinnerung/Querverweis vermerkt, nicht selbst
    entschieden (PM-/Archiv-Territorium, CLAUDE.md Abschnitt 6).
- **Noch zu klären:**
  1. ~~BMP180-Verkabelung~~ — **entschieden (ijon, 2026-07-26): RPi-direkt.**
  2. ~~Sprach-/Framework-Wahl für die neue RPi-App~~ — **entschieden:
     Rust** (Konsequenz aus der `sans-core`-Entscheidung oben).
- **Korrektur zur eigenen Arbeitsweise (Claude, 2026-07-26, nach
  Rückfrage von ijon):** SW0 blockiert **nicht** die weitere
  Milestone-/Feature-/Aufgaben-Definition für Z1. Sprach-/Framework-Wahl
  ist laut `CLAUDE.md` Abschnitt 2.4 ein **Implementierungsdetail**, das
  erst *nach* der Komponenten-/Schnittstellen-Zerlegung geplant wird,
  nicht davor — AD-002 und AD-003 belegen das bereits praktisch: alle
  dortigen Aufgaben (T1–T14) sind auf Verhaltens-/Schnittstellenebene
  formuliert, ohne Bezug zu `alexandria`/`sans`/`serif`/`monospace`, und
  bleiben unabhängig vom SW0-Ausgang gültig. Tatsächlich von SW0
  abhängig ist nur die *Wiederverwendungs*-Frage für konkrete
  Code-Bausteine — laut Bestandsaufnahme oben im Kern nur der serielle
  Arduino-Protokoll-Handler (`monospace`+`sans-core::hardware`, bereits
  kompatibles Paar). Weitere Aufgaben werden entsprechend so
  formuliert, dass sie SW0-unabhängig planbar sind; einzelne Aufgaben
  bekommen einen expliziten "hängt an SW0"-Vermerk nur dort, wo die
  Wiederverwendungsfrage die Aufgabe selbst tatsächlich verändert.
- **Quelle:** `docs/software/code-review.md`; `docs/hardware/electronics.md`;
  `project-management/decisions.md` (P1b, 2026-07-21); Chat 2026-07-26.
  **Nachtrag (2026-07-29):** `sans` vollständig gegengelesen (nicht nur
  `code-review.md`, das laut ijon nur ~ein Viertel des Codes abdeckt) —
  Befunde und Korrekturen an den obigen Wiederverwendungs-Einschätzungen:
  [`reference/sans-code-reusability-review.md`](reference/sans-code-reusability-review.md).

---

### AD-002: Nachscan-Flow für defekte Einzelseiten (Z1/Z2/Z3)

- **Kontext:** Einzelne Seitenfotos eines bereits fertig gescannten Buchs
  können defekt sein (Lichtreflexion, Staub, Dreck, Unschärfe). Gewünschter
  Ablauf: Nutzer markiert eine defekte Seite im Web-Portal (Z3); sobald er
  wieder am Gerät ist und das Buch zur Hand hat, lädt er das Projekt an der
  Maschine (Z1) neu, die ihn auffordert, die markierten Seiten aufzuschlagen;
  die neuen Aufnahmen ersetzen die alten im Archiv (Z2). Vollständige
  Herleitung, Datenmodell-Vorschlag und offene Rückfrage:
  [`reference/rescan-flow.md`](reference/rescan-flow.md).
- **Bestandsaufnahme (Wiederverwendung):** kein Code in `alexandria`/
  `sans`/`serif`/`monospace` deckt irgendeinen Teil davon ab — für Z2/Z3
  gab es beim Schreiben dieser Repos noch kein VM-Konzept (siehe AD-001),
  für Z1 kommt die einzige relevante Vorarbeit aus einer nie umgesetzten
  Ideenliste von 2019 (`docs/software/webviews-ui-spec.md`): eine
  On-Device-Job-Übersicht ("Übersichtsliste der bereits gescannten Bücher
  – auf dem Gerät") und ein manueller Einzelbild-Auslöser ("Manual
  scan-view … Button 'shoot picture'") — beide als Konzept direkt
  wiederverwendbar für diesen Flow, aber nirgendwo implementiert.
- **Entscheidung (ijon, 2026-07-26):**
  1. **Nicht Teil des MVP** — spätere Ausbaustufe (v2). Der Ablauf
     braucht aber schon **jetzt, im MVP-Bau**, architektonische
     Vorbereitung, damit er später ohne Datenformat-Migration ergänzt
     werden kann (siehe Konsequenzen unten und `reference/rescan-flow.md`
     §4).
  2. **Projekt-Auswahl an der Maschine:** On-Device-Liste am Touchscreen
     (nicht Web-Portal-Trigger, nicht Job-ID/Barcode-Eingabe) — der
     Nutzer wählt am Gerät selbst, welches Buch er wieder auflegt.
  3. **Seiten-Ansage:** ausschließlich die erkannte, gedruckte
     Seitenzahl ("Seite 47 aufschlagen"). **Keine** Sequenzposition als
     Fallback ("die 23. Doppelseite") — laut ijon für den Nutzer beim
     Blättern nicht nachvollziehbar und damit nutzlos.
- **Alternativen (verworfen):**
  - Web-Portal löst den Nachscan-Modus aus, Maschine übernimmt automatisch
    die letzte Anfrage — verworfen, weil die Maschine kein Sitzungskonzept
    zum Portal hat und mehrere c-base-Crew-Mitglieder das Gerät nutzen;
    eine Liste am Gerät macht offene Punkte sichtbar, statt sie über eine
    Fernauslösung zu verstecken.
  - Eindeutige Job-ID/Barcode-Eingabe am Touchscreen — verworfen, mehr
    Reibung für den Nutzer, kein "hier ist noch was offen"-Hinweis wie
    bei einer Liste.
  - Sequenzposition als Seiten-Ansage ("Kte Doppelseite von vorne") —
    ausdrücklich von ijon verworfen, s.o.
- **Konsequenzen:**
  - MVP muss beim Bau von Z1 (Kamera-Pipeline) und Z2 (Archiv-Schreiber)
    bereits so ausgelegt werden, dass (a) jede Seite eine über die Zeit
    stabile Datei-Identität hat (fortlaufende `sequence_number` als
    Dateiname, links=gerade/rechts=ungerade statt separatem Seiten-Feld —
    korrigiert 2026-07-26, nicht mehr Spread-Index+Seite, siehe
    `reference/rescan-flow.md` §4.1 — nicht Upload-Reihenfolge/
    Zeitstempel), (b) die pro Seite erkannte Seitenzahl
    dauerhaft in der Metadaten-Datei landet (nicht nur transient für die
    Vollständigkeitsprüfung verwendet wird), (c) die pro Buch kalibrierten
    Parameter (Seitenbreite, Seitenzahl-Crop-Rechtecke links/rechts) Teil
    des persistierten Metadaten-Datensatzes werden, nicht nur RPi-seitiger
    Laufzeitzustand bleiben. Details, JSON-Schema-Vorschlag und
    Aufgabenzuschnitt: [`reference/rescan-flow.md`](reference/rescan-flow.md)
    §4/§5, Aufgaben in [`tasks.md`](tasks.md).
  - **Geklärt (ijon, 2026-07-26):** was die Maschine ansagt, wenn die
    defekt markierte Seite auch von der OCR nicht erkannt wurde —
    Schätzung aus den `sequence_number`/`recognized_page_number` der
    Nachbarseiten plus Vorschaubild zum Abgleich. Das interne Speichern/
    Verwenden der Sequenzposition ist dabei ausdrücklich in Ordnung —
    verworfen war nur ihre direkte **Ansage** an den Nutzer (Punkt 3
    oben), nicht ihre interne Nutzung. Details:
    [`reference/rescan-flow.md`](reference/rescan-flow.md) §3.
- **Quelle:** Chat 2026-07-26 (dieses Gespräch); `docs/software/
  webviews-ui-spec.md`; `software-planning/reference/
  process-assumptions-audit.md`; `software-planning/reference/
  z1-z3-triage-overview.md`.

---

### AD-003: MVP-Übernahme aus `scan-process.md`/`webviews-ui-spec.md` + Not-Stop-Recovery und Live-Druckdiagnose

- **Kontext:** `docs/process/scan-process.md` (2016–2019-Zustandsautomat)
  und `docs/software/webviews-ui-spec.md` (2019-Ideenliste) enthalten
  Prozess-/UI-Detail, das nie gegen den aktuellen MVP-Zuschnitt
  (AD-001, `z1-z3-triage-overview.md`) geprüft wurde. Vollständige
  Triage (Übernehmen/Mitdenken/Fallenlassen, Punkt für Punkt):
  [`reference/legacy-process-docs-triage.md`](reference/legacy-process-docs-triage.md).
  Diese Durchsicht deckte außerdem eine Lücke im bestehenden
  Pickup-Erkennungs-Design auf (nur Boolean gespeichert, kein Rohwert)
  — daraus entstand zusätzlich der Wunsch nach Live-Druckdiagnose.
- **Bestandsaufnahme (Wiederverwendung):** kein neuer Code nötig für die
  hier getroffenen Entscheidungen — die Druck-Baseline-Messung
  existiert bereits als Konzept in `scan-process.md` (Schritt 8.1/8.9)
  und wird für die MVP-Kalibrierung ohnehin gebraucht
  (`process-assumptions-audit.md`). **Korrektur (2026-07-29):** die
  "Box ohne Saug-/Blas-Nebenwirkungen bewegen"-Formulierung hier bezog
  sich fälschlich mit auf Schritt 1.1 (Power-on-Homing) — Homing,
  Move-to-Top (T9) und die Kalibrierungs-Auffahrt (T27) sind drei
  unterschiedliche Bewegungsprimitive, nicht dieselbe (siehe
  `reference/legacy-process-docs-triage.md` für die volle Korrektur).
  Move-to-Top selbst bleibt trotzdem keine neue Maschinen-Fähigkeit —
  nur kein Homing-Wiederverwendungsfall.
- **Entscheidung (ijon, 2026-07-26):**
  1. **Move-to-Top-Button als expliziter Not-Stop-Recovery-Schritt,
     Priorität vor Move-Down.** Nach einem Stopp bleibt die Box
     irgendwo mitten im Zyklus stehen (ggf. Vakuum/Bläser gerade aktiv
     gewesen); der Nutzer muss die Box kontrolliert zur oberen
     Endlage zurückfahren können, **ohne** dass dabei automatisch die
     normale Saug-/Blas-Sequenz erneut anläuft, damit er das Buch dazwischen
     prüfen/befreien kann.
  2. **Move-Down-Jog-Control vorhanden, aber niedriger priorisiert** als
     Move-to-Top.
  3. **Live-Druckdiagnose direkt im Scan-Bildschirm** (nicht als
     separater Debug-Screen): aktueller Umgebungsdruck (Messung ohne
     laufende Saugpumpe, wiederverwendet aus dem ohnehin vorhandenen
     Baseline-Schritt) sowie die mBar-Differenzwerte der letzten drei
     Umblättervorgänge. Zweck laut ijon: Entwicklungs-Werkzeug, um
     bessere Schwellwerte für "Pickup Success"/"Pickup Failure"
     herzuleiten, statt des bisher nie empirisch geprüften
     "preset amount" aus `scan-process.md` 8.9.
- **Alternativen (verworfen):** separater Debug-Screen für die
  Druckdiagnose statt Integration in den Scan-Bildschirm — verworfen
  (ijon, 2026-07-26), weil die Werte live mit dem tatsächlichen
  Maschinenverhalten korreliert werden sollen, nicht nachträglich auf
  einem separaten Screen nachgeschaut.
- **Konsequenzen:**
  - MVP-Motorsteuerung (Z1) braucht die Move-to-Top-Aktion als
    eigenständig auslösbare, von Auto-Scan entkoppelte Funktion —
    keine automatische Wiederaufnahme des Scans danach.
  - Für die Druck-Diagnose zusätzlich **bestätigt (ijon, 2026-07-26)**:
    ein separates Persistenz-Log (`pressure-log.jsonl`) pro Scan-Job,
    getrennt von der AD-002-Metadaten-Datei (andere Konsumenten/
    Lebenszyklus — Analyse über viele Bücher hinweg zur
    Schwellwert-Herleitung, nicht Buch-/Rescan-Daten). Format-Vorschlag
    und Begründung:
    [`reference/legacy-process-docs-triage.md`](reference/legacy-process-docs-triage.md).
  - Weitere Empfehlungen aus derselben Triage, ebenfalls **bestätigt
    (ijon, 2026-07-26)**: Zoom-Live-Vorschau (100%-Crop neben
    Gesamt-Vorschau) und der Pickup-Failure-Abbruch nach 3
    Fehlversuchen (Übernahme aus `scan-process.md` 8.2).
  - Aufgabenzuschnitt: [`tasks.md`](tasks.md), T9–T14.
- **Quelle:** Chat 2026-07-26 (dieses Gespräch); `docs/process/
  scan-process.md`; `docs/software/webviews-ui-spec.md`.

---

### AD-004: Z1-Komponentenzerlegung

- **Kontext:** Z1 (RPi-Steuerung + Touchscreen-UI + Firmware-Anbindung)
  wurde bisher nur über verstreute Einzelentscheidungen (AD-002, AD-003)
  bearbeitet, ohne dass die zugrundeliegende Komponentenstruktur selbst
  festgehalten war — T1–T14 gehören zu unterschiedlichen Komponenten,
  aber diese Zuordnung stand nirgends. CLAUDE.md 2.4 verlangt die
  Komponenten-/Schnittstellenzerlegung *vor* Implementierungsdetails;
  SW0 (Sprach-/Framework-Wahl) war zum Zeitpunkt dieses Eintrags bewusst
  offen gehalten (AD-001-Korrektur — inzwischen entschieden: Rust, siehe
  AD-001 oben) — diese Zerlegung ist davon ohnehin unabhängig formuliert,
  das bleibt auch nach der SW0-Entscheidung gültig. Vollständige
  Komponenten-Steckbriefe (Zuständigkeit/Nicht-Zuständigkeit/
  Schnittstelle/Wiederverwendung je Komponente), Datenfluss-Übersicht
  und Zuordnung bestehender Aufgaben:
  [`reference/z1-components.md`](reference/z1-components.md).
- **Bestandsaufnahme (Wiederverwendung):** siehe Referenzdokument, je
  Komponente einzeln aus der AD-001-Tabelle zugeordnet — zusammengefasst:
  Kamera-Einzelbild-Aufnahme und der Arduino-Serial-Handler haben echte
  Wiederverwendungssubstanz; Motor-Sequenzierung nur als Konzept
  (Arduino-Implementierung überholt durch FOC-Board); alles andere
  (Kalibrierungs-UI, Crop+Scale-Vorschau-Pipeline, Page-Number-
  Recognition-Service, Metadaten-Flow, Job/Archiv-Manager) hat keinen
  Wiederverwendungs-
  Kandidaten, komplett neu.
- **Entscheidung:** acht Komponenten, entlang der natürlichen
  Prozess-Stufen geschnitten: (1) Hardware Abstraction Layer, (2)
  Camera Capture & Preview Service, (3) Page-Number Recognition (OCR)
  Service, (4) Setup-Calibration Controller, (5) Auto-Scan
  Flip-Cycle Controller, (6) Metadata Capture Flow, (7) Job/Archive
  Manager, (8) Touchscreen UI Shell. UI Shell bewusst getrennt von den
  Controllern (reine Darstellung/Eingabe vs. Prozesslogik).
- **Alternativen (verworfen):**
  - Monolithischer Scan-Controller (Motor+Kamera+OCR+Archiv in einem
    Block) — verworfen, erschwert isoliertes Testen und die bereits
    identifizierte Wiederverwendung (Kalibrierung und der v2-Nachscan-
    Flow, AD-002, brauchen beide dieselbe "Einzelbild ohne
    Nebenwirkungen"-Grundfunktion; ein Monolith würde das duplizieren).
  - Trennung nur nach Hardware- vs. Software-Seite (Arduino-seitig vs.
    RPi-seitig) — verworfen, zu grob, zerlegt den eigentlich noch zu
    bauenden RPi-seitigen Großteil nicht in testbare Einheiten.
- **Konsequenzen:**
  - **Update 2026-07-26 (Abschluss der Zerlegung):** alle acht
    Komponenten sind inzwischen vollständig in Aufgaben zerlegt (T1–T31,
    siehe Referenzdokument, Abschnitt "Status: all 8 components broken
    into tasks"). Ursprünglich hier vermerkte Einzelpunkte (Offline-
    Fallback ISBN-Lookup) sind erledigt — für MVP wird durchgängig
    Internetverbindung vorausgesetzt, keine Fallback-Logik nötig.
  - **UI-Sprache entschieden (ijon, 2026-07-26): MVP englisch, später
    dual-language (Deutsch + Englisch).** Keine Lokalisierungs-
    Infrastruktur für MVP, aber T30/T31 sollten Strings nicht so
    hart-codieren, dass Deutsch später ein Rewrite wird.
  - Weiterhin offen: Upload-Fehlerverhalten im Job/Archive Manager
    (deckt sich mit Punkt #6 aus `reference/z1-z3-triage-overview.md`);
    wer die buchweite Lücken-/Konsistenzprüfung
    der Seitenzahlenfolge übernimmt (Vorschlag: Job/Archive Manager,
    nicht bestätigt).
- **Quelle:** Chat 2026-07-26 (dieses Gespräch); alle vorherigen Z1-
  bezogenen Referenzdokumente (`process-assumptions-audit.md`,
  `z1-z3-triage-overview.md`, `legacy-process-docs-triage.md`,
  `rescan-flow.md`).

---

### AD-005: Metadata Capture Flow — Cover-Fotos & LLM-gestützte Erkennung

- **Kontext:** Component 6 aus AD-004 (Metadata Capture Flow) war bisher
  nur grob umrissen (ISBN-Scan→Lookup→manuelles Formular). ijon
  erweitert das jetzt konkret: Vorder-/Rückcover-Fotos für ein
  Thumbnail im Web-Portal, dieselben Fotos als Input für eine
  bildverstehende LLM zur Metadaten-Erkennung/-Suche, und eine
  Datenbank-Abgleichspflicht auch für manuell eingegebene Werte.
  **Widerspricht/korrigiert eine frühere Einordnung:**
  `z1-z3-triage-overview.md` Punkt 7 hatte die Cover-Foto-an-LLM-Idee
  als "eher v2-Komfort, nicht MVP-kritisch" eingestuft — das gilt mit
  dieser Entscheidung nicht mehr, die Idee ist jetzt Teil des
  MVP-Ablaufs. Vollständige Ablaufbeschreibung, Fallback-Kette und
  offene Punkte: [`reference/z1-components.md`](reference/z1-components.md)
  §6.
- **Bestandsaufnahme (Wiederverwendung):** kein Code-Wiederverwendungs-
  Kandidat in den fünf Alt-Repos — auch die Cover-Foto/LLM-Idee war dort
  nie mehr als eine unimplementierte Notiz
  (`docs/project/target-state-requirements.md`). Externe Abhängigkeiten
  (Open Library/Google-Books-Suche, lokale AI-Hardware für die LLM-
  Inferenz) sind bereits an anderer Stelle als Infrastruktur
  entschieden, nicht neu.
- **Entscheidung (ijon, 2026-07-26):**
  1. Vorder-/Rückcover-Foto wird **immer** aufgenommen (Thumbnail-Zweck,
     unabhängig vom weiteren Ablauf).
  2. Fallback-Kette: ISBN-Barcode+Lookup → (falls nicht verfügbar/
     erfolglos) Cover-Fotos an bildverstehende LLM zur Erkennung+Suche
     → (falls auch das scheitert) manuelles Formular. **Korrektur
     (ijon, 2026-07-26): der ISBN-Barcode sitzt nicht zwingend auf dem
     Cover** — genauso oft auf einer der ersten/letzten Innenseiten des
     Buchs. Eigene, vom Cover-Foto getrennte Aufforderung an den
     Nutzer, danach zu schauen (Cover **und** Innenseiten); falls ISBN
     zwar gedruckt, aber kein Barcode sichtbar/lesbar ist, tippt der
     Nutzer die ISBN-Ziffern direkt per Bildschirmtastatur ein
     (leichtgewichtiger als das volle manuelle Formular). Details:
     [`reference/z1-components.md`](reference/z1-components.md) §6.
  3. **Auch die manuelle Eingabe wird nicht mehr blind übernommen** —
     Abgleich/Autovervollständigung gegen dieselben öffentlichen
     Buch-Datenbanken wie beim ISBN-Lookup, bevor sie als final gilt.
  4. Der DB-Lookup-Baustein wird als **eine gemeinsame** Fähigkeit mit
     zwei Abfragemodi (ISBN-exakt, Titel/Autor-Suche) entworfen, von
     drei Stellen im Ablauf wiederverwendet, statt dreimal separat
     implementiert.
- **Alternativen:** keine explizit verworfenen — ijons Vorgabe war
  bereits konkret genug, um direkt als Entscheidung übernommen zu
  werden; einzige von mir vorgeschlagene, noch unbestätigte
  Detail-Alternative ist die UI-Interaktion für Schritt 5 (Suche-nach-
  Eintippen statt Live-Autovervollständigung), siehe Referenzdokument.
- **Konsequenzen:**
  - Component 6 ist jetzt der umfangreichste, am wenigsten in Aufgaben
    zerlegte Baustein aus AD-004 — noch keine Tasks.
  - **Kamera-Geometrie für Cover-Fotografie gelöst (ijon, 2026-07-26),
    keine offene Hardware-Frage mehr:** Box fährt nach oben (dieselbe
    Move-to-Top-Primitive wie T9), Nutzer legt das **geschlossene**
    Buch manuell erst rechts (Vorderseite, rechte Kamera), dann links
    (Rückseite, linke Kamera) hin — sequenziell, nicht gleichzeitig.
    Löst damit auch den bisher offenen Punkt 2 aus
    `z1-z3-triage-overview.md` zum Rückcover-Barcode gleich mit (der
    ist ja im Cover-Foto aus Schritt 1 ohnehin schon enthalten).
  - **Offline-Verhalten beim Metadaten-Schritt — für MVP kein Thema
    (ijon, 2026-07-26):** MVP geht von vorhandener Internetverbindung
    aus, keine Fallback-Logik nötig. Gilt nur für diesen optionalen
    Anreicherungs-Flow, nicht für den Kern-Scan-Prozess (HAL/Auto-Scan),
    der weiterhin offline funktionieren muss.
- **Quelle:** Chat 2026-07-26 (dieses Gespräch); `docs/project/
  target-state-requirements.md`; `software-planning/reference/
  z1-z3-triage-overview.md` (Punkt 7, jetzt überholt).

---

### AD-006: Board-Zuordnung + RPi↔FOC-Board-Protokoll als G-Code

- **Kontext:** Drei Boards (MKS-ESP32-FOC, Arduino, RPi) hatten bisher
  keine eigene, saubere Spezifikationsdatei — Anforderungen lagen
  verteilt in `tasks.md`/`z1-components.md`/`esp32-foc-firmware-
  requirements.md`. Zusätzlich war das RPi↔FOC-Board-Wire-Protokoll seit
  T20/AD-001 als "SimpleFOC `Commander`-basiert, aber inhaltlich nicht
  festgelegt" offen. hrmny und ijon haben das Protokoll jetzt konkret
  festgelegt (Chat 2026-07-31): eine G-Code-Zeilensprache.
- **Entscheidung:**
  1. Drei eigenständige, historienfreie Zielzustands-Spezifikationen auf
     oberster Ebene (analog zu `tasks.md`/`architecture.md`), je eine pro
     Board: [`ligature.md`](ligature.md) (MKS ESP32 FOC),
     [`monospace.md`](monospace.md) (Arduino), [`sans-serif.md`](sans-serif.md)
     (Raspberry Pi, Backend+UI zusammen). `tasks.md` bleibt für
     Detail-Historie/Aufgabenschnitt bestehen, jetzt mit einer
     Board-Zuordnungstabelle am Anfang.
  2. **RPi↔FOC-Board-Protokoll ist G-Code**, nicht ein rohes
     `Commander`-Kommandoset. Basis-Vokabular von hrmny/ijon vorgegeben:
     `G28` (Homing), `G73` (Page-Turn-Completion-Move), `G0`/`G1`
     (schneller/langsamer Move), `M3`/`M5` (Job-Start/-Ende). Ergänzt
     (Claude, 2026-07-31, um die volle Firmware-Spec aus
     `esp32-foc-firmware-requirements.md` abzudecken): `G30`
     (Touchdown-and-Hold), `M24` (Resume-nach-Hold, hrmny: "fortsetzen"),
     `M40`/`M41`/`M42` (Kalibrierungs-Trigger), `M112` (Sofort-Stop,
     Konvention aus RepRap/Marlin übernommen), `?` (Status-Telemetrie,
     Konvention aus GRBL übernommen). **Kein Move-to-Top-Kommando, kein
     `G90`/`G91`** — beides bewusst verworfen (2026-07-31, nach
     Rückmeldung von hrmny/ijon), siehe Konsequenzen unten.
     **Parameter-Konvention (hrmny, 2026-07-31):** `F` ausschließlich
     Vorschub/Geschwindigkeit (mm/min), niemals Kraft/Drehmoment; `S`
     wird nirgends benutzt (reserviert für Spindeldrehzahl in
     Standard-G-Code, hier ohne Spindel irreführend). Neu eingeführt:
     `T` für Drehmoment/Kraft/Strom-Werte (Zielwert, Limit oder
     Messwert), `R` für die Retreat-Distanz von `G73` bzw. die
     Umdrehungszahl bei `M41` (Bedeutung lokal zum jeweiligen Befehl,
     wie Standard-G-Code es bei `R` in Canned Cycles auch handhabt).
     Vollständige Kommandotabelle inkl. Antwort-Framing
     (`ok`/`done`/`error`, unsolicited `fault`) und durchgerechnetes
     Beispiel eines vollständigen Jobs:
     [`ligature.md`](ligature.md) §4–§10, §14.
  3. **`G73` neu gefasst (2026-07-31, nach zwei Rückmeldungsrunden von
     ijon — korrigiert zwei zu enge Fassungen von Claude nacheinander:
     erst nur die kleine −10%-Wiggle-Bewegung, dann fälschlich erst
     *nach* einer separaten, abgeschlossenen Pickup-Prüfung gestartet):**
     `G73` deckt die **gesamte Aufwärtsbewegung** eines Page-Turn-
     Versuchs ab — gesendet direkt nach `M24`, ohne vorherige
     Prüf-Bewegung — bis zum Zielpunkt (105–120% der Seitenbreite),
     inklusive der eingebetteten Retreat-Bewegung (die eigentliche
     "Wiggle", Teil von `G73` statt eines separaten Aufrufs). **Nicht**
     enthalten: die anschließende Abwärtsbewegung zur nächsten Seite —
     die übernimmt der nächste `G30`-Aufruf (Touchdown), der ohnehin von
     der aktuellen Position absteigt.
     **Pickup-Prüfung (ijon, 2026-07-31): kein eigenes G-Code-Kommando
     nötig, das FOC-Board kennt "Pickup-Erfolg" gar nicht** — der RPi
     liest den Differenzdruck ~~(eigener Sensor, nicht am Board)~~
     **Korrektur (ijon, 2026-08-01, siehe AD-001): der Drucksensor bleibt
     doch am Arduino, nicht am RPi direkt** — der RPi bezieht den
     Differenzdruck während `G73` über ein kontinuierliches
     Streaming-Kommando vom Arduino (`monospace.md` §5/§6,
     `sans-serif.md` §1.2), nicht mehr über eine eigene I2C-Messung. Der
     Grundmechanismus unten (RPi überwacht selbst, FOC-Board weiß von
     nichts, Abbruch+frisches `G30` bei Fehlschlag) bleibt unverändert —
     nur die Quelle des Druckwerts ist jetzt eine zweite serielle
     Verbindung (Arduino) statt eines RPi-lokalen I2C-Reads, mit dem
     entsprechenden Korrelations-Hinweis in `sans-serif.md` §1.2. Läuft
     parallel während `G73` bereits läuft; bei Erfolg passiert nichts (die
     Bewegung läuft einfach weiter), bei Fehlschlag bricht der RPi die
     laufende `G73`-Bewegung ab und sendet danach ein frisches `G30` für
     den nächsten Versuch — unabhängig davon, wo genau der Abbruch die
     Box stehen lässt. **Korrektur (ijon, 2026-08-02, siehe `ligature.md`
     §3.8/§6.2):** der Abbruch hier ist **`M53`, nicht `M112`** — `M112`
     ist jetzt ausschließlich echter Not-Halt (entwaffnet, verriegelt
     `Fault`, braucht `M999`+`M3` zur Freigabe), `M53` ist der Routine-
     Abbruch für genau diesen Fehlschlag-Fall (stoppt nur, bleibt armiert/
     homed, kein Fault). Diese Textstelle beschrieb ursprünglich denselben
     `M112`-für-beides-Fehler, den `ligature.md` selbst mittlerweile
     korrigiert hat — hier nachgezogen, nicht nur dort. Exakte
     Prüf-Position/-Zeit innerhalb der Bewegung ist nicht festgelegt. Während `G73` läuft,
     pollt der RPi `?` außerdem fortlaufend, um Arduino-Relais (Blower
     ab ~80%, Trennfächer/Vakuum-Timing) selbst zeitlich zu steuern —
     das FOC-Board bleibt dabei ohne jede Kenntnis vom Arduino-Zustand
     oder vom Pickup-Ausgang.
  4. **Move-to-Top hat keinen eigenen Code (ijon, 2026-07-31):**
     sowohl der Recovery-Button am Touchscreen als auch das
     Job-Ende-Parken sind ein **einfacher `G0`** auf einen kleinen
     Absolutwert nahe der durch `G28` etablierten Null-Position (die
     liegt am oberen Ende der Achse, nahe dem Endstop) — kein
     firmware-seitig persistierter Margin-Wert, der RPi liefert das
     Ziel wie bei jedem anderen `G0` direkt mit. Vereinfacht das
     Protokoll gegenüber Claudes erster Fassung (dort: eigener Code
     `G28.1`).
  5. **Drive-Mode-Umschaltung (Torque- vs. Position-based) — Vorschlag,
     noch nicht bestätigt:** zwei eigenständige Codes statt eines
     Parameters (nachdem `S` als Modus-Parameter wegen der obigen
     Parameter-Konvention entfällt): `M50` (Position-based, Angle-Mode,
     harter Zielpunkt) / `M51 T<Limit>` (Torque-based, dieselbe
     Velocity+Current-Limit-Mechanik wie Touchdown, generalisiert auf
     beliebige Moves, nachgiebig bei Widerstand). Begründung:
     [`ligature.md`](ligature.md) §11.
- **Alternativen:**
  - **Rohes `Commander`-Kommandoset ohne G-Code-Anlehnung** (z.B.
    JSON-Zeilen oder eigene Binärframes) — verworfen (hrmny/ijon,
    2026-07-31): G-Code ist eine für CNC-/Motorsteuerung etablierte,
    lesbare Konvention, die sich beim Debuggen per Terminal direkt
    tippen lässt, ohne dass ein eigenes Tool nötig ist.
  - **Ein einziges kombiniertes Board-übergreifendes Protokoll**
    (RPi↔Arduino und RPi↔FOC-Board identisch) — verworfen: die beiden
    Boards haben grundverschiedene Aufgaben (Relais-Schalten vs.
    geschlossene Motorregelung mit Telemetrie); ein gemeinsames
    Protokoll hätte für das Arduino unnötigen G-Code-Ballast bedeutet.
    `monospace.md` bekommt bewusst ein einfacheres zeilenbasiertes
    Kommando/Antwort-Schema ohne G-Code-Anlehnung.
- **Konsequenzen:**
  - `esp32-foc-firmware-requirements.md` §7 ("Protokoll noch nicht
    festgelegt") ist durch diese Entscheidung überholt — Pointer auf
    `ligature.md` ergänzt, Inhalt dort nicht dupliziert.
  - T20 (`tasks.md`) zerfällt sauberer in zwei Teile: das
    Protokoll-Design selbst ist jetzt in `ligature.md` festgehalten,
    T20 beschreibt weiterhin den RPi-seitigen Client dagegen (jetzt in
    `sans-serif.md` §1.1 zielzustand-spezifiziert).
  - Der Drive-Mode-Vorschlag (Punkt 5 oben) ist **nicht** final — vor
    Firmware-Implementierung von `M50`/`M51` mit hrmny/ijon zu
    bestätigen.
  - ~~**Pneumatik-Drucksensor am Arduino ist optional, nicht entschieden
    (ijon, 2026-07-31)**~~ — **final gestrichen (ijon, 2026-08-01, siehe
    AD-001):** keine Priorität für die MVP-Phase, aus allen
    Zielzustandsdokumenten entfernt (`monospace.md` enthält dazu jetzt
    gar nichts mehr, weder Protokoll noch Platzhalter-Abschnitt). Der
    Sensor existierte nur als Idee für einen noch nicht beschlossenen
    Pneumatik-Umbau (Blower-Fan/Trennfächer-Fan ersetzt durch Druckluft +
    Magnetventile) — davon unabhängig ist und bleibt der BMP180 (eigener
    I2C-Sensor in der Saugbox) am Arduino, siehe AD-001.
- **Quelle:** Chat 2026-07-31 (dieses Gespräch); `esp32-foc-firmware-
  requirements.md`; `touchdown-motion-sketch.md`.

---

### AD-007: SSH-erreichbares Diagnose-Tool für Relais + BMP180 (Vorgriff auf T21/T22)

> **Status: umgesetzt (2026-08-01/02).** Beide Repos wie unten entschieden
> gebaut und gemergt — `monospace` PR #2 → `master` (`17d547b`, neues
> Text-Protokoll, Steppercode entfernt) und `sans` PR #6 → `master`
> (`beab768`, `hw_diag` als interaktive Session statt Ein-Kommando-CLI, s.
> `monospace.md` §9.1). Auf echter Hardware getestet: `PRESS START`
> erreicht ~49Hz bei Oversampling=2 (Ziel war ≥5Hz, ideal ≤50Hz — beides
> erfüllt), Relais→Aktor-Zuordnung von ijon verifiziert. Erste echte
> Druckmessdaten (3-Zustands-Modell) siehe
> `docs/hardware/bmp180-vacuum-drop-test.md`. Details/Aufgaben-Status:
> `tasks.md` T21/T22.

- **Kontext:** Für die anstehende `monospace`-Firmware-Implementierung
  auf dem RPi (siehe `monospace-AGENTS.md`) reicht ein reines
  Firmware-Flash-und-Test-Zyklus nicht — ijon will am Ende dieses
  Schritts per SSH auf den RPi zugreifen, ein Programm aufrufen und
  damit (a) Druckwerte vom BMP180 live in der Konsole sehen und (b) alle
  vier Relais schalten können. Zweck: Sensorfunktion validieren und auf
  dieser Basis erste empirische Pickup-Success-Schwellwerte ableiten.
  Das deckt sich mit den bereits bestehenden "Done when"-Kriterien von
  T21/T22 (`tasks.md`) — dieses Tool ist im Kern ein Vorgriff auf einen
  Teil dieser beiden Tasks, kein komplett neuer Scope.
- **Geprüft — bestehender Code:**
  - `sans-core::hardware` (`sans` Repo, `github.com/libreflip/sans`,
    lokal unter `incoming/github/sans` einsehbar): `Hardware`-Struct
    öffnet den seriellen Port (`serialport`-Crate), läuft in einem
    eigenen Thread, Kommandos/Antworten laufen über Channels. **Aber:**
    `protocol.rs`s `Command`/`Response`-Encoding ist fest an das *alte*
    binäre 3-Kommando-Protokoll gekoppelt (`BOX`/`LIGHT`/`FLIP`,
    2-Byte-Frames) — für das neue zeilenbasierte ASCII-Protokoll aus
    `monospace.md` §4/§5 nicht verwendbar. Wiederverwendbar ist nur das
    Transport-Skelett (Port öffnen, Thread, Channel-Pattern), nicht die
    Protokoll-Logik selbst — bestätigt dieselbe Einschätzung, die T22
    bereits für die RPi-seitige Client-Arbeit trifft.
  - `sans-ctrl` (`sans` Repo, 29 Zeilen): reines `clap`-Argument-Parsing
    für einen **anderen** Zweck (Fernsteuerung des `sans-server`-Daemons
    — `status`/`jobs`/`do restart`/`do stop`), keine einzige Aktion
    implementiert. Kein sinnvoller Ausgangspunkt für direkten
    Hardware-Zugriff — das Tool redet mit einem (noch nicht
    existierenden) Server-Prozess, nicht mit dem seriellen Port.
  - `sans-core/src/bin/camcal.rs`: **wichtiger Präzedenzfall** — der
    `sans`-Workspace hat bereits die Konvention, kleine
    Diagnose-/Kalibrierungs-Programme als eigene Binaries unter
    `sans-core/src/bin/` abzulegen (dort für die Kamera-Kalibrierung).
    Ein neues Diagnose-Binary für Arduino-Relais/BMP180 fügt sich in
    dasselbe, bereits etablierte Muster ein.
  - `monospace/arduinofucker/arduinofucker.py` (`monospace` Repo, 57
    Zeilen): interaktives Python-Menü-Tool über `pyserial`, mit
    Hintergrund-Thread fürs Lesen — strukturell gut geeignet (das
    Hintergrund-Lesen passt zum unaufgeforderten `PRESS <mbar>`-Zeilen
    während `PRESS START`), aber sendet ebenfalls das *alte* binäre
    Protokoll und müsste für das neue Protokoll komplett neu geschrieben
    werden — de facto ein Parallel-Tool zum ohnehin für T21/T22 nötigen
    Rust-Client, mit Risiko, dass beide auseinanderlaufen.
- **Entscheidung:** Diagnose-Tool als neues Binary in `sans-core/src/bin/`
  (Rust), das den für T21/T22 ohnehin zu bauenden RPi-seitigen
  Protokoll-Client direkt nutzt/mit-entwickelt — kein separates
  Parallel-Tool. CLI-Subcommands 1:1 nach `monospace.md` §5:
  `press`, `press-stream` (mit optionalem CSV-Log, da genau das für die
  Schwellwert-Ableitung gebraucht wird — bloßes Scrollen im Terminal
  reicht dafür nicht), `vacuum on/off`, `fan on/off`, `blower on/off`,
  `light on/off`, `all-off`. **Sofort verfügbarer Zwischenschritt ohne
  jeden Code:** `arduino-cli monitor` (ohnehin Teil des
  Firmware-Test-Workflows, `monospace.md` §8) funktioniert bereits als
  roher serieller Terminal für einen ersten Konnektivitätstest, sobald
  die Firmware geflasht ist — nutzbar, während das eigentliche Binary
  noch gebaut wird.
- **Alternativen:**
  - **`arduinofucker.py` fürs neue Protokoll umschreiben** (ijons Plan
    B) — zurückgestellt, nicht verworfen: bleibt Fallback, falls der
    Rust-Weg ins Stocken gerät. Nicht Standardweg, weil er eine zweite,
    parallele Implementierung desselben Protokolls bedeutet, die
    getrennt von der echten T21/T22-Arbeit gepflegt werden müsste.
  - **Nur `arduino-cli monitor`/`picocom`, kein eigenes Programm** —
    verworfen als alleinige Lösung: erfüllt "Kommando tippen, Antwort
    sehen", aber nicht die Schwellwert-Ableitung (keine Zeitstempel,
    kein strukturiertes Log). Bleibt aber als kostenloser Sofort-Test
    Teil des Workflows (siehe oben).
  - **`sans-ctrl` ausbauen** — verworfen: falscher Zuschnitt, dafür
    gedacht mit einem Server-Daemon zu reden, nicht mit dem seriellen
    Port.
- **Konsequenzen:**
  - Zieht einen Teil von T21/T22 (RPi-seitiger Rust-Client für das neue
    Protokoll) in diese RPi-Implementierungs-Session vor, statt es auf
    später zu verschieben — `tasks.md` entsprechend ergänzt.
  - `sans-core/Cargo.toml` pinnt `serialport = "3.2"` (2018er Stand) —
    beim Bauen auf dem aktuellen RPi-Toolchain ggf. Versionsbump nötig;
    das ist ein Update einer bereits akzeptierten Abhängigkeit, keine
    neue Abhängigkeit, die erneute Freigabe bräuchte.
  - Der neue Rust-Client ersetzt `sans-core::hardware/protocol.rs`
    perspektivisch komplett (altes Binärprotokoll wird nirgendwo mehr
    gebraucht) — das alte Modul kann beim Einbau des neuen entfernt
    statt daneben stehen gelassen werden, sobald ijon das bestätigt.
- **Quelle:** Chat 2026-08-02 (dieses Gespräch); `reference/
  sans-code-reusability-review.md`; direkte Durchsicht von
  `incoming/github/sans` und `incoming/github/monospace/arduinofucker`.

---

### AD-008: `ligature` (ESP32-FOC-Firmware) implementierungsreif vorbereitet

- **Kontext:** ijon bat darum, `ligature.md` "auf dieselbe Weise"
  vorzubereiten wie `monospace.md` zuvor — Lücken schließen, die eine
  Umsetzungs-Session bräuchte, plus dazugehörige `AGENTS.md`. Anders als
  bei `monospace` existiert hier **kein bestehendes LibreFlip-Repo** und
  **keine bereits funktionierende Hardware** — das MKS-ESP32-FOC-Board ist
  bei der ersten Inbetriebnahme durchgebrannt (`docs/hardware/
  electronics.md` §2.4, 2026-07-25), ein Ersatzboard wurde bestellt, der
  Motor ist mechanisch noch nicht eingebaut (Kollision mit der Blasdüse,
  `project-management/risks.md` R4). Kein neuerer Stand dazu im Archiv
  gefunden (Stand dieses Eintrags) — nicht angenommen, dass sich das seit
  2026-07-30 geändert hat.
- **Entscheidung:**
  1. **Baudrate auf `115200` festgelegt** (`ligature.md` §4) — reine
     Konsistenzentscheidung zur `monospace`-Baudrate, keine technische
     Notwendigkeit von SimpleFOCs `Commander`.
  2. **PlatformIO-Projektkonfiguration konkret ausformuliert**
     (`ligature.md` §15), inkl. der über Websuche verifizierten,
     nicht offensichtlichen `lib_archive = false`-Notwendigkeit für
     SimpleFOC unter PlatformIO (Quelle: SimpleFOCs eigene Doku,
     `docs.simplefoc.com/library_platformio`, plus
     Community-Bestätigung) — ohne diese Zeile schlägt der Build
     fehl, kein offensichtlicher Fehler, der sich schnell selbst
     finden ließe.
  3. **Diagnose-Tool: eigenes, separates Binary (`foc_diag`), keine
     `hw_diag`-Erweiterung — korrigiert (ijon, 2026-08-02).**
     Ursprünglich hier entschieden: `hw_diag` erweitern, analog zur
     Wiederverwendungs-Logik aus AD-007. **Von ijon zurückgewiesen:**
     `hw_diag` verwaltet genau eine serielle Verbindung mit genau einer
     Protokoll-/Zustandslogik; eine Erweiterung auf eine zweite,
     unabhängige Verbindung mit einer deutlich komplexeren
     `Armed`/`Homed`/`Fault`-Zustandsmaschine wäre kein kleiner Zusatz,
     sondern ein Umbau bereits funktionierenden Codes — "grenzwertig
     bescheuert" (ijon). Zusätzlich: ein Testwerkzeug für den
     BLDC-Treiber braucht ohnehin eine eigene, strengere Auslegung
     (mehr Sicherheitsabfragen vor Arm-/Kalibrierbefehlen, prominente
     Fault-Behandlung, siehe `ligature.md` §16.1) statt einfach
     `hw_diag`s Design zu übernehmen. Neues Binary `foc_diag`
     (`sans-core/src/bin/foc_diag.rs`), eigenständig, aber im selben
     Crate wie `hw_diag`/`camcal.rs` — Wiederverwendung bleibt auf
     Projektstruktur-Ebene, nicht auf Code-Ebene zwischen den beiden
     Boards.
  4. **Kein Repo eigenmächtig angelegt** — da keines existiert, wird das
     im `AGENTS.md`-Begleitdokument explizit als Rückfrage an ijon
     markiert, nicht selbst entschieden.
  5. **Verbindliche Sicherheits-Vorbedingung ergänzt** (`ligature-
     AGENTS.md`): jede Umsetzungs-Session muss den *aktuellen* physischen
     Hardware-Stand (Ersatzboard verbaut? Motor mechanisch montiert?
     Verkabelung passend zu §2?) bei ijon/hrmny erfragen, bevor
     irgendetwas geflasht oder der Motor scharfgeschaltet wird — nicht
     aus der Existenz dieses Dokuments auf Testbereitschaft schließen.
     Deutlich strenger formuliert als bei `monospace`, weil dieses Board
     bereits einmal real durchgebrannt ist.
- **Alternativen:**
  - **Physischen Hardware-Stand selbst annehmen/schätzen** — verworfen:
    genau der Fehlertyp, vor dem die eigene Memory-Lektion dieser Sitzung
    warnt (volatile Fakten nicht ungeprüft fortschreiben). Stattdessen:
    Doku so gestalten, dass jede künftige Session selbst nachfragen
    *muss*, statt einen möglicherweise veralteten Stand zu erben.
  - **`hw_diag` erweitern statt eines separaten Tools** — ursprünglich
    hier gewählt (Analogie zu AD-007), von ijon nach genauerem
    Hinsehen verworfen: die Analogie trägt nicht, weil AD-007s
    Wiederverwendungsargument (ein Board, ein Protokoll, ein
    Betriebsmodell) hier nicht zutrifft — zwei Boards, zwei Protokolle,
    zwei Zustandsmaschinen. Aufwand für die Verallgemeinerung wäre real,
    nicht nur ein zusätzlicher `match`-Zweig.
- **Konsequenzen:**
  - `ligature.md` ist jetzt (Protokoll/Baudrate/Toolchain) vollständig
    genug, um eine Umsetzungs-Session zu starten, **sobald die Hardware
    tatsächlich bereitsteht** — das Dokument selbst kann diese
    Voraussetzung nicht herstellen.
  - `ligature-AGENTS.md` neu angelegt, Muster identisch zu
    `monospace-AGENTS.md` (zwei Dateien reichen zur Umsetzungs-Session,
    keine weiteren Planungsdateien).
  - T20/T32 (`tasks.md`) unverändert gelassen — deren "Depends on"-Zeilen
    zum Board-Bring-up sind ohnehin schon korrekt als offen/blockiert
    markiert, keine Korrektur nötig, nur die neuen Dateien ergänzen das.
- **Quelle:** Chat 2026-08-02 (dieses Gespräch); `docs.simplefoc.com/library_platformio`;
  `docs/hardware/electronics.md` §2.4; `project-management/risks.md` R4.

---

### AD-009: Start/Stop/Not-Halt-Taster — RGB-Farbschema, Blinkmuster, Tasten-Semantik

- **Kontext:** Neuer 16mm-RGB-Taster eingetroffen (tastend, nicht
  rastend), an den Arduino angeschlossen — Pinbelegung/Typenschild-
  Daten: `docs/hardware/electronics.md` §3.3. ijon hat den Grund-Split
  bereits vorgegeben (Chat, 2026-08-07): der Arduino reicht
  Tastendruck-Events roh an den RPi durch; **Farbe und Blinkmuster
  liefert der RPi**, nicht die Firmware — das ist konsistent mit der
  bereits etablierten "dummes Board"-Philosophie dieser Firmware
  (`monospace.md` §3 Punkt 2: keine eigenständige Zeitsteuerung/Automatik
  außer zwei explizit genehmigten Ausnahmen, zu denen dieser Taster
  nicht gehört). Der Taster ist ein **kombinierter** Start/Stop/Not-Halt-
  Taster — seine Bedeutung ändert sich je nach Maschinenzustand, was per
  Farbe/Blinkmuster kommuniziert werden muss. Vorgabe von ijon: Blau =
  Standby, Grün = bereit für Auto-Scan, während Auto-Scan = Stop-Taste
  (Farbe offen, Empfehlung erbeten), gestoppt/Fehler = Rot oder Rot
  blinkend in unterschiedlichen Mustern (Empfehlung erbeten). Bereits
  bestehender, bindender Grundsatz in `sans-serif.md` §5.3: Recovery
  von einem echten Stop darf **nie eine einzelne reflexartige Aktion**
  sein (zwei getrennte Aufrufe, `clear_fault()` dann `arm()`) — jede
  Tasten-Semantik hier muss sich daran halten, nicht daran vorbei
  entwerfen.
- **Entscheidung (Empfehlung, Bestätigung durch ijon ausstehend):**
  1. **Split bestätigt:** Arduino meldet debounced `EVENT BUTTON
     PRESSED` (reine Signalaufbereitung, keine automatische Aktion);
     RPi sendet `LED SET <r> <g> <b>`, auch für Blinken (RPi schickt
     wiederholt An/Aus im gewünschten Takt) — keine Blink-Logik auf dem
     Arduino.
  2. **Farb-/Zustandstabelle:**

     | Zustand | Farbe | Muster | Tastendruck bewirkt |
     |---|---|---|---|
     | Standby (`sans-serif.md` §8.1, außerhalb aktiver Bewegung) | Blau | Dauerlicht | **Neuer Job** (s. Punkt 3, ijon 2026-08-08) |
     | Bereit für Auto-Scan (§8.1 Schritt 7, „Ready to scan?") | Grün | Dauerlicht | Start (= Touchscreen-[Start]) |
     | Automatische Bewegung — jede vom Host kommandierte Bewegung, nicht nur Auto-Scan (s. Punkt 2a, ijon 2026-08-08) | Amber/Orange | Dauerlicht | Stop (= Touchscreen-[Stop], `stop()`+`all_off()`) |
     | Gestoppt, erwartet (User-Stop oder 3-Fehlversuche-Abbruch, Recovery-Screen §8.2.3) | Rot | langsam blinkend (~1 Hz, Vorschlag) | kein Effekt — Recovery bewusst nur über Touchscreen |
     | Gestoppt, unerwarteter Fault (unsolicited `fault ...` vom FOC-Board) | Rot | schnell blinkend (~4–5 Hz, Vorschlag) | kein Effekt, wie oben |

     Begründung Amber für „aktive Bewegung": folgt der in der
     Maschinensicherheit gebräuchlichen Konvention (sinngemäß IEC
     60204-1 Tabelle 5 / IEC 60447 für Drucktasterfarben: Rot=Stop/
     Gefahr, Grün=Start/sicherer Zustand, Gelb/Amber=Achtung/Prozess
     läuft, Blau=Hinweis/sonstige Funktion — Zusammenfassung aus
     allgemeinem Fachwissen, nicht gegen den Normtext im Detail
     geprüft, s. Quelle unten). Amber ist von Grün (bereit) und Rot
     (gestoppt/Fehler) eindeutig unterscheidbar und liest sich
     international als „läuft gerade, Vorsicht" — passt zur
     Zielgruppe „Laien ohne Einweisung" (`sans-serif.md` §0). Zwei
     Rot-Varianten machen sichtbar, ob ein Stop „erwartet" oder ein
     echter Fault war, ohne die Steuerungslogik zu ändern (beide landen
     weiter im selben Recovery-Screen, §5.3) — berührt die dort offene,
     nicht entschiedene Frage, ob 3-Fehlversuche-Abbruch/Stop/Fault
     dieselbe Kategorie sind, löst sie nicht auf, liefert aber am
     Indikator eine Unterscheidung.

     **2a. Geklärt (ijon, 2026-08-08): „immer Amber, wenn die Maschine
     sich automatisch bewegt"** — bewusst weiter gefasst als der
     ursprüngliche Vorschlag (nur Auto-Scan + Kalibrierung). Amber gilt
     jedes Mal, wenn eine vom Host kommandierte Bewegung läuft: Homing
     beim Job-Start (§8.1 Schritt 1), Kalibrierungs-Touchdown/-Aufstieg
     (§4), jeder Auto-Scan-Zyklus (§8.1 Schritt 8), `move_to_top()`
     (Job-Ende-Parken, Recovery-Screen §8.2.3) und Jog-Bewegungen
     (§8.2.3 „[Move down]"). Technisch am saubersten darüber
     abzuleiten, dass der FOC-Client (§1.1) `status().state` gerade
     `Moving` oder `Probing` meldet, plus „Auto-Scan-Screen aktiv" als
     Sonderfall (deckt die kurzen Pausen zwischen einzelnen Bewegungen
     *innerhalb* eines Auto-Scan-Zyklus ab, die sonst als kurzes
     Amber→Grün→Amber-Flackern sichtbar würden). **Kleiner, nicht bei
     ijon einzeln nachgefragter Rest-Punkt:** Momente, in denen Vakuum/
     Gebläse bereits aktiv sind, aber die Box selbst noch stillsteht
     (z.B. kurz nach Vakuum-Einschalten, vor dem eigentlichen Abheben),
     zählen hier ebenfalls als Amber, weil sie in dieselbe „nicht
     anfassen"-Gefahrenklasse fallen wie die Bewegung selbst — passt zum
     bereits bestehenden Sicherheits-Grundsatz in `sans-serif.md` §5.1,
     der Vakuum und Bewegung ohnehin gekoppelt behandelt.
  3. **Tasten-Semantik — geklärt (ijon, 2026-08-08):**
     - **Amber-Zustand:** Taste = Stop, immer, hat Vorrang vor jeder
       anderen Interpretation (das „Not-Halt"-Verhalten).
     - **Grün-Zustand:** Taste = Start.
     - **Blau-Zustand: Taste = „Neuer Job"** — löst denselben Ablauf
       aus, der auch beim Touchscreen-Einstieg in einen neuen Job
       passiert (Homing falls noch nicht gehomed, sonst direkt weiter
       zur Cover-Aufnahme, §8.1 Schritte 1–2). **Arbeitsannahme, nicht
       einzeln bei ijon nachgefragt:** gilt für den *echten* Leerlauf
       (vor dem allerersten Job oder nach Abschluss/Upload eines
       vorigen Jobs) — innerhalb eines bereits laufenden Jobs, während
       einer der Zwischenschritt-Screens (Cover/ISBN/Metadaten/
       Kalibrierungs-Vorbereitung, ebenfalls „Blau"), bleibt ein Druck
       ohne Effekt, weil „neuer Job" dort keine sichere, eindeutige
       Bedeutung hätte (welcher der beiden Jobs wäre gemeint?) und
       diese Screens ohnehin eigene, passgenaue Touchscreen-
       Bestätigungen haben. Falls ijon das anders meint (z.B. Druck
       bricht den aktuellen Job ab und startet neu), bitte korrigieren
       — nicht als Fehlinterpretation stillschweigend weitergetragen.
     - **Rot-Zustand:** weiterhin bewusst **kein** Effekt bei
       Tastendruck — Recovery bleibt zweistufig über den Touchscreen,
       siehe Kontext oben. Nicht Teil der 2026-08-08-Klärung, unverändert.
  4. **Boot-Zustand vor RPi-Verbindung:** LED aus (kein Kanal aktiv),
     bis die erste `LED SET`-Nachricht vom RPi kommt — analog zur
     bestehenden Regel „kein Aktor beim Boot aktiv" (`monospace.md` §3
     Punkt 1), auch wenn die LED kein sicherheitskritischer Aktor ist.
  5. **LED-Ansteuerung: aktiv-low, geklärt (ijon, Diodentest,
     2026-08-08).** War keine freie Design-Entscheidung, sondern durch
     die tatsächliche Innenverschaltung des Bauteils festgelegt — siehe
     `docs/hardware/electronics.md` §3.3 für die Messung. Ergebnis: C
     ist die Sammel-Anode (an 5V), R/G/B sind Kathoden → R/G/B-Pins am
     Arduino werden **aktiv-low** angesteuert (Pin LOW = jeweilige
     Farbe an), damit dieselbe Konvention wie bei den vier Relais
     (`monospace.md` §2). Der Taster-Eingang ist davon unabhängig
     (eigene, freie Pull-up/Pull-down-Wahl, ebenfalls aktiv-low gewählt,
     s. dort).
- **Alternativen (erwogen, verworfen):**
  - **Blink-Muster fest auf dem Arduino kodieren** (z.B. `LED MODE
    ERROR_SLOW` statt roher RGB-Werte) — verworfen: bricht mit der
    zentralen Firmware-Prämisse „nie eigenständige Zeitsteuerung/
    Automatik" (`monospace.md` §3 Punkt 2) und verschiebt UI-
    Entscheidungen in die Firmware, wo sie schwerer änderbar sind als
    im RPi-Code. Auch ijons eigene Vorgabe schließt das aus.
  - **Eigenes/willkürliches Farbschema statt Ampel-Konvention** —
    verworfen: IEC 60204-1 ist die in der Maschinensicherheit übliche,
    vielen Nutzern (auch unbewusst) vertraute Konvention — genau die
    Zielgruppe hier. Ein Abweichen hätte einen Grund gebraucht, den es
    hier nicht gibt.
  - **Amber auch für „gestoppt, erwartet" statt Rot** (da technisch
    kein Fault) — verworfen: der Taster kommuniziert dem Bediener „ist
    es sicher einzugreifen", nicht den internen Board-Zustand — aus
    Bediener-Sicht ist „Auto-Scan beendet, Maschine steht" so oder so
    ein Halt-Zustand, der wie ein Stop aussehen soll. Die interne
    Unterscheidung (Fault vs. sauberer Stop) wird stattdessen über die
    Blinkgeschwindigkeit transportiert, nicht über die Farbe.
  - **Taster generell als reflexartiges Recovery/Resume verwenden**
    (Druck im Rot-Zustand versucht `clear_fault()`+`arm()`) —
    verworfen: widerspricht direkt dem in `sans-serif.md` §5.3 bereits
    begründeten Grundsatz, dass Recovery nie eine einzelne
    reflexartige Aktion sein darf.
- **Konsequenzen:**
  - `monospace.md` erweitert: neue Pin-Belegung (§2), neue Kommandos
    `LED SET <r> <g> <b>` und unsolicited `EVENT BUTTON PRESSED`
    (§4/§5), neuer §10.
  - `sans-serif.md` erweitert: `set_led()`/`on_button_press` im
    Arduino-Client (§1.2), neue Indikator-/Tasten-Logik-Komponente
    (§11), Ergänzungen in §5.3 (Stop-Trigger) und §8.1 Schritt 7
    (Start-Trigger).
  - Neue Aufgaben T34 (Firmware/Protokoll-Hälfte) und T35 (RPi-Client +
    Indikator-Logik-Hälfte), Muster wie T20/T21/T22 (`tasks.md`).
  - **Verbleibende offene Punkte** (Blau-Verhalten, Amber-Umfang und
    LED-Polarität sind mit der 2026-08-08-Klärung oben erledigt): exakte
    Blink-Frequenzen (1 Hz/4–5 Hz sind Vorschläge, keine gemessenen/
    getesteten Werte); die in Punkt 3 markierte Arbeitsannahme zu „Neuer
    Job" innerhalb eines bereits laufenden Jobs (kein Effekt) — Annahme,
    nicht explizit bei ijon bestätigt.
- **Quelle:** ijon (Chat, 2026-08-07, Foto des Tasters, Pinbelegung
  6 Kontakte 2×Taster+R/G/B/C); Recherche Adafruit #3350 / Yueqing Husa
  Electric (Claude, Websuche, 2026-08-07, siehe
  `docs/hardware/electronics.md` §3.3); IEC 60204-1 Tabelle 5 /
  IEC 60447 (allgemein bekannte Konvention für Drucktasterfarben in der
  Maschinensicherheit, nicht im Detail gegen den Normtext geprüft —
  falls das für die tatsächliche Umsetzung sicherheitsrelevant wird,
  Normtext direkt beschaffen statt sich auf diese Zusammenfassung zu
  verlassen); ijon (Chat, 2026-08-08: Blau-Taste = neuer Job, Amber
  immer bei automatischer Bewegung — löst die beiden zuvor offenen
  Fragen aus Punkt 2/3 auf); ijon (Chat, 2026-08-08: Diodentest-Ergebnis
  C = Sammelanode, löst Punkt 5 auf, siehe
  `docs/hardware/electronics.md` §3.3).
