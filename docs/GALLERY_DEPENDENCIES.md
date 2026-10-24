# Galerie PS5 — dépendances vérifiables et limites d'automatisation

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
