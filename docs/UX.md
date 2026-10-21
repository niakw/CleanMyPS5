# CleanMyPS5 — UX console / DualSense

## Principes

CleanMyMac sert d'inspiration pour le **parcours** (une vue simple, des catégories, la revue), sans copier ses ressources ni sa présentation. CleanMyPS5 est conçu pour l'écran TV et la manette.

- **Pagination native :** 36 éléments maximum par page pour prévenir les freezes WebKit quand une bibliothèque contient des milliers de captures. L2/R2 passent les pages, les sélections sont conservées entre pages.
- **Une décision par écran.** Un CTA principal, plus une action secondaire compréhensible.
- **Lisible à distance.** Titres larges, catégories bien séparées, marges généreuses, contrastes élevés.
- **Navigation explicite.** Croix/joystick: déplacement spatial; ✕ valider; ◯ retour; L1/R1 sections.
- **Focus toujours visible.** Contours fortement contrastés, focus conservé après mise à jour d'une sélection, placé sur le contenu lorsqu'on entre dans une catégorie.
- **Aucun piège.** Aucun nettoyage en un clic, aucune case présélectionnée, aucune capture effacée automatiquement.
- **Même contenu sur TV et navigateur.** Clavier, souris et tactile en secours; manette via Gamepad API lorsqu'elle est disponible.
- **Vérité des chiffres.** Ne jamais afficher comme « libres » des Go mesurés sur un montage limité au lieu du vrai SSD console.

## Parcours cible

1. Accueil : scan intelligent, stockage réel seulement s'il a été vérifié, état de la console.
2. Découverte : scan strictement en lecture seule, avancée, sources connues/inaccessibles.
3. Synthèse : taille potentiellement récupérable par catégorie et niveau de risque.
4. Revue : groupes par jeu, chronologie, taille, doublons hash-confirmés, prévisualisation.
5. Sélection explicite : uniquement des éléments connus, jamais les sauvegardes ni les données système.
6. Opération future : export/quarantaine si supporté, double confirmation, vérifications avant et après, résultat réel.

## Cas etaHEN

Le fait d'observer « etaHEN Toolbox » dans les paramètres **ne prouve pas** qu'une configuration en `/data/etaHEN` est inutile ou que son effacement supprimera l'entrée ShellUI. Il faudra distinguer :
- code injecté en RAM ;
- payload lancé automatiquement ;
- fichiers de configuration et plugins ;
- éventuel enregistrement d'interface ou cache.

**Aucune suppression automatique d'etaHEN** sans validation de la cause.

## Critères de validation PS5 13.60

Manette DualSense réelle, parcours complet sans clavier, mode sans défilement horizontal, correct retour ◯, lisibilité en 1080p et 4K, gestion des chemins manquants, tests sans suppression, pas de demande de privilèges supplémentaires pour un simple inventaire.

Le navigateur PS5 et sa prise en charge Gamepad devront être vérifiés physiquement avant de considérer ce jalon comme terminé.

## Filtres médias (v0.3)

- Fichiers volumineux : seuil de 250 Mio, calculé à partir de l'inventaire réel.
- Même taille : **simple présélection de candidats**, aucune identité de contenu inférée sans hash et comparaison complète ultérieure.
- Filtre par identifiant de titre (extrait du chemin) ; il ne remplace pas le nom commercial d'un jeu.
- Date de dernière modification à partir des métadonnées POSIX, sans interprétation de la date de création réelle.
- Pages limitées à 36 lignes ; le nombre de fichiers et la couverture de scan sont séparés des tailles totales du SSD.

## Capture preview (v0.4)

- A separate **Voir la capture** button appears only on scanned JPG/PNG files when connected to the local native backend.
- Read-only preview opens in a controller-friendly overlay. Circle / Escape dismisses it, restoring the prior focus.
- Imported JSON inventory never unlocks media access; demos are never treated as real files.
- Videos and JXR/HDR screenshots are not yet displayable through this endpoint. No thumbnail conversion/cache is written to disk.

## Review workflow (v0.4)

The action **Examiner ma sélection** now opens a distinct dialog listing selected files and their occupied size, with explicit notice that none are verified to be useless. There is no delete button. Users may download a **review-only JSON manifest** on browsers that support downloads; this is a client-side file operation and does not modify PS5 files. A 500-entry cap prevents accidental massive exports.

Circle closes the review panel and restores the previously selected control. Gamepad focus is constrained to modal actions.

## Preview vidéo par plages (v0.5)

Les vidéos MP4/WebM sont affichées dans la modale avec contrôles accessibles à la manette : croix sur le bouton Lecture/Pause, commandes ±10 secondes, rond pour revenir. Lecture progressive strictement bornée à un morceau d'1 Mio maximum par requête ; aucun téléchargement complet, aucune conversion sur SSD, aucune miniature écrite.

La présence d'un conteneur MP4 ou WebM valide ne garantit pas que le navigateur PS5 puisse décoder le codec vidéo embarqué. Cette étape nécessite une validation physique du firmware et de WebKit.
