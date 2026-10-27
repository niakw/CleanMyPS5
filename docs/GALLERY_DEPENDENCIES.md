# Galerie PS5 — dépendances vérifiables et limites d'automatisation

## Montages imbriqués dans un album ou une miniature (27 octobre 2026)

Lors du relevé des compagnons de capture, chaque fichier JPG/JXR/PNG,
vidéo ou DAT doit appartenir au même périphérique (`st_dev`) que le
dossier de l'album réellement ouvert. Le candidat connu de miniature
`.jpg.jpeg` doit de même appartenir au périphérique de son dossier
`thumbnails/photo`. Si un fichier apparaît monté depuis un autre
système de fichiers, le groupe est classé ambigu ou la miniature
suspecte. Aucun octet ni plan de sauvegarde privée n'est publié.

Le test d'injection C simule séparément une DAT étrangère et une
miniature étrangère sans aucune écriture de fichier, et exige le refus
de l'inspection ainsi que du planificateur privé. Ce garde-fou est
conservateur : une identité de périphérique identique ne prouve pas
l'absence de bind mount et aucune suppression Galerie n'est permise.


## Mutation pendant l'énumération du dossier des miniatures (27 octobre 2026)

Le scanner prend maintenant un instantané des métadonnées du dossier parent
avant readdir, puis le compare immédiatement après l'énumération. Avant,
le premier instantané intervenait trop tard : une modification du parent
pendant l'énumération pouvait rester invisible si la miniature attendue
ne changeait pas. Le relevé est désormais classé miniature suspecte
et la sauvegarde privée du groupe est refusée.

Une injection hôte modifie les permissions du parent pendant readdir,
sans toucher aux fichiers média. Elle exige le refus de l'inspection
et du planificateur, ainsi que la remise à zéro des membres candidats.
Cette preuve n'atteste ni les index Sony ni la compatibilité console.


Ce document est une **cartographie de sources publiques**, pas une validation de
la base de données d'une PS5 donnée. Aucune voie de suppression, modification
de l'index multimédia ou restauration sur console n'est activée.

## Comportements constatés dans des projets indépendants

| Projet / source consultée | Observation issue du code ou README | Portée et incertitude |
|---|---|---|
| [ProsperoGallery — `src/gallery.c`](https://github.com/AmineWallah/ProsperoGallery/blob/main/src/gallery.c) et [README](https://github.com/AmineWallah/ProsperoGallery) | Parcours `/user/av_contents/{photo,video}/NPXS40087/<TitleID>/<dir>/`, avec média JPG/PNG/JXR/MP4/WebM et fichier `.dat` à proximité. Lecture du nom de jeu dans le `.dat` à l'offset 0x44. | Le README indique des essais FW **13.60**, mais suppose que `NPXS40087` soit identique sur toutes les consoles. Le code n'établit **pas** un contrat de suppression/retour arrière de la Galerie Sony. |
| [PShare — `src/pshare/core/media.cpp`](https://github.com/StonedModder/PShare/blob/main/src/pshare/core/media.cpp) | Une routine de suppression d'un média tente aussi de retirer des fichiers de même stem `.dat`, `.meta`, `.ext`. Le parcours des titres cherche le `.dat` à côté des captures. | Observation du **comportement de PShare**, pas preuve que tous ces fichiers sont toujours présents ou suffisants. Les erreurs de suppression des secondaires sont ignorées par ce projet : **ne pas reprendre cette politique**. |
| [PS5-Suite — `payload/main.c`](https://github.com/manos555555/PS5-Suite/blob/main/payload/main.c) | Pour un schéma particulier de screenshots, le code décrit un original `.dat`, un `.meta` et un thumbnail distinct sous `/user/av_contents/thumbnails/photo/…/<stem>.jpg.jpeg`. | Les conventions peuvent différer par titre, type de capture et firmware. Une association par chaîne de chemin **n'est pas une transaction validée**, et l'absence d'un objet au chemin supposé ne prouve pas qu'il n'existe aucun autre objet lié. |

**Conclusion technique :** au moins deux modèles de fichiers apparaissent dans ces
implémentations : des médias JPG/JXR/MP4 accompagnés d'un `.dat`, et un schéma
dans lequel une image visible est un thumbnail `.jpg.jpeg` distinct d'un
original `.dat` et d'un `.meta`. Déduire une règle globale d'un seul de ces
modèles risquerait d'effacer la mauvaise ressource ou de laisser une capture
fantôme dans l'interface Sony.

## Couverture des refus de permissions lors des compagnons (25 octobre 2026)

L'inspection en lecture seule distingue désormais les erreurs POSIX
`EACCES` et `EPERM` lors de l'ouverture de la racine, de la traversée
des dossiers et de la vérification des variantes de capture
(`.dat` compris). Un compagnon non accessible ne devient ni un
fichier « suspect », ni une preuve de dossier complet : l'interface
affiche **accès refusé**, sans révéler son chemin, et aucun compteur
de groupe fiable n'est publié. La planification d'une sauvegarde privée
est également refusée. Des fixtures injectent ces deux erreurs dans
le `fstatat` des compagnons et vérifient le refus sans écriture.

## Miniatures : variantes visibles inconnues dans le même dossier (25 octobre 2026)

L'examen en lecture seule du dossier `thumbnails/photo` du stem
sélectionné parcourt maintenant au plus **1 024 entrées**, à l'aide
d'un descripteur indépendant et de `O_NOFOLLOW`. Un fichier
`REC_001.png`, `REC_001.jpg.jpeg.tmp`, `.REC_001.part` ou une
variante de casse du même stem empêche de considérer le seul
`REC_001.jpg.jpeg` comme une couverture suffisante.
Cela vaut aussi si la miniature attendue est absente : l'existence
d'une variante visible conduit à un état **suspect**, et non à une
absence rassurante. La lecture d'un dossier incomplète ou annulée
bloque également l'observation. Une absence n'est publiée qu'après
revalidation de l'identité du dossier et de son emplacement.
Le planificateur de sauvegarde privée refuse alors le groupe entier,
sans modification des captures, miniatures ni index Sony.
Cette couverture du dossier *connu* ne garantit toujours pas la
découverte de toutes les dépendances système.

## Renommage des dossiers Galerie pendant l'inspection (25 octobre 2026)

Un descripteur de dossier ouvert reste lisible même après renommage
de l'album. La découverte des compagnons et du candidat de miniature
externe revalide maintenant en lecture seule **le chemin parent
complet depuis la racine**, via `openat` et `O_NOFOLLOW`, avant de
publier les compteurs ou un groupe privé.
Un dossier renommé puis remplacé au même chemin provoque
`FICHIER OU DOSSIER MODIFIE` (capture) ou
`MINIATURE SUSPECTE` (thumbnail), et aucun volume nominal ni groupe
de sauvegarde privé n'est accepté.
Les deux courses sont reproduites en tests Linux/macOS/sanitizers.
Cette vérification reste ponctuelle et ne valide aucun index Sony.

## Comportement conservateur de CleanMyPS5

- `capture_companions.c` recherche uniquement les composants **dans le même
  répertoire, au même stem**, à partir d'une capture et de son snapshot
  inode/taille/date ; il n'infère pas les bases, WAL/SHM ni les thumbnails.
- Les fichiers `.meta` et `.ext` associés à un stem sont maintenant détectés
  comme **dépendance non prise en charge** ; l'interface l'indique sans chemin
  privé. Les `.jpg.jpeg`, extensions inconnues ou compagnons cachés font
  échouer la validation au lieu de déclarer une sauvegarde complète.
- Le contrôle complémentaire recherche maintenant, en lecture seule, un
  **candidat** `/user/av_contents/thumbnails/photo/<relatif>/<stem>.jpg.jpeg`
  quand une capture photo a été validée. Il ouvre chaque composant avec
  `O_NOFOLLOW`, vérifie l'identité du fichier et du dossier et ne lit
  aucun pixel. Le résultat natif distingue une présence probable,
  l'absence **à ce seul chemin**, une impossibilité d'accès et une
  entrée suspecte. Aucun chemin, nom ni identifiant n'est remonté dans
  l'interface ; aucune de ces observations n'est assimilée à une
  dépendance définitivement liée à la capture.
- Une miniature extérieure inaccessible ou suspecte rend **toute
  inspection du groupe non fiable** : aucun compteur de fichiers ni
  volume n'est publié. Le prototype de sauvegarde **hôte uniquement**
  refuse un groupe photo si une miniature extérieure est inaccessible
  ou suspecte. Si un candidat stable existe sous le chemin connu, il
  devient un quatrième membre possible d'un **groupe de sauvegarde privé
  hôte**, toujours plafonné à 512 Mio et 4 membres. La copie du
  fichier thumbnail est vérifiée par SHA-256, comme les autres fichiers.
  La restauration ne vise qu'un second répertoire privé à droits 0700 :
  elle ne réécrit **jamais** la Galerie PS5. Les tests vérifient la
  présence du fichier dans le manifeste, sa copie, sa restauration et le
  rejet d'un groupe qui oublierait cette miniature. La CI injecte
  également `EACCES` pour vérifier un refus d'accès. Rien de ceci ne
  prouve l'exhaustivité des index Sony ni n'autorise une suppression.
- Même si les compagnons visibles réussissent, aucun bit ni résultat
  `verified` n'autorise une suppression, puisque la cartographie
  **inter-répertoires et index système** manque encore.
- L'inspection locale reste **en lecture seule**. Le moteur de copies privées
  hôte n'est pas lié dans l'application PS5 ni dans son helper.

## Prérequis de clôture pour un nettoyeur réellement réversible

1. **Sur PS5 FW 13.60**, établir la famille réelle de chaque capture et de ses
   dépendances, avec des exemples JPG, JXR/HDR, MP4/WebM, captures créées
   par plusieurs jeux et captures sauvegardées après mise à jour.
2. Observer séparément les dossiers `photo`, `video`, `thumbnails` et les
   index/BD associées ; identifier les éventuels fichiers de journal
   (WAL/SHM), les références croisées, les créations/suppressions effectuées
   par **la Galerie officielle** et les droits d'accès nécessaires.
3. Construire **une transaction de sauvegarde/restauration du graphe complet**
   avant de prévoir une suppression. Vérifier les empreintes et la
   réapparition de l'élément dans l'interface officielle, sans modifier
   une BD active à l'aveugle.
4. Exiger un mode sans écriture face à un format nouveau, une dépendance
   non identifiée, une capture ouverte/active, un fichier partagé ou
   une visibilité partielle. La compatibilité par version doit être
   prouvée, pas déduite d'un chemin existant.

Ce sont des **portes de validation fonctionnelle**, non des demandes de test
intermédiaire : le diagnostic natif doit rester stable avant la validation
conjointe du logiciel sur console.
