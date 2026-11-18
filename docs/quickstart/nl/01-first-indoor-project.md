<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Snel starten: eerste indoorproject

Plattegrond importeren, verdiepingen bijsnijden, lopen, rapport. Ongeveer 15 minuten. Je hebt een plattegrond nodig (PDF of afbeelding) met een schaalbalk of een bekende afstand, en een gekalibreerde ontvanger (zie [Een ontvanger kalibreren](03-calibrate-a-radio.md)), of de simulator "Demo (simulated network)" om te oefenen. Help > Rondleiding doet dezelfde stappen op een voorbeeldgebouw.

1. **Nieuw project.** Start Yantra en kies Nieuw indoorproject. Typ de projectnaam, de locatie, de netwerkoperator en waar het bestand moet komen, en dan OK. Wijzigingen worden meteen opgeslagen. Het eerste tabblad, Over het gebouw, neemt het adres (Zoeken op kaart toont het op een OpenStreetMap-kaart), een foto van het gebouw (Foto openen... of Foto nemen... met de camera), de meettechnicus, de meetdatum en notities; het rapport begint ermee.

2. **Plattegrond importeren.** Bestand > Verdieping toevoegen > Kies een plattegrondbestand... en open het blad. Het venster voor het importeren van de plattegrond opent (kies de pagina van een PDF).

3. **Verdiepingen bijsnijden.** Sleep met Rechthoek (of Veelhoek voor een onregelmatige omtrek) rond de eerste verdieping; Rechtzetten maakt een scheve plattegrond recht. Druk op Toevoegen als verdieping..., typ de naam en het niveau van de verdieping, en Toevoegen. Doe hetzelfde voor elke andere verdieping op het blad: één blad kan meerdere verdiepingen geven.

4. **Schaal en referentie.** Kies een verdieping, open Schaal met zijn pijltje, kies de schaalbalk en sleep langs de schaalbalk van de plattegrond, typ de lengte in meter en Toepassen (of een schaallijn over een bekende afstand). Verdiepingen van hetzelfde blad kunnen de schaal delen. Open Uitlijning en klik met Referentie op een punt dat op elke verdieping te vinden is, zoals de hoek van een trappenhuis. Sluit het venster.

5. **Lopen.** Druk in het tabblad Lopen op Kiezen... naast de radio en kies hem in Radio > Radio's..., kies dan de soort Aanwijzen en klikken, dan Starten. Klik waar je staat, loop in een rechte lijn aan een gelijkmatig tempo en klik waar je bent bij elke bocht. Stoppen aan het einde. De punten verschijnen gekleurd volgens het niveau. Kies met de simulator in plaats daarvan Muismeting en beweeg de muis langzaam over de plattegrond. Om iets op een plek te noteren (een foto, een tekst, een korte geluidsopname), houd je de muisknop daar iets langer dan een halve seconde ingedrukt; de notitie verschijnt als een genummerde speld.

6. **Dekkingskaart.** Druk in het tabblad Kaart op Kaart berekenen. De kaart wordt uit je punten geïnterpoleerd en vervaagt waar niemand gemeten heeft. Laat de aanwijzer op de plattegrond rusten om gemeten en geïnterpoleerde waarden te zien.

7. **Volgende verdieping.** Kies het tabblad van de volgende verdieping en herhaal stappen 5 en 6.

8. **Rapport.** Bestand > Rapport: de meettechnicus van het tabblad Over het gebouw wordt gebruikt tenzij je een andere typt; houd PDF en HTML aan en druk op Rapport maken. Het rapport wordt geschreven in de map `reports` naast het project; het venster Rapport klaar opent het (Openen) of zijn map (Tonen in map).

Volgende: [Indoor-looptest met acceptatierapport](02-walk-test-acceptance.md). Handleiding (Engels): hoofdstukken 13 (projecten, Over het gebouw, plattegronden, notities, interpolatie), 7 (looptests) en 10 (rapporten).
