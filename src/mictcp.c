#include <mictcp.h>
#include <api/mictcp_core.h>
#include <string.h>

#define nbMaxSocket 5

mic_tcp_sock mon_socket[nbMaxSocket];
unsigned short listeNumPortLoc[nbMaxSocket];
int pourcentagePerteAcceptable = 10;

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
    static int i = 1;
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    initialize_components(sm); /* Appel obligatoire */
    set_loss_rate(10);  //set le pourcentage de perte sur le rzo
    if(i>=nbMaxSocket){  //si déjà 5 sockets crées en tout, refuser la création d'un socket
        return(-1);
    }
    mon_socket[i-1].fd = i;
    mon_socket[i-1].state = IDLE;  //set le state du socket en idle
    i++;
    return mon_socket[i-2].fd;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(addr.port<1024||(0)){  //manque la vérif que addr ip est de la machine
        return(-1);
    }
    if(socket>=nbMaxSocket){  //si descripteur du socket incorrecte, refuser le binding
        return(-1);
    }
    mon_socket[socket-1].local_addr = addr;
    listeNumPortLoc[socket-1] = addr.port;
    return 0;
}

/*
 * Met le socket app_buffer_put en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(socket>=nbMaxSocket){  //si descripteur du socket incorrecte, refuser le connect
        return(-1);
    }
    mon_socket[socket-1].state = ESTABLISHED;  //set le state du socket en connecté
    return 0; //Pas d'établissement de connexion nécessaire pour l'heure
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{ 
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(socket>=nbMaxSocket){  //si descripteur du socket incorrecte, refuser le accept
        return(-1);
    }
    mon_socket[socket-1].remote_addr = addr;
    mon_socket[socket-1].state = ESTABLISHED;  //set le state du socket en connecté
    return 0; //Pas d'établissement de connexion
}

int pourcentagePerteFenetre(int* fenetre, int tailleFenetre){
    int sum = 0;
    for(int i=0;i<tailleFenetre;i++){
        sum+=fenetre[i];
    }
    return sum*100/tailleFenetre;
}

int addFenetre(int* fenetre, int res){
    static int i = 0;
    fenetre[i%100] = res;
    //printf("%d\n",i);
    i++;
    return i*(i<100)+100*(i>=100);
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    static int fenetreGlissante[100] = {0};
    static int num_seq = 0;  //numéro de séquence propre au socket
    mic_tcp_pdu pdu;
    pdu.header.source_port = mon_socket[mic_sock-1].local_addr.port;
    pdu.header.dest_port = mon_socket[mic_sock-1].remote_addr.port;
    pdu.header.seq_num = num_seq;
    pdu.payload.data = mesg;
    pdu.payload.size = mesg_size;
    unsigned long timeout = 2;  //valeur du timer ACK en ms
    mic_tcp_sock_addr addr_recue;
    mic_tcp_pdu pdu_ack;
    int effectively_sent;

    const int payload_size = 1500 - API_HD_Size;
    pdu_ack.payload.size = payload_size;
    pdu_ack.payload.data = malloc(payload_size);

    addr_recue.ip_addr.addr=malloc(100);
    addr_recue.ip_addr.addr_size=100;

    int tailleFenetre = 1;

    while(1){
        effectively_sent = IP_send(pdu,mon_socket[mic_sock-1].remote_addr.ip_addr);
        if((IP_recv(&pdu_ack,&(mon_socket[mic_sock-1].local_addr.ip_addr),&addr_recue.ip_addr,timeout))!=-1){
            if(strcmp(addr_recue.ip_addr.addr,"127.0.0.1") == 0  //vérification que l'adresse ip recue correspond à l'adresse destinataire du IP_send (localhost 127.0.0.1)
                    && pdu_ack.header.source_port == pdu.header.dest_port  //vérification que le numéro de port source du pdu recu correspond au numéro de port destinataire du pdu envoyé via IP_sent
                    && pdu_ack.header.syn != 1  //vérification que le pdu reçu n'est pas un SYN
                    && pdu_ack.header.ack == 1  //vérification que le pdu reçu est un ACK
                    && pdu_ack.payload.size == 0  //vérification que le pdu reçu n'ait pas de payload
                    && pdu_ack.header.ack_num == num_seq+1){  //vérification que le pdu reçu ait le bon numéro d'aquittement
                num_seq++;  //incrémentation du numéro de séquence
                tailleFenetre = addFenetre(fenetreGlissante,0);
                //printf("%d %d %d %d %d %d %d %d %d %d\n",fenetreGlissante[0],fenetreGlissante[1],fenetreGlissante[2],fenetreGlissante[3],fenetreGlissante[4],fenetreGlissante[5],fenetreGlissante[6],fenetreGlissante[7],fenetreGlissante[8],fenetreGlissante[9]);
                free(addr_recue.ip_addr.addr);
                return effectively_sent;
            }
        } else if(pourcentagePerteFenetre(fenetreGlissante,tailleFenetre)<=pourcentagePerteAcceptable){
            printf("perte acceptable\n");
            tailleFenetre = addFenetre(fenetreGlissante,1);
            //printf("%d %d %d %d %d %d %d %d %d %d\n",fenetreGlissante[0],fenetreGlissante[1],fenetreGlissante[2],fenetreGlissante[3],fenetreGlissante[4],fenetreGlissante[5],fenetreGlissante[6],fenetreGlissante[7],fenetreGlissante[8],fenetreGlissante[9]);
            free(addr_recue.ip_addr.addr);
            return(0);
        }
        printf("retransmission du pdu\n");
    }
}

/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    mic_tcp_payload payload;
    payload.data = mesg;
    payload.size = max_mesg_size;
    return app_buffer_get(payload);
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");
    return -1;  //marche jamais, pas implémenté
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_ip_addr local_addr, mic_tcp_ip_addr remote_addr)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    static int num_seq[nbMaxSocket] = {0};
    int i = 0;
    for(i=0;i<(sizeof(listeNumPortLoc)/sizeof(listeNumPortLoc[0]));i++){  //parcours de la liste des numéros de port pour trouver celui correspondant à celui du pdu reçu si existe
        if(pdu.header.dest_port == listeNumPortLoc[i]){
            if(pdu.header.seq_num == num_seq[i]){
                app_buffer_put(pdu.payload);
                num_seq[i]++;
            }
            break;
        }
    }
    mic_tcp_pdu pdu_ack;
    pdu_ack.header.ack_num = num_seq[i];
    pdu_ack.header.ack = 1;
    pdu_ack.header.syn = 0;
    pdu_ack.header.source_port = pdu.header.dest_port;
    pdu_ack.header.dest_port = pdu.header.source_port;
    pdu_ack.payload.size = 0;
    IP_send(pdu_ack,remote_addr);
}
