<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Démarrage rapide : test de parcours intérieur avec rapport de réception

Parcourez un bâtiment selon des critères de réception convenus et remettez un rapport PASS ou FAIL. Il vous faut un projet avec ses étages, ses échelles et ses antennes (voir [Premier projet intérieur](01-first-indoor-project.md)), la base de données de cellules du réseau et un récepteur étalonné aux gains que vous utiliserez (voir [Étalonner un récepteur](03-calibrate-a-radio.md)).

1. **Base de données de cellules.** Réseau > Base de données de cellules... : importez la liste de cellules de l'opérateur en CSV (ou saisissez les cellules) et Appliquer. Les cellules reçues sont alors nommées et le rapport les vérifie.

2. **Profil de réception.** Fichier > Rapport > Modifier les profils... : ajoutez un profil avec les critères convenus, par exemple « 95 % à -95 dBm ou plus », et choisissez sa base : les points mesurés, la surface testée (carrés parcourus de 2 m) ou la surface au sol de la carte interpolée (la confiance la plus faible comprise). Enregistrer. Sans profil, le rapport utilise le profil par défaut « Indoor acceptance » : 95 % des points mesurés à -95 dBm ou plus.

3. **Avant de parcourir.** Connectez le récepteur et vérifiez-le dans le menu Radio : pas d'avertissement d'auto-brouillage, pas de saturation, niveaux en dBm (pas en dBFS). Éloignez l'antenne de l'ordinateur portable ; une rallonge USB aide. Lancez un enregistrement si vous voulez aussi le trajet jusqu'au bâtiment (page Relevé).

4. **Parcourez chaque étage.** Sur la page Parcours, choisissez Pointer-cliquer ou Continu et Démarrer. Cliquez à chaque virage, marchez à allure régulière dans chaque pièce et chaque couloir que couvre la réception, et utilisez Points manuels pour les pièces que vous ne pouvez pas traverser. Arrêter à la fin de chaque étage, puis parcourez l'onglet de l'étage suivant. Chaque test de parcours est enregistré.

5. **Vérifiez la couverture.** Sur la page Post-traitement, regardez Bon/mauvais par rapport au seuil et les chiffres de Réception par étage ; parcourez à nouveau là où le plan montre des lacunes. Sur la page Carte, appuyez sur Calculer pour chaque étage pour les chiffres de surface au sol.

6. **Vérifiez le réseau.** Réseau > Vérification... (en direct ou sur une relecture) liste les cellules qui diffèrent de la base de données, les cellules nouvelles et manquantes, et le contrôle croisé des voisines. Elles figurent aussi dans le rapport.

7. **Rapport.** Fichier > Rapport : saisissez votre nom comme technicien de mesure, choisissez les étages et les profils, PDF et HTML, et Créer le rapport. Le rapport donne par étage le plan avec chaque point, la carte de couverture, les statistiques, la part des points mesurés à côté de la part de surface au sol (jamais mélangées) et le PASS ou FAIL de chaque profil ; la vérification du réseau suit.

8. **Remise.** Cochez éventuellement « Envoyer les fichiers une fois écrits » avec un dossier partagé, une adresse de téléversement ou votre propre serveur de messagerie (paramètres de Partage). Rien n'est envoyé si vous ne choisissez pas de méthode.

Manuel (en anglais) : chapitres 7 (tests de parcours), 8 (base de données de cellules), 10 (rapports et profils de réception) et 11 (post-traitement). Comment les chiffres sont calculés : [note sur la méthode de mesure](../../METHOD.md) (en anglais).
