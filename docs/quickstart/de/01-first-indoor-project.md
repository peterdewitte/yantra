<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Schnellstart: erstes Innenprojekt

Grundriss importieren, Etagen zuschneiden, begehen, Bericht. Etwa 15 Minuten. Sie brauchen einen Grundriss (PDF oder Bild) mit einer Maßstabsleiste oder einer bekannten Strecke und einen kalibrierten Empfänger (siehe [Einen Empfänger kalibrieren](03-calibrate-a-radio.md)) oder zum Üben den Simulator „Demo (simulated network)“. Hilfe > Rundgang führt dieselben Schritte an einem Beispielgebäude vor.

1. **Neues Projekt.** Starten Sie Yantra und wählen Sie Neues Indoor-Projekt. Geben Sie den Projektnamen, den Standort, den Netzbetreiber und den Speicherort der Datei ein, dann OK. Änderungen werden sofort gespeichert.

2. **Grundriss importieren.** Datei > Etage hinzufügen > Grundrissdatei wählen... und das Blatt öffnen. Das Fenster für den Grundrissimport öffnet sich (wählen Sie die Seite eines PDFs).

3. **Etagen zuschneiden.** Ziehen Sie mit Rechteck (oder Polygon für einen unregelmäßigen Umriss) einen Rahmen um die erste Etage; Begradigen richtet einen schiefen Grundriss gerade. Drücken Sie Als Etage hinzufügen..., geben Sie Namen und Ebene der Etage ein, dann Hinzufügen. Wiederholen Sie das für jede weitere Etage auf dem Blatt: Ein Blatt kann mehrere Etagen liefern.

4. **Maßstab und Referenz.** Wählen Sie eine Etage, wählen Sie die Maßstabsleiste und ziehen Sie entlang der Maßstabsleiste des Grundrisses, geben Sie ihre Länge in Metern ein und Übernehmen (oder eine Maßstabslinie über eine bekannte Strecke). Etagen desselben Blatts können den Maßstab teilen. Klicken Sie mit Referenz auf einen Punkt, der auf jeder Etage zu finden ist, etwa eine Treppenhausecke. Schließen Sie das Fenster.

5. **Begehen.** Wählen Sie im Reiter Begehung den Empfänger und die Art Punkt und Klick, dann Starten. Klicken Sie dort, wo Sie stehen, gehen Sie geradeaus in gleichmäßigem Tempo und klicken Sie an jeder Abzweigung dort, wo Sie sind. Stoppen am Ende. Die Punkte erscheinen nach Pegel eingefärbt. Wählen Sie mit dem Simulator stattdessen Mausmessung und bewegen Sie die Maus langsam über den Grundriss.

6. **Versorgungskarte.** Drücken Sie im Reiter Karte auf Karte berechnen. Die Karte wird aus Ihren Punkten interpoliert und blendet dort aus, wo niemand gemessen hat. Lassen Sie den Zeiger auf dem Grundriss ruhen, um gemessene und interpolierte Werte zu sehen.

7. **Nächste Etage.** Wählen Sie den Reiter der nächsten Etage und wiederholen Sie die Schritte 5 und 6.

8. **Bericht.** Datei > Bericht: Geben Sie Ihren Namen als Messtechniker ein, behalten Sie PDF und HTML bei und drücken Sie Bericht erstellen. Der Bericht wird in den Ordner `reports` neben dem Projekt geschrieben.

Weiter: [Innen-Begehungstest mit Abnahmebericht](02-walk-test-acceptance.md). Handbuch (englisch): Kapitel 13 (Projekte, Grundrisse, Interpolation), 7 (Begehungsmessungen) und 10 (Berichte).
