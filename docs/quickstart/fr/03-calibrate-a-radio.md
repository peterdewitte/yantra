<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Démarrage rapide : étalonner un récepteur

Une SDR mesure les niveaux par rapport à sa propre pleine échelle (dBFS). Un étalonnage les convertit en dBm au connecteur d'antenne, par récepteur et par réglage de gain. Il vous faut le récepteur, un générateur de signal (ou un récepteur de référence étalonné) sur la bande du réseau, un câble et des atténuateurs. Un terminal TETRA mesure lui-même en dBm et n'a pas besoin d'étalonnage.

1. **Réglez le récepteur comme pour le relevé.** Ouvrez un projet, choisissez le récepteur dans Radio > Radios... et réglez ses gains dans Radio > Paramètres. Les réglages de gain font partie de la clé d'étalonnage : un étalonnage fait à un gain ne s'applique qu'à ce gain.

2. **Branchez le générateur.** Sortie du générateur, à travers une atténuation connue, vers l'entrée d'antenne du récepteur. Les niveaux que vous saisissez sont les niveaux à l'entrée du récepteur : soustrayez donc les pertes du câble et des atténuateurs.

3. **Prévoyez les points.** Dans Radio > Étalonnage..., saisissez les fréquences en MHz (par exemple la porteuse la plus basse, celle du milieu et la plus haute du réseau), les niveaux en dBm (par exemple -90, -70, -50), le nombre de jours de validité de l'étalonnage et le nombre de lectures par point. Choisissez Générateur de signal (ou Radio de référence).

4. **Mesurez.** Démarrez le récepteur (onglet Parcours, Démarrer la radio), puis Démarrer dans l'assistant. Pour chaque point, réglez le générateur sur la fréquence et le niveau affichés et appuyez sur Mesurer. Une lecture trop proche de la pleine échelle, trop faible au-dessus du bruit ou saturée est refusée avec la raison : changez le niveau ou le gain et mesurez à nouveau. Ignorer laisse un point de côté.

5. **Enregistrez.** Enregistrer stocke un décalage par fréquence dans votre table d'étalonnage. La fenêtre d'étalonnage indique maintenant la clé comme valide ; les panneaux de relevé affichent les niveaux en dBm sans la mention non étalonné.

6. **Vérifiez.** Sous Vérification et erreur de fréquence, réglez le générateur sur un autre niveau et une autre fréquence, saisissez-les et appuyez sur Vérifier : l'erreur doit rester dans un dB. Mesurer les ppm sur une porteuse connue donne l'erreur de fréquence du récepteur.

7. **Bruit de référence.** Éloignez l'antenne du PC et appuyez sur Relever la référence : l'avertissement d'auto-brouillage compare alors le bruit à votre installation.

8. **Conservez-le.** Exporter enregistre la table dans un fichier pour un autre PC (Importer fusionne, l'entrée la plus récente l'emporte) ; un profil de configuration (Fichier > Paramètres > Général > Enregistrer le profil...) l'emporte aussi. Quand l'étalonnage expire, Yantra marque les niveaux comme expirés : étalonnez à nouveau.

Pour vous exercer sans générateur : choisissez le simulateur, activez « Simulated signal generator » dans ses paramètres et faites les mêmes étapes avec le niveau de son générateur. L'entrée est marquée « simulé (entraînement) » et ne s'applique jamais à un vrai récepteur. Le relevé à la souris crée lui-même un tel étalonnage d'entraînement quand votre table n'en a pas pour le simulateur.

Manuel (en anglais) : chapitre 3 (mesure et étalonnage). La méthode et son incertitude : [note sur la méthode de mesure](../../METHOD.md) (en anglais).
