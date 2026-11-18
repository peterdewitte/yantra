<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Schnellstart: Innen-Begehungstest mit Abnahmebericht

Begehen Sie ein Gebäude nach vereinbarten Abnahmekriterien und übergeben Sie einen Bericht mit PASS oder FAIL. Sie brauchen ein Projekt mit seinen Etagen und Maßstäben (siehe [Erstes Innenprojekt](01-first-indoor-project.md)), die Zelldatenbank des Netzes und einen Empfänger, der bei den verwendeten Verstärkungen kalibriert ist (siehe [Einen Empfänger kalibrieren](03-calibrate-a-radio.md)).

1. **Zelldatenbank.** Netz > Zelldatenbank...: Importieren Sie die Zellliste des Betreibers als CSV (oder geben Sie die Zellen ein) und Übernehmen. Empfangene Zellen werden dann benannt, und der Bericht prüft sie.

2. **Abnahmeprofil.** Datei > Bericht > Profile bearbeiten...: Fügen Sie ein Profil mit den vereinbarten Kriterien hinzu, zum Beispiel „95 % bei -95 dBm oder darüber“, und wählen Sie seine Grundlage: die gemessenen Punkte, die getestete Fläche (begangene Quadrate von 2 m) oder die Grundfläche der interpolierten Karte (die geringste Zuverlässigkeit mitgezählt). Speichern. Ohne Profil verwendet der Bericht das Standardprofil „Indoor acceptance“: 95 % der gemessenen Punkte bei -95 dBm oder darüber.

3. **Vor der Begehung.** Verbinden Sie den Empfänger und prüfen Sie ihn im Menü Empfänger: keine Warnung vor Eigenstörung, keine Übersteuerung, Pegel in dBm (nicht dBFS). Halten Sie die Antenne vom Laptop fern; ein USB-Verlängerungskabel hilft. Starten Sie eine Aufzeichnung, wenn Sie auch den Weg zum Gebäude festhalten wollen (Reiter Begehung, Aufzeichnen).

4. **Jede Etage begehen.** Wählen Sie im Reiter Begehung Punkt und Klick oder Kontinuierlich und Starten. Klicken Sie an jeder Abzweigung, gehen Sie in gleichmäßigem Tempo durch jeden Raum und jeden Flur, den die Abnahme umfasst, und verwenden Sie Manuelle Punkte für Räume, die Sie nicht durchqueren können. Stoppen am Ende jeder Etage, dann begehen Sie den Reiter der nächsten Etage. Jede Begehungsmessung wird aufgezeichnet.

5. **Versorgung prüfen.** Sehen Sie sich in der Auswahlliste Erweitert des Reiters Karte Gut/schlecht gegenüber der Schwelle und die Abnahme-Zahlen pro Etage an; begehen Sie erneut, wo der Grundriss Lücken zeigt. Drücken Sie im Reiter Karte auf jeder Etage Karte berechnen für die Zahlen der Grundfläche.

6. **Netz prüfen.** Netz > Überprüfung... (live oder auf einer Wiedergabe) listet Zellen, die von der Datenbank abweichen, neue und fehlende Zellen und die Kreuzprüfung der Nachbarn. Sie stehen auch im Bericht.

7. **Bericht.** Datei > Bericht: Geben Sie nur dann einen Messtechniker ein, wenn er den des Reiters Über das Gebäude ersetzen soll, wählen Sie die Etagen und die Profile, PDF und HTML, und Bericht erstellen. Der Bericht zeigt pro Etage den Grundriss mit jedem Punkt, die Versorgungskarte, die Statistik, den Anteil gemessener Punkte neben dem Anteil der Grundfläche (nie vermischt) und das PASS oder FAIL jedes Profils; ein FAIL nennt die schwachen Bereiche, in denen die Punkte unter der Schwelle bleiben. Die Netzüberprüfung folgt, und die Notizen auf dem Grundriss werden pro Etage aufgeführt. Das Fenster Bericht fertig öffnet den Bericht.

8. **Übergeben.** Kreuzen Sie bei Bedarf „Die Dateien nach dem Schreiben senden“ an, mit einem freigegebenen Ordner, einer Upload-Adresse oder Ihrem eigenen Mailserver (Einstellungen der Weitergabe). Es wird nichts gesendet, solange Sie keine Methode wählen.

Handbuch (englisch): Kapitel 7 (Begehungsmessungen), 8 (Zelldatenbank), 10 (Berichte und Abnahmeprofile) und 11 (Nachbearbeitung). Wie die Zahlen berechnet werden: [Notiz zur Messmethode](../../METHOD.md) (englisch).
