# Copie vérifiée et restauration : prototype hôte non destructif

## Portée exacte

`src/native/verified_backup.c` implémente un **prototype indépendant** de copie de sauvegarde de captures connues, de vérification et de restauration **dans un second dossier privé appartenant à l'application**. Ce code n'est **pas** compilé dans le titre `PPSA99784`, n'est exposé ni dans l'interface native ni par le helper, et **n'autorise aucune suppression ou modification de la Galerie PS5**.

L'objectif est d'éprouver le contrat de sauvegarde avant une éventuelle opération de maintenance, et non de prétendre que les bases de données/index multimédias PS5 sont restaurables aujourd'hui. Le moteur ne modifie jamais le fichier source.

## Propriétés testées

- Les entrées ne peuvent cibler que `/user/av_contents/photo/` ou `/user/av_contents/video/` sous une racine de **test** explicitement fournie. Parcours de chemin avec `openat` / `O_NOFOLLOW`, fichiers réguliers, sans liens physiques, et revalidation de l'identité, taille, mtime et ctime.
- Le répertoire de destination préexiste et doit appartenir au processus, sans permissions groupe/autres (mode 0700). Les identifiants de sauvegarde sont des tokens simples [A-Z0-9_-] de 1 à 48 caractères : aucun chemin arbitraire n'est accepté.
- Copie en blocs de 64 Kio, taille maximale de 512 Mio par objet. Une capsule `CMP5BAK1` contient taille et **SHA-256** du contenu, mais jamais le chemin source ou des identifiants personnels.
- Le contrôle partagé des groupes refuse désormais les chemins de 256 octets non terminés, les extensions incohérentes avec le type réel (image/vidéo/`.dat`), les variantes hors du même dossier et les composants de parcours ambigus. Les tests hôte vérifient aussi que l'API de sauvegarde rejette un groupe malformé avant toute création de capsule.
- La copie est d'abord écrite dans un fichier temporaire créé avec `O_EXCL`. Elle est validée avant une publication sans écrasement par `linkat`. Une sauvegarde existante ne peut pas être remplacée ; une annulation nettoie le temporaire.
- La vérification refuse capsules tronquées, modifiées, malformées, changements de fichier ou échec de SHA-256. La restauration utilise la même empreinte, écrit un nouveau fichier privé et refuse l'écrasement d'un fichier existant.
- Le test en C, lancé en CI sur Linux et macOS avec et sans sanitizers, compare l'empreinte à `hashlib.sha256`, vérifie la copie identique, le refus des liens physiques, l'annulation à mi-copie, le refus de collision, la restauration et la détection d'une capsule altérée.


Le planificateur `cmp5_plan_capture_group` et la vérification structurale `cmp5_validate_capture_group` sont maintenant partagés par les tests hôte et le titre natif, **sans** y importer `verified_backup.c` ni `verified_group.c`. L'inspection native présente seulement des compteurs et ne crée aucune capsule, ne restaure aucun objet et n'autorise aucune écriture dans la galerie. L'interface utilise en outre `cmp5_inspect_capture_companions` pour les résultats à un seul fichier ; l'ancien contrat `cmp5_plan_capture_group` ne change pas et exige toujours au moins deux membres pour la logique de sauvegarde hôte.

## Sauvegarde groupée hôte : transaction logique

`src/native/verified_group.c` utilise les fonctions de sauvegarde individuelle et le planificateur en lecture seule désormais extrait dans `src/native/capture_companions.c` sans les exposer à la console. Un planificateur **strictement en lecture seule** peut désormais rechercher les variantes partageant exactement le nom de base et le dossier d'une capture déjà identifiée. Il conserve au maximum quatre entrées, refuse tout dossier insuffisamment parcouru (limite 1 024 entrées), tout lien symbolique, tout inode remplacé, toute variante inconnue et tout doublon d'extension. Cette méthode **ne garantit pas** que toutes les dépendances d'une capture PS5 (miniatures, index de base, formats propriétaires) aient été identifiées ; elle ne vaut donc jamais autorisation de nettoyage. Pour `REC_001.jxr`, `REC_001.jpg` et `REC_001.dat`, la sauvegarde génère des capsules anonymisées `ALBUM__1`, `ALBUM__2`, `ALBUM__3`, puis un marqueur `ALBUM__COMMIT` de 256 octets. Il contient les tailles et empreintes SHA-256, ainsi qu'un condensat de manifeste. Ce condensat détecte une corruption accidentelle mais **n'est pas une signature cryptographique contre un adversaire possédant le dossier privé**.

Une interruption avant le dernier marqueur laisse éventuellement des capsules isolées ; elles ne constituent **pas** un groupe utilisable. Un deuxième lancement sur les mêmes identifiants refuse les collisions au lieu d'écraser les anciens composants. La restauration vers un autre dossier privé crée les fichiers `ALBUM__1` à `ALBUM__3`, vérifie chacun par SHA-256, puis publie `ALBUM__RESTORED`. Le système ne promet donc **ni restauration atomique du contenu en cas d'arrêt au milieu, ni récupération automatique des fichiers partiels** : il promet seulement qu'un marqueur de groupe complet ne soit créé qu'après vérification de chaque membre.

```sh
ctest --test-dir build -R 'verified_backup_fixture|verified_group_fixture' --output-on-failure
```

Les tests couvrent la corruption d'une capsule, la modification d'un fichier restauré, les interruptions, les identifiants interdits, les variantes sans média principal, les noms de base différents et les doublons d'extensions.

## Audit de reprise après interruption (lecture seule)

Le moteur dispose de deux contrôles complémentaires, **uniquement pour les dossiers privés du prototype sur ordinateur** : `cmp5_inspect_group_recovery` vérifie les capsules et le marqueur `__COMMIT`, tandis que `cmp5_inspect_restored_group_recovery` vérifie les fichiers bruts restaurés et le marqueur `__RESTORED`. Le second valide les empreintes SHA-256 des fichiers restaurés, pas uniquement celles des capsules sauvegardées. Les contrôles parcourent jusqu'à 4 096 entrées de répertoire, refusent les éléments inattendus dans l'espace de noms du groupe (par exemple `ALBUM__5`, `.ALBUM__5.part` ou un marqueur étranger) et vérifient les liens symboliques/physiques sans les suivre. Ils retournent quatre états :

- `EMPTY` : aucune capsule, aucun fichier temporaire et aucun marqueur connu.
- `INCOMPLETE` : composants ou fichiers temporaires présents, mais aucun marqueur validé.
- `VERIFIED` : marqueur de transaction présent et **vérification SHA-256 intégrale** de toutes les capsules réussie.
- `INVALID` : marqueur non fiable, composant non régulier, lien symbolique ou autre anomalie détectée.

Les compteurs de composants publiés, de fragments temporaires et d'entrées inattendues sont rapportés séparément. Les tests hôte simulent une interruption de restauration, un membre `__5` excédentaire, un `.part` résiduel et la corruption d'un fichier restauré. Ces contrôles **ne retirent jamais les fichiers `.part`**, ne réutilisent pas une sauvegarde interrompue et n'autorisent aucune action sur les originaux. Le rapport est ponctuel et ne constitue pas un verrou sur les fichiers : une seconde validation sera nécessaire au moment d'une éventuelle opération sûre. Le moteur reste non compilé dans le titre PS5 ; cette étape développe la détection avant de traiter les politiques de récupération.

## Ce qui n'est pas encore prouvé

Ce prototype ne restaure **pas** les captures dans `/user/av_contents` et ne répare aucune base d'index multimédia. Il sait maintenant valider logiquement un groupe de **2 à 4 composants de même nom de base**, dont les versions JXR/JPG et un sidecar `.dat` d'au plus 64 Kio ; les miniatures PS5 dans d'autres répertoires ne sont pas encore associées. Un groupe n'est marqué complet qu'après validation SHA-256 de tous ses membres. Une restauration groupée n'écrit que dans un **second espace privé** et publie à son tour un marqueur une fois les composants revérifiés. Les composants partiels laissés par un arrêt brutal sont ignorés sans marqueur, mais **leur nettoyage et leur reprise automatique restent à développer**. Un fichier `.*.part` éventuellement laissé par une interruption brutale reste isolé : il n'est jamais pris pour une sauvegarde finalisée. Les garanties de durabilité en cas de panne de stockage n'ont pas été démontrées sur PS5 13.60.

**Aucune suppression, aucune quarantaine du fichier original et aucune restauration système ne doivent être activées** avant la vérification des permissions du titre, des index multimédias, d'un protocole de sauvegarde multi-fichiers, des journaux de reprise et des tests réels sur le firmware ciblé.

## Vérification

```sh
cmake -S . -B build
cmake --build build --target cmp5-verified-backup-tests
ctest --test-dir build -R verified_backup_fixture --output-on-failure
```

L'autre parcours autonome `python3 tests/test_verified_backup.py` compile le même code sans utiliser de données de la console.
