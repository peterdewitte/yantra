<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Schnellstart: einen Empfänger kalibrieren

Ein SDR misst Pegel relativ zu seinem eigenen Vollausschlag (dBFS). Eine Kalibrierung rechnet sie in dBm am Antennenanschluss um, pro Empfänger und pro Verstärkungseinstellung. Sie brauchen den Empfänger, einen Signalgenerator (oder einen kalibrierten Referenzempfänger) im Band des Netzes, ein Kabel und Dämpfungsglieder. Ein TETRA-Endgerät misst selbst in dBm und braucht keine Kalibrierung.

1. **Den Empfänger wie für die Messung einstellen.** Öffnen Sie ein Projekt, wählen Sie den Empfänger unter Empfänger > Empfänger... und stellen Sie seine Verstärkungen unter Empfänger > Einstellungen ein. Die Verstärkungseinstellungen gehören zum Kalibrierschlüssel: Eine bei einer Verstärkung erstellte Kalibrierung gilt nur für diese Verstärkung.

2. **Den Generator anschließen.** Generatorausgang über eine bekannte Dämpfung an den Antenneneingang des Empfängers. Die eingegebenen Pegel sind die Pegel am Eingang des Empfängers; ziehen Sie also die Verluste von Kabel und Dämpfungsglied ab.

3. **Die Punkte planen.** Geben Sie unter Empfänger > Kalibrierung... die Frequenzen in MHz ein (zum Beispiel den niedrigsten, mittleren und höchsten Träger des Netzes), die Pegel in dBm (zum Beispiel -90, -70, -50), wie viele Tage die Kalibrierung gültig bleibt und die Messungen pro Punkt. Wählen Sie Signalgenerator (oder Referenzempfänger).

4. **Messen.** Starten Sie den Empfänger (Seite Messung, Empfänger starten), dann Starten im Assistenten. Stellen Sie für jeden Punkt den Generator auf die angezeigte Frequenz und den angezeigten Pegel und drücken Sie Messen. Eine Messung zu nah am Vollausschlag, zu schwach über dem Rauschen oder übersteuert wird mit Begründung abgelehnt: Ändern Sie Pegel oder Verstärkung und messen Sie erneut. Überspringen lässt einen Punkt aus.

5. **Speichern.** Speichern legt einen Versatz pro Frequenz in Ihrer Kalibriertabelle ab. Das Kalibrierfenster zeigt den Schlüssel nun als gültig; die Messanzeigen zeigen Pegel in dBm ohne den Hinweis unkalibriert.

6. **Überprüfen.** Stellen Sie unter Überprüfung und Frequenzfehler den Generator auf einen anderen Pegel und eine andere Frequenz, geben Sie diese ein und drücken Sie Überprüfen: Der Fehler sollte innerhalb eines dB liegen. ppm messen auf einem bekannten Träger ergibt den Frequenzfehler des Empfängers.

7. **Referenzrauschen.** Entfernen Sie die Antenne vom PC und drücken Sie Referenz erfassen: Die Warnung vor Eigenstörung vergleicht das Rauschen dann mit Ihrem Aufbau.

8. **Aufbewahren.** Exportieren speichert die Tabelle in einer Datei für einen anderen PC (Importieren führt zusammen, der neuere Eintrag gewinnt); ein Konfigurationsprofil (Datei > Einstellungen > Allgemein > Profil speichern...) nimmt sie ebenfalls mit. Läuft die Kalibrierung ab, kennzeichnet Yantra die Pegel als abgelaufen: Kalibrieren Sie erneut.

Üben ohne Generator: Wählen Sie den Simulator, schalten Sie in seinen Einstellungen „Simulated signal generator“ ein und führen Sie dieselben Schritte mit dem Pegel seines Generators aus. Der Eintrag wird als „simuliert (Übung)“ gekennzeichnet und gilt nie für einen echten Empfänger. Die Mausmessung erstellt selbst eine solche Übungskalibrierung, wenn Ihre Tabelle keine für den Simulator hat.

Handbuch (englisch): Kapitel 3 (Messung und Kalibrierung). Die Methode und ihre Unsicherheit: [Notiz zur Messmethode](../../METHOD.md) (englisch).
