<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Démarrage rapide : premier projet intérieur

Importer un plan, découper les étages, parcourir, rapport. Environ 15 minutes. Il vous faut un plan (PDF ou image) avec une barre d'échelle ou une distance connue, et un récepteur étalonné (voir [Étalonner un récepteur](03-calibrate-a-radio.md)), ou le simulateur « Demo (simulated network) » pour vous exercer. Aide > Visite guidée fait les mêmes étapes sur un bâtiment d'exemple.

1. **Nouveau projet.** Lancez Yantra et choisissez Nouveau projet intérieur. Saisissez le nom du projet, le site, l'opérateur du réseau et l'emplacement du fichier, puis OK. Les modifications sont enregistrées au fur et à mesure.

2. **Importer le plan.** Fichier > Ajouter un étage > Choisir un fichier de plan... et ouvrez la feuille. La fenêtre d'importation du plan s'ouvre (choisissez la page d'un PDF).

3. **Découper les étages.** Avec Rectangle (ou Polygone pour un contour irrégulier), tracez un cadre autour du premier étage ; Redressement redresse un plan incliné. Appuyez sur Ajouter comme étage..., saisissez le nom et le niveau de l'étage, puis Ajouter. Faites de même pour chaque autre étage de la feuille : une feuille peut donner plusieurs étages.

4. **Échelle et référence.** Sélectionnez un étage, ouvrez Échelle avec sa flèche, choisissez la barre d'échelle et tracez le long de la barre d'échelle du plan, saisissez sa longueur en mètres et Appliquer (ou une ligne d'échelle sur une distance connue). Les étages d'une même feuille peuvent partager l'échelle. Ouvrez Alignement et, avec Référence, cliquez sur un point présent à chaque étage, comme l'angle d'une cage d'escalier. Fermez la fenêtre.

5. **Parcourir.** Dans l'onglet Parcours, appuyez sur Choisir... à côté de la radio et choisissez-la dans Radio > Radios..., puis choisissez le mode Pointer-cliquer, puis Démarrer. Cliquez là où vous êtes, marchez en ligne droite à allure régulière et cliquez là où vous êtes à chaque virage. Arrêter à la fin. Les points apparaissent colorés selon le niveau. Avec le simulateur, choisissez plutôt Relevé à la souris et déplacez lentement la souris sur le plan.

6. **Carte de couverture.** Dans l'onglet Carte, appuyez sur Calculer la carte. La carte est interpolée à partir de vos points et s'estompe là où personne n'a mesuré. Laissez le pointeur sur le plan pour voir les valeurs mesurées et interpolées.

7. **Étage suivant.** Choisissez l'onglet de l'étage suivant et répétez les étapes 5 et 6.

8. **Rapport.** Fichier > Rapport : saisissez votre nom comme technicien de mesure, gardez PDF et HTML et appuyez sur Créer le rapport. Le rapport est écrit dans le dossier `reports` à côté du projet.

Ensuite : [Test de parcours intérieur avec rapport de réception](02-walk-test-acceptance.md). Manuel (en anglais) : chapitres 13 (projets, plans, interpolation), 7 (tests de parcours) et 10 (rapports).
