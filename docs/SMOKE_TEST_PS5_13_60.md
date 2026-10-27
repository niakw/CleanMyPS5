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
2. Sur l'ordinateur, vérifier les SHA-256 des archives
   `PPSA99784.zip` et `PPSA99784.ffpkg` avec `manifest.json`
   **avant** tout transfert. Choisir **une seule** des deux voies
   ci-dessous, selon les capacités déjà connues de son gestionnaire.
   Les deux contiennent le **même titre**, pas deux applications
   indépendantes. Ne pas installer un second helper ou un outil AMPR.
3. **Voie A, dossier homebrew par FTP (alternative sans installateur
   FFPKG) :** extraire `PPSA99784.zip`. Il contient un dossier
   `PPSA99784/` avec `eboot.bin`, `sce_sys/param.json`,
   `sce_module/libc.prx` et `assets/`. Si et seulement si le
   gestionnaire homebrew existant reconnaît les dossiers de titres
   sous `/data/homebrew/`, copier ce **dossier entier** à l'emplacement
   `/data/homebrew/PPSA99784/`. Ne pas copier l'archive ZIP à la
   place de `eboot.bin`, ni disperser ses fichiers directement
   sous `/data/homebrew`. Se limiter à ce nouveau répertoire,
   sans modifier un titre tiers, l'autoload, le registre système
   ou un fichier existant. L'apparition dans le menu PS5 n'est
   **pas démontrée** par la simple copie.
4. **Voie B, gestionnaire acceptant les FFPKG UFS2 :** lui présenter
   `PPSA99784.ffpkg` sans renommer l'extension ni prétendre qu'il
   s'agit d'un PKG retail signé. Cette compatibilité est toujours
   **non vérifiée** sur FW 13.60.
5. En cas de refus d'installation, titre absent ou écran noir,
   marquer `FAIL_INSTALL` ou `BLOCKED_INSTALLER` pour M2.1,
   **arrêter** et garder le package exact pour diagnostic. Ne pas
   forcer l'enregistrement Shell, la liste blanche sandbox, le
   firmware ou les paramètres etaHEN.
6. Conserver les jeux et données utilisateur hors de l'essai. Le
   périmètre M2 se limite aux **menus, diagnostics et aperçus en
   lecture seule** : ne pas confirmer de sauvegarde (R2), de
   restitution/quarantaine ou de conversion et ne pas démarrer
   le moteur AMPR (Carré dans Jeux et compression).
4. L'essai n'est autorisé que pour les **menus, diagnostics et aperçus
   en lecture seule**. Ne pas confirmer de sauvegarde privée
   (R2), de restitution/quarantaine ou de conversion ; ne pas lancer
   le moteur AMPR (Carré dans Jeux et compression).

## Séquence M2 (arrêt immédiat au premier échec bloquant)

| ID | Manipulation | Preuve / réussite attendue |
| --- | --- | --- |
| M2.1 | Installer/ouvrir `PPSA99784` par l'une des deux voies documentées, puis constater sa présence dans l'interface du gestionnaire / menu PS5 | Icône et interface native visibles ; pas de crash au démarrage ; sinon indiquer la voie et `BLOCKED_INSTALLER` |
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
