# CleanMyPS5

Le projet comporte désormais **une interface native PS5 expérimentale** qui compile le scanner générique de métadonnées dans `eboot.bin`. Le helper ELF reste **optionnel** pour la couverture plus large que pourrait permettre son propre contexte de privilèges, via `127.0.0.1:8877`. L'ancien frontend WebKit existe encore pendant le portage. Version : voir `VERSION`.

Le frontend natif utilise VideoOut et la manette et se compile comme un véritable titre homebrew `PPSA99784` (`eboot.bin`, métadonnées et icône). Son scan n'interrompt plus l'affichage : il est déclenché sur un worker séparé et offre un catalogue paginé des médias visibles dans le sandbox (jusqu'à 10 000 entrées dans les limites du scanner), avec filtres manette images/vidéos, jeu, ancienneté et taille, et tri taille/date/nom. Les recherches et aperçus visuels complets restent à porter. La compilation et les vérifications sur hôte sont réalisées, **mais aucune validation sur une PS5 13.60 n'est encore établie**. L'interface native n'a pas encore la parité fonctionnelle avec la version navigateur.

Le moteur est toujours **en lecture seule** : aucune suppression, désinstallation, purge, réparation, écriture système ou changement de configuration n'est actuellement autorisé. La détection de permissions n'accorde pas de privilèges et ne démontre pas qu'un effacement serait sûr.

## Fonctions disponibles

- Inventaire générique des fichiers accessibles dans `/data`, indépendant des apps installées chez le développeur. Programmes, ressources et fichiers inconnus restent visibles et protégés.
- Captures sous `/user/av_contents/photo` et `/video` sans imposer un dossier NPXS particulier. Miniatures, variantes SDR, fichiers annexes et temporaires sont inventoriés et protégés séparément.
- Journaux système : `system_log`, `system_logger2`, `vshlog`, `error`. Caches HTTP connus, y compris métadonnées des bases et WAL/SHM. Leur présence ne justifie aucune purge.
- Homebrews externes sous `/mnt/usb0..7/homebrew` et `/mnt/ext0..7/homebrew`. Catalogue protégé des données d’apps, installations `/user/app` et `/mnt/ext0..7/user/app`, homebrews et plugins etaHEN.
- Chemins, tailles nominales, dates de modification, recherche, tri, filtres ≥50/250 Mio et ≥90 jours. Écran Outils et export complet JSON de métadonnées, y compris les fichiers protégés.
- Rapport de couverture par racine : parcourue, absente, inaccessible, alias ignoré, partielle ou non parcourue. Déduplication des mêmes périphérique/inode dans l’inventaire. Les tailles ne mesurent pas l’espace récupérable.
- Contrôle des accès à 26 chemins candidats et mesures `statvfs` de `/data` et `/user/av_contents`, avec signalement de leur éventuelle identité de volume.
- Analyse générale coordonnée : inventaire, chemins, montages et catalogue. **Elle n’ouvre aucun contenu de fichier.**
- Contrôles supplémentaires **sur demande** : aperçus JPEG/PNG (32 Mio maximum), vidéo MP4/WebM par plages de 1 Mio, comparaison exacte bornée d’images et diagnostic limité d’autoload/configuration etaHEN. Ces contrôles lisent du contenu localement ; ils ne font pas partie de l’analyse générale.
- Retour au système officiel : checklist manuelle et diagnostics locaux. Aucune garantie concernant les historiques distants ou l’accès aux services Sony.

## Protection et limites

Le service écoute uniquement sur `127.0.0.1:8877`, sans télémétrie, dépendance Internet ni serveur sur PC. Il contrôle Host/origine HTTP, limite ses travailleurs à quatre et refuse les méthodes d’écriture. Les assets et les règles sont embarqués.

Le scanner utilise `openat` et des descripteurs de répertoire ancrés, refuse les liens symboliques et ignore sockets, FIFO et périphériques. Les sauvegardes, comptes, profils, licences, clés et autres noms personnels connus sont exclus selon `rules/metadata-v1.json`. Cette liste ne peut pas reconnaître toutes les données d’apps inconnues : aucun contenu n’est lu par l’inventaire et aucun résultat n’est déclaré jetable.

Par analyse : 30 000 entrées visitées, 10 000 fichiers, 2 500 entrées par racine, profondeur 12, rapport ≤8 Mio et délai indicatif de cinq secondes. Un appel système bloquant peut dépasser ce délai. Un chemin absent peut être normal ; un parcours terminé signifie terminé dans le périmètre autorisé, jamais « toute la console ».

Les groupes du catalogue d’applications peuvent être imbriqués. Ne pas additionner leurs tailles. Une date correspond à `mtime`, pas à la création ni au dernier accès. Les bases système et fichiers associés aux médias restent protégés. Aucun fichier FTP privé n’est incorporé au programme ou à sa démonstration.

## Application native et démarrage

`make native-app` utilise une chaîne de compilation communautaire épinglée, génère `dist/native/PPSA99784.zip` (dossier d'application `PPSA99784/`, `eboot.bin`, `sce_sys/`, `sce_module/`) et `dist/native/cleanmyps5-helper.elf`, payload silencieux qui fournit le backend local **sans ouvrir de navigateur**. La CI publie l'archive native, le helper, `manifest.json` et un **package homebrew expérimental `PPSA99784.ffpkg`**. Le manifeste SHA-256 couvre chaque fichier produit et précise que le firmware 13.60 et les opérations d'effacement ne sont pas validés.

Le `.ffpkg` est destiné aux gestionnaires de homebrews capables d'installer le format UFS2 ; il n'est **pas** un PKG officiel signé et reste non vérifié sur FW 13.60. Le format dossier ZIP reste possible avec un gestionnaire enregistrant les titres. Le scanner natif **ne nécessite pas de lancer le helper** ; son champ d'analyse est limité par les droits effectifs du titre. Le helper se lance séparément avec un chargeur de payloads compatible.

L'ancien `make package` produit toujours `dist/CleanMyPS5-elf.zip` avec le frontend WebKit et le payload intégré. Il est conservé le temps de porter les autres fonctionnalités vers l'application native. Ne pas confondre le prototype natif avec une version finale de nettoyage.

**Procédure de test et limites :** [Application native et sécurité des permissions](docs/NATIVE_APP.md). Aucun script ne déploie silencieusement du code ou ne change le firmware de la console.

## Développement et compilation

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
node --check web/app.js
node tests/test_ui.js
python3 scripts/check_branding.py
./build/cleanmyps5-server --root /chemin/vers/une/arborescence-de-test
```

Sur hôte, `--root` est obligatoire. Ne pas utiliser un vrai dossier personnel comme fixture de test.

Pour la PS5 : installer le [SDK communautaire](https://github.com/ps5-payload-dev/sdk), LLVM et définir `PS5_PAYLOAD_SDK` puis :

```sh
make package
```

Les règles déclaratives sont compilées dans le binaire ; elles ne téléchargent ni n’exécutent de scripts. La CI teste l’API native, les exclusions, les fichiers spéciaux, les limites et l’interface, puis compile l’ELF après ASan/UBSan. Elle n’exécute rien sur PS5.

[Livraison et limites](docs/DELIVERY.md) · [Matrice fonctionnelle](docs/FEATURE_MATRIX.md) · [Architecture](docs/ARCHITECTURE.md) · [Validation PS5](docs/VALIDATION_PS5_13_60.md) · [Audit technique des homebrews PS5](docs/PS5_HOMEBREW_TECH_AUDIT.md)

Licence GPL-3.0. Les sources sont conservées dans le dépôt GPL-3.0 et les dépendances externes restent identifiées dans la documentation du port natif.
