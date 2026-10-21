# Architecture de CleanMyPS5 0.10

Le binaire C11/POSIX embarque l’interface et les règles. Le serveur HTTP local écoute en loopback sur le port 8877, avec quatre travailleurs bornés et arrêt par SIGINT/SIGTERM. Aucun endpoint d’écriture ou de suppression n’existe.

## API

| Route GET | Rôle |
|---|---|
| `/api/status` | Version, hôte/cible PS5 et mode lecture seule |
| `/api/report` | Inventaire générique des métadonnées, règles et couverture |
| `/api/homebrew` | Catalogue borné et protégé des installations et données d’apps |
| `/api/preflight` | Accessibilité de 26 chemins candidats prédéfinis |
| `/api/volumes` | Mesures de deux montages connus, sans total SSD |
| `/api/media?file=…` | JPEG/PNG sur demande, signature vérifiée, ≤32 Mio |
| `/api/video?file=…` | MP4/WebM sur demande, plages bornées à 1 Mio ; HEAD autorisé |
| `/api/duplicates` | Comparaison exacte d’images sur demande, budget de lecture borné |
| `/api/diagnostics` | Lecture explicite de références autoload/config etaHEN connues |

`/`, `/index.html`, `/styles.css`, `/app.js` servent les assets embarqués. Les autres routes et méthodes sont refusées. Host/origine/referer/fetch-metadata sont contrôlés ; les réponses sont non mises en cache. Aucun accès réseau distant n’est proposé.

## Analyse générale

Les règles versionnées de `rules/metadata-v1.json` sont validées et transformées en en-tête C à la compilation. Elles décrivent des racines et exclusions de métadonnées, avec `cleanupValidated:false`. Elles ne confèrent jamais une autorisation d’effacement et ne contiennent aucun script.

L’inventaire parcourt les photos, vidéos et fichiers associés, les sources système connues, `/data`, les homebrews de huit slots USB et huit slots étendus. Les racines installées sont traitées par le catalogue dédié. Aucun parcours arbitraire de `/`, des comptes, des sauvegardes, des licences ou des bases d’installation n’est effectué.

La traversée garde les descripteurs de dossiers et utilise `openat(O_DIRECTORY|O_NOFOLLOW)` et `fstatat(AT_SYMLINK_NOFOLLOW)`. Les liens symboliques ne sont pas suivis, les entrées spéciales sont ignorées. L’inventaire déduplique les identités dev/inode ; le catalogue annonce des tailles nominales qui peuvent se chevaucher. Les données personnelles connues sont exclues par nom. Les apps inconnues peuvent avoir d’autres conventions : cette exclusion n’est pas une preuve d’absence de données privées.

Chaque fichier possède un chemin canonique dans l’espace de noms console, une taille, un mtime, une catégorie et une raison. Tous les fichiers hors captures sont protégés. Les captures sont uniquement sélectionnables pour une revue manuelle. Les variantes, index, miniatures, bases et WAL/SHM ne peuvent pas être sélectionnés comme déchets.

Chaque racine a un statut de couverture et les nombres d’entrées/fichiers/octets observés. Les limites locales permettent de continuer les autres racines après la troncature d’un grand dossier. Les limites globales et le délai souple interrompent proprement la suite, marquée non parcourue. Les racines absentes ne rendent pas l’analyse partielle ; les erreurs et troncatures la rendent partielle.

L’analyse coordonnée n’appelle ni les aperçus, ni les doublons exacts, ni le diagnostic de configurations. Ces lectures de contenu restent explicites et locales. Le rapport exporté inclut aussi les fichiers protégés et ne contient aucun contenu de fichier.

## Tests et livrable

Les tests synthétiques vérifient les apps inconnues, un autre dossier NPXS, les caches/journaux système, les exclusions privées, les liens, FIFO, alias d’inode, limites par racine, récupération après saturation HTTP, origine, méthodes et empreintes/mtime inchangés. JavaScript est testé séparément pour l’import, l’export, les filtres et le bilan.

`make package` livre un unique payload ELF avec interface intégrée. Ce format nécessite un lanceur de payloads et un navigateur console utilisable. Le dossier natif Itemzflow/ShadowMount avec véritable `eboot.bin` est une chaîne distincte qui reste à porter et valider. Aucun déploiement console n’est exécuté par le build.
