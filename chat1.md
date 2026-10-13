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

