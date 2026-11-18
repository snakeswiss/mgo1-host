# MGO1 Host

MGO1 Host.exe installe, règle et surveille vos hôtes dédiés SaveMGO pour MGO1 : le code du jeu
lui-même, construit depuis votre disque, qui héberge des parties jour et nuit sur un serveur Linux que vous louez. Le
programme ne parle qu'à ce serveur, par SSH.

- Bots : un serveur fait tourner jusqu'à 10 bots, chacun héberge sa propre partie avec son propre
  compte SaveMGO et son port UDP (5730 à 5739). BOTS en ajoute un en deux clics (avec un compte
  mémorisé sur ce PC), ouvre son port UDP dans le pare-feu du serveur, le démarre, l'arrête, relance
  sa partie ou le retire ; les autres pages affichent et commandent le bot choisi dans le menu.
- État en direct : voyant vert (EN LIGNE), orange (DÉMARRAGE) ou rouge (HORS LIGNE), la partie,
  la manche, le lien au lobby, le processeur et la mémoire de l'hôte.
- Joueurs en direct : pseudo, ping en ms (chaque seconde), équipe, éliminations / morts, points ;
  ACTIONS : expulser, ajouter à la liste noire, ajouter à la liste blanche.
- GAME REPORT : ce qui se passe dans la partie du bot, avec l'heure : arrivées, équipes et
  changements d'équipe, éliminations (arme, tir à la tête, points), étourdissements, CQC,
  suicides, le KEROTAN pris, lâché et posé dans un goal, le compte à rebours de la capture (15, 10
  et 5 s restantes), manches, GAME RESTART, fins de manche et vainqueurs, le bilan de chaque
  joueur, expulsions, vos propres actions. Sous chaque ligne, les paquets envoyés par l'hôte au
  serveur de lobby et les messages du jeu lui-même qui l'expliquent. Un fichier par jour sur ce PC
  (`Documents\MGO1 Host\Game Report\<serveur>\Bot <N>\<date>.log`), dans la langue du programme.
- JOURNAL : le journal de l'hôte dans la langue du programme (LIGNES TECHNIQUES ajoute les lignes
  brutes) ; ENREGISTRER l'écrit dans un fichier à envoyer à qui vous aide. Aucune donnée de
  connexion dedans, ni nulle part ailleurs. Les journaux du jour s'ouvrent sur leurs dernières lignes
  (LIGNES PRÉCÉDENTES pour celles d'avant) ; ENREGISTRER écrit toute la journée.
- RÉSEAU : les serveurs du jeu pour tous les bots (ceux de SaveMGO par défaut, ou un autre gate
  et serveur STUN : un nouveau service si SaveMGO s'arrête un jour, votre propre serveur, un
  serveur local), les ports UDP des bots et les pare-feu.
- RELANCER LA PARTIE, ARRÊTER L'HÔTE, DÉMARRER L'HÔTE, un redémarrage quotidien quand personne
  ne joue.
- Le compte SaveMGO de l'hôte, tous les réglages de Create Game (nom, mot de passe, commentaire,
  jusqu'à 15 règles et cartes, équipes, joueurs, classement, armes, tir allié, expulsions,
  équilibrage, chat vocal, durées, manches, tickets). Host Only est toujours activé.
- Les réglages de partie mémorisés sur ce PC : les derniers enregistrés (un nouveau bot les reçoit ;
  UTILISER les remet dans le formulaire, par exemple sur un serveur réinstallé) et jusqu'à 20
  préréglages à vous, leurs mots de passe de partie chiffrés.
- Les serveurs et les comptes SaveMGO mémorisés sur ce PC (mots de passe chiffrés pour votre
  compte Windows), l'installation et les mises à jour depuis le programme.
- Les sons des menus du jeu lors des clics (curseur, validation, annulation), joués depuis le
  STAGE_N.DAT de votre disque (le dossier FICHIERS DU DISQUE de la page SERVEUR) : le programme ne
  contient aucun son du jeu. OPTIONS les coupe ou règle leur volume.
- 11 langues, tout compris (messages, erreurs, journal) : anglais (par défaut), français,
  allemand, italien, espagnol, portugais (Brésil), russe, japonais, coréen, chinois (simplifié et
  traditionnel). OPTIONS en choisit une.
- Le logo SaveMGO en haut ouvre https://mgo2pc.com/portal/.

## Démarrage rapide : louez un serveur, puis INSTALLATION RAPIDE

Louez un serveur, puis INSTALLATION RAPIDE : trois champs et GO.

1. Louez un serveur Linux x86-64 sous Ubuntu 24.04 (2 cœurs, 2 Go de mémoire ou plus ; lequel et où : Quel serveur,
   plus bas). Son hébergeur vous donne son adresse IPv4 et son mot de passe root.
2. Lancez MGO1 Host. Sans serveur, il s'ouvre sur INSTALLATION RAPIDE (aussi dans le menu, tant qu'aucun serveur ne
   répond ; ensuite la page SERVEUR a son bouton). Remplissez l'ADRESSE et le MOT DE PASSE du serveur (PORT SSH 22 et
   UTILISATEUR root sont déjà là ; AUTHENTIFICATION : CLÉ SSH pour une clé), l'ID DU COMPTE et le MOT DE PASSE SaveMGO
   du bot (en majuscules ; gardés sur ce PC, chiffrés, comme COMPTE DE L'HÔTE garde les comptes ; MÉMORISÉ SUR CE PC en
   reprend un déjà gardé) et le dossier de votre disque (CHOISIR LE DOSSIER : celui qui contient `MGS` et `MODULES` ; la
   page dit aussitôt si les fichiers du disque y sont).
3. GO. Chaque étape s'affiche avec son état (EN ATTENTE, EN COURS, FAIT, ÉCHEC) et la progression d'ensemble :
   1. CONNEXION AU SERVEUR.
   2. SA CLÉ, L'AUTHENTIFICATION : la clé du serveur s'affiche une fois, FAIRE CONFIANCE (la seule question posée ; jamais
      de confiance donnée sans vous).
   3. INSTALLER L'HÔTE : les fichiers de l'hôte et ceux de votre disque partent sur le serveur. Quand le paquet ne contient
      pas le programme de l'hôte (le téléchargement public : aucun code du jeu dedans), le serveur le compile à partir
      des fichiers de votre disque : 20 à 60 minutes la première fois, son étape, son pourcentage et le temps écoulé sur la
      page ; le programme garde la connexion ouverte et les autres pages marchent pendant ce temps ; une mise à jour ne
      compile que ce qui a changé. Un serveur qui a déjà cette version : étape passée.
   4. COMPTE DU BOT 1.
   5. RÉGLAGES DE PARTIE : SAVEMGO HOST, DEATHMATCH PAR ÉQUIPE et DEATHMATCH sur CITY UNDER SIEGE, GHOST FACTORY,
      MOUNTAINTOP et BROWN TOWN, 8 joueurs, le commentaire `Virtual dolls fill the empty seats`, les autres réglages par
      défaut du programme (manches de 5 minutes, tir ami désactivé, l'expulsion des inactifs, le chat vocal).
   6. DOLLS VIRTUELS : 7 (les 8 places de la partie moins celle toujours gardée pour un vrai joueur : ils occupent les
      places libres, le plus récent part quand un vrai joueur arrive), COMME UN JOUEUR, niveau 5, le NIVEAU ADAPTATIF
      activé de 1 à 10.
   7. DÉMARRER LE BOT 1.
   8. BOT 1 EN LIGNE : la partie est dans le lobby de SaveMGO ; le tableau de bord s'ouvre.

Une étape qui échoue dit pourquoi en rouge et quoi faire ; GO reprend à cette étape (une compilation arrêtée repart où elle
s'est arrêtée ; un compte refusé par SaveMGO reprend au compte, une fois corrigé). ANNULER arrête l'étape en cours. Ensuite
chaque partie se change sur sa propre page (RÉGLAGES DE PARTIE, DOLLS VIRTUELS, COMPTE DE L'HÔTE, BOTS pour plus de
parties), comme avant. De A à Z, plus bas, fait chaque étape à la main.

## Build 1.00

Voici la build 1.00, la version qui suit les bêtas. Chaque version affichée dit `build 1.00` : le titre de la fenêtre et
À PROPOS (PROGRAMME : `MGO1 Host build 1.00`, PAQUET DE L'HÔTE : `build 1.00` et 12 chiffres hexadécimaux), la ligne du bas
une fois connecté (`agent build 1.00`, le programme du serveur), la page SERVEUR (VERSION INSTALLÉE et VERSION DE CE
PROGRAMME : `build 1.00` et les mêmes 12 chiffres), la première ligne des journaux enregistrés (`MGO1 Host build 1.00,
agent build 1.00, ...`), le journal de l'hôte lui-même (`version build 1.00 ...`), et les paquets :
`MGO1_Host_build-1.00_windows.zip`, `MGO1_Host_build-1.00_linux-x86_64.tar.gz`, `SHA256SUMS_build-1.00.txt`.

Depuis une bêta : remplacez le dossier du programme par celui-ci, connectez-vous, page SERVEUR, METTRE À JOUR L'HÔTE
(environ une minute, la partie de chaque bot redémarre ; les réglages, les comptes et les listes de joueurs sont
conservés). D'ici là, le programme du serveur est plus ancien que celui-ci et les pages disent PROGRAMME DU SERVEUR PAS
À JOUR avec le bouton sur place. Sur le serveur, `cat /opt/mgo1host/VERSION` donne alors `build 1.00 ...` et
`mgo1host-agent version` `"agent": "build 1.00"`. Gardez ce programme avec un serveur en build 1.00 : le MGO1 Host d'une
bêta prendrait le programme du serveur pour un plus ancien que le sien et proposerait de réinstaller sa bêta.

Ce que chaque bêta a apporté, avant la build 1.00 :

## Avant la build 1.00 : les bêtas v150

- L'hôte et ses dolls tournent sur le moteur v150 ; les joueurs PS2 et PCSX2 rejoignent ses parties (jusqu'à 8 joueurs).
- DOLLS VIRTUELS (leur propre page sous PLAYABLE DOLL) : des joueurs que le jeu du bot fait tourner lui-même, sans jeu à
  eux ni compte. Un PLAYABLE DOLL est un jeu entier (environ 0,23 d'un cœur) ; un doll virtuel coûte au bot environ 0,03
  à 0,05 d'un cœur quand il joue : jusqu'à 7 dans une partie de 8 (environ un demi-cœur pour le bot). Une place reste
  toujours libre pour un vrai joueur, et un doll virtuel sort aussitôt qu'un vrai joueur a besoin de sa place. (beta 15) Ils jouent comme des joueurs de
  leur niveau : leur arme (AK-47, XM16E1, SCORPION, M37, SVD ou AUTO, le M1911A1 sous la main), accroupis, penchés et en
  roulade dans un combat, grenades, CQC et roulades dans un ennemi au contact, l'objectif de la règle (KEROTAN,
  GA-KO, microfilm) par des chemins qui contournent les ennemis ; ils meurent et reviennent dans toutes les règles ;
  leur niveau 1 à 10 règle leur réaction, leur visée, leur vue et ces gestes. Réglez-les par bot (nombre, équipes,
  niveau, arme, noms) : la prochaine partie du bot les prend, ou ENREGISTRER ET RELANCER ; le nombre aussi en direct.
  Leurs parties restent hors des classements SaveMGO comme celles des dolls.
- Un joueur PS2 qui revient par la même connexion avec un autre disque (une PS2 coréenne ou nord-américaine, puis une
  PAL) entre : le bot de la bêta 13 gardait pour cette adresse le dialecte de session du premier disque jusqu'à la fin de
  sa partie (la PAL échouait alors sur l'erreur 1059).
- Bêta 16 (MGO1 Host 3.2 ; l'agent 3.4 sur le serveur : METTRE À JOUR L'HÔTE une fois). La DIFFICULTÉ d'un PLAYABLE DOLL
  a un choix de plus, avant 1 ENTRAÎNEMENT : COMME LES DOLLS VIRTUELS (dans les ACTIONS d'un DOLL, dans DIFFICULTÉ DES
  DOLLS pour tous les DOLLS d'un bot, dans le formulaire d'un nouveau DOLL). Le DOLL joue alors au niveau réglé pour les
  DOLLS virtuels de son bot (page DOLLS VIRTUELS ; le premier DOLL du bot prend le niveau du premier DOLL virtuel, le
  deuxième celui du deuxième, le dernier niveau pour les suivants) et prend aussitôt un nouveau niveau quand vous en
  enregistrez un là-bas ; son niveau adaptatif est désactivé, car les DOLLS virtuels ne s'adaptent pas.
- Bêta 16 : la page PLAYABLE DOLL dit pourquoi les PLAYABLE DOLLS d'un bot attendent, sur une ligne sous la ligne du
  bot (PLAYABLE DOLLS SANS COMPTE) quand il en joue moins qu'il n'y en a de réglés : aucune place libre (les places de
  la partie, ses vrais joueurs, les places tenues pour les DOLLS virtuels et celle gardée pour un vrai joueur), le bot
  arrêté, sa partie pas encore dans la liste, les processeurs et la mémoire du serveur, la protection de cadence (sa
  partie tournait sous sa cadence : moins de DOLLS pendant quelques minutes), ou les DOLLS sans compte désactivés sur
  le serveur (DOLL_GATE=0).
- Bêta 16 : RAPPORTS DE PLANTAGE, la troisième valeur de la ligne AFFICHAGE de la page JOURNAL (l'agent 3.4 sur le
  serveur). Quand le jeu d'un bot ou d'un DOLL se termine mal (un plantage, tué, bloqué puis relancé par le serveur, sa
  partie ou le lobby perdus, un autre code de sortie, ou son code sorti de sa mémoire), le serveur garde un petit
  rapport : ce qui s'est arrêté, sa partie et sa manche, ses joueurs, les dernières lignes du jeu. La liste montre pour
  chacun la date et l'heure à l'horloge de ce PC, son bot ou son DOLL, sa taille et la raison dans votre langue ; un
  clic l'ouvre comme un journal quotidien, avec RETOUR, ENREGISTRER (un fichier dans `Documents\MGO1 Host\Logs`),
  COPIER et RECHERCHE. Un bot qui a des rapports dans les dernières 24 heures le dit en rouge sur sa ligne de la page
  BOTS et sur le TABLEAU DE BORD : combien, l'heure et la raison du dernier.
- Bêta 16 : un bot dont les démarrages se terminent trop tôt (un réglage ou un fichier du disque manquant, un jeu mort
  dans ses premières minutes) n'est plus relancé toutes les 5 secondes : le serveur attend avant le démarrage suivant,
  30 s de plus à chaque fois et 10 minutes au plus. Sa ligne et le tableau de bord disent alors « Prochain démarrage en
  attente : les derniers ont échoué (JOURNAL). » et non une panne.
- Bêta 18 (MGO1 Host 3.3 ; l'agent 3.5 sur le serveur : METTRE À JOUR L'HÔTE une fois). Deux réglages, désactivés par
  défaut, destinés à rapprocher la force des deux sortes de DOLLS. Page DOLLS VIRTUELS, COMMANDES :
  DIRECT (comme avant) ou COMME UN JOUEUR (les DOLLS virtuels tournent, visent et tirent dans les limites de la manette ;
  avec la visée DIRECTE d'un PLAYABLE DOLL, les deux sortes sont à peu près de même force au niveau 5), enregistré avec les autres réglages des DOLLS virtuels du bot. Page
  PLAYABLE DOLL, VISÉE : MANETTE (comme avant) ou DIRECTE (le DOLL tourne sa vue à la vitesse d'un DOLL virtuel de son niveau, sans le stick), dans
  les ACTIONS d'un DOLL, dans VISÉE DES DOLLS pour tous les DOLLS d'un bot et dans le formulaire d'un nouveau DOLL ; un
  DOLL en partie la prend aussitôt. Avec un agent plus ancien sur le serveur, aucun des deux choix n'est affiché.
- Bêta 19 (MGO1 Host 3.4 ; l'agent 3.6 sur le serveur : METTRE À JOUR L'HÔTE une fois). Le NIVEAU ADAPTATIF des DOLLS
  virtuels, désactivé par défaut : sur la page DOLLS VIRTUELS, NIVEAU ADAPTATIF ACTIVÉ fait suivre au niveau de chaque
  DOLL virtuel la force des vrais joueurs de la partie, entre son NIVEAU LE PLUS BAS et son NIVEAU LE PLUS HAUT (1 et 10
  par défaut), le NIVEAU réglé étant son point de départ. Une partie en cours prend un changement en 2 s, sans
  redémarrage. Chaque DOLL virtuel en partie montre alors son niveau actuel, comme la page PLAYABLE DOLL :
  `NIVEAU 10 (ACTUEL 7,25)`. Avec un agent plus ancien, les trois lignes sont grisées et la page dit pourquoi.
- Bêta 22 (MGO1 Host 3.5 ; l'agent 3.7 sur le serveur : METTRE À JOUR L'HÔTE une fois). Les RÉGLAGES DE PARTIE et la
  copie du serveur : le formulaire n'est plus rempli par une réponse plus ancienne du serveur
  (un RECHARGER répondu après un ENREGISTRER y remettait l'ancien ordre des règles et cartes) ni par celle d'un autre bot ;
  quand les réglages du serveur ont été changés ailleurs, la page le dit et ENREGISTRER demande avant de les remplacer.
  Sous le formulaire, la page dit quand la partie en cours joue encore les réglages de son début (ce qui est enregistré
  prend effet à sa prochaine partie) et propose RELANCER LA PARTIE MAINTENANT ; les entrées de l'ordre que la partie ne
  joue pas sont nommées en rouge. Avec un agent plus ancien, la page est comme avant.

## Avant la build 1.00 : la bêta publique v141

- L'hôte et les dolls tournent sur le moteur v141 ; les joueurs PS2 et PCSX2 rejoignent ses parties.
- Jusqu'à 8 joueurs par partie, comme le propose le Create Game du jeu lui-même (NOMBRE DE JOUEURS ; la place de l'hôte
  n'est pas comptée).
- Les dix niveaux de difficulté des dolls (beta 6) : 1 ENTRAÎNEMENT, 2 FACILE, 3 NORMAL, 4 DIFFICILE, 5 EXPERT, 6 ÉLITE,
  7 VÉTÉRAN, 8 MAÎTRE, 9 EXTRÊME, 10 PRO. 6 ÉLITE est le CAUCHEMAR de la beta 5 (un doll réglé sur 6 avant est ÉLITE
  maintenant) : à partir de lui, les gestes d'élite, le lean, le carton pour s'éclipser après un kill ou pour sortir d'un
  combat rapide, des déplacements rapides. Avec le NIVEAU ADAPTATIF activé, le niveau d'un doll parcourt toute l'échelle,
  entre son NIVEAU ADAPTATIF MIN et MAX, selon la force de ses ennemis et de sa propre équipe.
- Les dolls n'ont besoin d'aucun compte : l'hôte les crée (DOLL_1, DOLL_2 ...) et les ajoute ou les retire tout de
  suite dans une partie en cours. Les dolls avec un compte SaveMGO restent disponibles, avec le même cerveau.
- Beta 7 : 10 PRO joue de façon surhumaine : il réagit instantanément, vise sans erreur, ne recule jamais et sait où
  sont ses ennemis (il ne tire toujours que sur ce qu'il voit). Les niveaux 1 à 9 ne changent pas et restent humains.
  Avec le NIVEAU ADAPTATIF activé, un doll réglé sur 10 ne joue ainsi que tant que son niveau reste proche de 10 (contre
  des joueurs forts) ; contre des débutants son niveau descend et il rejoue comme un humain.
- Beta 7 : les parties avec dolls restent hors du classement de SaveMGO (CLASSEMENT SAVEMGO sur la page PLAYABLE DOLL,
  agent 3.1) : dès qu'un doll est dans une partie, les résultats des joueurs de cette partie partent avec un VS RATING
  de 0, comme SaveMGO le fait pour une partie avec mot de passe : ils apparaissent dans les scores de la partie, jamais
  dans le classement ni le VS RATING, personne ne booste donc son classement contre les bots. BOTS affiche « hors
  classement » après les joueurs d'une telle partie.

## Ce qu'il faut

- Un PC Windows 10 ou 11 (64 bits).
- Les fichiers de votre propre disque MGO1 (le disque US de MGS3 Subsistence, partie en
  ligne) : `MGS\NET\MGS3_N.ELF`, `MGS\NET\STAGE_N.DAT`, `MGS\NET\SLOT_N.DAT` et le dossier
  `MODULES`.
- Un compte SaveMGO par bot, utilisé par ce bot seulement (SaveMGO garde une seule session par
  compte). Les comptes MGO1 sont en MAJUSCULES : l'ID et le mot de passe sont faits de
  majuscules, de chiffres et de `. _ -`.
- Un serveur, comme ci-dessous.

## Quel serveur (Hetzner Cloud, prix de septembre 2026)

Un bot est une partie MGO1 : les joueurs s'y connectent, donc son emplacement fait leur ping.
Hetzner n'a pas de centre de données au Japon. Allers-retours entre les régions (de centre de
données à centre de données ; la ligne du joueur ajoute un peu) :

| serveur à | Japon | Est des USA | Ouest des USA | Europe |
|---|---|---|---|---|
| Hillsboro (ouest des USA) | 100 à 135 ms | 60 à 80 ms | (local) | 145 à 170 ms |
| Ashburn (est des USA) | 150 à 195 ms | (local) | 60 à 80 ms | 95 à 120 ms |
| Falkenstein, Nuremberg (Allemagne) | 235 à 250 ms | 95 à 120 ms | 145 à 170 ms | (local) |
| Singapour | 85 à 110 ms | | environ 180 ms | 160 à 180 ms |

Les types, prix hors taxes (Hetzner, depuis le 15 juin 2026), plus environ 0,50 EUR par mois
pour l'IPv4. Un bot demande environ 0,6 cœur et 350 Mo de mémoire :

| type | où | vCPU, mémoire | bots | par mois |
|---|---|---|---|---|
| CX23 | Allemagne, Finlande | 2, 4 Go | 3 | 5,49 EUR |
| CX33 | Allemagne, Finlande | 4, 8 Go | 6 | 8,49 EUR |
| CPX11 | Ashburn, Hillsboro | 2, 2 Go | 3 | 20,49 USD |
| CPX21 | Ashburn, Hillsboro | 3, 4 Go | 5 | 37,49 USD |
| CPX12 | Singapour | 1, 2 Go | 1 | 17,99 USD |

Que prendre :

- **Pour des joueurs japonais, américains et européens, le mieux : deux serveurs.** Un CX23 à
  Falkenstein ou Nuremberg pour l'Europe, un CPX11 à Hillsboro pour les Amériques et le Japon :
  environ 28 USD par mois pour 6 parties. Toutes les parties sont dans le même lobby SaveMGO :
  nommez-les par région (RÉGLAGES DE PARTIE, NOM DE L'HÔTE, par exemple `EU DM 1`, `US JP DM 1`)
  et chaque joueur rejoint la plus proche.
- **Un seul serveur pour tout le monde : un CPX11 à Hillsboro** (ouest des USA) : Japon et
  Europe tous deux entre 100 et 170 ms environ, l'est des USA à 60 à 80 ms. Si la plupart de vos
  joueurs sont en Europe et aux Amériques, plutôt Ashburn (est des USA) : Europe vers 100 ms,
  Japon 150 à 195 ms.
- **Europe seulement : un CX23** (3 parties), un CX33 pour 6. Les types CX sont parfois épuisés
  à un emplacement : prenez un autre emplacement allemand ou finlandais, ou un CX33.
- Pas les types Arm (CAX) : l'hôte a besoin de x86-64.

Tout autre hébergeur marche pareil : un VPS x86-64 sous Ubuntu 22.04 ou 24.04 (ou Debian 12),
2 cœurs, 2 Go de mémoire, une IPv4 publique et les ports UDP des bots ouverts. Un hébergeur avec
un centre de données à Tokyo met les joueurs japonais à quelques millisecondes et la côte ouest
des USA vers 100 ms. Avec le paquet public (l'hôte compilé sur le serveur) : Ubuntu 24.04 (le
compilateur de la version officielle : le même fichier), environ 2.7 Go de disque libres la
première fois, et plus il y a de cœurs, plus la première compilation est courte.

## De A à Z

Chaque étape à la main, pour qui veut la voir (INSTALLATION RAPIDE fait les étapes 2 à 6 d'un coup, plus haut).

### 1. Louer le serveur

1. hetzner.com, Cloud, créez un compte ; dans la Cloud Console, un projet, puis **Add Server** :
   - **Location** : comme ci-dessus (Hillsboro, Ashburn, Falkenstein, Nuremberg, Helsinki).
   - **Image** : Ubuntu 24.04.
   - **Type** : Shared vCPU, **x86 (AMD/Intel)**, le type choisi plus haut.
   - **Networking** : IPv4 publique activée.
   - **SSH keys** : la vôtre, ou aucune : Hetzner vous envoie alors le mot de passe root par
     e-mail.
   - Volumes, sauvegardes, placement groups : inutiles.
2. **Pare-feu** : aucun n'est nécessaire. Si vous ajoutez un Hetzner Cloud Firewall, autorisez
   en entrée TCP 22 (ce programme) et UDP 5730 à 5739 (les joueurs, un port par bot).
3. Notez l'**adresse IPv4** du serveur (la page du serveur dans la Cloud Console).

### 2. Connecter le programme

1. Décompressez le dossier où vous voulez ; gardez `server` à côté de `MGO1 Host.exe`. Lancez
   `MGO1 Host.exe` (en anglais la première fois : OPTIONS, LANGUAGE pour le français).
2. Page SERVEUR : NOM (le vôtre, facultatif), ADRESSE (l'IPv4), PORT SSH 22, UTILISATEUR root,
   AUTHENTIFICATION : MOT DE PASSE (celui reçu de Hetzner ; MÉMORISER activé le garde sur ce PC,
   chiffré) ou CLÉ SSH (FICHIER DE CLÉ et PHRASE SECRÈTE), SE CONNECTER.
3. La première fois, le programme montre la clé du serveur : FAIRE CONFIANCE. « Connecté. »
   s'affiche en vert.

### 3. Installer l'hôte

Page SERVEUR, INSTALLATION / MISE À JOUR : CHOISIR LE DOSSIER (celui des fichiers de votre
disque : le dossier qui contient `MGS` et `MODULES`), INSTALLER L'HÔTE, OUI. Quelques minutes :
le programme vérifie le serveur, envoie l'hôte et les fichiers du disque (environ 90 Mo),
installe Wine et un service pour le bot 1. Chaque étape s'affiche dessous, puis « L'hôte est
installé » ; VERSION INSTALLÉE dit alors `build 1.00` et 12 chiffres hexadécimaux.

Avec le paquet public (la publication GitHub : son dossier `server` contient `mgo1host-src.tar.gz`
et pas de `mgo1host.exe`, son VERSION dit `build 1.00 source`), l'hôte est compilé sur votre serveur
à partir de ces sources et des fichiers de votre disque : la première fois prend environ 35
minutes sur 2 cœurs (moins avec plus de cœurs), ses étapes et son pourcentage affichés au fur et à mesure ; une
mise à jour environ 8 minutes ; le même paquet une seconde fois ne construit rien. Il faut le
disque nord-américain (une autre version est refusée en la nommant) et environ 2.7 Go libres sur
le serveur la première fois (195 Mo restent pour les mises à jour). Rien du jeu ne quitte votre
serveur.

### 4. Le compte SaveMGO du bot

COMPTE DE L'HÔTE : l'ID DU COMPTE et le MOT DE PASSE en majuscules, MÉMORISER SUR CE PC activé
(le programme le garde, chiffré, pour d'autres bots), ENREGISTRER ET RELANCER.

### 5. La partie

RÉGLAGES DE PARTIE : le NOM DE L'HÔTE que les joueurs voient dans le lobby, un MOT DE PASSE DE
LA PARTIE si elle est privée, un COMMENTAIRE, l'ORDRE DES RÈGLES ET CARTES (jusqu'à 15), les
équipes, les joueurs (8 au plus), le classement, les armes, le tir allié, les expulsions,
l'équilibrage, le chat vocal, les RÉGLAGES DES RÈGLES (durées, manches, tickets). ENREGISTRER ET
RELANCER. REIKO HINOMOTO / ROWDY REIKO (le Create Game du jeu l'affiche en ???) : Reiko Hinomoto et
Rowdy Reiko jouent à la place de deux PERSONNAGES UNIQUES (GRU contre KGB : Raikov et Sokolov), donc l'activer
active les PERSONNAGES UNIQUES, et désactiver les PERSONNAGES UNIQUES la désactive.

Chaque ENREGISTRER (et ENREGISTRER ET RELANCER) garde aussi ces réglages sur ce PC comme les derniers
enregistrés : RÉGLAGES DE PARTIE MÉMORISÉS SUR CE PC, en bas de la page. Un nouveau bot les reçoit
d'office ; sur un serveur réinstallé, UTILISER sur DERNIERS ENREGISTRÉS les remet dans le formulaire,
puis ENREGISTRER. ENREGISTRER COMME PRÉRÉGLAGE garde le formulaire sous un nom à vous (jusqu'à 20
préréglages, pour n'importe quel bot de n'importe quel serveur) ; UTILISER en remplit le formulaire,
OUBLIER le supprime. Rien ne part vers le serveur avant ENREGISTRER.

### 6. La partie se lance toute seule

Rien à lancer dans le jeu : le bot se connecte à SaveMGO avec son compte, crée sa partie avec
ces réglages (Host Only : le bot ne joue pas) et la garde ouverte jour et nuit. Une minute plus
tard environ, TABLEAU DE BORD montre le voyant vert, EN LIGNE, et la partie dans le lobby. Si une
partie se termine ou si le lien au lobby se perd, le bot en recrée une tout seul.

- ENREGISTRER ET RELANCER (RÉGLAGES DE PARTIE) : les nouveaux réglages tout de suite.
  ENREGISTRER seul : pour la prochaine partie.
- RELANCER LA PARTIE (TABLEAU DE BORD ou ACTIONS du bot) : une nouvelle partie maintenant.
- ARRÊTER L'HÔTE / DÉMARRER L'HÔTE : le bot arrêté ou en marche ; il démarre avec le serveur.

### 7. Comment les joueurs rejoignent

Dans MGO1 (leur PS2 ou PCSX2 réglés pour SaveMGO) : le mode en ligne, connexion avec leur propre
compte SaveMGO, le lobby, puis **Join Game** et la partie par son NOM DE L'HÔTE dans la liste
(avec le mot de passe de la partie s'il y en a un), ou **Quick Join** et la règle de la partie.
Les joueurs doivent utiliser les mêmes serveurs que les bots (ceux de SaveMGO, sauf si vous les
avez changés dans RÉSEAU).

## Plus de parties : plus de bots

BOTS, AJOUTER UN BOT : un compte mémorisé sur ce PC (ou un nouveau), le port UDP (le prochain
libre de 5730 à 5739), AJOUTER LE BOT. Le programme crée le bot, ouvre son port UDP dans le
pare-feu du serveur lui-même (ufw ou firewalld, s'il y en a un), lui envoie les derniers réglages de
partie enregistrés depuis ce PC (les mêmes nom, mot de passe et commentaire aussi : changez-les dans
RÉGLAGES DE PARTIE) et le compte, et le démarre : sa partie est dans le lobby une minute plus tard. Si votre hébergeur a un pare-feu
(Hetzner Cloud Firewall), autorisez-y aussi ce port. Le bot choisi dans le menu (BOT, ses
flèches) est celui qu'affichent et commandent TABLEAU DE BORD, JOUEURS, GAME REPORT, RÉGLAGES DE
PARTIE, COMPTE DE L'HÔTE et JOURNAL. ACTIONS sur la ligne d'un bot : le gérer, le démarrer ou
l'arrêter, relancer sa partie, le retirer. La liste noire est commune à tous les bots ; chaque bot
a ses propres réglages, son compte et sa liste blanche. Plusieurs serveurs : la page SERVEUR les
garde tous (un clic pour se connecter).

## Des joueurs joués par le serveur : PLAYABLE DOLL

Un PLAYABLE DOLL est un joueur que le serveur fait jouer lui-même dans la partie d'un de ses bots,
pour qu'une partie ne soit jamais vide : il se connecte sans aucun compte (DOLL_1, DOLL_2 ...) ou
avec son propre compte SaveMGO (jamais celui d'un bot, jamais le vôtre), rejoint la partie de son
bot, prend une équipe, choisit ses armes et joue la règle (combats, objectif, nourriture, cachettes),
et continue PC éteint. Autant que la partie de son bot a de places (64 par serveur au plus, selon
ses processeurs et sa mémoire).

Après un nouveau MGO1 Host, METTRE À JOUR L'HÔTE une fois (page SERVEUR) : tant que le serveur fait
tourner un programme plus ancien, les pages TABLEAU DE BORD, BOTS et PLAYABLE DOLL le disent en rouge
avec le bouton sur place (les DOLLS sans compte, les dix niveaux de difficulté, les bornes du niveau adaptatif et les
réglages récents en ont besoin).

DOLLS SANS COMPTE (avec un programme du serveur plus ancien : METTRE À JOUR L'HÔTE une fois) : une
ligne par bot, son nombre de DOLLS et - UN DOLL / + UN DOLL. Un DOLL ajouté là s'appelle
DOLL_<numéro> (le plus petit libre) : il se connecte à un petit lobby que le serveur fait tourner pour
les DOLLS de ses bots (sur le serveur lui-même, rien à ouvrir, rien à créer sur SaveMGO) et rejoint la
partie de son bot comme n'importe quel joueur. DOLLS DANS LA LISTE DES PARTIES DE TX-55
dit comment la liste des parties de SaveMGO et son API publique les montrent : SOUS LE COMPTE DE
L'HÔTE (par défaut : chaque DOLL compte comme un joueur de la partie, sous le nom du compte du bot hôte,
la partie apparaît donc peuplée), AVEC LEURS PROPRES IDS (chacun avec son numéro et sans nom, seulement si
la base du lobby accepte un joueur sans compte), NON LISTÉS (comme avant). Chaque bot le prend en compte au
redémarrage de sa partie. Listés, une place reste toujours libre pour un vrai joueur. Les résultats des
DOLLS n'y sont jamais enregistrés. CLASSEMENT SAVEMGO : PARTIES SANS DOLLS (par défaut : dès qu'un
DOLL est dans une partie, les résultats de cette partie sortent du classement et du VS RATING de SaveMGO jusqu'à sa
fin, comme ceux d'une partie avec mot de passe), TOUTES LES PARTIES (au classement, avec ou sans DOLLS, comme avant),
AUCUNE PARTIE (aucune partie du serveur au classement) ; chaque bot le prend en compte au redémarrage de sa partie.
- UN DOLL retire d'abord le plus grand numéro ; SUPPRIMER LE DOLL dans
ses ACTIONS fait de même pour celui-là. Leurs noms : NOM DU PROCHAIN DOLL donne au DOLL
qu'ajoute + UN DOLL le nom de votre choix ; NOM EN JEU et RENOMMER dans les ACTIONS d'un DOLL le changent ensuite
(vide : de nouveau DOLL_<numéro>). 3 à 15 caractères : lettres A à Z, chiffres, espaces et . _ - ! ? + *, jamais le
nom d'un autre DOLL ; c'est le nom que tous les joueurs voient en jeu. Un DOLL en partie la quitte et revient sous
son nouveau nom en une demi-minute environ. Choisissez des noms que les vrais joueurs n'utilisent pas : les DOLLS
prennent tout joueur portant le nom d'un DOLL pour l'un des leurs.

PLAYABLE DOLL, AJOUTER UN DOLL AVEC UN COMPTE SAVEMGO (AJOUTER UN PLAYABLE DOLL avec un agent plus ancien) : un compte mémorisé sur ce PC (ou un nouveau), son bot,
sa DIFFICULTÉ (1 ENTRAÎNEMENT à 10 PRO), son ÉQUIPE (AUTO, ROUGE, BLEU, SPECTATEUR), ses ARMES (SELON SON RÔLE, ou
une arme), AJOUTER LE DOLL. Il joue dès que la partie de son bot a une place pour lui : PLACE POUR
UN VRAI JOUEUR (activée par défaut) laisse toujours une place libre, et quand un vrai joueur arrive
dans une partie pleine, le dernier DOLL part aussitôt. Quand il joue moins de DOLLS d'un bot qu'il n'y en a de réglés,
une ligne sous la ligne du bot, dans PLAYABLE DOLLS SANS COMPTE, dit pourquoi : aucune place libre,
avec les places de la partie, ses vrais joueurs, les places tenues pour ses DOLLS virtuels et celle gardée pour un vrai
joueur ; le bot arrêté ; sa partie pas encore dans la liste ; les processeurs et la mémoire du serveur ne permettent
pas un DOLL de plus ; la protection de cadence (la partie tournait sous sa cadence : le serveur laisse entrer moins de
DOLLS pendant quelques minutes ; DOLL_PACE_GUARD dans le server.env du serveur) ; les DOLLS sans compte désactivés
(DOLL_GATE=0). EN JEU EN MÊME TEMPS : combien de DOLLS les
processeurs et la mémoire du serveur permettent en plus de ses bots (un DOLL est une partie entière :
0,6 cœur compté et 250 Mo, un quart des cœurs laissé libre : une partie à court de processeur
tourne moins vite que l'horloge et ses joueurs voient du lag ; les parties des bots passent en premier). La ligne de chaque DOLL montre ce qu'il fait, en direct ; ACTIONS
change sa difficulté, son équipe, son ORDRE (JOUER LIBREMENT, OBJECTIF, SE CACHER, DÉFENDRE, SUIVRE
ou CIBLER un joueur de sa partie), ses armes, JOUE oui ou non, QUITTER LA PARTIE MAINTENANT,
SUPPRIMER LE DOLL. S'il n'y a que des DOLLS dans une partie, ils regardent en spectateurs jusqu'à
l'arrivée d'un vrai joueur : leurs comptes ne gagnent jamais de points entre eux. Leurs mots de
passe sont gardés chiffrés sur ce PC et n'apparaissent dans aucun journal.

Ils jouent en coéquipiers : ils répondent à vos appels codec (Affirmatif, Négatif, ou rien, selon
leur niveau et ce qu'ils font ; Négatif quand ils sont trop loin ou en plein combat ; rien quand ils
sont étourdis) et font ce que vous demandez : SUIVEZ-MOI, COUVREZ-MOI (deux DOLLS au plus avec vous ;
quand vous vous arrêtez pour tirer, ils se mettent à couvert près de vous et tirent sur ce que vous
visez), HALTE, VENEZ ICI, DÉFENDEZ LA CIBLE (l'objectif joué plus fort), ALLEZ, ALLEZ, ALLEZ ! (ils
repartent, ceux qui vous suivent se dispersent) ; ils appellent eux-mêmes ENNEMI EN VUE, COUVREZ-MOI,
ZONE DÉGAGÉE, BEAU TRAVAIL. Ils franchissent les clôtures, les murets et les fenêtres du labo (une
roulade par-dessus les plus basses), montent leurs silencieux à chaque apparition (M1911A1, Mk22,
XM16E1 ; pas à partir d'ÉLITE), se penchent et s'accroupissent en tirant en vue subjective aux niveaux élevés, tirent à
l'AK-47 et à la M63 au coup par coup ou par courtes rafales à distance, achèvent un ennemi endormi,
et jouent une partie nommée HEAD SHOT... (ou HEAD_SHOT...) en headshots uniquement. Ils ne savent que
ce qu'un joueur pourrait savoir : un ennemi vu, ses pas tout près, ses tirs à moins de 35 m ; jamais
ils ne visent ni ne suivent un ennemi à travers un mur. Leurs difficultés se distinguent comme il
faut (mesuré : trois DOLLS avec le même fusil, niveau 5 35 victimes et 20 morts, niveau 3 25 et 29,
niveau 1 19 et 36). Avec SBMM BOTS activé (page PLAYABLE DOLL, la
partie d'un bot à la fois ; ou NIVEAU ADAPTATIF dans les ACTIONS d'un DOLL), leur niveau parcourt toute l'échelle selon
la force de leurs ennemis et de leur propre équipe (les victimes et les morts de chaque joueur ces dernières minutes, le
VS RATING d'un vrai joueur, un rival qui domine les dolls, pesés selon ce qu'on en sait) et la tournure du match (les
tickets ou l'objectif, les manches), jugés toutes les 5 s tant que de vrais joueurs jouent. NIVEAU ADAPTATIF MIN et MAX
(les ACTIONS d'un DOLL, 1 et 10 par défaut) sont le contrôle du serveur : le niveau adaptatif n'en sort jamais. La ligne
affiche la difficulté réglée au-dessus d'ACTIONS (NIVEAU 5 EXPERT) et le niveau adaptatif en dessous (ACTUEL 7,3
VÉTÉRAN) ; son aide, les forces, leur fiabilité et pourquoi le niveau bouge, ou que le DOLL attend un vrai joueur.
Désactivé, DIFFICULTÉ DES DOLLS les règle tous d'un coup. COMME LES DOLLS VIRTUELS, le premier choix de
DIFFICULTÉ dans les ACTIONS d'un DOLL, de DIFFICULTÉ DES DOLLS et du formulaire d'un nouveau DOLL : le DOLL n'a pas de
difficulté à lui, il joue au niveau réglé pour les DOLLS virtuels de son bot (page DOLLS VIRTUELS : le premier DOLL du
bot prend le niveau du premier DOLL virtuel, le deuxième celui du deuxième, le dernier niveau pour les suivants) et en
prend aussitôt un nouveau quand vous l'enregistrez là-bas. Sa ligne affiche ce niveau au-dessus d'ACTIONS et
(niveau des DOLLS virtuels) en dessous ; dans ses ACTIONS, NIVEAU ADAPTATIF, MIN et MAX sont affichés désactivés et ne se changent
pas (le niveau adaptatif est désactivé : les DOLLS virtuels ne s'adaptent pas), et SBMM BOTS est désactivé pour un bot
dont tous les DOLLS suivent. En deathmatch, ils portent l'unité que le jeu leur donne. VISÉE, dans les
ACTIONS d'un DOLL, dans VISÉE DES DOLLS pour tous les DOLLS d'un bot et dans le formulaire d'un nouveau DOLL : MANETTE
(par défaut, comme avant : le DOLL tourne sa vue par le stick, comme un joueur) ou DIRECTE (il tourne sa vue à la vitesse d'un DOLL virtuel de son
niveau, sans le stick) ; un DOLL en partie la prend aussitôt, et la garde pour ses parties suivantes. Les lignes ne sont
là que si l'agent du serveur le prend en charge. JOURNAL,
JOURNAL DE et JOUR : ce que chaque bot et chaque DOLL a fait, jour par jour, dans votre langue.

Ils saluent (l'emote : Triangle maintenu) pour la kill cam de leur victime quand rien ne les menace,
répondent à une emote que vous leur faites (celle d'un ennemi aussi, en vous épargnant le temps de
répondre) et vous saluent de temps en temps ; ils font des roulades sur un ennemi pour le renverser
(le porteur du KEROTAN, du GA-KO ou du microfilm le lâche ; à court de munitions ; avant une attaque
CQC ou stun). Le flash d'une grenade stun les aveugle tant que leur écran est blanc ; une grenade chaff
leur coupe le codec et le radar (aucun appel envoyé ni entendu, l'objectif là où il a été vu la
dernière fois) ; un fumigène vous cache à eux au-delà de 4 m et ruine leur visée à travers (leur
propre fumigène couvre leur repli) ; avec les lunettes thermiques d'une caisse d'armes, ils voient à
travers la fumée et visent plus juste. Leur arme de soutien (grenade, stun, fumigène, chaff, WP,
claymore, livre) est tirée au sort à chaque apparition.

Ils se battent comme des joueurs, à tous les niveaux : chargeur vide, ils rechargent en courant se
mettre à couvert, puis tiennent en joue l'angle d'où vient l'ennemi ; dans un échange de tirs à
distance qui s'éternise, ils changent de place (un autre angle, un couvert bas) ; en train de perdre,
ils se replient hors de vue vers leurs alliés, parfois dans leur carton, et vous attendent l'arme
braquée sur l'angle d'où vous allez surgir, ou cachés dans le carton jusqu'à ce que vous passiez ; de
temps en temps ils feignent de fuir pour vous attirer dans l'angle qu'ils tiennent ; un ennemi disparu
derrière un angle, ils surveillent cet angle un moment ; un carton immobile au loin les trompe comme il
tromperait un joueur. Ils s'approchent d'un ennemi endormi avant de l'achever et trouvent seuls la
sortie des culs-de-sac des cartes (la fosse de Snake à Mountaintop, une pièce fermée de Graniny Gorki
Lab).

## Des joueurs joués par la partie du bot elle-même : DOLLS VIRTUELS

La page PLAYABLE DOLL a deux blocs de nombre séparés, chacun avec sa propre explication : PLAYABLE
DOLLS SANS COMPTE (- UN DOLL, + UN DOLL : une partie entière chacun sur le serveur) et DOLLS VIRTUELS, EN DIRECT (- UN
DOLL VIRTUEL, + UN DOLL VIRTUEL par bot : sa partie en cours prend le nombre en quelques secondes, sans redémarrage ; un
nouveau entre dès qu'une place est libre, celui au plus grand numéro s'en va ; les prochaines parties du bot gardent ce
nombre).

Un DOLL virtuel est un joueur que la partie d'un bot fait jouer elle-même : ni partie, ni compte, ni service à lui,
environ 0,05 cœur chacun quand il joue (un PLAYABLE DOLL est une partie entière : environ un quart de cœur). Il rejoint la partie du
bot comme un vrai joueur, prend une équipe et joue comme un joueur de son niveau : il court et tire avec son arme (à la
tête aux niveaux les plus hauts), s'accroupit, se penche, roule, lance des grenades, saisit un ennemi au contact (CQC)
ou roule dans lui, joue l'objectif de la règle (KEROTAN, GA-KO, microfilm) par des chemins qui contournent les
ennemis et continue après une chute comme le veut la règle. Sans vrai joueur dans la partie, ils attendent ; ils jouent tant qu'un vrai joueur joue,
retiennent leur arrivée pendant celle d'un vrai joueur et laissent toujours une place libre pour un vrai joueur (quand
il la prend, le plus récent part aussitôt et revient quand une place se libère). CLASSEMENT SAVEMGO et DOLLS DANS LA
LISTE DES PARTIES DE TX-55 (page PLAYABLE DOLL) s'appliquent aussi à eux. Il leur faut le programme serveur de ce MGO1
Host (METTRE À JOUR L'HÔTE une fois).

DOLLS VIRTUELS (sous PLAYABLE DOLL dans le menu) montre ceux de chaque bot (AFFICHER : ce bot, comme BOT sous le menu),
puis pour le bot affiché : EN PARTIE MAINTENANT, chacun de ses DOLLS virtuels dans la partie (son joueur, son équipe, sa
vie et ses morts, ou qu'il arrive, s'en va ou attend hors de la partie) ; puis leurs réglages : NOMBRE DE DOLLS VIRTUELS
(de 0 aux joueurs de la partie moins l'hôte et la place gardée), une ligne pour chacun avec son NOM EN JEU (vide :
VDOLL1, VDOLL2 ... ; les règles des noms de DOLL), son ÉQUIPE (AUTO : à son tour le camp qui a le moins de joueurs ;
ROUGE, BLEU) et son NIVEAU (1 à 10, 5 par défaut : à 1 il réagit en une seconde environ, vise large et voit à 16 m ; à
10 il réagit aussitôt, vise la tête et voit à 85 m ; accroupi et penché dès 4, grenades dès 5, CQC et roulades dans
un ennemi dès 6 ; ce n'est pas l'échelle des PLAYABLE DOLLS), son ARME (AUTO : une au hasard à chaque vie ; AK-47,
XM16E1, SCORPION, M37, SVD ; le M1911A1 quand son chargeur est vide près d'un ennemi), et TOUS LES DOLLS VIRTUELS pour
l'équipe, le niveau ou l'arme de tous d'un coup. ENREGISTRER : la prochaine partie du bot les prend (une ligne
bleue le dit tant que la partie en cours en a d'autres) ; ENREGISTRER ET RELANCER : maintenant (la partie redémarre, ses
joueurs sont déconnectés) ; RECHARGER : de nouveau les réglages du serveur.

COMMANDES, sous NOMBRE DE DOLLS VIRTUELS, vaut pour tous les DOLLS virtuels du bot : DIRECT (par défaut,
comme avant) ou COMME UN JOUEUR, avec lequel ils tournent, visent et tirent dans les limites de ce que la manette permet
à un joueur. Avec la visée DIRECTE d'un PLAYABLE DOLL, les deux sortes de DOLLS sont à peu près de même force au
niveau 5 (duels de 10 minutes). Il s'enregistre avec le reste (ENREGISTRER : la
prochaine partie du bot ; ENREGISTRER ET RELANCER : maintenant). La ligne n'est là que si l'agent du serveur le prend en
charge.

NIVEAU ADAPTATIF, sous COMMANDES, vaut pour tous les DOLLS virtuels du bot : DÉSACTIVÉ (par défaut, comme
avant : chacun joue à son NIVEAU) ou ACTIVÉ : pendant la partie, le niveau de chaque DOLL virtuel suit la force des vrais
joueurs de la partie (leurs victimes et leurs morts ces dernières minutes, le VS RATING d'un vrai joueur, le score des
équipes), entre le NIVEAU LE PLUS BAS et le NIVEAU LE PLUS HAUT (de 1 à 10 ; 1 et 10 par défaut) ; son NIVEAU est son
point de départ. Trop forts pour vos joueurs au niveau 10 ? Activez-le, ou baissez le NIVEAU LE PLUS HAUT. ENREGISTRER
l'envoie aussitôt à la partie en cours (en 2 s, sans redémarrage) et le garde pour les parties suivantes. Tant qu'il est
activé, chaque DOLL virtuel en partie montre `NIVEAU 10 (ACTUEL 7,25)` : son NIVEAU, puis son niveau actuel. Un PLAYABLE
DOLL réglé COMME LES DOLLS VIRTUELS garde leur NIVEAU, pas leur niveau actuel. Avec un agent du serveur plus ancien,
les trois lignes sont grisées et une ligne dit de METTRE À JOUR L'HÔTE.

## Au quotidien

- TABLEAU DE BORD et JOUEURS montrent le bot en direct (chaque seconde).
- Une expulsion utilise l'expulsion du jeu lui-même : le joueur part aussitôt. LISTES DE JOUEURS :
  un nom en liste noire est expulsé maintenant et à chaque fois qu'il rejoint ; avec LISTE BLANCHE
  SEULE activée, seuls les noms de la liste blanche peuvent jouer (les autres sont expulsés dès
  qu'ils rejoignent). L'hôte n'est pas le serveur de SaveMGO : ces listes ne valent que pour ses
  propres parties.
- GAME REPORT : AFFICHER choisit les lignes (tout, combats, joueurs, partie, le bot et vous) ;
  PAQUETS ET MESSAGES montre les détails sous chaque ligne. Les fichiers sur ce PC s'écrivent tant
  que le programme est connecté, le serveur garde les 31 derniers jours : les jours manquants
  s'écrivent à la connexion suivante. OUVRIR LE DOSSIER les ouvre.
- JOURNAL : ce que fait l'hôte, dans la langue du programme : les écrans et menus qu'il
  traverse, la partie créée, le lobby, les listes, les expulsions, les redémarrages. LIGNES
  TECHNIQUES : chaque ligne telle que le serveur l'a écrite (en anglais).
- RÉSEAU : si les serveurs de SaveMGO s'arrêtent un jour, ou pour héberger sur un autre service,
  entrez son gate (connexion et lobbies) et son serveur STUN, ENREGISTRER ET RELANCER LES BOTS.
  Les joueurs doivent utiliser les mêmes serveurs. RÉTABLIR SAVEMGO remet ceux de SaveMGO.
- Le redémarrage quotidien (page SERVEUR) attend que personne ne joue (une heure au plus).
- METTRE À JOUR L'HÔTE installe un hôte plus récent depuis un dossier de programme plus
  récent ; les réglages, les comptes et les listes de joueurs restent, la partie de chaque bot
  redémarre.

## Journaux et aide : aucune donnée de connexion

- JOURNAL, ENREGISTRER : `Documents\MGO1 Host\Logs\Bot <N> <date>.txt`, les lignes dans la
  langue du programme puis telles que le serveur les a écrites, à envoyer à qui vous aide. Les fichiers du Game Report sont dans
  `Documents\MGO1 Host\Game Report`.
- JOURNAL, AFFICHAGE, RAPPORTS DE PLANTAGE : les rapports que le serveur garde quand le jeu d'un
  bot ou d'un DOLL s'est mal terminé (les 40 plus récents de chacun, 31 jours), le plus récent d'abord : date et heure à
  l'horloge de ce PC, BOT ou DOLL et son numéro, la taille, et pourquoi (le jeu a planté ; il a été tué, la mémoire du
  serveur insuffisante ; il était bloqué et le serveur l'a relancé ; sa partie ou sa connexion au lobby a été perdue ;
  il s'est terminé avec un code ; son code est sorti de sa mémoire). La ligne sous la page montre ce que le serveur en
  a écrit, sa partie, sa manche et ses joueurs. Un clic ouvre le rapport : son en-tête (ce qui s'est arrêté, quand, la
  version, la partie, chaque joueur), puis les dernières lignes du jeu avant la fin, lues comme un journal quotidien
  (heure à l'horloge de ce PC, erreurs en rouge, avertissements en jaune, les lignes que ce programme connaît dans
  votre langue). RECHERCHE garde les lignes qui contiennent un texte ; COPIER met le rapport dans le presse-papiers tel
  que le serveur l'a écrit ; ENREGISTRER écrit `Documents\MGO1 Host\Logs\Bot <N> <date>_<raison>.txt` (les lignes dans
  votre langue, puis telles que le serveur les a écrites) ; RETOUR revient à la liste. Avec un programme serveur plus
  ancien, la page dit METTRE À JOUR L'HÔTE. Un bot qui a connu une telle fin dans les dernières 24 heures affiche une
  ligne rouge sur sa ligne (BOTS) et sous son état (TABLEAU DE BORD) : combien de rapports, l'heure et la raison du
  dernier.
- Aucun ne contient d'ID de compte ni de mot de passe : l'hôte masque l'ID du compte du bot, son
  mot de passe et le mot de passe de la partie (`***`) dans tout ce qu'il écrit (son journal, le
  Game Report, son état, le journal des paquets P2P de dépannage) ; le programme du serveur les
  masque encore dans ce qu'il envoie ; les fichiers du programme lui-même ne les contiennent
  jamais (les comptes mémorisés et les mots de passe des réglages de partie mémorisés sont
  chiffrés).
- Un serveur qui a fait tourner un hôte bien plus ancien peut encore avoir l'ID du compte
  dans d'anciennes lignes de journal : JOURNAL les masque aussi. Pour les effacer sur le serveur :
  `journalctl --rotate` puis `journalctl --vacuum-time=1s`.

## Sécurité

- Le programme n'utilise que SSH (TCP 22). Il vérifie la clé du serveur à chaque connexion et
  prévient si elle change.
- Le mot de passe du serveur reste sur votre PC, chiffré pour votre compte Windows (DPAPI),
  seulement si vous choisissez MÉMORISER. La phrase secrète d'une clé n'est jamais enregistrée.
  Les comptes SaveMGO mémorisés sur ce PC sont chiffrés de la même façon (ID et mot de passe).
- Le mot de passe SaveMGO d'un bot part vers le serveur, dans `/etc/mgo1host/bot<N>.env`
  (lisible par root seulement) ; le programme ne l'affiche plus jamais.
- Les réglages du programme : `%APPDATA%\MGO1 Host\config.ini`, avec les réglages de partie qu'il
  garde (les derniers enregistrés, les préréglages) : leurs mots de passe y sont chiffrés comme les
  comptes.
- Le dossier `server` contient l'hôte construit depuis votre disque : gardez-le pour votre
  usage.

## En cas de problème

- Le serveur ne répond pas : vérifiez l'adresse, que le serveur tourne et que TCP 22 est
  autorisé.
- Connexion refusée : l'utilisateur (root) et le mot de passe ou la clé.
- HORS LIGNE en rouge, « Erreur de connexion » avec le message du jeu (« Server login failed.
  ( 816:-4 ) ») : le compte du bot dans COMPTE DE L'HÔTE (ID et mot de passe en majuscules), puis
  ENREGISTRER ET RELANCER. HORS LIGNE en rouge, « L'hôte a échoué » : JOURNAL et GAME REPORT.
- La partie est EN LIGNE mais personne ne peut rejoindre : le port UDP du bot (RÉSEAU) doit être
  ouvert dans le pare-feu de l'hébergeur (Hetzner Cloud Firewall).
- Pour demander de l'aide : JOURNAL, ENREGISTRER, et envoyez le fichier (il ne contient aucune
  donnée de connexion).
- Sur le serveur lui-même : `mgo1hostctl bots`, `mgo1hostctl -b 2 status`, `mgo1hostctl
  players`, `mgo1hostctl log`, `mgo1hostctl report`.

## Le compiler

Inutile pour s'en servir : INSTALLATION RAPIDE compile l'hôte sur votre serveur. Pour compiler vous-même, sous Ubuntu
24.04 (sous Windows : `wsl --install -d Ubuntu-24.04` une fois dans PowerShell, puis les mêmes lignes dans Ubuntu) :

    git clone https://github.com/snakeswiss/mgo1-host.git && cd mgo1-host
    sudo apt install g++-mingw-w64-x86-64 binutils-mingw-w64-x86-64 g++ cmake make perl git python3 \
        libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev
    bash hostpanel/build.sh          # MGO1 Host.exe (Windows), sans le disque
    python3 tools/server_src.py --out mgo1host-src.tar.gz
    sudo bash host/build_server.sh --src mgo1host-src.tar.gz --disc /chemin/du/disque --out mgo1host.exe --tree ~/mgo1host-build
    bash hostpanel/package.sh --source mgo1host-src.tar.gz --out out   # le paquet public : le serveur compile l'hôte
    bash hostpanel/package.sh --host-exe mgo1host.exe --out out       # avec votre propre hôte : pour vos serveurs seulement

(`/chemin/du/disque` : le dossier qui contient `MGS` et `MODULES`.) Le dossier est alors `out/MGO1 Host`.
`hostpanel/build.sh` télécharge Dear ImGui, OpenSSL et libssh2 depuis GitHub la première fois.

Linux et macOS (sans le disque non plus) : `bash hostpanel/build_linux.sh` fait `hostpanel/build-linux/mgo1-host` ;
`bash hostpanel/build_macos.sh` (sur un Mac : ci-dessous) fait `hostpanel/build-macos/MGO1 Host/MGO1 Host.app` et
`MGO1-Host-macos.zip`. Avec `MGO1_HOST_SHOWCASE=1`, `mgo1-host --selftest <dossier>` dessine les captures d'écran de la
version publiée : un serveur inventé mais plausible (tous les noms inventés, le serveur `mgo1-server.example`), chaque page à
la taille de la fenêtre, dans `<dossier>/1280` (1280 x 800) et `<dossier>/1600` (1600 x 1000 à 125 %), en anglais
(`MGO1_HOST_SHOWCASE=all` : toutes les langues ; sous Linux sans bureau : `xvfb-run -a`).

## macOS : compiler, vérifier, signer

`MGO1 Host.app` est un seul programme universel (Apple silicon et Intel) pour macOS 10.15 ou plus récent. Il se compile
sur un Mac, ou par le workflow GitHub Actions `.github/workflows/hostpanel-macos.yml` (un runner macOS 15, Apple
silicon) : la machine Linux qui compile les programmes Windows et Linux ne peut ni le compiler ni le lancer.

Ce qu'il faut pour compiler :

- Les Command Line Tools de Xcode : `xcode-select --install` (clang, make, lipo, strip, codesign, iconutil, ditto).
- cmake par Homebrew : `brew install cmake`.
- perl, git et python3 (les Command Line Tools les fournissent ; sinon `brew install git python`).
- GitHub la première fois : Dear ImGui, GLFW, OpenSSL et libssh2 sont téléchargés et compilés dans `hostpanel/deps/macos`,
  un dossier par jeu d'architectures (`DEPS=<dossier>` les met ailleurs ; `JOBS=<n>` règle les tâches en parallèle).

La compilation, universelle (arm64 et x86_64, par défaut) ou pour une seule architecture :

    bash hostpanel/build_macos.sh
    ARCHS="arm64" bash hostpanel/build_macos.sh
    ARCHS="x86_64" bash hostpanel/build_macos.sh

Chacune fait `hostpanel/build-macos/MGO1 Host/MGO1 Host.app` (`Contents/MacOS/mgo1-host`,
`Contents/Resources/MGO1Host.icns`, `Contents/Info.plist`, `Contents/PkgInfo`), signée ad hoc, et
`hostpanel/build-macos/MGO1-Host-macos.zip` : le dossier `MGO1 Host` avec l'application, README.txt, LISEZMOI.txt et
`licenses`. Les vérifications (le workflow les fait aussi) :

    A="hostpanel/build-macos/MGO1 Host/MGO1 Host.app"
    file "$A/Contents/MacOS/mgo1-host"                    # Mach-O universal binary with 2 architectures
    lipo -info "$A/Contents/MacOS/mgo1-host"              # x86_64 arm64 (une seule avec ARCHS=...)
    codesign --verify --deep --strict --verbose=2 "$A"    # valid on disk, satisfies its Designated Requirement

Sur un Mac avec un écran, `"$A/Contents/MacOS/mgo1-host" --selftest /tmp/mgo1-selftest` dessine chaque page dans chaque
langue (`layout_report.txt` : 0 erreur).

Le premier lancement : décompressez, mettez le dossier `server` à côté de `MGO1 Host.app`. Un zip téléchargé avec un
navigateur est mis en quarantaine : macOS lance alors l'application depuis une copie cachée et le dossier `server`
n'est pas trouvé. Avant le premier lancement, dans le Terminal : `xattr -dr com.apple.quarantine "<le dossier MGO1 Host
décompressé>"`. Puis vérifiez, sur un serveur de test :

1. L'application s'ouvre : sa fenêtre, le logo SaveMGO, l'icône dans le Dock et dans le Finder.
2. Les polices sont nettes (Retina aussi) ; OPTIONS, LANGUE : chacune des 11 langues se lit (japonais, coréen et chinois
   avec les polices du Mac).
3. SERVEUR, SE CONNECTER avec un mot de passe (MÉMORISER activé : le lancement suivant se connecte tout seul), puis avec
   une clé SSH (CHOISIR LA CLÉ : la fenêtre de fichiers du Mac ; la phrase secrète de la clé est demandée, jamais
   gardée).
4. INSTALLATION / MISE À JOUR, CHOISIR LE DOSSIER : la fenêtre de dossiers du Mac ; les fichiers du disque sont trouvés.
5. Les sons des menus sur les clics une fois le dossier FICHIERS DU DISQUE réglé (OPTIONS : coupés, le volume).
6. RÉGLAGES DE PARTIE : les réglages du serveur affichés ; ENREGISTRER ; ENREGISTRER ET RELANCER demande d'abord (OUI,
   NON).
7. JOURNAL (en direct et quotidien ; ENREGISTRER écrit `~/Documents/MGO1 Host/Logs` et l'ouvre dans le Finder) et GAME
   REPORT (ses fichiers dans `~/Documents/MGO1 Host/Game Report`). macOS demande une fois si MGO1 Host peut utiliser le
   dossier Documents : OK (de nouveau après chaque nouvelle compilation signée ad hoc).
8. Quittez et relancez : les serveurs, les comptes et les options sont gardés.

Où il garde les choses : `~/Library/Application Support/MGO1 Host` (`config.ini`, et `secret.key` : la clé AES-256-GCM,
32 octets aléatoires, mode 0600, lisible par votre utilisateur seul ; le programme remet à 0600 un fichier plus ouvert).
Les mots de passe qu'il garde (MÉMORISER, MÉMORISER SUR CE PC, les mots de passe des parties) sont chiffrés avec cette
clé ; aucun des deux fichiers ne contient de mot de passe ni d'ID de compte en clair ; sans `secret.key`, les mots de
passe gardés sont oubliés. Le Game Report et les journaux enregistrés : `~/Documents/MGO1 Host`.

La signature : build_macos.sh signe ad hoc (`codesign --sign -`), assez pour le Mac qui l'a compilé et, la quarantaine
retirée, pour les autres ; Gatekeeper ne sait pas qui l'a fait. Une version pour tout le monde est signée avec un
certificat Developer ID Application et le hardened runtime, notarisée et agrafée (stapler), une étape à part, sur le
compte Apple de l'éditeur :

    codesign --force --deep --options runtime --timestamp --sign "Developer ID Application: <nom> (<équipe>)" "$A"
    ditto -c -k --keepParent "$A" notarize.zip
    xcrun notarytool submit notarize.zip --keychain-profile <profil> --wait
    xcrun stapler staple "$A"

puis le zip refait depuis l'application agrafée.

## Pour les développeurs : une partie de 8 dans la liste du lobby

Une partie Host Only de 8 joueurs est le 9 du jeu (la place de l'hôte) : NOMBRE DE JOUEURS 8 écrit `max_players = 9`.
L'hôte annonce une telle partie au lobby de SaveMGO comme 9 (00/08 dans la liste des parties), comme l'attend
le jeu. Pour les joueurs dont le jeu échoue à rejoindre cette forme (1059),
`HOST_LIST_9_AS_8=1` dans `/etc/mgo1host/server.env` du serveur (ou, sur le serveur, `mgo1host-agent host-list-9 8` ;
retour : `host-list-9 9`) l'annonce comme 8, la forme plus ancienne (7 places) ; chaque bot le prend à son prochain
démarrage. MGO1 Host n'a pas de ligne pour cela.
