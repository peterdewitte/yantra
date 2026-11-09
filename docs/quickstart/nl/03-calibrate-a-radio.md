<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Snel starten: een ontvanger kalibreren

Een SDR meet niveaus ten opzichte van zijn eigen volle schaal (dBFS). Een kalibratie zet ze om naar dBm aan de antenneaansluiting, per ontvanger en per versterkingsinstelling. Je hebt de ontvanger nodig, een signaalgenerator (of een gekalibreerde referentieontvanger) op de band van het netwerk, een kabel en verzwakkers. Een TETRA-terminal meet zelf in dBm en heeft geen kalibratie nodig.

1. **Stel de ontvanger in zoals je gaat meten.** Open een project, kies de ontvanger in Radio > Radio's... en stel zijn versterkingen in bij Radio > Instellingen. De versterkingsinstellingen maken deel uit van de kalibratiesleutel: een kalibratie gemaakt bij één versterking geldt alleen voor die versterking.

2. **Sluit de generator aan.** Uitgang van de generator, via een bekende verzwakking, naar de antenne-ingang van de ontvanger. De niveaus die je typt zijn de niveaus aan de ingang van de ontvanger, dus trek het verlies van kabel en verzwakker af.

3. **Plan de punten.** Typ in Radio > Kalibratie... de frequenties in MHz (bijvoorbeeld de laagste, middelste en hoogste draaggolf van het netwerk), de niveaus in dBm (bijvoorbeeld -90, -70, -50), hoeveel dagen de kalibratie geldig blijft en het aantal metingen per punt. Kies Signaalgenerator (of Referentieradio).

4. **Meten.** Start de ontvanger (pagina Meting, Radio starten), dan Starten in de wizard. Zet voor elk punt de generator op de getoonde frequentie en het getoonde niveau en druk op Meten. Een meting te dicht bij volle schaal, te zwak boven de ruis of overstuurd wordt met de reden geweigerd: verander het niveau of de versterking en meet opnieuw. Overslaan laat een punt weg.

5. **Opslaan.** Opslaan bewaart één afwijking per frequentie in je kalibratietabel. Het kalibratievenster toont de sleutel nu als geldig; de meetpanelen tonen niveaus in dBm zonder de markering ongekalibreerd.

6. **Verifiëren.** Zet onder Verificatie en frequentiefout de generator op een ander niveau en een andere frequentie, typ ze in en druk op Verifiëren: de fout hoort binnen een dB te liggen. ppm meten op een bekende draaggolf geeft de frequentiefout van de ontvanger.

7. **Referentieruis.** Haal de antenne weg van de pc en druk op Referentie vastleggen: de waarschuwing voor eigen storing vergelijkt de ruis dan met jouw opstelling.

8. **Bewaren.** Exporteren bewaart de tabel in een bestand voor een andere pc (Importeren voegt samen, het nieuwere item wint); een configuratieprofiel (Bestand > Instellingen > Algemeen > Profiel opslaan...) neemt hem ook mee. Als de kalibratie verloopt, markeert Yantra de niveaus als verlopen: kalibreer opnieuw.

Oefenen zonder generator: kies de simulator, zet "Simulated signal generator" aan in zijn instellingen, en doe dezelfde stappen met het niveau van zijn generator. Het item wordt gemarkeerd als "gesimuleerd (oefening)" en geldt nooit voor een echte ontvanger. De muismeting maakt zelf zo'n oefenkalibratie als je tabel er geen heeft voor de simulator.

Handleiding (Engels): hoofdstuk 3 (meting en kalibratie). De methode en haar onzekerheid: [notitie over de meetmethode](../../METHOD.md) (Engels).
