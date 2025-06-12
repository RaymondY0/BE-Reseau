#include <mictcp.h>
#include <api/mictcp_core.h>
#include <string.h>
#include <pthread.h>

#include <unistd.h>

#define nbMaxSocket 5
#define N 10000000000000

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

mic_tcp_sock mon_socket[nbMaxSocket];
unsigned short listeNumPortLoc[nbMaxSocket];
int pourcentagePerteAcceptable;
pthread_t client_th;

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

void* thread_client(void* arg){
    unsigned long timeout = 2;  //temps en ms

	
	static int fenetreGlissante[100] = {0};  //ne fonctionne que pour 1 socket, sinon aurait fallu une fenetre et une variable num_seq static pour chaque socket possible
    static int num_seq = 0;  //numéro de séquence propre au socket
	static int tailleFenetre = 1;

    mic_tcp_pdu pdu_send;

    mic_tcp_sock_addr addr_recue;
    mic_tcp_pdu pdu_recue;
	
	addr_recue.ip_addr.addr = malloc(100);
    addr_recue.ip_addr.addr_size = 100;

    const int payload_size = 1500 - API_HD_Size;
    pdu_recue.payload.size = payload_size;
    pdu_recue.payload.data = malloc(payload_size);
	
	mic_tcp_payload payload;
    payload.data = malloc(payload_size);
    payload.size = payload_size;

    int socket;

    pthread_mutex_lock(&mutex);

    while(1){
        pthread_cond_wait(&cond,&mutex);
        socket = 1;  //il faudrait que l'on puisse récupérer le descripteur du socket appelant mictcp, mais je ne sais pas comment faire, et dans la mesure où l'algo ne fonctionne que pour 1 socket, on va considérer l'utilisation que du socket 1
        
        if(mon_socket[socket-1].state == SYN_SENT){
            pdu_send.header.source_port = mon_socket[socket-1].local_addr.port;
            pdu_send.header.dest_port = mon_socket[socket-1].remote_addr.port;
            pdu_send.header.ack = 0;
            pdu_send.header.syn = 1;
            pdu_send.header.seq_num = 5;  //utilisation de seq_num dans le pdu syn pour négocier le taux de perte acceptable
            pdu_send.payload.size = 0;
            while(1){
                if(IP_send(pdu_send, mon_socket[socket-1].remote_addr.ip_addr)==-1){
                    mon_socket[socket-1].state = IDLE;
                    break;
                }
                if((IP_recv(&pdu_recue, &mon_socket[socket-1].local_addr.ip_addr, &addr_recue.ip_addr, timeout))!=-1){
                    if(strcmp(addr_recue.ip_addr.addr,"127.0.0.1") == 0  //vérification que l'adresse ip recue correspond à l'adresse destinataire du IP_send (localhost 127.0.0.1)
                            && pdu_recue.header.source_port == mon_socket[socket-1].remote_addr.port
                            && pdu_recue.header.ack == 1
                            && pdu_recue.header.syn == 1
                            && pdu_recue.payload.size == 0){ //vérif si adresse de source reçue est le même que l’adresse de destination mise dans le IP_send mais pas avec un ==
						mon_socket[socket-1].state = ESTABLISHED;
                        break;
                    }
                }
            }
            if(mon_socket[socket-1].state == ESTABLISHED){
                pdu_send.header.ack = 1;
                pdu_send.header.syn = 0;
                pdu_send.payload.size = 0;
                IP_send(pdu_send, mon_socket[socket-1].remote_addr.ip_addr);
            }

        } else if(mon_socket[socket-1].state == CLOSING){  //utilisation de l'état CLOSING comme un état DATA_SENT
            //printf("payload size : %d\n", payload.size);
            payload.size = payload_size;
            pdu_send.payload.size = app_buffer_get(payload);
			pdu_send.header.source_port = mon_socket[socket-1].local_addr.port;
			pdu_send.header.dest_port = mon_socket[socket-1].remote_addr.port;
			pdu_send.header.seq_num = num_seq;
			pdu_send.payload.data = payload.data;
			int effectively_sent;

			while(1){
				effectively_sent = IP_send(pdu_send,mon_socket[socket-1].remote_addr.ip_addr);
				if((IP_recv(&pdu_recue,&(mon_socket[socket-1].local_addr.ip_addr),&addr_recue.ip_addr,timeout))!=-1){
                    //printf("%d,%d\n",pdu_recue.header.syn,pdu_recue.header.ack_num);
                    while(pdu_recue.header.syn == 1){  //ignorer les pdu SIN-ACK perdu
                        IP_recv(&pdu_recue,&(mon_socket[socket-1].local_addr.ip_addr),&addr_recue.ip_addr,timeout);
                    }
					if(strcmp(addr_recue.ip_addr.addr,"127.0.0.1") == 0  //vérification que l'adresse ip recue correspond à l'adresse destinataire du IP_send (localhost 127.0.0.1)
							&& pdu_recue.header.source_port == pdu_send.header.dest_port  //vérification que le numéro de port source du pdu recu correspond au numéro de port destinataire du pdu envoyé via IP_sent
							&& pdu_recue.header.syn != 1  //vérification que le pdu reçu n'est pas un SYN
							&& pdu_recue.header.ack == 1  //vérification que le pdu reçu est un ACK
							&& pdu_recue.payload.size == 0  //vérification que le pdu reçu n'ait pas de payload
							&& pdu_recue.header.ack_num == num_seq+1){  //vérification que le pdu reçu ait le bon numéro d'aquittement
						num_seq++;  //incrémentation du numéro de séquence
						tailleFenetre = addFenetre(fenetreGlissante,0);
						
						sprintf(payload.data,"%d",effectively_sent);
                        payload.size = sizeof(payload.data);
						break;
					}
				} else if(pourcentagePerteFenetre(fenetreGlissante,tailleFenetre)<pourcentagePerteAcceptable){
					printf("perte acceptable\n");
					tailleFenetre = addFenetre(fenetreGlissante,1);
					
					sprintf(payload.data,"%d",0);
                    payload.size = sizeof(payload.data);
                    break;
				}
				printf("retransmission du pdu\n");
			}
            app_buffer_put(payload);
			mon_socket[socket-1].state = ESTABLISHED;
		}
    }
}

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
    static int i = 1;
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    int res = initialize_components(sm); /* Appel obligatoire */
    if((i == 1) && (res == 1) && (sm == CLIENT)){
        pthread_create(&client_th, NULL, thread_client, "1");
    }
    set_loss_rate(5);  //set le pourcentage de perte sur le rzo
    if(i>nbMaxSocket){  //si déjà 5 sockets crées en tout, refuser la création d'un socket
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
    unsigned long timeout = 0.002;  //temps en s

    pthread_mutex_lock(&mutex);
    while(mon_socket[socket-1].state != SYN_RECEIVED){
        pthread_cond_wait(&cond,&mutex);
    }
    pthread_mutex_unlock(&mutex);

    mic_tcp_pdu pdu_syn_ack;
    pdu_syn_ack.header.source_port = mon_socket[socket-1].local_addr.port;
    pdu_syn_ack.header.dest_port = addr->port;
    pdu_syn_ack.header.syn = 1;
    pdu_syn_ack.header.ack = 1;
    pdu_syn_ack.payload.size = 0;
    int i;
    mon_socket[socket-1].state = SYN_SENT;
    for(i=0;i<N;i++){
        IP_send(pdu_syn_ack, mon_socket[socket-1].remote_addr.ip_addr);  //envoi du PDU SYN-ACK
        sleep(timeout);
        if(mon_socket[socket-1].state == ESTABLISHED){
            *addr = mon_socket[socket-1].remote_addr;
            break;
        }
    }
	return(-(i>=N));
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échecmon_socket[socket-1].local_addr.ip_addr
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr)
{ 
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if(socket>=nbMaxSocket){  //si descripteur du socket incorrecte, refuser le accept
        return(-1);
    }
    unsigned long timeout = 0.002;  //temps en s

	mon_socket[socket-1].remote_addr = addr;
	mon_socket[socket-1].state = SYN_SENT;
	
	while(mon_socket[socket-1].state == SYN_SENT){
        pthread_cond_broadcast(&cond);
		sleep(timeout);
	}
    return -(mon_socket[socket-1].state != ESTABLISHED);
}

/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    unsigned long timeout = 0.002;  //temps en s
	
	mic_tcp_payload payload;
    payload.data = mesg;
    payload.size = mesg_size;
	app_buffer_put(payload);
	mon_socket[mic_sock-1].state = CLOSING;
    pthread_mutex_lock(&mutex);
	pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&mutex);
	
	while(mon_socket[mic_sock-1].state == CLOSING){
		sleep(timeout);
	}

    app_buffer_get(payload);
	return atoi(payload.data);
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
            if(mon_socket[i].state == ESTABLISHED){
                if(pdu.header.seq_num == num_seq[i]){
                    app_buffer_put(pdu.payload);
                    num_seq[i]++;
                }
                mic_tcp_pdu pdu_ack;
                pdu_ack.header.ack_num = num_seq[i];
                pdu_ack.header.ack = 1;
                pdu_ack.header.syn = 0;
                pdu_ack.header.source_port = pdu.header.dest_port;
                pdu_ack.header.dest_port = pdu.header.source_port;
                pdu_ack.payload.size = 0;
                IP_send(pdu_ack,remote_addr);
                break;
            } else if(mon_socket[i].state == IDLE){
                if(pdu.header.syn == 1
                        && pdu.header.ack != 1
                        && pdu.payload.size == 0){
                    pthread_mutex_lock(&mutex);

                    pourcentagePerteAcceptable = pdu.header.seq_num;  //le serveur accepte le taux de perte proposé par le client
                    mic_tcp_sock_addr addrDist;
                    addrDist.ip_addr = remote_addr;
                    addrDist.port = pdu.header.source_port;
                    mon_socket[i].remote_addr = addrDist;
                    mon_socket[i].state = SYN_RECEIVED;

                    pthread_cond_broadcast(&cond);

                    pthread_mutex_unlock(&mutex);
                }
            } else if(mon_socket[i].state == SYN_SENT){
                if(strcmp(remote_addr.addr,"127.0.0.1") == 0  //vérification que l'adresse ip recue correspond à l'adresse destinataire du IP_send (localhost 127.0.0.1)
                        && pdu.header.dest_port == mon_socket[i].local_addr.port){ //vérif
                    mon_socket[i].state = ESTABLISHED;
                    break;
                }
            }
        }
    }
}

