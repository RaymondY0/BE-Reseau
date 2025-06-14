# Projet MICTCP : Implémentation d’un protocole de transport à fiabilité partielle

## Commandes de compilation et d'exécution:

## Lancer le récepteur (client)
./tsock_texte -s localhost 9000 (Utilisez un numéro de port supérieur à 1024)
./tsock_video -s -t mictcp

## Lancer l’émetteur (serveur)
./tsock_texte -p 9000
./tsock_video -p -t mictcp

## V1:
La transmission de données fonctionne lorsque le réseau ne subit aucune perte. 

## V2:
Implémentation de Stop and Wait pour assurer la fiabilité totale. Lorsqu’un pdu ACK n’est pas reçu dans un délai imparti, le paquet est renvoyé. L’utilisation de numéros de séquence et d'acquittement permet de s'assurer de la réception et de l'ordonnancement des envois, le serveur refusant tout paquet ayant de mauvais numéros de séquence, et pareil côté client avec les pdu ACK et les numéros d'acquittement.

## V3:
Implémentation de la fenêtre glissante pour assurer la fiabilité partiel: Au début de la communication, un taux de perte acceptable est négocié, même si en pratique le client annonce le taux de perte acceptable tandis que le serveur le prend en compte sans discuter. Pour calculer le taux de perte réel, le programme incrémente progressivement, à chaque envoi de données concluant ou perte acceptable, la taille de la fenêtre glissante de 1 jusqu'à atteindre 100, taille max arbitraire de la fenêtre. À chaque tentative, il enregistre un 0 dans un tableau représentant la fenêtre si le message a bien été reçu, ou un 1 en cas de perte acceptable.
En comptant le nombre de 1, le programme calcule le taux de perte actuel. En cas de perte, si ce taux est inférieur au taux de perte acceptable, la perte est acceptable au regard de la fiabilité partielle, et la donnée n'est pas retransmise.

## V4.1:
Implémentation de la phase de connexion : three-way handshake (SYN, SYN-ACK, ACK). L’inconvénient est que côté récepteur, c'est le thread applicatif qui effectue les IP_send alors qu'en théorie il aurait fallu que ce soit le thread protocolaire mictcp, qui effectue les process_received_pdu, qui s'en occupe. Cette décision a été prise car dans le cas contraire, il aurait fallu que le thread mictcp crée lui-même un thread dédié exclusivement à envoyer et retransmettre les pdu de connexion, car sinon il aurait été bloqué en état de retransmission sans pour autant pouvoir recevoir les pdu qui l'en débloquera. Un autre inconvénient est que dans l'idéal il aurait fallu utiliser des pthread_cond_timedwait dans la boucle de retransmission du pdu SYN-ACK, mais l'implémentation étant plus complexe à mettre en place, le choix a été de partir sur un simple sleep, qui fonctionne, mais qui ne peut pas être interrompu.

## V4.2:
Implémentation de l'asynchronisme côté émetteur : gestion de threads applicatif et protocolaire distincts. Parallèlement au côté récepteur, l'application communique avec mictcp par le biais du buffer applicatif et d’une variable condition qui vient “réveiller” le protocole. Ne sachant pas comment transmettre à mictcp l'information du descripteur du socket appelant, le choix a été de se concentrer sur un seul socket fonctionnel, au descripteur 1. Le problème est que l'on ne peut transmettre que des payload entre les deux threads, or cette structure n'est composée que d'un champ data et size, et les deux sont importants par la suite pour la transmissions des pdu au récepteur. Il aurait fallu ajouter un champ int socket aux payload, mais cela impliquerait de modifier l'implémentation des fonctions app_buffer dans mictcp_core, et ne les ayant pas implémenté nous-même, on préfère l'éviter.
