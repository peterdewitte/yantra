<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Snel starten: indoor-looptest met acceptatierapport

Loop een gebouw af tegen afgesproken acceptatiecriteria en lever een rapport met PASS of FAIL op. Je hebt een project nodig met zijn verdiepingen, schalen en antennes (zie [Eerste indoorproject](01-first-indoor-project.md)), de celdatabase van het netwerk en een ontvanger die gekalibreerd is bij de versterkingen die je gaat gebruiken (zie [Een ontvanger kalibreren](03-calibrate-a-radio.md)).

1. **Celdatabase.** Netwerk > Celdatabase...: importeer de cellijst van de operator als CSV (of typ de cellen in) en Toepassen. Ontvangen cellen krijgen dan een naam en het rapport controleert ze.

2. **Acceptatieprofiel.** Bestand > Rapport > Profielen bewerken...: voeg een profiel toe met de afgesproken criteria, bijvoorbeeld "95 % op of boven -95 dBm", en kies de basis: de gemeten punten, de geteste oppervlakte (belopen vakken van 2 m) of de vloeroppervlakte van de geïnterpoleerde kaart (met de laagste betrouwbaarheid meegeteld). Opslaan. Zonder profiel gebruikt het rapport het standaardprofiel "Indoor acceptance": 95 % van de gemeten punten op of boven -95 dBm.

3. **Voor je loopt.** Verbind de ontvanger en controleer hem in het menu Radio: geen waarschuwing voor eigen storing, geen oversturing, niveaus in dBm (niet dBFS). Houd de antenne weg van de laptop; een USB-verlengkabel helpt. Start een opname als je ook de weg naar het gebouw wilt vastleggen (pagina Meting).

4. **Loop elke verdieping.** Kies op de pagina Lopen Aanwijzen en klikken of Continu en Starten. Klik bij elke bocht, loop aan een gelijkmatig tempo door elke kamer en gang die de acceptatie omvat, en gebruik Handmatige punten voor kamers waar je niet door kunt lopen. Stoppen aan het einde van elke verdieping, en loop dan het tabblad van de volgende verdieping. Elke looptest wordt opgenomen.

5. **Controleer de dekking.** Bekijk op de pagina Nabewerking Goed/slecht ten opzichte van de drempel en de Acceptatie-cijfers per verdieping; loop opnieuw waar de plattegrond gaten toont. Druk op de pagina Kaart per verdieping op Uitvoeren voor de cijfers van de vloeroppervlakte.

6. **Controleer het netwerk.** Netwerk > Verificatie... (live of op een herhaling) toont cellen die afwijken van de database, nieuwe en ontbrekende cellen, en de kruiscontrole van de buren. Ze staan ook in het rapport.

7. **Rapport.** Bestand > Rapport: typ je naam als meettechnicus, kies de verdiepingen en de profielen, PDF en HTML, en Rapport maken. Het rapport geeft per verdieping de plattegrond met elk punt, de dekkingskaart, de statistieken, het aandeel gemeten punten naast het aandeel vloeroppervlakte (nooit gemengd) en de PASS of FAIL van elk profiel; de netwerkverificatie volgt.

8. **Overdragen.** Vink eventueel "De bestanden verzenden na het schrijven" aan met een gedeelde map, een uploadadres of je eigen mailserver (instellingen voor Delen). Er wordt niets verzonden tenzij je een methode kiest.

Handleiding (Engels): hoofdstukken 7 (looptests), 8 (celdatabase), 10 (rapporten en acceptatieprofielen) en 11 (nabewerking). Hoe de cijfers berekend worden: [notitie over de meetmethode](../../METHOD.md) (Engels).
