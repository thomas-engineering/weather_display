# Claude Code: Verbrauch senken und längere Sitzungen ermöglichen

Diese Maßnahmen reduzieren unnötigen Kontext, Tool-Ausgaben und
Modellaufrufe. Eine vollständige Umgehung von Nutzungslimits ist nicht
möglich.

## Die fünf wichtigsten Maßnahmen

- Aufgaben klein und klar abgrenzen.
- Große Logs und Testausgaben filtern.
- Nur relevante Dateien lesen lassen.
- Zwischen unabhängigen Aufgaben `/clear` und bei langen Aufgaben `/compact`
  verwenden.
- CLAUDE.md, MCP-Tools und Antworten kurz halten.

## 1. Kontext regelmäßig komprimieren

Während langer Sitzungen:

```text
/compact
```

Gezielter:

```text
/compact Bewahre Ziel, Architekturentscheidungen, geänderte Dateien,
offene Fehler, Testbefehle und die nächsten Schritte.
Verwerfe erledigte Diskussionen und alte Terminalausgaben.
```

`/compact` reduziert den alten Gesprächskontext. Die Komprimierung selbst
kann jedoch Nutzung verbrauchen.

## 2. Zwischen unabhängigen Aufgaben `/clear` verwenden

Nach einem abgeschlossenen Teilziel:

```text
/clear
```

Danach nur die erforderlichen Informationen erneut laden:

```text
Lies STATUS.md und setze die dort beschriebene Aufgabe fort.
```

Für den Status eignet sich beispielsweise:

```markdown
# Aktueller Stand

## Ziel
...

## Erledigt
- ...

## Geänderte Dateien
- ...

## Bekannte Probleme
- ...

## Nächster Schritt
...
```

## 3. Nur relevante Dateien lesen lassen

Nicht:

```text
Analysiere das gesamte Repository.
```

Besser:

```text
Untersuche zunächst nur die Dateien rund um den Login.
Suche nach `validateSession` und nenne die drei relevantesten Dateien.
```

Dateibereiche und Grenzen explizit angeben:

```text
Lies nur `src/auth/`, `package.json` und die zugehörigen Tests.
Ignoriere `node_modules`, Builds, Logs und generierte Dateien.
```

## 4. Große Terminalausgaben filtern

Nicht:

```bash
cat application.log
```

Besser:

```bash
tail -n 150 application.log
```

Nur relevante Fehler ausgeben:

```bash
grep -iE "error|exception|failed" application.log | tail -n 100
```

Bei Tests:

```bash
npm test 2>&1 | tail -n 120
```

Oder Ausgabe speichern und gezielt filtern:

```bash
npm test 2>&1 | tee /tmp/test.log
grep -E "FAIL|ERROR|failed|passed" /tmp/test.log | tail -n 80
```

Große Logs, JSON-Dateien, Lockfiles und generierter Code sollten nicht
vollständig in den Kontext gelangen.

## 5. Aufgaben in kleine Schritte aufteilen

Nicht:

```text
Überarbeite die gesamte Anwendung, behebe alle Fehler und schreibe Tests.
```

Besser:

```text
Analysiere nur den Fehler im Zahlungsmodul.
Noch nichts ändern. Nenne die Ursache und zwei Lösungsmöglichkeiten.
```

Danach:

```text
Setze Lösung 2 nur in `src/payments/` um.
Ändere keine anderen Bereiche.
```

Anschließend:

```text
Führe nur die Tests für das Zahlungsmodul aus.
```

## 6. Änderungsgrenzen definieren

```text
Ändere ausschließlich:
- src/api/users.ts
- src/api/users.test.ts

Keine Umstrukturierung, keine neuen Abhängigkeiten
und keine Änderungen am Datenbankschema.
```

Zusätzlich:

```text
Wenn die Ursache nicht in diesen Dateien liegt, stoppe
und erkläre warum.
```

Das verhindert unnötige Repository-Analysen und ausufernde Änderungen.

## 7. Tests stufenweise ausführen

Nicht nach jeder kleinen Änderung die vollständige Testsuite starten.

Zuerst:

```bash
npm test -- src/auth/login.test.ts
```

Erst danach:

```bash
npm test
```

So werden große Testausgaben und unnötige Agent-Schritte vermieden.

## 8. Nicht benötigte MCP-Server deaktivieren

MCP-Server und Tools können zusätzlichen Kontext verursachen. Für eine
konkrete Sitzung sollten nur benötigte Integrationen aktiv sein, zum
Beispiel:

- Dateisystem
- GitHub
- Datenbank
- Browser
- Jira

Nicht benötigte Tools und besonders Tools mit großen Rückgaben deaktivieren
oder nur gefilterte Ergebnisse liefern lassen.

Besser:

```text
Gib nur die zehn neuesten offenen Issues mit ID, Titel und Status zurück.
```

Statt:

```text
Lade alle Issues des Projekts.
```

## 9. CLAUDE.md kurz halten

Eine kurze CLAUDE.md wird regelmäßig berücksichtigt. Sie sollte nur wichtige
Projektregeln enthalten:

```markdown
# Projektregeln

- Paketmanager: pnpm
- Tests: pnpm test
- Lint: pnpm lint
- Änderungen klein halten.
- Vor Änderungen relevante Dateien lesen.
- Keine Dependencies ohne Rückfrage hinzufügen.
- Nach Änderungen betroffene Tests ausführen.
- Keine generierten Dateien bearbeiten.
```

Lange Architektur- oder API-Dokumentationen besser separat speichern und
nur bei Bedarf laden:

```text
Lies ARCHITECTURE.md nur, wenn die aktuelle Aufgabe
die Systemarchitektur betrifft.
```

## 10. Passendes Modell verwenden

Für einfache Aufgaben reicht oft ein schnelleres oder günstigeres Modell:

- kleine Umbenennungen
- einfache Tests
- Formatierungen
- einfache Fehlermeldungen
- Routineänderungen

Leistungsfähigere Modelle für:

- Architekturentscheidungen
- komplexes Debugging
- große Refactorings
- schwierige Sicherheits- oder Nebenläufigkeitsprobleme

Das Modell kann in Claude Code über die Modellauswahl beziehungsweise
`/model` gewechselt werden:

```text
/model
```

## 11. Denkaufwand passend einsetzen

Ausführliches Reasoning ist für komplexe Aufgaben nützlich, aber für kleine
Dateiänderungen oft unnötig.

Neben der Formulierung im Prompt lässt sich der Denkaufwand auch direkt über
den Befehl `/effort` einstellen (z. B. low/medium/high); die Wahl wird als
Standard für neue Sitzungen gespeichert. Für Routineaufgaben (Umbenennungen,
kleine Fixes, Formatierungen) niedrigen Effort wählen, für Architektur- oder
Debugging-Aufgaben höheren:

```text
/effort
```

Für eine einfache Änderung zusätzlich im Prompt:

```text
Führe die Änderung direkt aus.
Keine ausführliche Erklärung, nur eine kurze Zusammenfassung
und das Testergebnis.
```

Für eine komplexe Aufgabe:

```text
Plane zuerst gründlich, aber ändere noch keine Dateien.
```

## 12. Ausführliche Antworten begrenzen

Bei Routineaufgaben:

```text
Antworte nach Abschluss mit:
1. Geänderten Dateien
2. Einer kurzen Zusammenfassung
3. Den ausgeführten Tests
4. Verbleibenden Problemen

Maximal zehn Zeilen.
```

Das reduziert unnötige Ausgabetokens.

## 13. Wiederholte Dateianalysen vermeiden

Claude nicht wiederholt dasselbe Repository analysieren lassen:

```text
Nutze die bereits identifizierten Dateien.
Lies keine weiteren Dateien, sofern es nicht erforderlich ist.
```

Oder:

```text
Prüfe nur die Änderung aus dem letzten Schritt.
Keine erneute Gesamtanalyse.
```

## 14. Einen sparsamen Sitzungsablauf verwenden

Empfohlener Ablauf:

1. `/clear`
2. Eine klar abgegrenzte Aufgabe stellen
3. Nur relevante Dateien analysieren lassen
4. Kleine Änderung durchführen
5. Betroffene Tests ausführen
6. STATUS.md aktualisieren
7. `/compact` oder `/clear` verwenden
8. Nächste Teilaufgabe starten

Beispiel:

```text
Behebe den Fehler beim Zurücksetzen des Passworts.

Grenzen:
- Nur src/auth/reset-password.ts
- Nur zugehörige Tests
- Keine neue Dependency

Vorgehen:
1. Lies die beiden relevanten Dateien.
2. Erkläre die Ursache in höchstens fünf Sätzen.
3. Implementiere den kleinsten sinnvollen Fix.
4. Führe nur den betroffenen Test aus.
```

## 15. Weitere Stellschrauben

`/fast`-Modus für Routine-Interaktionen, bei denen Geschwindigkeit
wichtiger ist als Tiefe (Opus/4.8):

```text
/fast
```

`settings.json`-Allowlist für wiederkehrende, ungefährliche Bash-/MCP-Aufrufe
anlegen, statt jedes Mal eine Rückfrage zu beantworten:

```text
Erlaube in den Projekteinstellungen wiederkehrende Befehle wie
`npm test`, `git status` und `git diff` ohne erneute Rückfrage.
```

Für Zwischenschritte, deren Rohausgabe man nicht mehr braucht, einen Fork
statt eines frischen Subagents verwenden — der Fork übernimmt den
bestehenden Kontext, ein neuer Subagent müsste ihn erst wieder aufbauen:

```text
Lies die letzten 200 Zeilen aus application.log in einem Fork
und fasse nur die Fehler zusammen. Ich brauche die Rohausgabe nicht.
```

Vorbeugend statt reaktiv komprimieren, bevor eine absehbar große
Tool-Ausgabe den Kontext füllt:

```text
Bevor du den vollständigen Build-Log analysierst, komprimiere zuerst
den bisherigen Gesprächsverlauf mit /compact.
```

Plan-Mode bei unklaren oder größeren Aufgaben nutzen, um Änderungen zu
vermeiden, die wieder verworfen werden müssen:

```text
Plane die Umstellung auf die neue API zunächst nur,
ändere noch keine Dateien. Ich bestätige den Plan, bevor du startest.
```

Terminal-Ausgaben nicht von Hand in den Chat kopieren, sondern Claude die
Befehle selbst ausführen und filtern lassen:

```text
Führe den Testlauf selbst aus und zeig mir nur die
fehlgeschlagenen Tests. Ich soll die Ausgabe nicht selbst einfügen müssen.
```

## Was nicht oder nur begrenzt hilft

- `/compact` ist keine kostenlose Tokenquelle; die Komprimierung selbst
  kann Nutzung verbrauchen.
- Prompt-Caching kann Kosten und Latenz reduzieren, aber nicht beliebig
  viele Tokens kostenlos machen.
- Subagents entlasten den Hauptkontext, verbrauchen aber ebenfalls
  Modellnutzung.
- `--dangerously-skip-permissions` spart keine Tokens und erhöht nur das
  Risiko.
- Lange Regeln in CLAUDE.md erhöhen den regelmäßig geladenen Kontext.
- Der vollständige Terminalverlauf sollte nicht in den Chat kopiert werden.
- Ein größeres Abo ist nicht die einzige Optimierung, aber Limits lassen
  sich nicht vollständig umgehen.

## Die fünf wichtigsten Maßnahmen (Zusammenfassung)

- Aufgaben klein und klar abgrenzen.
- Große Logs und Testausgaben filtern.
- Nur relevante Dateien lesen lassen.
- Zwischen unabhängigen Aufgaben `/clear` und bei langen Aufgaben `/compact`
  verwenden.
- CLAUDE.md, MCP-Tools und Antworten kurz halten.
