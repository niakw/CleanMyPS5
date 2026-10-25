# Copie vérifiée et restauration : prototype hôte non destructif

## Portée exacte

`src/native/verified_backup.c` implémente un **prototype indépendant** de copie de sauvegarde de captures connues, de vérification et de restauration **dans un second dossier privé appartenant à l'application**. Ce code n'est **pas** compilé dans le titre `PPSA99784`, n'est exposé ni dans l'interface native ni par le helper, et **n'autorise aucune suppression ou modification de la Galerie PS5**.

L'objectif est d'éprouver le contrat de sauvegarde avant une éventuelle opération de maintenance, et non de prétendre que les bases de données/index multimédias PS5 sont restaurables aujourd'hui. Le moteur ne modifie jamais le fichier source.

## Propriétés testées

- Les entrées ne peuvent cibler que `/user/av_contents/photo/` ou `/user/av_contents/video/` sous une racine de **test** explicitement fournie. Parcours de chemin avec `openat` / `O_NOFOLLOW`, fichiers réguliers, sans liens physiques, et revalidation de l'identité, taille, mtime et ctime.
- Le répertoire de destination préexiste et doit appartenir au processus, sans permissions groupe/autres (mode 0700). Les identifiants de sauvegarde sont des tokens simples [A-Z0-9_-] de 1 à 48 caractères : aucun chemin arbitraire n'est accepté.
- Copie en blocs de 64 Kio, taille maximale de 512 Mio par objet **et par groupe logique complet**. Ce plafond reste une condition des fonctions de sauvegarde hôte ; le diagnostic natif en lecture seule dispose d'un contrôle séparé permettant de mesurer une vidéo de plusieurs Gio sans copier ni lire son contenu. Une capsule `CMP5BAK1` contient taille et **SHA-256** du contenu, mais jamais le chemin source ou des identifiants personnels.
- Le contrôle partagé des groupes refuse désormais les chemins de 256 octets non terminés, les extensions incohérentes avec le type réel (image/vidéo/`.dat`), les variantes hors du même dossier et les composants de parcours ambigus. Les tests hôte vérifient aussi que l'API de sauvegarde rejette un groupe malformé avant toute création de capsule.
- La copie est d'abord écrite dans un fichier temporaire créé avec `O_EXCL`. Elle est validée avant une publication sans écrasement par `linkat`. Une sauvegarde existante ne peut pas être remplacée. **Une interruption laisse le temporaire privé en place**, sans nettoyage automatique d'un nom pouvant avoir été remplacé. Un reçu n'est émis qu'après vérification du résultat publié.
- L'inode de staging est maintenu ouvert pendant la publication des capsules de sauvegarde et des fichiers privés restaurés : les deux noms créés par `linkat` sont comparés au descripteur effectivement écrit. Les tests remplacent volontairement `.part` après un `fsync` réussi pour empêcher la publication d'un fichier tiers. Le dernier `unlinkat` de staging reste sujet à une course POSIX face à un écrivain ignorant `flock` ; ces garanties ne sont donc **pas** une transaction complète avec la Galerie Sony.
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

## Correspondance d'origine persistante des albums (prototype hôte)

Une sauvegarde contenant seulement des capsules anonymes et leur SHA-256 ne
permettait pas de retrouver **quel composant était à restaurer où** après
fermeture de l'application. Le prototype produit donc maintenant, **avant**
le marqueur `__COMMIT`, un fichier privé `<ID>__ORIGINS` de taille fixe :
une version de format, le nombre de composants, l'empreinte du manifeste,
puis les chemins d'origine, types et tailles dans **l'ordre exact** des
capsules. Un SHA-256 protège aussi les octets du fichier de correspondance.
Les chemins ne sont jamais ajoutés au reçu public ni aux diagnostics natifs.
L'empreinte du fichier détecte les corruptions et incohérences, mais
**n'authentifie pas** le contenu contre une personne ayant déjà les droits
d'écriture sur le dossier privé : celle-ci pourrait recalculer le SHA-256.

Cette correspondance reste **sensible** : elle expose les chemins des
captures à quiconque peut lire le dossier de sauvegarde. Elle n'est créée
que dans un espace privé vérifié (0700 ; fichier de mode 0600) par le
**prototype hôte** et **n'est pas exportée** dans l'application PS5 ou
par le helper. Elle n'est **pas chiffrée** et ne doit donc pas être
copiée vers un support partagé. Aucun mécanisme de nettoyage de
la Galerie n'en dépend à ce stade.

La lecture `cmp5_read_group_origin_map` fonctionne après fermeture,
revérifie l'intégrité des capsules et du manifeste, l'inode du fichier de
correspondance, les identifiants, extensions, tailles et associations
photo/vidéo/`.dat`/miniature, et n'émet la correspondance qu'en cas de
cohérence. Un groupe historique dépourvu de cette table reste consultable
comme fragments privés, mais **ne satisfait plus le contrat d'album
restaurable vérifié**. Les tests couvrent les ordres de membres différents,
la corruption d'un chemin enregistré et les marqueurs hérités.

**Limite importante :** une correspondance de noms n'est pas une
sauvegarde transactionnelle des index, des bases ni des associations Sony.
Le moteur ne sait toujours pas reconstruire une Galerie PS5 complète, et
l'écriture vers `/user/av_contents` reste interdite.

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

## Transactions groupées sérialisées (25 octobre 2026)

Le moteur hôte de sauvegarde multi-fichiers verrouille maintenant
**tout le dossier privé** du précontrôle jusqu'à la vérification du
marqueur COMMIT final. La restauration tient un verrou partagé sur
sa source et un verrou exclusif sur sa destination jusqu'à la
validation RESTORED ; les deux acquisitions sont non bloquantes pour
éviter les interblocages. Ces verrous couvrent des groupes **aux IDs
différents** dans le même dossier, au lieu de se limiter à une capsule
nommée. Une tentative concurrente échoue sans commencer de nouvelle
transaction. Tous les descripteurs sont fermés en sortie, même en cas
d'erreur ou d'annulation.

La CI exécute deux processus réels sur deux groupes valides, bloque
le premier après la création du premier membre et vérifie que le
second ne peut démarrer ni une autre sauvegarde dans ce dossier, ni
une restauration concurrente. Après libération, les deux transactions
réussissent et leurs manifestes sont vérifiés. **Ce verrou est
coopératif** : une écriture extérieure au protocole reste possible ;
les identités et empreintes continuent d'être contrôlées. Le titre
PS5 et son helper n'exécutent toujours pas ces transactions hôte.

## Concurrence des transactions privées (25 octobre 2026)

Les trois chemins de mutation du laboratoire (mise en quarantaine,
restauration et résolution d'une interruption) prennent désormais un
**verrou interprocessus non bloquant sur l'inode du dossier privé**.
Deux transactions sur le même laboratoire, même si elles ciblent des
fichiers différents, ne s'exécutent donc pas en parallèle. Un verrou
occupé ou non pris en charge par le système de fichiers produit un
refus sans nouvelle écriture. Aucun fichier de verrou à nettoyer n'est
créé. Un test réel avec `fork`, deux fichiers valides et deux
transactions distinctes vérifie le refus pendant la contention, puis
la réussite après libération du verrou.

Ce verrou est **coopératif** et ne bloque pas les modifications faites
par un logiciel extérieur au protocole ; les contrôles d'identité
et SHA-256 restent indispensables. Le verrou est pris sur un dossier
hôte privé uniquement, pas sur un répertoire PS5.

## Reprise dirigée après interruption dans le laboratoire

Le moteur hôte possède désormais deux points de coupure exercés avec
`fork`/`_exit` **après synchronisation des deux liens**, pendant la mise en
quarantaine et pendant le retour à la source. Après redémarrage, ces
transactions sont signalées comme **ambiguës** : aucun nettoyage automatique
ni conclusion de succès. Une API explicite de résolution accepte uniquement
un choix du demandeur (conserver le nom source ou conserver le nom de
quarantaine), après vérification des deux noms sur le **même inode régulier
privé**, exactement deux liens physiques, une capsule externe SHA-256
valide et des contenus intégralement identiques. L'autre nom est retiré
uniquement après vérification et synchronisation. Liens symboliques,
collisions, répertoires publics, noms divergents et sauvegardes modifiées
restent refusés.

L'API n'est liée ni au titre PS5, ni au helper ni à la Galerie. La réussite
du scénario simulé ne prouve pas la reprise après une panne électrique sur
le système de fichiers réel de la console.

## Transaction de quarantaine : preuve hôte isolée (25 octobre 2026)

Une première chaîne **complète mais uniquement expérimentale** s'exécute
dans un dossier temporaire privé nommé `cmp5-quarantine-lab` (0700),
jamais dans les données de la console : vérifier la capsule SHA-256,
effectuer une véritable restauration de répétition dans un second
espace privé indépendant, revérifier la source, placer l'item en
quarantaine **sans effacer ses octets**, puis restaurer le nom initial
sans écraser un fichier existant.

Les transitions utilisent uniquement des noms fixes dans la même
arborescence privée. Un état intermédiaire avec deux noms présents,
une collision, un lien symbolique, une source modifiée ou une capsule
absente est **ambigu / refusé**, sans tentative d'effacement automatique.
Juste avant de retirer l'un des deux noms, la transaction vérifie à
nouveau **les deux liens physiques et leurs empreintes SHA-256** contre
la sauvegarde indépendante. Les tests modifient réellement les octets
après la synchronisation du checkpoint, pendant la quarantaine puis
pendant la restauration. Dans les deux cas, la suppression du nom est
refusée et les deux entrées restent disponibles pour un audit manuel.
Cette vérification ne rend pas `unlinkat` atomique face à un processus
extérieur qui ne respecte pas les verrous.

**Ce test ne libère pas d'espace disque**, n'établit ni l'autorisation
de nettoyer des caches PS5 ni les relations avec les index Sony.
L'implémentation reste EXCLUE du titre PS5 et du helper privilégié,
et ne propose aucun endpoint permettant de muter une console.

## Contrôle commun d'espace libre (hôte uniquement)

Avant de créer la première capsule de sauvegarde ou le premier fichier
restauré dans un **dossier privé**, les transactions individuelles et
groupées exigent une mesure `fstatvfs` utilisable. La comparaison se fait
sur `f_bavail` (blocs disponibles au processus), jamais `f_bfree`,
et vérifie la capacité prévue de **tous les membres du groupe**, le
manifeste, les métadonnées et une marge d'un Mio. Un dossier readonly,
une capacité inconnue ou trop faible, ou un calcul impossible annule
l'opération sans créer de fichier. Les tests C injectent `EIO`, un
montage readonly, les blocs réservés et des seuils exacts.

Ce contrôle ponctuel n'est **pas une réservation de capacité**. Une
écriture concurrente peut encore épuiser le disque ; la copie gère
toujours ses erreurs, fsync et les capsules incomplètes. Rien n'est
restauré dans la Galerie PS5 et aucune suppression n'est activée.

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

### Cohérence des contrôles de groupes

La vérification directe des groupes et celle de restauration privée
refusent désormais les fichiers `.part`, les membres dépassant le nombre
signé et les éléments inattendus du même espace de noms, même lorsque
les SHA-256 et `__COMMIT` concordent. Un reçu `VERIFIED` ne peut plus
être obtenu à travers une API moins stricte que l'inspection de
récupération. Cette règle n'active pas la restauration des index PS5.

### Répétition fiable des audits de répertoire

Les balayages privés `empty_group_namespace` et `inspect_namespace`
rouvrent maintenant le dossier **par `openat(dir, \".\", O_DIRECTORY | O_NOFOLLOW)`**,
au lieu de `dup(dir)` : ce dernier partageait la position de lecture du
dossier avec son appelant. Un appel ultérieur pouvait commencer à la fin du
répertoire et ignorer `ALBUM__5` ou un `.part` présent. Le nouveau descripteur
possède un curseur indépendant ; `closedir` est également contrôlé.
Un test hôte épuise volontairement le curseur du descripteur initial puis
vérifie que les deux audits voient toujours les entrées supplémentaires.
Aucun fichier de la Galerie n'est modifié par ces audits.

### Identité du dossier pendant les vérifications de groupe

Une vérification de capsules peut conserver un descripteur valide alors
que le **chemin d'espace privé** a été renommé et réattribué. Les deux
vérificateurs directs (`__COMMIT` et `__RESTORED`) enregistrent
maintenant l'identité POSIX complète du dossier au départ, puis rouvrent
le chemin privé sélectionné après les contrôles SHA-256, avant de rendre
un reçu. Inode, périphérique, mode et horodatages du dossier doivent
rester identiques à ceux du descripteur initial. Un test remplace
réellement le répertoire privé au cours de la vérification et exige un
refus, même si les anciens fichiers ouverts étaient encore lisibles.
Cette vérification ne constitue pas une transaction des index Sony.

### Vérification d'une capsule isolée et remplacement du dossier

Le vérificateur hôte d'une capsule individuelle ne valide plus les
empreintes SHA-256 à partir d'un descripteur de répertoire devenu orphelin.
Il mémorise le dossier privé (périphérique, inode, mode, taille, mtime et
ctime à la nanoseconde), puis contrôle **à la fin de l'inspection** que
le chemin choisi désigne toujours exactement ce même dossier. Une fixture
renomme le dossier après un bloc de lecture et en recrée un vide sous le
nom initial : le reçu doit être refusé malgré le contenu intact de
l'ancien dossier. Cela reste sans écriture dans la Galerie PS5.

### Un état EMPTY ou VERIFIED n'est jamais fondé sur un dossier remplacé

L'inspection read-only de récupération (`cmp5_inspect_group_recovery` et
`cmp5_inspect_restored_group_recovery`) vérifie maintenant l'identité de
son espace privé avant et **après** la recherche d'entrées et la validation
des marqueurs. Même une réponse `EMPTY` est refusée si le dossier a
été renommé puis remplacé pendant `readdir`. Une fixture intercepte
l'énumération, déplace le dossier et en crée un autre sous son ancien
chemin ; l'appel doit refuser de produire un état. Aucun index système
n'est modifié et les validations PS5 13.60 restent nécessaires.
