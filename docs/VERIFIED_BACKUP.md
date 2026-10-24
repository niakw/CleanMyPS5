# Copie vérifiée et restauration : prototype hôte non destructif

## Portée exacte

`src/native/verified_backup.c` implémente un **prototype indépendant** de copie de sauvegarde de captures connues, de vérification et de restauration **dans un second dossier privé appartenant à l'application**. Ce code n'est **pas** compilé dans le titre `PPSA99784`, n'est exposé ni dans l'interface native ni par le helper, et **n'autorise aucune suppression ou modification de la Galerie PS5**.

L'objectif est d'éprouver le contrat de sauvegarde avant une éventuelle opération de maintenance, et non de prétendre que les bases de données/index multimédias PS5 sont restaurables aujourd'hui. Le moteur ne modifie jamais le fichier source.

## Propriétés testées

- Les entrées ne peuvent cibler que `/user/av_contents/photo/` ou `/user/av_contents/video/` sous une racine de **test** explicitement fournie. Parcours de chemin avec `openat` / `O_NOFOLLOW`, fichiers réguliers, sans liens physiques, et revalidation de l'identité, taille, mtime et ctime.
- Le répertoire de destination préexiste et doit appartenir au processus, sans permissions groupe/autres (mode 0700). Les identifiants de sauvegarde sont des tokens simples [A-Z0-9_-] de 1 à 48 caractères : aucun chemin arbitraire n'est accepté.
- Copie en blocs de 64 Kio, taille maximale de 512 Mio par objet **et par groupe logique complet**. Ce plafond reste une condition des fonctions de sauvegarde hôte ; le diagnostic natif en lecture seule dispose d'un contrôle séparé permettant de mesurer une vidéo de plusieurs Gio sans copier ni lire son contenu. Une capsule `CMP5BAK1` contient taille et **SHA-256** du contenu, mais jamais le chemin source ou des identifiants personnels.
- Le contrôle partagé des groupes refuse désormais les chemins de 256 octets non terminés, les extensions incohérentes avec le type réel (image/vidéo/`.dat`), les variantes hors du même dossier et les composants de parcours ambigus. Les tests hôte vérifient aussi que l'API de sauvegarde rejette un groupe malformé avant toute création de capsule.
- La copie est d'abord écrite dans un fichier temporaire créé avec `O_EXCL`. Elle est validée avant une publication sans écrasement par `linkat`. Une sauvegarde existante ne peut pas être remplacée ; une annulation nettoie le temporaire.
- La publication réussie exige maintenant également la disparition du fichier privé `.part` et la réussite de `fsync` sur le dossier de destination. Si `fsync` ou la suppression du temporaire échoue **après** un `linkat` réussi, la fonction retourne un échec et **aucun reçu de durabilité** ; un fichier privé déjà publié peut subsister, et n'est jamais effacé automatiquement. Des tests injectent cet échec sur sauvegarde et restauration.
- Les lectures des capsules et des groupes recontrôlent leurs noms et inodes après comparaison SHA-256. Les marqueurs `__COMMIT` et `__RESTORED` sont relus **après** tous les membres : un manifeste renommé pendant l'opération n'est pas accepté. Cela reste une garantie bornée de prototype hôte, pas une transaction atomique avec les index de la Galerie.

- La vérification refuse capsules tronquées, modifiées, malformées, changements de fichier ou échec de SHA-256. La restauration utilise la même empreinte, écrit un nouveau fichier privé et refuse l'écrasement d'un fichier existant.
- Le test en C, lancé en CI sur Linux et macOS avec et sans sanitizers, compare l'empreinte à `hashlib.sha256`, vérifie la copie identique, le refus des liens physiques, l'annulation à mi-copie, le refus de collision, la restauration et la détection d'une capsule altérée.


Le planificateur `cmp5_plan_capture_group` et la vérification structurale `cmp5_validate_capture_group` sont maintenant partagés par les tests hôte et le titre natif, **sans** y importer `verified_backup.c` ni `verified_group.c`. L'inspection native présente seulement des compteurs et ne crée aucune capsule, ne restaure aucun objet et n'autorise aucune écriture dans la galerie. L'interface utilise en outre `cmp5_inspect_capture_companions` pour les résultats à un seul fichier ; l'ancien contrat `cmp5_plan_capture_group` ne change pas et exige toujours au moins deux membres pour la logique de sauvegarde hôte.

## Sauvegarde groupée hôte : transaction logique

`src/native/verified_group.c` utilise les fonctions de sauvegarde individuelle et le planificateur en lecture seule désormais extrait dans `src/native/capture_companions.c` sans les exposer à la console. Un planificateur **strictement en lecture seule** peut désormais rechercher les variantes partageant exactement le nom de base et le dossier d'une capture déjà identifiée. Il conserve au maximum quatre entrées, refuse tout dossier insuffisamment parcouru (limite 1 024 entrées), tout lien symbolique, tout inode remplacé, toute variante inconnue et tout doublon d'extension. Cette méthode **ne garantit pas** que toutes les dépendances d'une capture PS5 (miniatures, index de base, formats propriétaires) aient été identifiées ; elle ne vaut donc jamais autorisation de nettoyage. Pour `REC_001.jxr`, `REC_001.jpg` et `REC_001.dat`, la sauvegarde génère des capsules anonymisées `ALBUM__1`, `ALBUM__2`, `ALBUM__3`, puis un marqueur `ALBUM__COMMIT` de 256 octets. Il contient les tailles et empreintes SHA-256, ainsi qu'un condensat de manifeste. Ce condensat détecte une corruption accidentelle mais **n'est pas une signature cryptographique contre un adversaire possédant le dossier privé**.

**Couverture complète des compagnons visibles (tests hôte) :** l'API de sauvegarde exige maintenant une nouvelle énumération du dossier d'origine par descripteur sécurisé. Le nombre et l'identité des membres demandés doivent correspondre exactement à tous les fichiers de même nom de base qui sont visibles et validables (y compris les variantes `.dat`). Omettre volontairement un compagnon visible bloque la transaction **avant la première écriture privée**. Une seconde énumération après copie et avant publication de `__COMMIT` rejette l'apparition tardive d'une variante ou le remplacement d'un inode : des capsules peuvent rester isolées, mais aucun marqueur de groupe complet n'est publié. Les tests injectent une variante `.png` pendant la copie et contrôlent l'absence de `__COMMIT`. Cette garantie est limitée au **dossier observé** ; elle ne prouve toujours pas l'exhaustivité des bases, index et miniatures de la Galerie PS5. Elle n'autorise aucune suppression.

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

## Correspondance sauvegarde / restauration privée

L'API **hôte et en lecture seule** `cmp5_inspect_backup_restore_pair` rapproche maintenant une sauvegarde et un groupe restauré, même si leurs identifiants de groupe diffèrent. Elle exige des **répertoires privés distincts en termes de périphérique/inode**, refuse les alias du même dossier et confirme :

1. Que le groupe de sauvegarde et le groupe restauré sont tous deux vérifiés indépendamment, sans fichiers supplémentaires ou temporaires ambigus.
2. Que leurs reçus comportent le même nombre de membres, le même nombre d'octets et le même SHA-256 de manifeste, lequel contient les tailles et SHA-256 ordonnés de chaque membre.
3. Que les deux espaces de travail gardent la même identité et ne changent pas pendant le contrôle ; avant de publier `MATCH`, les états de récupération sont relus.

Les statuts sont `UNVERIFIABLE` (inspection impossible), `NOT_READY` (au moins un groupe vide ou interrompu), `MATCH` (restauration privée correspondant à la sauvegarde choisie), `MISMATCH` (deux groupes complets mais de contenus différents) et `INVALID` (un groupe corrompu ou ambigu). Les tests couvrent ces cas ainsi que les refus d'alias et d'identifiants invalides.

Cette comparaison ne restaure **pas** les entrées d'index, miniatures ou associations de la Galerie PS5 ; même `MATCH` ne donne **aucune** permission de supprimer la capture originale. L'état observé est ponctuel, pas une transaction atomique sur le système de fichiers.

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

## Précontrôle de l'espace de noms avant toute copie (hôte uniquement)

La restauration et la sauvegarde de groupe rejettent désormais **avant
le premier fichier écrit** tout composant, marqueur ou fragment `.part`
préexistant dans l'espace de noms du groupe demandé (y compris les
composants des membres suivants). Le parcours de dossier est borné à
4 096 entrées et refuse une analyse incomplète. Aucun fragment antérieur
n'est supprimé automatiquement. Les fixtures créent une ancienne
`.ALBUM__3.part` et vérifient l'absence de création de `ALBUM__1`.

Cette mesure évite une restauration partielle inutile face à un conflit
**déjà présent**. Elle ne verrouille pas la destination contre un autre
processus et ne rend pas le nettoyage de la Galerie PS5 fonctionnel :
les données système et leurs index n'ont toujours aucune transaction
restaurable validée sur console. Le code reste volontairement hors du titre
natif et du helper de diagnostic.

## Enracinement des espaces privés de sauvegarde (prototype hôte)

Un garde partagé entre sauvegarde et restauration refuse désormais les
chemins relatifs, les composants `.` et `..`, les chemins ambigus ou
terminés par `/`, les cibles canoniques dans `/user`, `/system`,
`/system_ex`, `/data/etaHEN` et `/data/pldmgr`. Le chemin canonique est
parcouru par descripteurs avec `openat(O_DIRECTORY|O_NOFOLLOW)` sur
**tous les composants**, puis son inode/périphérique comparé à la
résolution du chemin d'origine. Le répertoire final doit toujours
appartenir au processus et ne laisser aucun accès groupe ou autres.
Cette précaution évite de prendre un dossier sensible ou une cible
remplacée par lien symbolique pour un espace de stockage privé.

Le garde tolère les alias des **répertoires parents**, utiles notamment
pour les chemins temporaires sur macOS, seulement s'ils résolvent et
restent identiques à un répertoire privé autorisé. Un lien symbolique
au dernier composant est refusé. Ce contrôle n'est pas une protection
contre un processus ayant déjà accès à un répertoire privé et capable
d'en modifier le contenu ; les validations de transactions restent
nécessaires. Aucun code de sauvegarde n'est chargé sur PS5.

## Identité de capture à la nanoseconde

Le catalogue natif mémorise maintenant mtime, ctime et leurs fractions
nanoseconde en plus de la taille, de l'inode et du périphérique. Les
scanners de compagnons, la sauvegarde hôte et la correspondance des
groupes partagent exactement ces snapshots. Un fichier réécrit à taille,
inode et seconde inchangés n'est plus considéré comme l'ancienne capture
lorsque le changement est visible dans mtime ou ctime à la nanoseconde.
Les tests injectent des identités dont seule la nanoseconde ou la seconde
ctime diffère et imposent le refus avant toute capsule privée. Aucune
suppression ou restauration des index Sony sur PS5 n'est autorisée.

## Refus des sources de restauration ambiguës

La restauration privée d'un groupe ne se contente plus de valider son
marqueur `__COMMIT` et ses empreintes : **avant la première écriture**, elle
exige aussi l'état `VERIFIED` de l'inspection de récupération complète.
Une capsule inconnue `ALBUM__5` ou un fichier temporaire
`.ALBUM__4.part` dans le même espace de noms entraîne désormais un refus
sans créer `ALBUM__1` dans le dossier de restauration. Après la copie des
composants, et **avant `__RESTORED`**, la source est inspectée à nouveau
et le manifeste courant doit être identique à celui sélectionné.
Cela empêche de finaliser une restauration privée à partir d'une source
devenue ambiguë pendant la copie ; le refus peut laisser des membres
partiels dans la destination, jamais un groupe officiellement restauré.
Ces gardes restent hôte uniquement et ne restaurent pas les index Sony.

Une fixture supplémentaire injecte un `.ALBUM__4.part` dans le **dossier
de sauvegarde après publication du premier membre restauré**, pendant la
copie des autres membres. Même avec toutes les empreintes valides, la
restauration refuse le marqueur `__RESTORED`, classe la source comme
`INVALID` et la sortie sans marqueur comme `INCOMPLETE`. Cette preuve
couvre la seconde inspection, et pas seulement le précontrôle initial.
