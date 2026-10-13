Les problémes :
- Il faudrait revoir les fenêtres et la porte (vrai porte ouvrable avec 'e')
- Probléme de transition entre les textures (tes changement pour la transition chemin herbe fonctionnent sur certains endroits comme tu peux le voir sur le screen)
- Performances, mon PC a tendance à ralentir : affiche les fps et ajoute un menu avec la touche echap permettant d'accèder aux options graphiques
- Il y a des plantes sur le chemin, ça ne devrait pas
- L'herbe de base (grass) ne semble pas toutes récoltable, ça doit être le cas

Ce qui est bien :
- Le plafond
- Les touffes d'herbe sont bien denses

Ce que j'aimerais : 
- Des étagère plus ou moins remplies derrière le marchand et sur le coté
- Herbe plus haute (x1.5)
- Plus d'herbes et de plantes 

========================================================

- Le sol de la cabane devrait plutot être des dalles de pierre
- Les fondations (tour de la maison) peuvent être en remblais avec la textur actuelle pourquoi pas, mais il ne faut pas que ce soit si carré. Eventuellement ajouter des irégularités.
- L'escalier d'entré, préférable en bois, un vrai escalier ou une marche un peu plus dévelopée.
- Autour de la maison, je propose de faire une place circulaire pas forcement régulière avec la texture du chemin.
- J'aimerais aussi que le terrain sois globalement moins plan en respectant cependant quelques conditions : 
    - Tous les secteurs doivent avoir leur bordure au même niveau et de manière fluide
    - Le secteur du marchand doit avoir la place centrale plane de manière à ce que la cabane puisse s'y trouver sans incohérence.
- J'aimerais aussi avoir différent types de buissons, potentiellement formant des groupe plus gros et plus hauts, jusqu'à x1.5 en hauteur par rapport à l'herbe.
- La vitesse du joueur est x0.75 dans les herbes, x0.5 dans les buissons

===============================================================

- L'escalier est bien mais un peu de pierre de fndation apparait au travers de la marche la plus haute
- La fondation de pierre de la maison ne couvre pas bien les angles de la maison
- Je pense que les volets des fenêtre ainsi que les jardinière pour remplacer/compléter le rebord extérieurs pourraient être développés d'avantage. Peut-être un modéle dédiés ? La porte aussi
- Les contours de fenêtre et porte devraient avoir une texture différentes.
- On doit pouvoir se mettre à croupi avec CTRL gauche
- Certaines texture/modéle de chiffons je crois, bavent et sont incohérents, je te joins quelques screen

===============================================================

- J'aimerais un peu de vie, génére moi quelques petits animaux : 
    - Des oiseaux qui se baladent dans le ciel
    - Des insectes dans l'herbe comme des papillons
    - Une chatte grise rayée avec des yeux jaunes dans le shop

================================================================

- Il faudrait qu'un élément récoltable soit mis en surbrillance
- Les secteurs suivant les chemin du secteur du marchand seront maintenant des secteurs "route" qui ne seront pas achetables
- Si un secteur est acheté sur le bord d'une route, alors celle-ci crée un chemin vers le secteur depuis le chemin principal pour permettre son accès.
- J'aimerais aussi refaire l'ui du menu principal et des options
- On peut aussi refaire le menu du marchand

===============================================================

J'ai aussi ajouté une clef ELEVENLAB_API_KEY pour générer du son, des effets notamment, ou de la narration, des voix.
J'aimerais que les oiseaux aient un vrai model, ils sont trop simples. Aussi, ils volent par groupes, mais j'aimerais que certains oiseaux volent seul.
Essayon de mettre un peu de son : 
- Le vent et le bruissement de l'herbe
- Les bruits de pas du joueur
- Le bruit de la porte qui souvre ou se ferme
- Le bruit des oiseaux

=================================================================

- Ajoute sur le shop actuel une pancarte général store au dessus de la porte
- Crée également une second cabane au bord du chemin non loin de la place du general store, ce sera la cabane du sector manager, celui qui vend, reprend les secteurs. On  trouvera un marchand différent, plus manager mais toujours dans la vibe campagne ghibli. Ajoute à ce nouveau shop une pancarte comme la première.
Du coup, le marchand du général store ne s'occupe plus des secteurs, c'est bien le marchand du sector manager.
On fera la disctinction des secteurs : 
- libres : achetable par le joueur, en vert comme maintenant
- publique : commerces, routes
- privés : achetés par d'autres joueur
- Au joueur : en bleu comme aujourd'hui
J'aimrais aussi que les nuages avances plus vite (x2)
En revanche, l'animation de la végéation peut ralentir de 33%
Toutes les plantes doivent être récolatbles. Il me semble que les fougères, et peut-être d'autres, ne le sont pas.
- On ajoutera aussi des panneaux à chaque intersection de route pour afficher la direction des shops
- Aux intersections de la place du general store, on ajoutera des panneaux pour afficher North South East West

====================================================================

Niveau sons, les bruits de pas son trop rapides, il faut compter un bruit par pas et imaginer un nombre de pas par secondes réaliste. Le bruit d'ouverture de la porte est un peu long, il devrait être synchro avec l'animation. Il s'agirait plus d'un grincement peut-être.
Les bruits de pas sur le bois et la pierre dans la cabane ne conviennent pas.
Au niveau des fenêtres, le volet de droite est inverssé, la poignée devriat être vers l'extérieur, pas coté fenêtre
Les pancartes devraient être plus classiques, pas besoins de toutes ces décorations
Sur les panneaux, on ne voit pas bien les inscription, il faut plus de contraste. Aussi la police de caractére devrait être plus manuscrite.
Les oiseaux semble voler de coté...
Au sujet des routes qui permettent l'accès à un secteur privé je voudrais changer le principe :
- Chaque secteur privé peut-être amélioré via la carte des secteur du sector manager : quand on clique sur le secteur, on a une liste d'améliorations possibles.
- Si un secteur est au bord d'une route, alors une amélioration pour que la route rejoigne le secteur est disponible pour 50c
- Si un secteur est au bord de plusieurs route, alors on propose une amélioration par route possible
- Les route de jonction à des secteurs privé vont jusqu'à la limite du secteur, pas dessus
- Les étagères du sector manager devraient plutot contenir des rouleaux, des cartes, des livres, des outils de mesure, ce genre de chose.
- Il faudrait aussi refaire les fenêtres correctement pour les maisons, peut-être un modéle spécifique.
- Je pense qu'on pourrait ajouter une nouvelle maison pour un vendeur de vétement (je te laisse trouver un nom approprié). On peut peut-être changer de maison pour éviter la répétition.
- Le vendeur de vétement prendra donc a sa charge la vente de vétements du general store

=============================================================================

- Je propose d'ajouter les musiques au jeu. Tu peux aller chercher les musique d'ambiance du premier projet et les incorporer en tant que musique d'ambiance à l'actuel. Pour rappel, les musiques se lance à partir de quelques secondes en jeu. Le lancement, tout comme l'arret, se fait progressivement. Quand une musique se termine, une autre se lance quelques temps aprés. Le choix de la musique est aléatoire, mais jamais deux fois de suite la même.
- On devrait également ajouter au général store un sac à dos. Quand on l'achéte, il est directemet équipé sur notre dos. Quand on l'a d'équipé, on peut appuyer sur F pour le lacher à terre. On pourra alors appuyer sur F en le regardant (si il nous appartient) pour accéder à son inventaire. On aura en paralléle notre inventaire à coté, permettant de faire des échanges entre les deux par drag & drop. En regardant le sac, on peut aussi appuyer sur G pour essayer de transvaser la totalité de notre inventaire dedans. Proche d'un marchand, on comptabilise les objet des sacs à dos qui nous appartiennent. Le sac coute 200c. Il permet également d'y stocker deux outil, visibles sur chaque cotés du sac à dos.
- Il faudrait pouvoir switcher de vue (1rst person, 3rd person, 3rd person from front) avec la touche V

==================================================================================

- Chez le sector manager, on aura un nouvel onglet permettant de proposer des amélioration pour le village, le village étant la zone des shops, mais également tous les secteurs autour géré par ce sector manager
- Une premiére amélioration est la construction du tailor, qui doit donc ne pas exister de base. ça coutera 300c
- Une autre amélioration seront les panneaux de direction, pour 75c
- Une autre amélioration seront des lanternes sur la devanture de chaque boutique, ainsi que des lanternes autour de la zone de shop et aux intersections dans tout le village : 200c
- Une autre amélioration sera la décoation de la zone de shop (200c): 
    - Des bordures de chemin en pierre
    - Des buissons de fleurs et pots de fleurs
    - Des arbres
    - Des bancs sur lesquels on peut s'asseoir
- Amélioration des chemins en pavets de pierre, pour le centre du village (zone de shop) : 200c (les chemin de pavets de pierre augmente x1.2 la vitesse de marche dessus)
- si le centre du village est pavé, Amélioration des chemins en pavets de pierre pour le reste du village sauf liaisons vers les secteurs (ça se seront les privés qui pourront le faire)
- Si le village est pavé (centre et village) : les secteurs privé disposent de l'amlioration des chemins de lisaison en pavet aussi (sur les lisaisons de terre existant déjà)
- Dans l'inventaire, si on appuis sur A en survolant un objet, on peut le jeter. Si c'est un outil, on demandera une confirmation
- Si on porte le sac, on ne voit pas l'inventaire du sac quand on va dans notre inventaire. Il faut poser le sac pour regarder dedans. Idem pour y stocker des objets récoltés, si on récolte de l'herbe par exemple, celle-ci ne va pas automatiquement dans le sac, il faudra poser le sac pour y faire le transfert.
- Comme on retirer le sac avec F (il faudra d'ailleur le mentionner au joueur), on appuira aussi sur F pour remettre le sac. E servira a accéder à l'inventaire du sac.

=================================================================================

Il faudra un modéle animé pour le joueur
A certains endroits, les lanternes pourraient être mises sur le même poteau que pour les panneaux plutot que de mettre un poteau supplémentaire
Les arbres de l'amélioration d'embelissement doivent être beaucoup plus grand pour faire de l'ombre au centre du village, peut être en ajouter quelques uns
Les pots de fleurs et bancs devraient avoir leur propre modéle et devrait ne pas avoir d'herbe à leur emplacement, peut-être même un petit chemin de terre jusqu'aux bancs
Ajoute aussi un panneau de triche permettant d'avancer le temps et de définir son nombre de crédits (CTRL + ALT + ù puis CTRL + SHIFT + *)

=======================================================

Les lanternes devraient illuminer un tout petit peu moins (-20%) et osciller aléatoirement, comme une flamme de bougie.
our les arbres, génére également des modéle, des arbres avec un beau et grand feuillage, genre saule ou dans le genre.

J'aimerais ajouter également une charette de transport, achetable au general store pour 1000c. Quand elle est achetée, elle vient dans l'inventaire. Elle s'équipe pour permettre de la poser, on voit alors à l'endroit ou pointe notre curseur une projection de la charette. On la place alors à cet endroit au clic gauche.
La charette a deux roues et deux manches à l'avant. En regardant devant la charette, on peut appuyer sur F pour la conduire. On avance alors à 50% de notre vitesse normale. On appuiera sur F de nous pour quitter la conuite de la charette.
Dans la charette, on peut placer 2 conteneurs (nouveau) en ayant le conteneur dans les main et en cliquant sur l'emplacement qui se met en surbrillance au survol.
Les conteneur s'achéte au general store, arrivent dans l'inventaire. En cliquant dessus, on les équipes en main. En cliquant par terre, on la dépose. En appuyant sur e dessus, on la prend. En la visant et appuyant sur f, on accéde à son inventaire avec en parraléle le notre.
2 nouveaux conteneur :
- La cagette : peut contenir des plantes, autant d'emplacement que dans un sac, vaut 200c et on peut en empiler jusqu'à 3, mais on ne peut en prendre qu'une en mains.
- le tonneau : peut contenir l'équivalent de 2.5 cagettes, vaut 420c. Quand on porte un tonneau, on avance 50% moins vite.

===========================================================

Les bancs sont à l'envers
ça crache lorsque je veux accéder à l'inventaire
```
LoginId:d2de880de5f049489cabda75ab65d74d
EpicAccountId:3bc7cdab0a864b799aab1e33e82b6ae8

Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000010

UnrealEditor_CoreUObject
UnrealEditor_CoreUObject
UnrealEditor_Erewhon!SInventoryPanel::Construct() [C:\Users\ADMIN\Documents\Unreal Projects\Erewhon\Source\Erewhon\UI\SInventoryPanel.cpp:181]
UnrealEditor_Erewhon!TSlateDecl<SInventoryPanel,RequiredArgs::T0RequiredArgs>::operator<<=() [C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\SlateCore\Public\Widgets\DeclarativeSyntaxSupport.h:983]
UnrealEditor_Erewhon!AErewhonPlayerController::OpenInventory() [C:\Users\ADMIN\Documents\Unreal Projects\Erewhon\Source\Erewhon\Core\ErewhonPlayerController.cpp:407]
UnrealEditor_Erewhon!TBaseUObjectMethodDelegateInstance<0,AErewhonCharacter,void __cdecl(void),FDefaultDelegateUserPolicy>::Execute() [C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Core\Public\Delegates\DelegateInstancesImpl.h:609]
UnrealEditor_Erewhon!FEnhancedInputActionEventDelegateBinding<TDelegate<void __cdecl(void),FDefaultDelegateUserPolicy> >::Execute() [C:\Program Files\Epic Games\UE_5.8\Engine\Plugins\EnhancedInput\Source\EnhancedInput\Public\EnhancedInputComponent.h:340]
UnrealEditor_EnhancedInput
UnrealEditor_Engine
UnrealEditor_EnhancedInput
UnrealEditor_Engine
UnrealEditor_Engine
UnrealEditor_Engine
UnrealEditor_Engine
UnrealEditor_Erewhon!AErewhonPlayerController::PlayerTick() [C:\Users\ADMIN\Documents\Unreal Projects\Erewhon\Source\Erewhon\Core\ErewhonPlayerController.cpp:447]
UnrealEditor_Engine
UnrealEditor_Engine
UnrealEditor_Engine
UnrealEditor_Core
UnrealEditor_Core
UnrealEditor_Core
UnrealEditor_Core
UnrealEditor_Engine
UnrealEditor_Engine
UnrealEditor_Engine
UnrealEditor_Engine
UnrealEditor
UnrealEditor
UnrealEditor
UnrealEditor
UnrealEditor
UnrealEditor
kernel32
ntdll
```

==============================================================

Les saules font un peu jungle, on peut les garder pour plus tard mais là c'est pas un arbre qu'on met dans un village.
La charette contient déjà des choses, elle devrait être vide pour qu'on puisse y metre cagettes et tonneaux.
Lorsque l'on conduit la charette, celle-ci doit suivre le joueur en roulant. Elle s'orientera en tournant autour du milieu entre les roues. Si on la pousse pendant qu'on la conduit (on va vers elle, à l'envers donc), elle roule à l'envers.

==============================================================

L'axe en bois de la charette pour tenir les roues est visible  et pas centré avec les roues, il est beaucoup trop en avant. Soit il faut l'aligner, soit le retirer.
On va ajouter une nouvelle amélioration pour un secteur de type prairie : apothicairerie. Elle coute 2000c et construit une petite maison d'apoticaire à l'intérieur avec une piéce dans laquelle on a une cheminée qu'on peut allumer avec du petit bois qu'on peut acheter au generals store. ça permet de faire un peu de lumiére. On à aussi une table sur laquelle on a un petit chaudron et un mortier.
Autour de la maison, on a quelques arbres.
Si des améliorations de jonction avec la route sont faites, un chemin complémentaire est ajouté jusqu'à l'apoticairerie.
Avec le mortier, on peut créer des écrasés de plante qui valent 1.5x plus que la plante elle même et prenne 1s chaque à faire. Ensuite, dans le chaudrons, on peut mettre les écrasés d'herbe pour faire des bases de potion, puis en combinant d'autres type de plantes, on obtiens des potions différentes.
Les potions peuvent être utilisées pour obtenir des effets comme un buff de vitesse, la vision nocturne, de la force (pour ne pas ête ralenti quand on porte quelquechose), un saut plus haut, rapetisser ou grandir.
Ces potions valent x2 par rapport à l'écrasé.
Il faudrait aussi ajouter la lune et les étoiles (la lune est en opposition au soleil et brille beaucoup moins).

==================================================================

Les tonneaux et cagettes placés dans la charette ne sont pas assez bas, ils sont au niveau du dessus des barières autour de la charette, mais ils devraient être au niveau du plancher de la charette
Les pierres des murs de l'apoticairie doivent être plus grosse (x2)

===============================================================

Le jeu est fermé. Tu peux aussi retirer les cout pour les fabrications dans le mortier et le chaudron.
Aussi, les étagères de l'apoticairies doivent être du coté droit de la piéce, il doit y en avoir 3 et on doit pouvoir y placer les potions. Elles seront ainsi visibles par stack (10).

================================================================

Fait de beaux models pour les potions.
La barre de progression pour faire les potions n'est pas assez visible, pas assez de contraste
Je pense que le chaudron devrait être sur le feu. n'hésite pas à faire un beau model animé pour le feu.
Si le feu est allumé, alors on peu faire des potions.
On peut aussi mettre des écrasés sur les étagères.
Si on fait E sur une étagère, on accède à son inventaire, on voit les différentes rangées et on peut donc y placer là où on veut nos items

==============================================================

Peut-être une cheminée modélisée sera plus jolie
Grossi encore un peu la pierre utilisée pour les murs
Je propose de faire une petite piéce sur la droite de l'apoticairie qui serait la remise, accessible depuis l'intérieur.
Cette remise serait une amélioration de l'apoticairerie achetable pour 1000c.
D'ailleurs je pense que le mobilier devra être achetable au general store : étagères, table, chaudron, mortier. Je te laisse mettre des prix cohérents. 
Aussi, les éléments mobilier, ainsi que les conteneurs, ne peuvent pas être posé sur un secteur qui n'est pas le notre. En revanche, on peut toujours poser n'importe où une charette. Et on peut toujours y déposer cagette et tonneaux, même si on est pas sur son secteur
Grossi un peu la taille des potions et éventuellement ajoute leur de petits effets de lumiére

