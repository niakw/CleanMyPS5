# CleanMyPS5 — premier essai non destructif sur PS5 13.60

**Jalon M2 : PAS ENCORE VALIDÉ.** Cette fiche sert uniquement à collecter
la première preuve d'exécution du titre natif. Les tests hôte et le packaging
ne peuvent pas la remplacer. Ne pas la présenter comme une procédure de
nettoyage, une preuve de privilège élevé ou un test de compression.

## Avant l'essai — préparation fermée

1. Depuis [l'exécution native vérifiée côté CI](https://github.com/niakw/CleanMyPS5/actions/runs/40574602802),
   récupérer l'artefact `PPSA99784-native-title-unverified-on-13-60`.
   Il contient `PPSA99784.ffpkg`, `PPSA99784.zip`,
   `manifest.json` et `release-readiness.json`. Le téléchargement
   dépend de l'accès GitHub aux artefacts du dépôt.
2. Utiliser **un seul** format reconnu par le gestionnaire de titres
   homebrew déjà installé. Le `.ffpkg` est un format expérimental
   **non signé par Sony**. Si aucun format n'est reconnu, **arrêter** :
   ne pas modifier le registre des titres, la liste blanche sandbox,
   le firmware ni les paramètres etaHEN pour forcer l'installation.
3. Vérifier les SHA-256 des deux archives à l'aide de `manifest.json`
   sur l'ordinateur avant tout transfert. Conserver le jeu et les
   données utilisateur en dehors de cet essai. Aucun second helper
   ni outil AMPR n'est à installer : ils sont empaquetés dans le titre.
4. L'essai n'est autorisé que pour les **menus, diagnostics et aperçus
   en lecture seule**. Ne pas confirmer de sauvegarde privée
   (R2), de restitution/quarantaine ou de conversion ; ne pas lancer
   le moteur AMPR (Carré dans Jeux et compression).

## Séquence M2 (arrêt immédiat au premier échec bloquant)

| ID | Manipulation | Preuve / réussite attendue |
| --- | --- | --- |
| M2.1 | Installer puis ouvrir `PPSA99784` depuis le menu PS5 | Icône et interface native visibles ; pas de crash au démarrage |
| M2.2 | Naviguer à la DualSense entre Accueil, Analyse, Accès, Nettoyage, Retour officiel ; revenir avec Cercle | Pas de blocage, focus perdu, écran vide ou action destructive |
| M2.3 | Lancer **une** Analyse générale (sans helper manuel) | Les cinq contrôles aboutissent ou signalent explicitement une couverture partielle / un refus de permission ; vidéo et manette restent réactives |
| M2.4 | Ouvrir un catalogue ou la fiche d'une capture **si accessible** ; consulter un aperçu JPG/PNG disponible (sans R2) | Métadonnées/aperçu cohérents ; si inaccessible, marquer `BLOCKED_PERMISSIONS`, jamais `PASS` |
| M2.5 | Revenir au menu, fermer l'application normalement, la rouvrir | Pas de crash ni de données annoncées faussement comme nettoyées |

**Ne pas tester dans M2 :** lancement du moteur AMPR, écriture d'un
nouveau jeu, sauvegarde/restauration privée, effacement de répétition,
suppression de média, caches, plugins, mises à jour, jailbreak ou
manipulation de données Sony. Le jalon M3 sera un essai séparé, après M2.

## Rapport minimal à partager après l'essai

Renseigner `PASS`, `FAIL`, `BLOCKED_PERMISSIONS`, `SKIPPED` pour
chaque ID M2.1–M2.5, plus :
- identifiant de commit/artefact testé ;
- gestionnaire et **format** utilisés (pas d'identifiant personnel) ;
- écran atteint et libellé d'erreur en cas d'échec ;
- si Analyse était complète, partielle ou refusée ;
- crash oui/non, fermeture/reprise oui/non.

Aucun nom de capture, chemin privé, identifiant de compte, adresse IP,
journal brut ou capture d'écran privée n'est requis. En cas de blocage,
transmettre uniquement la catégorie et l'étape. Un résultat non testé
ne doit pas être inventé.

## Règle de clôture

M2 n'est validé que sur **une PS5 13.60 réelle**, après preuve des
cinq étapes. Une permission refusée ne justifie jamais une élévation
ou un contournement automatique. Si une fonction reste inaccessible,
marquer M2 partiel et décider explicitement d'un périmètre de sortie
réduit. M3 (compression) et M4 (nettoyage) restent indépendamment bloqués.
