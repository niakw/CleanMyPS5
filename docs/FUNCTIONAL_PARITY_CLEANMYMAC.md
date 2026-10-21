# Audit fonctionnel — CleanMyPS5 vs CleanMyMac (21 octobre 2026)

## Objet et méthode

Référence : documentation officielle MacPaw sur Smart Care, My Tools, My Clutter, Applications et Space Lens (octobre 2026). Cet inventaire compare les **fonctions**, pas la charte graphique ni les ressources de marque. Les statuts reflètent uniquement le code présent sur `main`, les tests hôte et les limites documentées ; **aucune fonction PS5 n'a encore été validée sur une console 13.60 réelle**.

Sources :
- https://macpaw.com/support/cleanmymac/knowledgebase/smart-care
- https://macpaw.com/support/cleanmymac/knowledgebase/my-tools
- https://macpaw.com/support/cleanmymac/knowledgebase/duplicate-files
- https://macpaw.com/support/cleanmymac/knowledgebase/large-and-old
- https://macpaw.com/support/cleanmymac/knowledgebase/space-lens
- https://macpaw.com/support/cleanmymac/knowledgebase/missing-features

## Matrice vérifiable

Légende : ✅ implémenté sur hôte seulement ; 🟡 partiel / borné / lecture seule ; ❌ absent ; — non applicable ou inadapté sur PS5.

| CleanMyMac / besoin PS5 | CleanMyPS5 | Écart vérifié et acceptation |
|---|---|---|
| Smart Care : analyse coordonnée | 🟡 | Orchestration lecture seule et scorecard ajoutées (inventaire, stockage, doublons, autoload et chemins). Erreurs par module visibles ; reste non validé sur PS5 |
| Smart Care : recommandation et bilan | 🟡 | Sélection manuelle, aucune suggestion certifiée ni espace réellement libéré |
| Cleanup : fichiers journaux | 🟡 | Journaux etaHEN (.log) répertoriés, pas d'autres sources validées ni effacement |
| Cleanup : caches utilisateur/système | ❌ | Aucune cartographie PS5 certifiant que les caches sont jetables ; ne jamais supprimer save/config |
| Cleanup : corbeilles | ❌ | Pas d'équivalent PS5 avéré ; à confirmer dans les mounts multimédias |
| Cleanup : captures écran & vidéos | 🟡 | Inventaire JPG/JXR/PNG, MP4/WebM, revue, photos JPG/PNG et vidéo selon codec ; pas de synchronisation base multimédia ni retrait sûr |
| My Clutter : fichiers volumineux et anciens | 🟡 | Seuils 50 / 250 Mio et filtre ≥90j par mtime ; dernier accès et autres dossiers non couverts |
| My Clutter : doublons exacts | 🟡 | Comparaison octet à octet bornée (128 images, 32 Mio/fichier, 128 Mio lectures), hardlinks exclus, pas de vidéo |
| My Clutter : images similaires | ❌ | Pas de détection perceptuelle ni fusion de quasi-doublons |
| My Clutter : recherche / groupes | 🟡 | Par jeu, taille, mtime, nom, pagination 36 ; pas de recherche globale ni de galerie miniature |
| Space Lens : carte arborescente stockage | 🟡 | Statvfs 2 montages + carte des captures par jeu en barres photo/vidéo, sélection manette ; pas de navigation arbitraire du SSD |
| Applications : désinstallation | ❌ | Pas d'API de désinstallation contrôlée / maintenance base système |
| Applications : résidus d'installations | 🟡 | Diagnostic autoload etaHEN, preflight et catalogue protégé des homebrews/plugins (taille nominale). Aucune corrélation avec registre d'installation, donc aucun orphelin avéré |
| Applications : PKG/DLC incomplets | ❌ | Aucune corrélation registre/metadata FPKG, protections inconnues |
| Applications : mises à jour | ❌ | Gestionnaire d'updates PS5 non implémenté ; ne pas modifier firmware |
| Performance : entretien et redémarrage | — | Aucune réparation système générale justifiée ; proposer des diagnostics spécialisés uniquement |
| Performance : purge DNS | — | Effacement DNS peut perturber jailbreak/résolution personnalisée ; ne pas copier comme automatisme |
| Performance : libération RAM | — | Les promesses 'RAM cleaner' sont trompeuses et sensibles sur console ; ne pas implémenter |
| Protection : malware | ❌ | Pas de base de signatures ni de moteur d'analyse fiable pour PS5 homebrew |
| Protection : confidentialité | 🟡 | Serveur en loopback, origine HTTP contrôlée, pas d'écriture ; pas d'audit des autorisations payloads |
| Mesures santé système | 🟡 | Volumes statvfs et prédiagnostic, pas de télémétrie température/CPU/GPU ou SMART validée |
| Actions rapides 'My Tools' | 🟡 | Navigation par cinq sections ; pas de lanceur d'outils centralisé |
| Sauvegarde / quarantaine / restauration | ❌ | Export JSON de revue uniquement (ce n'est **pas** une copie de données) |
| Suppression sélective / vérification | ❌ | Pas de point d'écriture, pas de confirmation double, pas de contrôle base multimédia |
| Contrôle manette TV | 🟡 | Gamepad API / navigation / modal ; non testé sur véritable WebKit PS5 13.60 |
| Installation dans l'accueil PS5 | ❌ | Payload ELF standard et expérimental navigateur ; aucune application PS5 persistante installée |
| Compactage / défragmentation du SSD | — | Éviter : pas de défragmentation flash, pas de gains imaginaires |

## Priorités, avec portes de sécurité

### P0 — Vérifications réelles, aucune écriture
1. Confirmer un seul payload démarré, son cycle de vie, l'URL loopback, la notification et la navigation DualSense sur FW 13.60.
2. Confirmer les chemins de captures et la base de métadonnées liée au menu Galerie ; confirmer si les montages sont des doublons logiques.
3. Prouver que des résultats complets ou partiels sont présentés sans déclarer faussement l'espace « libéré ».

### P1 — Parité de lecture seule utile
4. Agréger les modules dans un Smart Care non destructif avec statuts et avertissements.
5. Étendre la revue Large & Old (50 Mio et âges via mtime, **pas** la vraie date de dernière ouverture).
6. Visualiser par jeu, date et volume avec filtre/tri manette ; disponibilité des aperçus JXR selon décodeur et SDK.
7. Diagnostic dédié des installations FPKG, fichiers temporaires, cache homebrews, en prouvant leurs relations avec un logiciel installé avant toute qualification.

### P2 — Nettoyage fiable, uniquement après validation P0/P1
8. Définir le contrat des éléments supprimables (autorisations, registre de capture, liens logiques, qualité des sauvegardes).
9. Sauvegarde réelle des fichiers, restauration vérifiée, mécanisme de quarantaine compatible avec l'espace libre.
10. Prévisualisation des différences, confirmation deux étapes, verrouillage en cours d'opération, revalidation des inodes et journaux d'exécution.
11. Vérifier les états et capacités avant/après via API appropriées ; traiter interruption, rollback et erreurs partielles.
12. Auditer journaux, mises à jour et fichiers système via modules **spécifiques PS5**, jamais balayage destructif générique.

## Exclusions intentionnelles
- Ne pas prétendre scanner les logiciels malveillants sans moteur ou base de signatures validés.
- Ne pas reprendre les nettoyages RAM, DNS, corbeille macOS ou défragmentation comme si la PS5 était un Mac.
- Ne pas supprimer directement `/data/etaHEN` ni la Toolbox des Paramètres : distinguer résidus, autoload et injection ShellUI.
- Ne jamais toucher aux sauvegardes, aux jeux, aux licences ni aux bases de captures sans validation spécifique.

## Définition du mot « terminé »
**Implémenté + tests hôte + compilation ELF ≠ validé sur PS5.** Une fonctionnalité est « prête PS5 » uniquement quand son comportement réel est vérifié sur FW 13.60, DualSense comprise. Une fonctionnalité de nettoyage est « prête à être activée » uniquement quand sauvegarde, restauration, synchronisation, consentement et tests d'interruption ont été démontrés.
