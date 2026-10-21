# Return to Live — retour au système officiel

## Objectif

Préparer **volontairement** une console ayant utilisé des payloads et homebrews à revenir à une utilisation officielle. Ce module aide à inventorier les éléments connus et à planifier les actions. Il ne modifie aucun fichier PS5, ne désactive aucun chargeur et n'interagit pas avec les services en ligne.

**Avertissement :** il n'existe aucune garantie qu'un effacement local, une réinitialisation ou une mise à jour prévienne une suspension du compte ou de la console. Les informations éventuellement déjà collectées par Sony ne sont pas accessibles à l'application ; aucune conclusion « indétectable » ou « prête pour PSN » n'est possible.

## Contrôles pris en charge

- Diagnostic des fichiers d'autoload internes / USB reconnus et du réglage de démarrage de la Toolbox ; le résultat ne prouve pas quels payloads ont été chargés en mémoire.
- Inspection limitée des dossiers homebrew et plugins connus, tous **protégés** et jamais considérés comme déchets.
- Inspection des répertoires PS5 candidats sans lecture des sauvegardes, licences ou bases système.
- Réglages DNS, proxy et filtres, version effective du firmware et état de compte : **non vérifiables par le moteur actuel**, contrôles manuels.

## Plan manuel

1. **Sauvegarder.** Utiliser les fonctions système PS5 de sauvegarde des données et captures. Un export JSON de CleanMyPS5 n'est pas une copie des fichiers. Confirmer la restauration avant une réinitialisation.
2. **Contrôler les autoloaders.** Désactiver les démarrages automatiques dans les gestionnaires d'origine et vérifier les configurations reconnues, sans suppression aveugle.
3. **Identifier les homebrews et plugins.** Employer uniquement les mécanismes de désinstallation adaptés et validés. Ne pas confondre dossier présent et application installée/active.
4. **Revoir les paramètres réseau.** Consulter manuellement DNS, proxy, listes de blocage et réseau personnel. Ne pas supposer qu'un réglage « automatique » garantit quoi que ce soit sur un compte.
5. **Redémarrage normal.** Après désactivation des chargeurs, faire un arrêt complet puis tester le comportement sans relancer de payload. Les contrôles natifs ne sont pas une preuve d'absence de modification persistante.
6. **Choisir la procédure officielle.** La **mise à jour** modifie le firmware et peut faire perdre l'accès à l'environnement actuel. La **réinitialisation** efface les données utilisateur. La **réinstallation** du logiciel système est une option de dernier recours : elle efface les données et réinstalle le système. Aucune ne peut garantir l'état des historiques distants.
7. **Vérifier le compte indépendamment.** Consulter les informations officielles de support et de suspension PlayStation ; un outil local ne peut pas prononcer ni lever une suspension.

## Procédures et documentation officielle PlayStation

- Mode sans échec : https://www.playstation.com/fr-fr/support/hardware/safe-mode-playstation/
- Réinstallation du logiciel système : https://www.playstation.com/fr-fr/support/hardware/reinstall-playstation-system-software-safe-mode/
- Restauration des paramètres par défaut : https://www.playstation.com/fr-fr/support/hardware/restore-default-settings/
- Politique et état des suspensions : https://www.playstation.com/fr-fr/support/account/suspensions/

Les guides de Sony font notamment la distinction entre « restaurer les paramètres par défaut » (conserve les données de jeu/utilisateur) et « réinitialiser » / « réinstaller le logiciel système » (perte de données). Suivre les écrans et documentations officielles selon la version réellement installée.

## Garde-fous techniques

- Aucune API de suppression, nettoyage de journaux système, camouflage ou manipulation d'identifiants console.
- Jamais de bouton « empêcher un ban » ou « nettoyer toutes les traces » ; un résultat local incomplet reste explicitement signalé.
- Les cases de la checklist n'attestent que les déclarations manuelles de l'utilisateur et n'entraînent aucune opération.
- Le rapport exporté ne comporte pas de chemins de fichiers personnels, ni de contenu de configuration, ni de données de compte.
- Une validation réelle sur console PS5 13.60 est nécessaire, distincte des compilations ELF et des tests host.
