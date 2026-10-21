# CleanMyPS5 — application native PS5 et moteur de maintenance

## Deux processus, une interface installée

**L'interface utilisateur n'est plus obligée d'utiliser le navigateur.** Un premier frontend natif est compilé comme titre PS5 `PPSA99784`, avec `eboot.bin`, `sce_sys/param.json`, `sce_sys/icon0.png` et `sce_module/libc.prx`. Il utilise VideoOut et la manette pour sélectionner les sections. Le moteur reste séparé dans le payload silencieux `cleanmyps5-helper.elf`, uniquement sur `127.0.0.1:8877`.

Ce découplage est intentionnel : l'installabilité d'un titre ne lui confère pas les permissions d'un payload jailbreaké. Le frontend effectue des sondes locales en lecture seule sur `/app0`, `/download0`, `/data` et `/user/av_contents` (une ouverture réussie ou un état inaccessible/absent, sans prétendre distinguer ces deux erreurs). Dans son écran Analyse, il effectue aussi un `GET /api/preflight` via le helper, affiche les comptes accessible/refusé/absent, ne communique pas sur le réseau local et **ne modifie jamais de fichiers**. L'ancien frontend WebKit reste disponible pour la transition et le diagnostic.

**Statut de validation :** compilation croisée et contrôle de structure du titre sur GitHub Actions, *pas encore de lancement prouvé sur PS5 firmware 13.60*. Les chemins et les droits sont propres au firmware, au loader et au contexte d'exécution. Le format obtenu est un **dossier de titre homebrew**, pas un PKG retail signé.

## Artefacts

- `dist/native/PPSA99784.zip` : archive contenant uniquement le dossier `PPSA99784/` et ses fichiers installables dans un environnement qui enregistre les titres homebrew.
- `dist/native/cleanmyps5-helper.elf` : moteur d'inspection à lancer avec un chargeur ELF approprié ; ne lance pas le navigateur.
- `cleanmyps5.elf` : ancienne variante navigateur conservée tant que les fonctionnalités de l'interface native ne sont pas équivalentes.

À partir du dépôt, `make native-app` génère les deux premiers fichiers. Le script télécharge dans un dossier temporaire un outil de compilation natif **épinglé sur un commit précis**, remplace le code et l'icône de démonstration, compile le titre, compile le helper séparément et vérifie les fichiers produits. Aucune dépendance, image personnelle ou configuration réseau n'est introduite dans les sources suivies par Git.

## Installation de test, sans toucher au firmware

1. Extraire `PPSA99784.zip` et déposer **le dossier `PPSA99784`** dans `/data/homebrew/PPSA99784/` avec un serveur FTP déjà disponible sur la console.
2. Le gestionnaire de homebrews ou monteur de titres compatible doit déclarer ce dossier dans l'interface PS5. Copier le dossier par FTP ne garantit pas à lui seul qu'un raccourci apparaisse. Ne pas le faire passer pour un installateur PKG officiel.
3. Avec le chargeur de payloads déjà installé, lancer `cleanmyps5-helper.elf` manuellement pour le premier test. Si un ancien `cleanmyps5.elf` tourne sur le port 8877, l'arrêter d'abord.
4. Depuis l'accueil PS5, lancer **CleanMyPS5** comme une application : Haut/Bas sélectionnent une des quatre rubriques, Croix ouvre le détail ou relance le contrôle, Cercle retourne au menu. La rubrique Stockage réalise sa propre sonde de droits même si le helper est arrêté.
5. Si `ENGINE OFFLINE OR NETWORK RESTRICTED` apparaît, vérifier le chargeur du helper, les permissions réseau loopback du titre, l'occupation du port et la notification de démarrage. Ne jamais interpréter ce message comme une autorisation de monter ou modifier `/data`.

Le développement ciblé est le firmware **13.60**, pour lequel l'archive doit encore être essayée sur une vraie console. L'intégration à un autoloader n'est pas activée par le build : le premier lancement doit être manuel. Un firmware mis à jour pourrait supprimer la compatibilité du jailbreak.

## Ce qui fonctionne dans le prototype et ce qui reste bloquant

| Fonction | Prototype natif | Validation console |
|---|---|---|
| Titre installable avec icône et `eboot.bin` | Compilé | À faire |
| Interface VideoOut et navigation DualSense | Implémentées | À faire |
| Prédiagnostic via helper local `/api/preflight` | Implémenté ; sondes de chemins natifs distinctes en lecture seule | À faire |
| Signalement d'accès refusé / absence / accès apparent | Implémenté côté moteur | À faire |
| Analyse exhaustive des captures / caches dans le frontend natif | Non porté | — |
| Galerie, aperçu, doublons et filtres avancés dans le frontend natif | Non porté | — |
| Élévation native avec réponse vérifiée | Non intégrée | — |
| Suppression de captures par API cohérente avec la galerie | Non | — |
| Nettoyage des caches prouvés recréables et orphelins confirmés | Non | — |
| Récupération d'espace et vérification avant/après | Non | — |

## Ordre des travaux nécessaires avant d'activer le nettoyage

**1. Prouver les permissions, ne pas les supposer.** Le titre natif sandboxé et le helper doivent relever séparément les résultats `openat`, `fstatvfs`, et les erreurs par racine. Pour un éventuel mécanisme d'élévation, utiliser un protocole explicite, borné, avec confirmation de la capacité accordée et contrôles effectifs après réussite ; ne pas tenter d'accès silencieux au kernel.

**2. Produire des candidats sûrs, pas une liste de choses à effacer.** Chaque entrée exige une catégorie, une source de découverte, un statut d'application active, la vérification de son appartenance au titre, des liens physiques/associés, et un état de permission. Les chemins de sauvegardes, clés, comptes, licences, firmwares, bases actives, plugins chargés, PKG installés et montages inconnus restent exclus.

**3. Supprimer transactionnellement.** Pour les captures, trouver l'API appropriée à la galerie ou établir une méthode validée de synchronisation avec ses index avant tout effacement. Prévoir une confirmation explicite par lot, un mode prévisualisation, la possibilité de restaurer ou d'exporter, la prévention des liens symboliques, un verrou d'identité via descripteur, puis mesure `statvfs` avant/après. Si une étape n'est pas disponible, refuser la suppression.

**4. Ne pas promettre de nettoyage anti-ban.** La rubrique « Return to Live » n'est qu'un guide de préparation à des procédures officielles, jamais une garantie de non-détection ou de retour à l'état d'origine.

## Dépendances et licences

Le port natif est construit à l'aide de l'outil communautaire public `drakmor/ps5-native-app-boilerplate` à la révision épinglée `470695e0c557f99ff2df0e36e4df713c5e636526` (GPL-3.0-or-later). Le code suit GPL-3.0 ; les sources originales et notices doivent rester accessibles. Les ABI de manette et socket nécessitent encore une validation sur le firmware cible.

## Données personnelles

Le code public, les règles et la CI ne doivent pas contenir d'identifiants de profil, IP locale, e-mails personnels, captures privées ou jetons. `scripts/check_repository_hygiene.py` et `scripts/audit_privacy.py` examinent les fichiers suivis ; l'historique Git doit être audité séparément. Un résultat CI vert ne prouve pas qu'aucun secret n'a jamais été exposé. Ne jamais joindre d'export de scan contenant des chemins privés sans les caviarder.
