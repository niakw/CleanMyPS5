# CleanMyPS5 — critères de clôture (constat vérifiable)

L'application n'a **pas encore atteint la fin du développement**. Distinguer
explicitement deux objectifs évite de faire passer un build CI pour une
application PS5 validée.

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
