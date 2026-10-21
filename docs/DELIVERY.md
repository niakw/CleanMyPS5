# Livraison 0.10.1-analysis

## Ce qui est livré

Un exécutable `cleanmyps5.elf` contenant le scanner, les règles et toute l’interface. Pas de deuxième variante, de serveur à lancer sur un ordinateur, de téléchargement de scripts ou de fichiers de la console. L’application reste strictement en lecture seule.

L’archive source contient les modifications complètes et les tests. Les rapports de métadonnées d’une console réelle et les images de référence fournies pendant la conception ne font pas partie du code ou des archives applicatives.

## Correctif de sécurité 0.10.1

Le comparateur de doublons ouvre la racine une seule fois, vérifie le descripteur avec fstat et réutilise ce même accès pendant l’énumération et les comparaisons. Il ne vérifie plus un chemin avec lstat avant de le rouvrir. Remplacer ou renommer la racine pendant l’analyse ne redirige plus les lectures vers un autre arbre. Les identités des fichiers restent revalidées après ouverture.

Un test déterministe remplace le chemin de la racine juste après son ouverture et vérifie que seuls les fichiers de l’arbre initial sont comparés. Il couvre également le refus d’une racine symbolique ou non répertoire.

## Périmètre établi à partir des besoins

| Besoin | Comportement livré |
|---|---|
| Entretien coordonné | Analyse des métadonnées, catalogue, montages et chemins |
| Journaux et caches système | Inventaire protégé des racines connues, sans purge |
| Captures, miniatures, variantes | Inventaire élargi ; fichiers associés préservés |
| Apps, plugins, ELF inconnus | Découverte générique ; aucune liste d’apps personnelle requise |
| Gros fichiers et anciens fichiers | Recherche, tri et filtres dans les catégories ; ancienneté par mtime |
| Doublons | Comparaison exacte d’images sur demande ; taille égale insuffisante |
| Démarrage | Diagnostic limité d’autoload sur demande ; aucun désarmement automatique |
| Stockage | Carte des captures, tailles nominales et mesures de deux montages visibles |
| Outils | Accès aux fonctions opérationnelles et indication des fonctions absentes |
| Export | Métadonnées complètes ou sélection manuelle ; aucune copie de contenu |
| Confidentialité | Loopback, pas de télémétrie, contenus privés non lus par l’analyse générale |
| Désinstallation, résidus certifiés, mises à jour | Non implémentés ; fichiers inconnus protégés |
| Purges, quarantaine, sauvegarde/restauration | Non implémentées ; pas de gain libéré annoncé |
| Malware, permissions, processus actifs | Non implémentés ; aucune simulation de résultats |
| Mail, cloud, instantanés du Mac | Aucun équivalent intégré à la PS5 |

## Format demandé et limite actuelle

Pour Itemzflow / ShadowMount, le format de dossier demandé est bien `eboot.bin` + métadonnées d’application native. Le projet actuel utilise le démarrage et le runtime du SDK de payloads. Le livrable conserve donc l’autre format autorisé, **l’ELF unique**.

La chaîne native examinée documente notamment des différences de CRT/imports et de bibliothèque C. Produire seulement un conteneur SELF ou changer une extension ne règle pas ces différences. Le portage natif et ses tests sur console restent à faire. Aucun `eboot.bin` renommé ou prétendument compatible n’est distribué.

Références de format : [dossiers ELF websrv](https://github.com/ps5-payload-dev/websrv#installing-homebrew), [applications natives ps5link](https://github.com/Rufidj/ps5link-sdk#packaging-and-installing).

## Validation distincte

- Tests hôte : API, protections, métadonnées et interface JavaScript sur fixtures synthétiques.
- Contrôle mémoire : tests natifs réussis avec UBSan. ASan compile, mais son runtime LLVM 18 se bloque sur ce Mac dans AsanInitInternal/InitializeShadowMemory avant main ; le contrôle ASan local ne peut pas être déclaré réussi. Le job Linux ASan/UBSan de CI reste configuré.
- Compilation croisée : ELF PS5 x86-64, assets et règles intégrés.
- À vérifier sur console : accès effectif aux racines, restrictions du sandbox PS5, démarrage du navigateur, contrôle DualSense et fin du cycle de vie.

Cette version est livrable comme outil d’analyse à tester. Elle n’est pas une application native Itemzflow ni un nettoyeur avec effacement validé sur 13.60. Aucun fichier de console n’a été modifié pour cette livraison.
