## Analyse native des applications : compteurs invalidés après échange de dossier (27 octobre 2026)

Le scanner de répertoires publiait déjà une couverture **partielle** et
annulait le sous-total des caches lorsque le nom d'un dossier
`/data/<app>` ou `/data/homebrew/<app>` ne désignait plus
l'inode précédemment ouvert. Les octets et le nombre de fichiers
issus de ce dossier pouvaient toutefois rester dans le **bilan global**,
le classement des éléments volumineux et la réponse JSON.

L'attribution est désormais annulée également à ces trois niveaux :
l'entrée est conservée et marquée partielle, mais avec **0 octet et
0 fichier attribuables**. Deux injections de course déterministes
couvrent le renommage en fin d'inspection du dossier homebrew
et celui du dossier d'application, y compris ses caches ; les autres
répertoires valides restent comptés. Le scan reste en lecture seule ;
aucune date de cache, taille nominale ou similitude de nom ne
permet de conclure à un fichier supprimable.

Ces tests hôte ne certifient toujours pas les permissions, les
processus au repos ni la sécurité d'une suppression sur PS5 13.60.

# CleanMyPS5 — critères de clôture (constat vérifiable)

## Reprise LZ4 : fichiers runtime et plugins préservés (26 octobre 2026)

Un correctif du moteur AMPR évite désormais la **réinstallation
du runtime et des plugins en cours de reprise**, qui survenait
avant même que l'empreinte du plan source ait été vérifiée.
Une reprise doit examiner les fichiers partiellement publiés,
pas les remplacer. Le moteur exige des fichiers runtime et plugins
réguliers sans lien symbolique ou lien dur ; un plugin inaccessible
est traité comme une anomalie, non comme une absence. Le runtime
publié est relu avec SHA-256 et comparé au runtime actuellement
sélectionné ; si celui-ci a changé, la reprise échoue de façon
conservatrice **sans remplacement automatique**. Les nouvelles
conversions installent toujours les composants nécessaires.

Les contrôles structurels CI sont appliqués au code C transformé
du moteur. La validation Linux/macOS, les sanitizers, CodeQL et la
compilation de l'ELF PS5 ne remplacent **pas un test matériel de
reprise réelle sur PS5 13.60**. Les opérations de suppression
Sony restent hors périmètre.


## Conversion LZ4 : verrou de profil sur les checkpoints (26 octobre 2026)

La reprise du worker AMPR ne réutilise plus silencieusement un
**profil de jeu modifié après une pause**. Une nouvelle conversion
écrit dans `request.bin` un marqueur versionné et l'empreinte SHA-256
du profil appliqué (ou une preuve explicite d'absence de profil). Lors
de la reprise, le moteur confirme le même contenu **avant**
l'application des règles de fichiers et **avant** toute réécriture
de la demande. Il vérifie également la stabilité du profil entre
lecture et empreinte. La représentation est partagée avec l'inventaire
natif en lecture seule et validée dans la CI du moteur transformé.

Une reprise d'ancien checkpoint sans marqueur ou avec profil changé
est bloquée sans supprimer la sortie partielle. Cela empêche de
mélanger le plan de fichiers d'un nouveau profil avec les anciens
volumes LZ4, mais **ne prouve pas** que les sorties compressées sont
lançables sur PS5 13.60. Tests hôtes de pin SHA-256, profil absent,
mutation, extension malformée, lecteur C/C++ et politique worker.
La validité finale reste soumise à l'exécution réelle sur matériel.


## Audit SHA-256 global des archives natives (26 octobre 2026)

Dans **Archives privées**, le bouton **Carré** lance maintenant un audit
volontaire, annulable, de **toutes les capsules individuelles et tous les
groupes visibles dans le catalogue privé borné** (au maximum 32 de chaque).
Chaque élément est relu intégralement et vérifié avec sa propre empreinte
SHA-256 et, pour les groupes, son manifeste et sa carte d'origine.

La synthèse distingue archives examinées / archives cryptographiquement
valides ; un fichier corrompu **reste compté comme examiné et invalide**.
Les transactions interrompues, les objets non reconnus et les limites de
scan conduisent à un **périmètre partiel**, jamais à un bilan exhaustif
affirmé. L'absence d'un dossier privé ou une annulation n'émet aucun
résultat positif. L'espace total vérifié peut compter plusieurs copies
de la même source et n'est donc pas un gain de nettoyage.

L'audit compare les inventaires avant et après hachage **sans dépendre
de l'ordre de `readdir`** et maintient le descripteur original du dossier
privé pendant l'opération. Un remplacement du dossier par un autre,
même avec les mêmes noms de capsules, invalide le résultat. Lorsque
le contrôle réussit, l'interface renouvelle aussi son catalogue avec
la liste finale effectivement revérifiée ; si l'audit échoue,
l'ancien catalogue est invalidé.

Tests : navigation DualSense simulée, archives privées réelles
synthétiques (une capsule et un groupe), corruption d'un seul membre,
staging interrompu et annulation ; CI Linux/macOS et sanitizers.
La progression du worker indique désormais **N archives contrôlées
sur M** sans prétendre représenter les octets déjà lus à l'intérieur
d'une archive. La synthèse est visible même si l'onglet sélectionné
ne contient aucune entrée ; la bibliothèque affiche quatre lignes
par page pour éviter que le résultat ne chevauche le dernier item.
En cas d'audit annulé ou impossible, aucun bilan positif n'est
publié. Le verdict **de chaque capsule et de chaque groupe** est
également associé à son identifiant dans la liste : « SHA-256 OK » ou
« SHA non valide ou inaccessible ». Les catalogues sont triés
par identifiant à chaque scan pour éviter d'afficher le résultat
d'une autre archive si `readdir` change d'ordre entre les deux scans.
Le moteur conserve désormais une empreinte de métadonnées **de chaque
capsule, manifeste de groupe, carte d'origine et membre de groupe**
avant le passage SHA-256 ; il la compare à l'état des mêmes noms
après le passage. Une écriture *sur place*, une substitution, un
changement de permissions ou de lien pendant l'audit rend le bilan
**indisponible** et invalide le catalogue affiché, même si le nombre
et les noms des archives n'ont pas bougé. Deux tests host injectent
volontairement la mutation d'une capsule après sa propre vérification
et d'un membre de groupe après le contrôle du groupe. Ils exigent
qu'aucune validation globale ne soit publiée. Le second passage
échantillonne un fichier à la fois afin de contenir l'usage de pile
du worker natif.

Ce contrôle de fraîcheur reste un constat **à l'instant final de
l'audit**, pas un verrouillage transactionnel : tout fichier peut
changer ensuite. Ouvrir une archive reste donc soumis à une
vérification indépendante, et cet audit ne valide aucun index Sony. Ces comportements ont
des tests de navigation, de tri et de mise en page en plus du test
hôte utilisant de vraies archives privées.

Le titre natif ne contient **aucune fonction de restauration Galerie
ni de suppression**. Cet audit ne prouve pas la restaurabilité des
index Sony, ni le lancement sur une PS5 13.60 réelle.


## Archives de groupe : état des originaux visible sur la PS5 (26 octobre 2026)

Le parcours natif **Archives privées → Groupes → X** sépare désormais
deux éléments qui étaient confondus dans l'interface : l'intégrité
cryptographique de l'archive sauvegardée et l'état actuel des fichiers
qui avaient été copiés. Après une vérification SHA-256 réussie du groupe,
un worker lance le contrôle en lecture seule des locateurs d'origine
persistés dans la carte `CMP5MAP2`. L'écran résume seulement les
**nombres** de fichiers inchangés, absents, modifiés ou impossibles
à inspecter. Aucun chemin privé n'est affiché.

Si cette seconde inspection échoue, le groupe conserve son résultat
« archive intègre » mais l'état des sources reste **indisponible**.
La disparition d'une capture n'efface pas non plus une archive valide.
Le test de navigation simule un groupe SHA valide avec sources absentes
ou inconnues, ainsi qu'une inspection indisponible. Une fixture C
du mode natif sauvegarde un groupe synthétique, retire temporairement
un fichier source, vérifie que celui-ci est signalé `MISSING`, puis
revérifie l'intégrité indépendante de la copie.

Le nombre de fichiers inchangés ne prouve PAS la cohérence des
miniatures, métadonnées secondaires et index Sony ; aucune
restauration Galerie ou suppression PS5 n'est activée. La liaison
matérielle à une PS5 13.60 reste une validation séparée.


## Restaurations privées : dossiers parents liés au chemin d'origine (26 octobre 2026)

Le contrôle read-only des fichiers source sauvegardés n'utilise plus
l'identité du seul fichier ouvert pour conclure à un état
`UNCHANGED`, `CHANGED` ou `MISSING`. Il rouvre désormais, depuis
la racine de test épinglée, **chaque composant du chemin du dossier
parent** avec `O_NOFOLLOW`, et confirme qu'il s'agit toujours de
l'inode du dossier inspecté. En cas de renommage ou de substitution,
le résultat est `UNKNOWN`.

La fixture `test_group_identity_races.c` vérifie la distinction entre
un fichier réellement absent d'un dossier stable (`MISSING`) et une
absence constatée dans un ancien dossier renommé (`UNKNOWN`). Elle
teste également une source initialement intacte dont le dossier parent
a été remplacé au moment du contrôle : aucune attestation `UNCHANGED`
ne doit alors sortir.

Ce renforcement n'est pas une photographie atomique des bases Sony
et ne confère **aucun droit de suppression ni de restauration Galerie**.
Il demeure une vérification du prototype de restauration en espace
privé hôte, non un résultat de test matériel PS5 13.60.


## Fichiers compagnons : limite exacte des chemins (26 octobre 2026)

Le lecteur de fichiers associés à une capture protégeait l'allocation
`Cmp5MediaCandidate.preview_path[256]` avec un calcul qui ne réservait
pas explicitement l'octet NUL de fin. Un fichier primaire `.jpg` peut
tenir dans 255 caractères, alors qu'un fichier compagnon de même nom
en `.jpeg` réclame 256 caractères plus le terminateur. Dans ce cas
limite, la copie pouvait écrire un octet hors du champ de chemin
et altérer le champ suivant de la structure.

Le calcul vérifie maintenant séparément la place du répertoire, du
séparateur, du nom complet et du NUL, avec soustractions bornées pour
éviter un dépassement d'entier. Deux tests complémentaires couvrent
la frontière arithmétique exacte et une arborescence synthétique contenant
réellement les fichiers `.jpg` et `.jpeg` aux longueurs limites.
Le groupe est refusé comme ambigu, les champs de sortie restent vides
et aucun original n'est modifié.

La protection est utilisée par les sondes en lecture seule et par le
planificateur de sauvegardes privées. Elle ne valide ni les références
des bases Sony ni la restauration de la Galerie.


## Sources de restauration : identité du nom et du fichier (26 octobre 2026)

La revue hôte, en lecture seule, des sources de captures sauvegardées
(`cmp5_inspect_group_originals`) garde maintenant ouvert le descripteur
du **dossier parent** jusqu'à la vérification du dernier composant du chemin.
Après deux lectures `fstat` du contenu sélectionné, elle recontrôle le nom
avec `fstatat(..., AT_SYMLINK_NOFOLLOW)` et compare l'identité, les permissions
et les horodatages du fichier désigné à ceux du descripteur ouvert.

Cela élimine un faux diagnostic possible lorsqu'un fichier source est
renommé ou remplacé pendant le contrôle : l'archive reste inchangée,
mais la source est signalée **UNKNOWN**, jamais **UNCHANGED**. Une fixture
hôte injecte les deux courses exactement entre la lecture de l'inode
et la vérification du nom, puis vérifie que la divergence est refusée.
Le chemin d'origine n'est ni recréé ni modifié.

Cette vérification n'est **pas** une preuve transactionnelle atomique de
l'état Sony : d'autres changements peuvent survenir après le dernier
contrôle. Le résultat reste purement informatif. Aucune restauration vers
`/user/av_contents` ni suppression Galerie n'est activée.


## Confidentialité des capsules et restauration privée (26 octobre 2026)

La validation d'une archive SHA-256 ne vérifiait pas partout si le fichier
était encore **privé**. Le moteur rejette désormais les capsules, les
marqueurs `__COMMIT`, les fichiers restaurés et les entrées de récupération
dont le propriétaire n'est plus le compte courant ou dont les permissions
accordent des droits au groupe ou aux autres utilisateurs. Le contrôle de
l'origin map était déjà présent et reste en vigueur.

Les fixtures modifient les permissions de fichiers qui conservent pourtant
leur contenu et leur SHA-256 : la vérification, la restauration et la
qualification `VERIFIED` doivent les refuser. Un fichier `.part` créé
avec des permissions privées peut rester classé « interrompu », tandis
qu'un fichier `.part` exposé est qualifié « invalide ». Il n'y a pas
de modification ni de suppression des fichiers concernés par ces vérifications.

**Portée :** les essais de restauration restent limités à des dossiers
privés de simulation hôte. Aucun contrôle des bases de données Sony,
aucune restauration de Galerie PS5 et aucune suppression réelle
ne sont activés par ce changement.


## Inventaire privé : concurrence et identifiants bornés (25 octobre 2026)

La revue native des capsules est maintenant rejetée si le même inode de
répertoire a subi une modification détectable de taille, de mode, de liens
ou d'horodatage (mtime/ctime à la précision disponible) pendant sa lecture.
Le chemin canonique est également réouvert et son identité revérifiée avant
toute publication de compteurs. Une fixture modifie réellement le contenu
d'un dossier privé pendant `readdir`, vérifie le refus de publier un
inventaire périmé, puis constate que les fichiers sont intacts et à nouveau
visibles au scan suivant.

Les identifiants `CAP_...` et `GRP_...` sont contrôlés avec une limite
stricte de 20 caractères plus le terminateur ; les champs trop longs ou
non terminés sont rejetés avant vérification des archives. La fixture
inclut des buffers de 21 octets sans terminateur.

Ces contrôles détectent des changements observables mais ne fournissent
pas de snapshot transactionnel atomique sur tous les systèmes de fichiers.
Ils ne prouvent ni intégrité d'un contenu par eux-mêmes, ni restaurabilité
des index Sony ; un groupe sélectionné doit toujours être revalidé et
haché indépendamment. La suppression des médias PS5 reste désactivée.

## Archives natives : chemin stable et transactions temporaires visibles (25 octobre 2026)

L'inventaire natif en lecture seule des capsules et des groupes de sauvegarde
revalide désormais le **chemin du répertoire privé** après la lecture. Si
le dossier d'origine a été renommé ou remplacé entre l'ouverture et la
publication des résultats, le scanner rejette tout le bilan, même si
son descripteur initial permet encore de lire les anciens fichiers.
Une fixture déterministe remplace le dossier en cours d'inspection et
vérifie que l'ancien contenu reste intact et qu'aucun résultat obsolète
n'est publié.

Les fichiers de staging des groupes `.GRP_...__COMMIT.part` et
`.GRP_...__ORIGINS.part` (noms plus longs que 32 caractères) sont
maintenant comptés comme transactions **interrompues** et signalent une
couverture partielle au lieu de disparaître du compteur. Les variantes
inconnues continuent à bloquer la validation. Les tests hôte incluent
une exécution Linux, une macOS et un contrôle AddressSanitizer/UBSan.

**Limite :** l'inventaire est indicatif. Le choix d'une archive
déclenche toujours la vérification cryptographique indépendante. Aucun
fichier Gallery, base Sony ou jeu n'est supprimé ni restauré par ces
contrôles ; aucune preuve de fonctionnement PS5 13.60 n'en découle.


## Capsules privées : publication liée au descripteur écrit (25 octobre 2026)

La sauvegarde et la restauration **hôte uniquement** conservent
désormais ouvert le fichier de staging jusqu'après sa publication.
`publish()` refuse que le nom `.part` désigne un autre inode que
celui effectivement écrit, et revalide après le `linkat` les deux noms
contre le descripteur du fichier original avant tout nettoyage.
Deux scénarios de course (sauvegarde et restauration) remplacent
le temporaire juste après `fsync` **sans erreur d'E/S** : le contenu
étranger et le staging original doivent rester intacts et aucun reçu de
publication ne doit être émis. Cela complète les protections des marqueurs
de groupe, mais ne rend toujours pas l'ultime `unlinkat` atomique
face à un tiers non coopératif. Aucun code de suppression de capture
sur console n'est activé.


## Groupe privé : marqueur COMMIT rattaché au fichier réellement écrit (25 octobre 2026)

Le prototype de sauvegarde/restauration privée **hôte uniquement**
garde désormais ouvert son descripteur de staging lors du `linkat`
de publication du marqueur `__COMMIT` / `__RESTORED`.
Il vérifie que le fichier temporaire visible avant publication est
bien l'inode ayant reçu et synchronisé le manifeste, puis vérifie
que les deux noms après le lien ciblent exactement cet inode.
En cas de substitution, la publication est refusée **sans suppression
du temporaire étranger**. Une fixture C renomme le staging
juste après `fsync` et le remplace avant `linkat` ; elle exige
l'absence de marqueur COMMIT et la préservation des deux fichiers.
Le dernier `unlinkat` de nettoyage d'un staging publié garde une
fenêtre de course POSIX face à un acteur externe de même UID ignorant
le verrou `flock` : cela reste un prototype privé, non une preuve
de transactions Sony ou une autorisation de nettoyer sur PS5.


## Échecs des copies privées : interdiction d'effacer un temporaire remplacé (25 octobre 2026)

Le prototype **hôte uniquement** de sauvegarde/restauration laisse désormais
les fichiers `.part` non publiés en place après un échec ou une
annulation. L'ancien nettoyage automatique avec `unlinkat(dir, nom)`
pouvait effacer un fichier tiers déposé sous le même nom après la
création du temporaire ; POSIX n'offre pas de suppression conditionnelle
sur l'identité de l'inode. Les nouveaux tests remplacent artificiellement
le fichier temporaire durant `fsync`, provoquent `EIO` et vérifient que
le fichier de remplacement demeure intact dans les deux sens.
Les noms `.part` résiduels bloquent volontairement toute réutilisation
automatique (`O_EXCL`) jusqu'à audit manuel. Le marqueur de validation
`__COMMIT` d'un **groupe** applique désormais le même comportement :
une annulation ou erreur laisse le temporaire intact, y compris lorsque
son nom a été remplacé par un fichier tiers. Un test C simule cette
substitution pendant l'annulation du commit privé. Les noms déjà publiés
sont désormais refusés **avant** une nouvelle copie lourde, avec
`linkat` toujours utilisé pour le refus atomique final.
Les racines PS5 `/system_data`, `/app0`, `/update` et les
pseudo-filesystems `/dev`, `/proc` sont explicitement interdits
comme espaces privés de laboratoire. Aucune écriture dans la Galerie
réelle et aucune suppression sur console ne sont autorisées.



## Helper HTTP : cadrage strict et refus des origines ambiguës (25 octobre 2026)

Le serveur de diagnostic (lecture seule, loopback) impose une ligne de
requête à trois champs exacts et les seules versions HTTP/1.0 ou 1.1.
Les caractères NUL (refusés dès la réception, avant le parsing), données/pipelines dans le même message et en-têtes
`Content-Length` / `Transfer-Encoding` sont refusés. `Host` est
unique et `Origin`, `Referer` et `Sec-Fetch-Site` ne peuvent pas
apparaître plusieurs fois, même avec des valeurs identiques. La CI
injecte de vraies requêtes TCP malformées en plus des tests navigateur
et vérifie les codes 400/403/405 ; aucune opération de nettoyage n'est
activée. C'est une réduction de surface d'attaque du helper, pas une
élévation de droits ni une preuve matérielle de la console.


## Métadonnées Galerie : refus d'accès et lecture partielle (25 octobre 2026)

Le lecteur natif des fichiers compagnons `.dat` marque désormais
la couverture **partielle** si un fichier présent pendant l'inventaire
ne peut pas être ouvert. Il assemble aussi les lectures POSIX courtes
jusqu'à la fin de la fenêtre bornée, au lieu de publier un titre
tronqué. Un EOF prématuré ou un champ de nom non terminé sur les
160 octets lus produit une couverture partielle et aucun nom de jeu.
La fixture de Galerie injecte un `EACCES`, des lectures d'un octet,
un EOF prématuré et une mutation pendant lecture. La présence des
autres captures n'est pas effacée du catalogue. Ce contrôle reste en
lecture seule et n'autorise aucune suppression.

## Comparaison de noms HTTP sans accès hors limites (25 octobre 2026)

La classification des champs des trois protocoles de lecture locale utilise
désormais une comparaison dont la taille du nom attendu est connue à la
compilation. Tout nom reçu de longueur différente est rejeté avant lecture
du littéral. Les tests incluent des noms d'en-têtes de 700 à 3 000 caractères
et s'exécutent sous AddressSanitizer/UBSan en plus des builds Linux/macOS.
C'est une protection contre un dépassement de lecture côté décodeur, pas
une preuve d'authentification du service local.

## Durcissement de la lecture HTTP locale (25 octobre 2026)

Les trois décodeurs HTTP du titre, du helper et du moteur AMPR appliquent
maintenant une validation commune de la syntaxe des noms d'en-têtes.
Les noms d'en-têtes HTTP étant insensibles à la casse, un second
`content-length`, un `CONTENT-TYPE` ou un
`transfer-encoding` déguisé ne peuvent plus contourner les contrôles
contre les longueurs ambiguës et les réponses chunked. Les champs de
cadrage reconnus restent obligatoirement dans l'orthographe attendue
des composants embarqués. Le titre refuse également toute ligne de
statut helper différente de `HTTP/1.1 200 OK`. Les tests hôte couvrent
ces variantes dans les trois protocoles. Cette validation de trames
n'authentifie pas un processus ni ne donne de droits sur la PS5.

L'application n'a **pas encore atteint la fin du développement**. Distinguer
explicitement deux objectifs évite de faire passer un build CI pour une
application PS5 validée.

## Sauvegarde privée d'une miniature externe (24 octobre 2026)

Le prototype **hôte uniquement** peut maintenant vérifier une copie
SHA-256 d'une miniature JPG.JPEG conservée sous thumbnails/photo,
au sein d'une transaction privée contenant aussi le média visible et
son fichier DAT. Quatre membres au maximum, 512 Mio au total,
vérification des identités, refus d'un groupe sans la miniature connue
et restauration strictement dans un second espace privé 0700.
Des fixtures exécutent les vraies routines de sauvegarde/restauration.

Ceci **ne prouve pas** le graphe complet de la Galerie Sony. Les
bases, index, éventuels journaux WAL/SHM et les dépendances inconnues
ne sont pas inclus ; aucune écriture en Galerie ni suppression n'est
autorisée dans le titre PS5.

## ELF intégrés : prévalidation renforcée (24 octobre 2026)

Avant toute transmission au chargeur local, le titre contrôle l'identité
x86-64, le type exécutable, la version, la taille de l'en-tête et la
table des segments des deux ELF intégrés. Une lecture partielle ou un
fichier malformé ne transmet aucun octet. Le validateur du titre et le
build du packer appliquent les mêmes contraintes, avec des lectures
d'en-tête bornées plutôt que charger tout l'exécutable en RAM. Les
fixtures injectent objets non chargeables et table de segments invalide.
Cela ne constitue pas une preuve de chargement réel sur FW 13.60.

## Helper de permissions : EPERM et EACCES (24 octobre 2026)

Le précontrôle en lecture seule du helper classe maintenant les deux
erreurs POSIX `EACCES` et `EPERM` comme « denied /
permission-denied » lors de l'ouverture de la racine, de l'examen de
l'entrée et de l'ouverture ancrée d'un dossier. Un refus `EPERM` ne
doit plus être noyé dans les erreurs « unknown ». Un test hôte injecte
`EPERM` aux trois étapes et confirme que l'inventaire ne prend
pas une autorisation manquante pour un dossier vide. Aucun droit
supplémentaire n'est demandé ou accordé par ce changement.

## Diagnostic de stockage : parcours incomplet détecté (24 octobre 2026)

La page native Accès au stockage prend maintenant en compte les échecs
de `closedir`, au même titre que les erreurs de `readdir`. La sonde
reste « listable » avec ses entrées déjà vues, mais son statut I/O
indique une **couverture partielle**, jamais une inspection complète.
Deux injections déterministes d'erreur `EIO` dans les tests de navigation
vérifient le comportement réel du scanner, sans accès à une PS5.
Si la lecture est limitée et la fermeture échoue, les deux problèmes
sont signalés, sans aucune écriture ni suppression.

## Deux livraisons distinctes

| Livraison | État du code | État de preuve | Verdict |
|---|---|---|---|
| **Diagnostic natif, lecture seule** | Interface DualSense, sondes, inventaire, catalogue, filtres, prévisualisation partielle, comparateur borné et analyse de compagnons compilés | Tests hôte et génération d'un titre homebrew ; **pas de validation matérielle sur 13.60** | **Candidat technique, non validé sur console** |
| **Nettoyeur réversible** | Prototypes SHA-256 hôte seulement ; effacement désactivé | Index multimédias, miniatures, privilèges et transaction de suppression/restauration PS5 non démontrés | **Non terminé / bloqué** |

## Cinq conditions bloquantes avant livraison fonctionnelle

1. **Installation/exécution native sur FW 13.60 :** gestionnaire de titres compatible, installation/désinstallation, démarrage sans WebKit, veille/reprise, contrôles DualSense.
2. **Permissions réelles :** vérifier pour le titre et le helper séparément chaque racine d'accès, les erreurs et limites, sans confusion entre descripteur ouvert et permission d'effacer. **Préparation logicielle intégrée :** le titre distingue refus/absence/erreur et le bilan hôte teste qu'une sonde incomplète impose une couverture partielle. Le scanner commun signale maintenant comme **partiels** les chemins trop longs, erreurs de lecture de dossiers, analyses sans aucune racine parcourue **et inventaires dont seuls les dossiers applicatifs (ou USB) sont accessibles mais aucune racine de captures photo/vidéo ne l'est**. Un résultat de zéro capture ne peut donc pas être présenté comme complet en l'absence de couverture réelle de la Galerie. Aucune preuve matérielle 13.60 n'est encore disponible.
3. **Dépendances de la Galerie :** [cartographie comparative des sources PS5](GALLERY_DEPENDENCIES.md) ajoutée : schémas divergents entre média + `.dat`, `.meta` / `.ext` et thumbnails `.jpg.jpeg` ; ces métadonnées connues mais non supportées bloquent désormais la validation d'un groupe. **Reste à prouver** sur console le lien captures → sidecars/miniatures → bases et index (WAL/SHM inclus), sans inventer de chemin ni de règle de purge.
4. **Sauvegarde/restauration système réellement réversible :** transaction vérifiée sur PS5, refus des conflits de fichiers, annulation, arrêt brutal, reconstitution des index et cohérence avant/après.
5. **Nettoyage et mesure :** critères réellement sûrs par catégorie de données, confirmation explicite, contrôle de l'espace libéré et exclusion des sauvegardes, licences, bases actives, plugins chargés.

**Aucune de ces cinq preuves matérielles ou fonctionnelles n'est acquise** du seul
fait que les builds CI passent. Le prototype de copie hôte ne compte pas comme
une restauration de la Galerie PS5.

## Ce que produit désormais chaque build natif

L'artefact de CI contient un `release-readiness.json` calculé à partir du
`manifest.json` du titre. Il n'accorde jamais automatiquement une validation
PS5 ou une autorisation de suppression :

- `nativeArtifact.status: packaged-not-console-tested` : composants présents dans le manifeste d'archive ;
- `readOnlyDiagnosticRelease.status: hardware-validation-required` : candidat de diagnostic non validé physiquement ;
- `reversibleCleanerRelease.status: blocked` : nettoyage réversible non autorisé ;
- `blockingConditions` : les cinq preuves restant à établir.

Un test automatisé rejette tout manifeste annonçant
`firmware1360HardwareValidated=true`, `cleanupEnabled=true` ou des
permissions supposées élevées. L'archive du titre reste un **homebrew non signé
retail**, et l'absence d'essai matériel reste visible.

## Méthode de clôture

Conserver **une seule branche `main`**. Prioriser les régressions qui empêchent
la stabilité, l'installation et l'analyse complète dans le sandbox réellement
disponible. Reporter les fonctions non essentielles (formats additionnels,
cosmétique, élargissement spéculatif des chemins) ; ne pas fabriquer de
« pourcentage terminé » sur des fichiers inventoriés.

La demande de validation console ne remplace ni les tests automatisés ni
l'implémentation des fonctions attendues. Aucun résultat « vert » de CI
n'autorise à publier un effaceur automatique.

## Compression des jeux — état du composant natif

Le module AMPR/LZ4 est embarqué dans un seul titre CleanMyPS5 et son
lancement utilise le chargeur ELF du jailbreak existant. Les dossiers
homebrew peuvent être précontrôlés puis soumis à une conversion **après
deux confirmations**, sans écrasement ni suppression de l'original.
Le protocole affiche également l'avancement compact du moteur.
**La compilation hôte/PS5 ne constitue pas une validation sur console
13.60 ni une garantie de démarrage d'un jeu converti.** Le nettoyage
destructeur général demeure désactivé.

La compression intégrée refuse désormais une conversion si une autre
est active ou en file, bloque la répétition d'une demande acceptée ou
ambiguë et propose un moniteur de fond accessible par Triangle.
Les tests hôte du moteur réel sont inclus dans le build de titre.

Le parcours de compression accepte désormais, en plus des dossiers,
les images de jeux reconnues par ShadowMountPlus. L'identité de chaque
image est confirmée par une API en lecture seule du moteur AMPR
intégré, puis vérifiée à nouveau avec la taille source avant création
d'une sortie distincte. Les sources `.ffpfsc` déjà compressées sont
exclues, et aucun test de gameplay PS5 13.60 n'a été effectué.

La CI vérifie désormais les protections du moteur dans le **C
transformé** avant sa compilation. Toute disparition des blocages de
suppression, d'écrasement, de forçage ou de contournement de
vérification doit faire échouer le build. Un jeu image identifié mais
non monté reste non éligible à la compression : le précontrôle
différencie explicitement l'état `later` d'une compatibilité démontrée.

### Barrières supplémentaires pour le moteur embarqué

Les sources de jeux et les destinations du moteur AMPR sont désormais
limitées aux racines connues de l'application. Le moteur refuse par
défaut toutes les requêtes HTTP POST sauf démarrage, pause et reprise
d'une compression. Il compare aussi de nouveau les métadonnées d'une
image après précontrôle pour éviter une soumission fondée sur une
identité devenue périmée. La suite native CI compile la version
transformée du moteur et vérifie ces barrières, sans pouvoir
remplacer un essai matériel PS5 13.60.


## Audit des privilèges (24/10/2026)

Le titre PS5, le helper optionnel en lecture seule et le moteur AMPR sont
**trois acteurs séparés**. Les droits d'un helper lancé par elfldr ne
sont pas transférés au titre installé ; UID/EUID et accès lecture ne
sont **pas** une autorisation de suppression. Le contrat compilé
`native/privilege_contract.hpp` distingue leurs opérations et refuse
les écritures de Galerie, caches système, installations et autoload.

NiakPS5 et Eden Encore disposent d'un élévateur ponctuel à handshake
PID/Title ID, clone des credentials, validation et rollback. Il ne faut
pas importer l'élévation générique dans un nettoyeur avant une transaction
de sauvegarde/restauration des index PS5 réellement validée. Voir
[la revue dédiée](PRIVILEGE_AUDIT.md).


### Doubles liens : aucune promesse d'espace récupérable

L'analyse des doublons exclut désormais **toutes** les entrées avec
`st_nlink != 1`, même si le second lien physique se trouve hors de
l'arborescence examinée. Elle ne déduit jamais l'espace récupérable
à partir de deux noms identiques, et signale une couverture partielle
lorsqu'elle a dû exclure des fichiers à liens multiples. Les tests de
serveur simulent deux doublons réellement indépendants et plusieurs
hardlinks, dont un vers un dossier non scanné.

### Couverture galerie : photos ET vidéos

Le scanner exige maintenant deux observations indépendantes **complètes**
des répertoires `/user/av_contents/photo` et
`/user/av_contents/video` pour déclarer la couverture de la Galerie
non partielle. Une seule famille accessible, l'autre absente ou refusée,
ne peut plus aboutir à `partial=false`, même si les répertoires
homebrew ou système sont lisibles. Une fixture vérifie le passage de
photos seules (couverture partielle) à photos + vidéos lisibles (non
partielle), puis l'effet d'un scan tronqué. Ce contrôle ne couvre pas
les index Sony ni leur restauration et n'autorise aucune suppression.

### Revue du helper intégré (24 octobre 2026)

Le helper facultatif de diagnostic, **embarqué dans le même titre natif**,
se lance manuellement depuis la page Accès et demeure un processus distinct
sans élévation de l'interface `PPSA99784`. La CI contrôle toutes ses unités
C effectivement liées et leurs en-têtes, ainsi que les fichiers copiés dans
le titre. Les compteurs retournés par le helper ne remplacent jamais les
médias réellement sélectionnables dans le titre sandboxé. Sur PS5, ses
routes GET se limitent aux quatre endpoints agrégés explicitement requis ;
aucun streaming de média ni opération de suppression ne lui est accessible.

Un scan ne peut être déclaré non partiel qu'après lecture complète de
**photos et vidéos** (les index Sony restent hors couverture). Les accès
supplémentaires du helper, un UID nul ou une validation CI ne prouvent
ni la sûreté du nettoyage ni une récupération d'espace. **Nettoyeur
réversible toujours bloqué** sans validation matérielle des transactions
et de la reconstruction des index Galerie sur PS5 13.60.

### Aperçus photo et métadonnées vidéo : identité cohérente

Les lecteurs natifs `media_preview.c` et `video_metadata.c` comparent
maintenant le **snapshot complet** produit par l'inventaire
(périphérique/inode/taille, secondes et nanosecondes `mtime` et `ctime`)
avant toute lecture. Les vérifications de stabilité existantes continuent
après lecture, y compris pour les fichiers JPEG compagnons des JXR.
Les tests de lecture refusent les snapshots artificiellement périmés à
la nanoseconde, même lorsque la taille et l'heure à la seconde sont
identiques. Cela ne représente pas une preuve de suppression/restauration
sûre des captures de la Galerie PS5.

### Tests de couverture avant packaging natif

Les lecteurs photo et vidéo partagent la même identité à la nanoseconde.
La CI Linux, macOS et sanitizers exécute maintenant une fixture photo PNG
minimaliste sans dépendance à Pillow, qui couvre lecture actuelle, refus
des snapshots périmés et liens physiques. La suite existante de lecture
MP4/WebM, avec conteneurs malformés, complète également la matrice hôte.
Les tests natifs incluant décodage JPEG/PNG/JXR et packaging PS5 restent
maintenus : aucun test hôte ne remplace une preuve matérielle 13.60.

### Autoload : diagnostics résistants au remplacement de fichier

Le lecteur natif conserve le descripteur du répertoire parent et revalide,
après la lecture, l'identité du fichier sous son **nom actuel**, et pas
seulement celle du descripteur déjà ouvert. Les contrôles comparent mode,
liens, taille, inode/périphérique ainsi que mtime et ctime à la nanoseconde.
En cas de remplacement du chemin ou d'une modification très rapide, l'état
devient « unreadable / non vérifiable », et non une référence active prouvée.
Deux courses déterministes sont exécutées sur les runners hôte et sanitizers :
renommage pendant la lecture et changement de nanoseconde avant publication.
Aucune configuration d'autoload n'est écrite par l'application.

Les diagnostics d'autoload refusent également tout fichier texte contenant un
**octet nul interne** : aucun statut « aucune référence » ne peut être
conclu en ignorant silencieusement les octets qui suivent. Une fixture
vérifie le classement « non vérifiable » de ce cas, sans modifier les
configurations de la console.

### Consultation des supports externes depuis le titre natif

La page **Accès** utilise maintenant L1/R1 pour parcourir six pages de
sondes, comprenant les racines internes ainsi que les seize montages
`/mnt/usb0…7` et `/mnt/ext0…7`. Les montages facultatifs ne sont
interrogés que par ouverture ancrée sans symlink et `fstatvfs`, sans
énumération du contenu des jeux. Le libellé **CAPACITÉ SEULE** distingue
cette mesure du nombre de fichiers réellement parcourus. L'absence
d'un support USB ne change pas la conclusion de couverture des six
sondes essentielles, en particulier la Galerie photo et vidéo.

Le helper PS5 conserve ses seules routes agrégées autorisées : l'inventaire
plus détaillé des volumes via `/api/volumes` reste une interface
diagnostique hôte. Ni ce bilan ni l'UI ne constituent une preuve d'octets
récupérables ou une autorisation d'effacement.

### Galerie : une métadonnée corrompue ne masque plus les captures suivantes

Le scanner de la Galerie conservait `rootPartial=true` pour signaler un
`.dat` corrompu, puis utilisait **ce même état** pour arrêter toute la
traversée du dossier parent. Une métadonnée illisible pouvait donc masquer
des captures saines situées après elle. Désormais, une erreur récupérable
marque bien l'inventaire **partiel**, mais le parcours borné continue :
les photos et vidéos encore lisibles demeurent consultables. Seuls une
annulation, une limite de budget ou un échec de la sortie JSON arrêtent
la traversée. La fixture hôte crée une métadonnée de trois octets,
suivie de deux photos et d'une vidéo, et exige de retrouver les trois
médias tout en conservant `cleanupValidated=false`.

### Limite de l'index des noms de jeux

Les noms extraits des fichiers de métadonnées `.dat` sont plafonnés à
**128 lectures/titres** pour préserver le temps de scan et la mémoire du
titre natif. Quand un 129e titre nouveau apparaît, le scanner **ne déclare
plus le catalogue complet à tort** : il continue à lister les captures,
mais marque la couverture du dossier `partial`. Une fixture avec 130
jeux distincts exerce ce cas, sans demander l'accès à une vraie PS5.
Ce seuil limite seulement l'association des noms, pas le nombre maximal
de médias du catalogue (10 000 sous les limites globales de scan).

### Applications/homebrews : inventaire résistant aux erreurs de dossier

Le scanner des homebrews détecte les erreurs `readdir()` (distinctes
d'une fin de dossier) et `closedir()` : ces erreurs rendent le bilan
**partiel** au lieu de le laisser faussement « complet ». Une anomalie
dans un seul sous-dossier ne masque plus automatiquement les autres
fichiers lisibles, à condition de rester dans les limites fixes de temps,
profondeur et nombre de visites. La fixture hôte injecte une erreur EIO
à la fin d'une énumération et une erreur de fermeture, vérifie le statut
partiel ainsi que la présence des applications observées. Toujours
lecture seule : aucun titre, plugin ou fichier système n'est supprimé.

### Doublons photo : observations conservées après une erreur de fichier

La comparaison des doublons sur la Galerie photo signalait déjà les
échecs de lecture/statut comme `partial`, mais arrêtait toute l'énumération
sur ce même booléen. Une image illisible pouvait donc masquer les deux
images identiques suivantes. L'explorateur continue désormais après un
échec récupérable de `fstatat` ou de sous-dossier, dans les limites fixes
(128 images, 8 000 entrées, 6 s et 128 Mio d'I/O). Il conserve `partial`
et n'autorise aucune suppression. Le test injecte `EIO` sur la **première**
image rencontrée, puis exige la comparaison exacte des deux autres,
sans supposer un ordre d'énumération du disque.

### Bilan des applications sans double comptage évident

Le rapport hôte d'applications inspectait séparément `/data/homebrew`
ainsi que `/data/etaHEN/plugins`, puis retrouvait ces mêmes dossiers
comme entrées « app-data » du parent `/data`, gonflant les tailles
nominales. Le scanner exclut désormais ces conteneurs du parent et
inspecte séparément les autres entrées de `/data/etaHEN`, sans
recompter `plugins`. Un test contrôle le total exact sur des fichiers
synthétiques et le maintien des exclusions de sauvegardes/configurations.
Les chiffres restent **nominaux et observés**, pas une quantité de stockage
récupérable ; aucune suppression ni désinstallation n'a été activée.

### Scanner de jeux : erreurs d'accès (24 octobre 2026)

Une erreur de `fstatat` pour un nom sans extension ou de `openat`
pour un dossier déjà observé peut masquer un jeu. Ces erreurs marquent
désormais le scan **partiel** sans interrompre l'inventaire des éléments
sains. L'absence normale de `eboot.bin` ou `sce_sys` ne suffit pas
à marquer tous les dossiers comme défectueux. Tests hôte déterministes
avec injection d'`EIO` et d'`EACCES`, sans écriture sur console.

### Couverture des racines essentielles d'applications (24 octobre 2026)

L'inventaire natif vérifie séparément la traversée de `/data` et de
`/user/app`. Leur absence, un refus d'accès ou une erreur ne doivent plus
produire un faux total « zéro application, scan complet » : le bilan devient
**partiel**, même lorsque des homebrews restent visibles. Le résultat JSON
retourne `rootCoverage.appData` et `rootCoverage.installedTitles` et la
page Apps et plugins affiche le diagnostic `/DATA` et `/USER/APP` sur
les deux pages. Les racines optionnelles plugins/USB peuvent légitimement
être absentes. Tests hôte : deux racines lisibles, une racine manquante et
un environnement entièrement inaccessible. Ce contrôle reste en lecture
seule et ne donne aucun droit d'effacement.

### Plus grosses installations observées (lecture seule)

L'écran natif **Apps et plugins** offre deux pages par L1/R1. La première
présente les totaux par catégorie et la seconde les **quatre éléments de
premier niveau les plus volumineux** (homebrew, plugin, données d'app ou
installation). Le scanner calcule le classement à partir des tailles
nominales de fichiers visibles dans ses budgets, en ignorant les dossiers
et fichiers privés déjà exclus. Le titre ne reçoit que quatre libellés
ASCII assainis, leur catégorie, leur taille nominale et le statut partiel,
jamais les chemins complets. Une taille ne démontre pas qu'un fichier peut
être supprimé. L'écran reste en lecture seule, sans désinstallation ni
nettoyage automatique.
