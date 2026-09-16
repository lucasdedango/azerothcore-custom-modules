# AGENTS.md

## But du repo

Ce repo contient les modules AzerothCore personnalisés ajoutés au serveur, leurs configurations, le `docker-compose.override.yml` utilisé par le serveur, ainsi que la documentation joueur.

L'objectif est de garder les modifications maison faciles à maintenir, réinstaller et mettre à jour après une mise à jour d'AzerothCore / Playerbots.

## Modules personnalisés à maintenir

- `mod-server-customization`
  - automatisations `.mine`, `.herb`, `.gather`, `.grind`, `.grindtarget`
  - patchs Playerbots associés
  - gestion monture, routing, récupération, loot, sécurité verticale, etc.
- `mod-mount-chest`
  - coffres cuivre / argent / monture sur les kills open-world
- `mod-solo-loot-reroll`
  - reroll intelligent du loot pour joueur solo
- `mod-dynamic-difficulty`
  - module expérimental / actuellement non prioritaire

Chaque module doit garder son `conf/*.conf.dist` à jour et documenter toute nouvelle option de configuration.

## Modules externes installés sur le serveur

Ces modules ne sont pas développés dans ce repo, mais doivent être listés ici pour garder une vue complète de l'installation :

- `mod-playerbots`
- `mod-individual-progression`
- `mod-aoe-loot`
- `mod-autobalance`
- `mod-auctionsim`

Ne pas modifier leur code directement sauf via un patch explicitement maintenu dans un module custom.

## Docker / configuration serveur

Le repo contient aussi le `docker-compose.override.yml`.

Règles :
- préserver les volumes et les bases de données ;
- ne jamais utiliser `docker compose down -v` ;
- les variables `AC_*` doivent rester lisibles et commentées ;
- après modification d'une variable d'environnement, recréer `ac-worldserver` si nécessaire ;
- distinguer clairement les options AzerothCore natives des options de modules.

## Patches Playerbots

Les modifications de Playerbots doivent être appliquées via les scripts maintenus dans `mod-server-customization`, pas par édition manuelle non suivie.

Les scripts doivent être :
- idempotents autant que possible ;
- explicites si le code upstream attendu a changé ;
- sûrs en cas d'échec ;
- faciles à réappliquer après une mise à jour de `mod-playerbots`.

## Documentation joueur

Le repo contient le HTML des commandes joueur, typiquement :

`azerothcore-commandes-joueur.html`

À chaque ajout, suppression ou changement de commande :
1. mettre à jour le code ;
2. mettre à jour ce HTML dans le même changement ;
3. garder les exemples courts et copiables.

Commandes custom actuellement importantes :
- `.mine [noloot]`
- `.herb [noloot]`
- `.gather [noloot]`
- `.grind`
- `.grindtarget`
- `.autostatus` / `.as`
- `.autostop` / `.astop`
- `.damagedebug`
- `.profession view` / `.profession gathering 1..3` / `.profession crafting 1..3` / `.profession default`

## Style de travail

Faire des changements ciblés et faciles à relire.

Avant de modifier une API AzerothCore ou Playerbots, vérifier l'API actuelle dans le code source upstream.

Les messages paramétrés envoyés avec `ChatHandler::PSendSysMessage` utilisent la syntaxe
`fmt` d'AzerothCore (`{}`, `{:.2f}`, etc.), jamais les marqueurs `printf` (`%u`, `%s`,
`%.2f`, etc.). Lors d'une modification de messages joueur, contrôler tous les modules
custom pour éviter que les marqueurs soient affichés littéralement en jeu.

Pour un bug :
1. identifier la cause ;
2. corriger le minimum nécessaire ;
3. conserver les comportements déjà validés ;
4. mettre à jour config et documentation si le comportement visible change.

Ne jamais proposer une manipulation qui risque les données du serveur pour résoudre un problème de code ou de configuration.
