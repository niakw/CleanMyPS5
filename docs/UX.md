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
