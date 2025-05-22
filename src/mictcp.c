#include <mictcp.h>
#include <api/mictcp_core.h>
#include <string.h>

mic_tcp_sock mon_socket[5];
unsigned short listeNumPortLoc[5];

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
    static int i = 1;
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    initialize_components(sm); /* Appel obligatoire */
    set_loss_rate(0);
    mon_socket[i-1].fd = i;
    mon_socket[i-1].state = IDLE;
    i++;
    return mon_socket[i-2].fd;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr)
{
    static int i = 0;
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(addr.port<1024||(0)){ //manque la vérif que addr ip est de la machine
        return(-1);
    }
    mon_socket[socket-1].local_addr = addr;
    listeNumPortLoc[i] = addr.port;
    return 0;
}

/*
 * Met le socket app_buffer_put en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    return 0; //Pas d'établissement de connexion
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{ 
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    mon_socket[socket-1].remote_addr = addr;
    return 0; //Pas d'établissement de connexion
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    static int num_seq = 0;
    mic_tcp_pdu pdu;
    pdu.header.source_port = mon_socket[mic_sock-1].local_addr.port;
    pdu.header.dest_port = mon_socket[mic_sock-1].remote_addr.port;
    pdu.header.seq_num = num_seq;
    pdu.payload.data = mesg;
    pdu.payload.size = mesg_size;
    unsigned long timeout = 1000000;
    mic_tcp_sock_addr addr_recue;
    mic_tcp_pdu pdu_ack;
    int effectively_sent;
    while(1){
        effectively_sent = IP_send(pdu,mon_socket[mic_sock-1].remote_addr.ip_addr);
        if((IP_recv(&pdu_ack,&mon_socket[mic_sock-1].local_addr.ip_addr,&addr_recue.ip_addr,timeout))!=-1){
            if(strcmp(addr_recue.ip_addr.addr,mon_socket[mic_sock-1].remote_addr.ip_addr.addr)==0
            && addr_recue.port == mon_socket[mic_sock-1].remote_addr.port && pdu_ack.header.syn != 1
            && pdu_ack.payload.size == 0 && pdu_ack.header.ack_num == num_seq+1){
                break;
            }
        }
    }
    num_seq++;
    return effectively_sent;
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
    return -1;
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
    static int num_seq[5] = {0,0,0,0,0};
    int i = 0;
    for(i=0;i<(sizeof(listeNumPortLoc)/sizeof(listeNumPortLoc[0]));i++){
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
    pdu_ack.header.source_port = pdu.header.dest_port;
    pdu_ack.header.dest_port = pdu.header.source_port;
    pdu_ack.payload.size = 0;
    IP_send(pdu_ack,remote_addr);
}
