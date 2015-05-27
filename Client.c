/****************************************************************************/
/* rcftp client- Stop & Wait and Go Back N   
 * Autor: Luis Jesús Pellicer 
 * Description : Code developed for the work of the subject Computer Networks in University of Zaragoza
 * Version: First version developed without considering aspects of code extension or efficiency due to the short time for implementation */
/****************************************************************************/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "rcftp.h"
#include "rcftpclient.h"
#include "multialarm.h"
#include <signal.h>
#define RCFTP_BUFLEN 512
//definición de variables globales
char f_verbose=0; //flag, 1: imprimir por pantalla información extra
struct sockaddr_in  *saddr_ipv4;
struct addrinfo hints, *servinfo; //estructura para preparar las estructuras de direcciones del socket para su uso posterior
struct rcftp_msg mssg;
struct sockaddr_storage caddr;
struct rcftp_msg sendbuffer;
struct rcftp_msg recvbuffer;

	 //estructura genérica para almacenamiento de direcciones IPv4
struct sockaddr_in6 *saddr_ipv6; //estructura genérica para almacenamiento de direcciones IPv6
char ipstr[INET6_ADDRSTRLEN], *ipver;
void *addr;

/**************************************************************************/
/* MAIN                                                                   */
/**************************************************************************/


/**************************************************************************/
/*  algoritmo 2 (practica 4)  */
/**************************************************************************/
void stop_wait(int sock,char * remote,int verb){
	int ultimoMensaje=0;	// Si se lee final de fichero vale 1
	int ultimoMensajeConfirmado=0;	//Si se recibe la confirmación del final por parte del servidor valdra 1.	
	int auxiliar, auxiliar2;
	int respuestaEsperada=0; //Si el servidor responde correctamente valdrá 1.
	int salirDelBucle=0;	//Variable para gestionar el bucle pérdidas de mensajes y errores
	uint32_t nextCliente;	//Siguiente trama 
	uint32_t nextServidor;	//Para comparar la trama que pide el servidor con la que se debe enviar.
	uint32_t numseq;	
	ssize_t recvsize,sentsize;		//Guarda bytes enviados y recibidos.
	extern volatile int timeout_vencido;	//Variable de los time outs.
	timeout_vencido=0;
	int numero=0;
	//Configuracion de los time outs.
	signal(SIGALRM,handle_sigalrm);
	unsigned long usec=2000000;
	settimeoutduration(usec);
	// Socket y entrada estándar no bloqueantes.
	int sockflags= fcntl(sock,F_GETFL,0);
	char *  aux [RCFTP_BUFLEN];
	fcntl(sock,F_SETFL,sockflags | O_NONBLOCK);
	int entrada_flags= fcntl(0,F_GETFL,0);
	fcntl(0,F_SETFL, entrada_flags | O_NONBLOCK);
	if(verb){
		printf("Comunicación con stop & wait\n");
	}
	int len=0;
	//Leer
	while((len=read(0,(void *) sendbuffer.buffer,RCFTP_BUFLEN))<0){};
	
	if(verb){
		printf("Leidos %d bytes\n",len);
	}
	if(len==0){
		ultimoMensaje=1;
	}
	if(verb){
		printf("Empaquetando el mensaje\n");
	}
	//EmpaquetarEnStructRCFTP
	sendbuffer.version=RCFTP_VERSION_1;
	sendbuffer.numseq=htonl(0);
	numseq=0;
	nextCliente=len+numseq;
	if(ultimoMensaje==1){
		if(verb){
			printf("Ultimo mensaje\n");
			printf("NUMSEQ %d\n",nextCliente);
		}
		sendbuffer.flags=F_FIN;
	}
	else if(ultimoMensaje==0){ sendbuffer.flags=F_NOFLAGS;}
	sendbuffer.len=htons(len);
	sendbuffer.sum=htons(xsum((char*)&sendbuffer,sizeof(sendbuffer)));
	if(verb){
		printf("Mensaje empaquetado\n");
	}
	//ENVIAR//////////////////////////////////////////////////////////////////////////////////
	while(ultimoMensajeConfirmado==0){
		printf("timeoutsvencidos: %d\n",timeout_vencido);
		respuestaEsperada=0;
		//Enviar mensaje
		
		salirDelBucle=0;
		while(salirDelBucle==0){
			numseq=ntohl(sendbuffer.numseq);
			//Enviar
			if(verb){
				printf("Enviando\n");
			}
			
			if((sentsize=sendto(sock,(char *)&sendbuffer,sizeof(sendbuffer),0,(struct sockaddr *)(servinfo->ai_addr),(servinfo->ai_addrlen))) != (sizeof(sendbuffer))){
				if(sentsize !=-1){
					fprintf(stderr,"Error: enviados %d bytes de un mensaje de %d bytes\n",(int)sentsize,(int)sizeof(sendbuffer));	
				}
				else{
					perror("Error en sendto");
				}
				exit(1);
			}
			else{salirDelBucle=1;}
		}
		nextCliente=ntohl(sendbuffer.numseq);
		nextCliente=nextCliente+ntohs(sendbuffer.len);
		//Activar alarma
		print_rcftp_msg(&sendbuffer,sentsize);			
		
		addtimeout();
		
		salirDelBucle=0;
		//Recibir mensaje
		if(verb){
			printf("Recibir confirmación\n");
		}
		/* Bucle para comprobar errores y perdidas de mensajes */ //Recibir Respuestaaaaaa//
		while(salirDelBucle==0){
			//Si vence el time out se reenvía.
			if(timeout_vencido>0){
				if(verb){
					printf("Mensaje perdido, ha saltado time out.\n");
				}
				respuestaEsperada=0;
				timeout_vencido=0;
				salirDelBucle=1;
			}
			else{
				if((recvsize = recvfrom(sock,(char *)&recvbuffer,sizeof(recvbuffer),0,(struct sockaddr *) (servinfo->ai_addr),(socklen_t *) (&servinfo->ai_addrlen)))==-1){
				}
				else{	//Se ha recibido respuesta desactivar timeout
					print_rcftp_msg(&recvbuffer,recvsize);	
					printf("MENSAJE RECIBIDO %d\n",numero);
					numero++;
					timeout_vencido=0;
					/*Comprobar que la respuesta es correcta */
					numseq=ntohl(sendbuffer.numseq);
					
					nextServidor=ntohl(recvbuffer.next);
					//Si es el ultimo mensaje
					
					if(ultimoMensaje==1){
						//Comprobar cabecera 

						if((issumvalid(&recvbuffer,recvsize))&&(recvbuffer.version==RCFTP_VERSION_1)&&(nextCliente==nextServidor)){
							//En caso de que el servidor este ocupado se cierra el programa
							if((recvbuffer.flags==F_BUSY) ||(recvbuffer.flags==F_ABORT) ){
								if(verb){
									printf("El servidor esta ocupado o solicita aborto inmediato. Se va a cerrar el programa.\n");	
									exit(1);			
								}
							}
							//Si se recibe la confirmación de fin es el mensaje esperado
							if(recvbuffer.flags==F_FIN){
								if(verb){
									printf("Ultimo mensaje y recibes flag fin\n");
								}
								respuestaEsperada=0;
								salirDelBucle=1;
								canceltimeout();
								ultimoMensajeConfirmado=1;
							}
							else{
							//En el caso de no recibir la confirmación deseada se ignora.
								respuestaEsperada=0;salirDelBucle=0;
								ultimoMensajeConfirmado=0;
								if(verb){
									printf("Ultimo mensaje y no recibes flag fin\n");
								}
							}
						}
						else{
							if(verb){printf("Error en las cabeceras del mensaje1.\n");				}
							respuestaEsperada=0;salirDelBucle=0;
						}
					}
					//Sino es el ultimo mensaje 
					else if((issumvalid(&recvbuffer,recvsize))&&(recvbuffer.version==RCFTP_VERSION_1)){		
						printf("Checksum y version de protocolo correcto\n");
						/*Comprobar el next
						 *Next confirmando el envio completo del mensaje anterior.*/
						if(nextCliente==nextServidor){
							if(verb){
								printf("Confirmación de que todo el mensaje ha sido recivido correctamente\n");	
							}
							canceltimeout();
							respuestaEsperada=1;
							salirDelBucle=1;
		
							
						}
						//El servidor lanza un next erroneo, se ignora y reenvia
						else if(nextServidor<numseq){
							if(verb){printf("Next erróneo del servidor, se ignora\n");

							}	
							salirDelBucle=0;	
						}
						else if(nextServidor>nextCliente){
							if(verb){printf("Next erróneo del servidor, se ignora\n");
							}	
							salirDelBucle=0;	
						}
					/* Si el servidor lanza un next aceptando solo una parte del
					 * envio se envia lo que no se ha recibido más nueva informacion.*/
						else{
							if(verb){printf("Se acepta parte del mensaje\n");
							}
							canceltimeout();
							len=nextCliente-nextServidor;
							auxiliar=ntohs(sendbuffer.len)-len;
							printf("PartePerdida: %d, ParteRecibida %d \nnextCliente %d, nextServidor %d,  numseq %d, \n",len,auxiliar,nextCliente,nextServidor,numseq );
							memmove(sendbuffer.buffer,sendbuffer.buffer+auxiliar,len);				
							while((auxiliar2=read(0,aux,auxiliar))<0){}	
							memmove(sendbuffer.buffer+len,aux,auxiliar2);
							/* La respuesta no ha sido la esperada
							 * como el servidor ha recibido una parte
							 * del mensaje se crea uno nuevo con parte
							 * del anterior y parte nueva y se empaqueta */
							respuestaEsperada=0;
							//EmpaquetarEnStructRCFTP
							sendbuffer.version=RCFTP_VERSION_1;
							sendbuffer.numseq=htonl(nextServidor);
							sendbuffer.next=htonl(0);
							numseq=nextServidor;
						 	sendbuffer.flags=F_NOFLAGS;
							sendbuffer.len=htons(0);
							len=len+auxiliar2;
							printf("len %d\n",len);
							sendbuffer.len=htons(len);
							sendbuffer.sum=0;
							sendbuffer.sum=htons(xsum((char*)&sendbuffer,sizeof(sendbuffer)));
							salirDelBucle=1;
							
						}// Fin else next aceptado
					}// Las cabeceras estan mal
					else{
						if(verb){printf("Error en las cabeceras del mensaje2\n");
						}
						respuestaEsperada=0;salirDelBucle=0;
					}// Else no es el ultimo mensaje o las cabeceras estan mal
				}// Else si la respuesta no es -1
			}// Else de si no se ha vencido el timeOut
				
		}//Bucle  de confirmación
		//Confirmado, preparar el siguiente envío.
		if(respuestaEsperada==1){
				if(verb){
					printf("Respuesta esperada correcta\n");
				}
				if(ultimoMensaje){
					ultimoMensajeConfirmado=1;
				}
				else{
					while((len=read(0,(void *) sendbuffer.buffer,RCFTP_BUFLEN))<0){
						
					}
					if(len==0){
						ultimoMensaje=1;
					}
					//EmpaquetarEnStructRCFTP
					sendbuffer.version=RCFTP_VERSION_1;
					sendbuffer.numseq=recvbuffer.next;
					sendbuffer.next=htonl(0);
					numseq=ntohl(recvbuffer.next);
					nextCliente=len+numseq;
					printf("nextCliente : %d numseq: %d \n",nextCliente,numseq);
					if(ultimoMensaje==1){
						sendbuffer.flags=F_FIN;
						sendbuffer.len=htons(len);
						sendbuffer.sum=0;
						sendbuffer.sum=htons(xsum((char*)&sendbuffer,sizeof(sendbuffer)));
					}
					else{ 	
						sendbuffer.flags=F_NOFLAGS;
						sendbuffer.len=htons(len);
						sendbuffer.sum=0;
						sendbuffer.sum=htons(xsum((char*)&sendbuffer,sizeof(sendbuffer)));
					}
				}		
		}
	}
}
/**************************************************************************/
/*  algoritmo 3 (practica 4)  */
/**************************************************************************/
void go_back_n (int sock,char * remote,int verb,int ventana){
	ventana=ventana/512;
	int nextEsperado=0;
	int nextAenviar=0;
	int cambio=0;
	int diferencia=0;
	int laVentana=ventana;
	int auxiliar4=0;
	if(verb){
		printf("Comunicación con go back n con ventana de %d\n",ventana);
	}
	int cuidado=0;	//Indica si el next esperado pertenece al mensaje final.
	int esta=0;
	int poscursor=0;
	int poscursor2=0;
	int numseqaux=0;
	int numlen=0;
	int dif=0;
	int totaleido=0;
	int numeroEnviado=0;
	int numeroRecibido=0;
	int ultimoMensaje=0;
	int ultimoMensajeConfirmado=0;
	int auxiliar=0;
	int auxiliar3=0;
	int auxiliar2=0;
	int salirDelBucle=0;
	uint32_t recvsize;
	uint32_t sentsize;
	int enviar[ventana];			// Contiene información sobre si se tiene que enviar o no la trama i-esima.
	int ordenEnviados[ventana];		// Orden en que se han enviado las tramas.
	int len=0;
	int cursor=0;
	int fin=0;
	int cursor2=0;
	int numseq=0;
	int nextCliente=0;
	int nextServidor=0;
	extern volatile int timeout_vencido; 
	timeout_vencido=0;
// Configuracion de los time outs.
	signal(SIGALRM,handle_sigalrm);
	unsigned long usec=2000000;
	settimeoutduration(usec);
// Para el recvto	struct sockaddr_storage remotee;
// Socket y entrada estandar no bloqueantes.
	int sockflags= fcntl(sock,F_GETFL,0);
	fcntl(sock,F_SETFL,sockflags | O_NONBLOCK);
	int entrada_flags= fcntl(0,F_GETFL,0);
	fcntl(0,F_SETFL, entrada_flags | O_NONBLOCK);
// Rellenar con 0 las estructuras.
	int i=0;
	while(i<ventana){
		enviar[i]=0;
		ordenEnviados[i]=-1;
		i++;
	}
	struct rcftp_msg ventanaMensajes[ventana];	// Ventana

// Llenar por primera vez la ventana de emision.
	if(verb){
		printf("Leyendo\n");
	}
	cursor=0;
	while (cursor<ventana){
		while((len=read(0,ventanaMensajes[cursor].buffer,RCFTP_BUFLEN))<0){}	
		enviar[cursor]=1;
		totaleido=totaleido+len;
		//EmpaquetarEnStructRCFTP
		if(len==0){
			ultimoMensaje=1;
		}
		ventanaMensajes[cursor].version=RCFTP_VERSION_1;
		if(cursor==0){
			ventanaMensajes[cursor].numseq=htonl(0);
			ventanaMensajes[cursor].numseq=htonl(0);
			numseq=0;
			nextCliente=len;
			nextEsperado=len;
		}
		if(cursor>0){
			ventanaMensajes[cursor].numseq=htonl(0);
			ventanaMensajes[cursor].numseq=htonl(nextCliente);
			
			numseq=nextCliente;
			nextCliente=numseq+len;
		}
		nextAenviar=nextCliente;
		if(ultimoMensaje==1){
			if(verb){
				printf("Ultimo mensaje\n");
			}
			ventanaMensajes[cursor].flags=F_FIN;
		}
		else if(ultimoMensaje==0){ 
			ventanaMensajes[cursor].flags=F_NOFLAGS;
		}
		ventanaMensajes[cursor].next=htonl(0);
		ventanaMensajes[cursor].len=htons(0);
		ventanaMensajes[cursor].len=htons(len);
		ventanaMensajes[cursor].sum=htons(0);
		ventanaMensajes[cursor].sum=htons(xsum((char*)&ventanaMensajes[cursor],sizeof(ventanaMensajes[cursor])));
		if(verb){
			printf("Mensaje empaquetado\n");
		}
		
		
		if(len==0){
			cursor=ventana;
		}
		cursor++;
	}
		
	while (ultimoMensajeConfirmado==0){
	// Enviar mensaje
		
	// Se envía toda la ventana de forma ordenada.
		if(ordenEnviados[0]!=-1){
			auxiliar3=ordenEnviados[0];
		}
		else{
			auxiliar3=0;
		}
		i=0;

		while(i<ventana){
			
		/* Si el mensaje aun no se ha enviado o se debe volvera enviar, se envia.
		 *  ENVIAR
		 */ 
			if(enviar[auxiliar3]==1){
					
				if((sentsize=sendto(sock,(char *)&ventanaMensajes[auxiliar3],sizeof(ventanaMensajes[auxiliar3]),0,servinfo->ai_addr,servinfo->ai_addrlen)) != (sizeof(ventanaMensajes[auxiliar3]))){
					if(sentsize !=-1){
						fprintf(stderr,"Error: enviados %d bytes de un mensaje de %d bytes\n",(int)sentsize,(int)sizeof(ventanaMensajes[auxiliar3]));	
					}
					else{
						perror("Error en sendto");
					}
					exit(1);
				}
				enviar[auxiliar3]=0;
				numeroEnviado++;
				addtimeout();
				if(verb){
					printf("Enviados: %d bytes\n",sentsize);
				}
			// Averiguar en que posicion esta.
				fin=0;
				numseqaux=0;
				numlen=ntohl(ventanaMensajes[auxiliar3].numseq);
				
				esta=0;
				cursor2=0;
	
				while(fin==0){
					if(ordenEnviados[cursor2]==-1){
						esta=0;
						ordenEnviados[cursor2]=auxiliar3;
						fin=1;
					}
					else{
						numseqaux=ntohl(ventanaMensajes[ordenEnviados[cursor2]].numseq);
						if(numseqaux!=numlen){
							cursor2++;
						}else{
							
							esta=1;

							fin=1;
						}
					}
					
				}
			// Si esta esta en el cursor2.
				fin=0;
				if(esta==1){
					while(cursor2<ventana-1){
						if(ordenEnviados[cursor2+1]!=-1){
							ordenEnviados[cursor2]=ordenEnviados[cursor2+1];
						}
						if(ordenEnviados[cursor2+1]==-1){
							ordenEnviados[cursor2]=auxiliar3;
							fin=1;
							cursor2=ventana-1;
						}
						cursor2++;
						
					}
					if(fin==0){
						ordenEnviados[ventana-1]=auxiliar3;
					}
				}
				
			}
			if(auxiliar3!=ventana-1){
				auxiliar3=auxiliar3+1;
			}
			else if(auxiliar3==ventana-1){
				auxiliar3=0;
			}
			i++;
		}

	// RECIBIR RESPUESTA///////
		cursor2=0;
		//printf("Orden enviados Despues de enviar\n");
		//printf("%d%d%d%d\n",ordenEnviados[0],ordenEnviados[1],ordenEnviados[2],ordenEnviados[3]);
									
		salirDelBucle=0;
		while (salirDelBucle==0){
			

			if(timeout_vencido>0){
				auxiliar3=timeout_vencido;
				
				cursor2=0;
				while(cursor2<auxiliar3){
					enviar[ordenEnviados[cursor2]]=1;
					timeout_vencido--;
					cursor2++;
				}
				salirDelBucle=1;
			}
			else{
				if((recvsize = recvfrom(sock,(char *)&recvbuffer,sizeof(recvbuffer),0,(struct sockaddr *) (servinfo->ai_addr),(socklen_t *) (&servinfo->ai_addrlen)))==-1){}
				else
				{

					ventana=laVentana;
					nextServidor=ntohl(recvbuffer.next);
				/* En primer lugar hay que buscar en la ventana de emision cual es el mensaje
				 * que se esta comprobando. */
					cursor=0;
					auxiliar3=0;
					while(auxiliar3==0){

						auxiliar2=ntohs(ventanaMensajes[cursor].len)+ntohl(ventanaMensajes[cursor].numseq);
						//printf("%d next Esperado %d ventana \n",nextEsperado,auxiliar2);
						if(auxiliar2==nextEsperado){
							if(cuidado==1){
								if(ventanaMensajes[cursor].flags==F_FIN){
									auxiliar3=1;
								}
								else{cursor++;
								}
							}
							else{
								if(ventanaMensajes[cursor].flags!=F_FIN){
									auxiliar3=1;
								}
								else{
									cursor++;
								}
							}
						}
						else{
							cursor++;
						}
					}
					auxiliar3=0;
				// En cursor esta el mensaje del cual se espera recibir la confirmacion
					numseq=ntohl(ventanaMensajes[cursor].numseq);

					numeroRecibido++;
				// Comprobar version y checksum.

					if(!issumvalid(&recvbuffer,recvsize)) {
						if(verb){
							printf("Checksum erroneo\n");
						}
					}
					else if(recvbuffer.version!=RCFTP_VERSION_1){
						if(verb){
							printf("Version erronea\n");
						}
					}
					else if(recvbuffer.flags==F_BUSY){
						if(verb){
							printf("Servidor ocupado, terminando programa\n");
						}
						exit(0);
					}
					else if(recvbuffer.flags==F_ABORT){
						if(verb){
							printf("Señal de aborto, terminando programa\n");
						}
						exit (0);
					}
				// Si la cabecera del mensaje es correcta se comprueba el next
					else if(nextServidor>nextAenviar){
						if(verb){
							printf("Next erroneo \n");
						}
					}
					else if(nextServidor<numseq){
						if(verb){
							printf("Next erroneo \n");
						}
					}
				// Trama entera confirmada correcta
					else if(nextServidor==nextEsperado){
						if(verb){
								printf("Trama confirmada entera\n");
							}	
					// No hay que leer por que se ha terminado de leer.
						if(ultimoMensaje==1){
							// En el caso de que se este esperando el final.
							
							if(recvbuffer.flags==F_FIN){
								if(nextServidor==totaleido){
									if(verb){
										printf("Ultimo mensaje confirmado, finalizando programa\n");
									}
									ultimoMensajeConfirmado=1;
									salirDelBucle=1;
									canceltimeout();
								}
								else{
									printf("Error, se ha recibido flag final con un next erroneo");
									exit(0);
								}
							}
						
							
							
						
							else{
								
						
							// En caso de no descartar reoordenar orden enviados.
							
								salirDelBucle=1;
							// En el caso de que no se descarteel final.
								canceltimeout();
							// Recolocar enviados
								//printf("ORDEN antes de actualizar\n");
								
							// El confirmado esta en cursor.
								cursor2=0;
								fin=0;
								while(fin==0){
									if(ordenEnviados[cursor2]==cursor){
										fin=1;
									}
									else{cursor2++;
									}
								}
								fin=0;
								while(cursor2<ventana-1){
									ordenEnviados[cursor2]=ordenEnviados[cursor2+1];
									cursor2++;
								}
								ordenEnviados[ventana-1]=-1;
							// ACTUALIZAR NEXT ESPERADO
						   /*
							* El siguiente next esperado debera ser el de la trama que continua 
							* a la que se ha confirmado, es decir la que contenga numseq=nextEsperado
							*/
								if(ultimoMensaje==1){
									if(nextEsperado==totaleido){
										cuidado=1;
									}
								}
								fin=0;
								cursor2=0;
								while(fin==0){
									numseq=ntohl(ventanaMensajes[cursor2].numseq);
									if(numseq==nextEsperado){
										nextEsperado=numseq+ntohs(ventanaMensajes[cursor2].len);
										fin=1;
									}
									else{
										cursor2++;
										if(cursor2==ventana){
											cursor2=0;
										}
									}
								}
								
							}
						}
					// Hay que leer porque no se ha leido el final.
						else{
							canceltimeout();
							salirDelBucle=1;
						// Leer.
							while((len=read(0,ventanaMensajes[cursor].buffer,RCFTP_BUFLEN))<0){}
							if(len==0){
								ultimoMensaje=1;
							}
						// ACTUALIZAR TOTAL LEIDO
							totaleido=totaleido+len;
						// Recolocar enviados
							//printf("ORDEN antes de actualizar\n");
						// El confirmado esta en cursor.
							cursor2=0;
							fin=0;
							while(fin==0){
								if(ordenEnviados[cursor2]==cursor){
									fin=1;
								}
								else{cursor2++;
								}
							}
							fin=0;
							while(cursor2<ventana-1){
								ordenEnviados[cursor2]=ordenEnviados[cursor2+1];
								cursor2++;
							}
							ordenEnviados[ventana-1]=-1;
						// Actualizar mascara
							enviar[cursor]=1;
						// ACTUALIZAR NEXT ESPERADO
					   /*
						* El siguiente next esperado debera ser el de la trama que continua 
						* a la que se ha confirmado, es decir la que contenga numseq=nextEsperado
						*/
							if(ultimoMensaje==1){
								if(nextEsperado==totaleido){
									cuidado=1;
								}
							}
							fin=0;
							cursor2=0;
							while(fin==0 && cursor2<ventana){
								numseq=ntohl(ventanaMensajes[cursor2].numseq);
								if(numseq==nextEsperado){
									nextEsperado=numseq+ntohs(ventanaMensajes[cursor2].len);
									fin=1;
								}
								else{cursor2++;}
							}
							if(cursor2==ventana){
								fin=0;
								cursor2=0;
								while(fin==0 && cursor2<ventana){
									numseq=ntohl(ventanaMensajes[cursor2].numseq)+ntohs(ventanaMensajes[cursor2].len);
									if(numseq==nextEsperado){
										nextEsperado=nextEsperado+len;
										fin=1;
									}
									else{cursor2++;
									}
								}
							}
							// EmpaquetarEnStructRCFTP
							ventanaMensajes[cursor].version=RCFTP_VERSION_1;
							ventanaMensajes[cursor].numseq=htonl(nextAenviar);
							ventanaMensajes[cursor].next=htonl(0);
							nextAenviar=nextAenviar+len;
							if(ultimoMensaje==1){
								ventanaMensajes[cursor].flags=F_FIN;
								ventanaMensajes[cursor].len=htons(0);
								ventanaMensajes[cursor].len=htons(len);
								ventanaMensajes[cursor].sum=0;
								ventanaMensajes[cursor].sum=htons(xsum((char*)&ventanaMensajes[cursor],sizeof(ventanaMensajes[cursor])));
								
							}
							else{ 	
								ventanaMensajes[cursor].flags=F_NOFLAGS;
								ventanaMensajes[cursor].len=htons(0);
								ventanaMensajes[cursor].len=htons(len);
								ventanaMensajes[cursor].sum=0;
								ventanaMensajes[cursor].sum=htons(xsum((char*)&ventanaMensajes[cursor],sizeof(ventanaMensajes[cursor])));
							}
						}
					}// FIN IF SE RECIBE CONFIRMACIÓN TRAMA ENTERA DEL MENSAJE MAS ANTIGUO
				// En el caso de que se reciba solo parte de la trama.
					else if(nextServidor<nextEsperado){
						if(recvbuffer.flags==F_FIN){
							printf("El servidor a enviado flag final con un next erróneo");
							exit(0);
						}
						if(verb){
							printf("Trama confirmada parcial\n");
						}				
						canceltimeout();
					// El confirmado esta en cursor.
					// Recolocar enviados
						cursor2=0;
						fin=0;
						while(fin==0){
							if(ordenEnviados[cursor2]==cursor){
								fin=1;
							}
							else{cursor2++;
							}
						}
						fin=0;
						while(cursor2<ventana-1){
							ordenEnviados[cursor2]=ordenEnviados[cursor2+1];
							cursor2++;
						}
						ordenEnviados[ventana-1]=-1;
					// Cambiar mascara
						enviar[cursor]=1;	
					// Recolocar el mensaje
						len=nextEsperado-ntohl(recvbuffer.next);
						auxiliar=ntohs(ventanaMensajes[cursor].len)-len;
						memmove(ventanaMensajes[cursor].buffer,ventanaMensajes[cursor].buffer+auxiliar,len);
						enviar[cursor]=1;
					// EmpaquetarEnStructRCFTP
						ventanaMensajes[cursor].version=RCFTP_VERSION_1;
						ventanaMensajes[cursor].numseq=recvbuffer.next;
						ventanaMensajes[cursor].next=htonl(0);		
						salirDelBucle=1;
					// El next esperado no cambia
						nextEsperado=nextEsperado;					
						ventanaMensajes[cursor].len=htons(0);
						ventanaMensajes[cursor].len=htons(len);
						ventanaMensajes[cursor].sum=0;
						ventanaMensajes[cursor].sum=htons(xsum((char*)&ventanaMensajes[cursor],sizeof(ventanaMensajes[cursor])));
						
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////					
					}// FIN ELSE SE HA RECIBIDO LA CONFIRMACION DE UNA PARTE DE LA TRAMA DEL MENSAJE MAS ANTIGUO
					else{
						if(nextServidor!=nextAenviar){
							/* 
							En el caso de que se reciba una confirmacion que no sea de la ventana mas antigua, se borraran de la ventana de 
							emision todas aquellas ventanas inferiores a la recibida.
							 */
							//Primero comprobar en donde esta.
							cursor2=0;
							auxiliar3=0;
							while(auxiliar3==0){

								auxiliar2=ntohs(ventanaMensajes[cursor2].len)+ntohl(ventanaMensajes[cursor2].numseq);
								auxiliar4=ntohl(ventanaMensajes[cursor2].numseq);
								if(nextServidor>=auxiliar4 && nextServidor<auxiliar2){
									if(cuidado==1){
										if(ventanaMensajes[cursor2].flags==F_FIN){
											auxiliar3=1;
										}
										else{cursor2++;
										}
									}
									else{
										if(ventanaMensajes[cursor2].flags!=F_FIN){
											auxiliar3=1;
										}
										else{
											cursor2++;
										}
									}
								}
								else{cursor2++;}
							}
							// En cursor2 esta el mensaje mas antiguo y en cursor el que se ha recibido.
							cambio=cursor;
							cursor=cursor2;
							cursor2=cambio;
							diferencia=0;
							auxiliar2=0;
							while(auxiliar2<ventana){
								if(ordenEnviados[auxiliar2]>=0){
									if(ntohl(ventanaMensajes[ordenEnviados[auxiliar2]].numseq)<nextServidor){
										diferencia=diferencia+1;
									}
									
								}
								auxiliar2++;
							}
							auxiliar2=0;
							
							//Cancelar time outs
							while(auxiliar2<diferencia){
								canceltimeout();
								auxiliar2++;
							}
							auxiliar4=ntohl(ventanaMensajes[cursor].numseq);
							auxiliar2=ntohs(ventanaMensajes[cursor].len)+ntohl(ventanaMensajes[cursor].numseq);
							
							if(ultimoMensaje==1){
							
								// No hay que leer mas
								//Actualizar Orden enviados.

								if(nextServidor>auxiliar4){
									// Trama parcial.
									canceltimeout();
									// Recolocar enviados
									if(ordenEnviados[0]==cursor){
										auxiliar3=0;
										fin=0;
										while(fin==0){
											if(ordenEnviados[auxiliar3]==cursor2){
												fin=1;
											}
											else{auxiliar3++;
											}
										}
										while(auxiliar3<ventana){
											ordenEnviados[auxiliar3]=-1;
											auxiliar3++;
										}
										auxiliar3=0;
										while(auxiliar3<ventana-1){
											ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
										}
										ordenEnviados[auxiliar3]=-1;
									}
									else if(ordenEnviados[0]!=cursor){
										auxiliar3=0;
										fin=0;
										poscursor=0;
										poscursor2=0;
										while(auxiliar3<ventana){
											if(ordenEnviados[auxiliar3]==cursor){
												poscursor=auxiliar3;
											}
											if(ordenEnviados[auxiliar3]==cursor2){
												poscursor2=auxiliar3;
											}
											auxiliar3++;
										}
										if(poscursor<poscursor2){
											auxiliar3=poscursor2;
											while(auxiliar3<ventana){
												ordenEnviados[auxiliar3]=-1;
												auxiliar3++;
											}
											auxiliar3=0;
											while(auxiliar3<=poscursor){
												ordenEnviados[auxiliar3]=-1;
												auxiliar3++;
											}
											auxiliar3=0;
											fin=0;
											while(fin==0){
												if(ordenEnviados[0]!=-1){
													fin=1;
												}
												else{
													auxiliar3=0;
													while(auxiliar3<ventana-1){
														ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
														auxiliar3++;
													}
													ordenEnviados[ventana-1]=-1;
												}
											}
												
										}
										else{
											auxiliar3=poscursor2;
											while(auxiliar3<=poscursor){
												ordenEnviados[auxiliar3]=-1;
												auxiliar3++;
											}
											auxiliar3=poscursor2;
											fin=0;
											if(poscursor!=ventana-1){

												auxiliar2=ordenEnviados[poscursor+1];
												while(fin==0){
													auxiliar3=poscursor2;
													if(ordenEnviados[auxiliar3]==auxiliar2){
														fin=1;
													}
													else{
														
														while(auxiliar3<ventana-1){
															ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
															auxiliar3++;
														}
														ordenEnviados[ventana-1]=-1;
													}
												}
											}
										}
										
									}
									// ACTUALIZAR NEXT ESPERADO
										
										if(nextEsperado==totaleido){
											cuidado=1;
										}
										nextEsperado=ntohl(ventanaMensajes[cursor].numseq)+ntohs(ventanaMensajes[cursor].len);
										
									//MASCARA
										enviar[cursor]=1;
										salirDelBucle=1;
										//ACTUALIZAR MENSAJE
										dif=ntohs(ventanaMensajes[cursor].len)+ntohl(ventanaMensajes[cursor].numseq);
										len=dif-nextServidor;
										auxiliar3=nextServidor-ntohl(ventanaMensajes[cursor].numseq);
										memmove(ventanaMensajes[cursor].buffer,ventanaMensajes[cursor].buffer+auxiliar3,len);
										ventanaMensajes[cursor].numseq=htonl(nextServidor);
										ventanaMensajes[cursor].sum=htons(xsum((char*)&ventanaMensajes[cursor],sizeof(ventanaMensajes[auxiliar3])));
								}
								else if(nextServidor==auxiliar4){
									// Trama entera.
									if(verb){
										printf("Trama confirmada entera\n");
									}	
								// En el caso de que se reciba el final.
										if(recvbuffer.flags==F_FIN){
											if((nextServidor==totaleido)){
												if(verb){
													printf("Ultimo mensaje confirmado, finalizando programa\n");
												}
												ultimoMensajeConfirmado=1;
												salirDelBucle=1;
											}
											else{
												//Se ignora
											}
										}
										
									// En el caso de que no se reciba final.
										else{

										// Recolocar enviados
											if(ordenEnviados[0]==cursor){
												auxiliar3=0;
												fin=0;
												while(fin==0){
													if(ordenEnviados[auxiliar3]==cursor2){
														fin=1;
													}
													else{auxiliar3++;
													}
												}
												while(auxiliar3<ventana){
													ordenEnviados[auxiliar3]=-1;
													auxiliar3++;
												}
											}
											else{
												auxiliar3=0;
												fin=0;
												poscursor=0;
												poscursor2=0;
												while(auxiliar3<ventana){
													if(ordenEnviados[auxiliar3]==cursor){
														poscursor=auxiliar3;
													}
													if(ordenEnviados[auxiliar3]==cursor2){
														poscursor2=auxiliar3;
													}
													auxiliar3++;
												}
												if(poscursor<poscursor2){
													auxiliar3=poscursor2;
													while(auxiliar3<ventana){
														ordenEnviados[auxiliar3]=-1;
														auxiliar3++;
													}
													auxiliar3=0;
													while(auxiliar3<poscursor){
														ordenEnviados[auxiliar3]=-1;
														auxiliar3++;
													}
													auxiliar3=0;
													fin=0;
													while(fin==0){
														if(ordenEnviados[0]==cursor){
															fin=1;
														}
														else{
															auxiliar3=0;
															while(auxiliar3<ventana-1){
																ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
																auxiliar3++;
															}
															ordenEnviados[ventana-1]=-1;
														}
													}
														
												}
												else{
													auxiliar3=poscursor2;
													while(auxiliar3<poscursor){
														ordenEnviados[auxiliar3]=-1;
														auxiliar3++;
													}
													auxiliar3=poscursor2;
													fin=0;
													if(poscursor!=ventana-1){

														auxiliar2=ordenEnviados[poscursor+1];
														while(fin==0){
															auxiliar3=poscursor2;
															if(ordenEnviados[auxiliar3]==auxiliar2){
																fin=1;
															}
															else{
																
																while(auxiliar3<ventana-1){
																	ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
																	auxiliar3++;
																}
																ordenEnviados[ventana-1]=-1;
															}
														}
													}
												

												}
												
											}
										// ACTUALIZAR NEXT ESPERADO
											
											
											if(nextEsperado==totaleido){
												cuidado=1;
											}
											nextEsperado=ntohl(ventanaMensajes[cursor].numseq)+ntohs(ventanaMensajes[cursor].len);
											
										}								

								}
								else{
									
								}
							}
							// SI HAY QUE LEER.
							else{
								
								if(nextServidor>auxiliar4){
									
									salirDelBucle=1;
									// Trama parcial.
									canceltimeout();
									// Recolocar enviados
									if(ordenEnviados[0]==cursor){
										auxiliar3=0;
										fin=0;
										
										while(fin==0){
											if(ordenEnviados[auxiliar3]==cursor2){
												fin=1;
											}
											else{auxiliar3++;
											}
										}
										
										while(auxiliar3<ventana){
											ordenEnviados[auxiliar3]=-1;
											auxiliar3++;
										}
										
										auxiliar3=0;
										while(auxiliar3<ventana-1){
											ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
											auxiliar3++;
										}
										ordenEnviados[auxiliar3]=-1;
									}
									else{
										auxiliar3=0;
										fin=0;
										poscursor=0;
										poscursor2=0;
										
										while(auxiliar3<ventana){
											if(ordenEnviados[auxiliar3]==cursor){
												poscursor=auxiliar3;
											}
											if(ordenEnviados[auxiliar3]==cursor2){
												poscursor2=auxiliar3;
											}
											auxiliar3++;
										}
										
										if(poscursor<poscursor2){
											auxiliar3=poscursor2;
											
											while(auxiliar3<ventana){
												ordenEnviados[auxiliar3]=-1;
												auxiliar3++;
											}
											auxiliar3=0;
											
											while(auxiliar3<=poscursor){
												ordenEnviados[auxiliar3]=-1;
												auxiliar3++;
											}
											
											auxiliar3=0;
											fin=0;
											while(fin==0){
												if(ordenEnviados[0]!=-1){
													fin=1;
												}
												else{
													auxiliar3=0;
													while(auxiliar3<ventana-1){
														ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
														auxiliar3++;
													}
													ordenEnviados[ventana-1]=-1;
												}
											}
											
										}
										else{
											auxiliar3=poscursor2;
											while(auxiliar3<=poscursor){
												ordenEnviados[auxiliar3]=-1;
												auxiliar3++;
											}
											auxiliar3=poscursor2;
											fin=0;
											
											if(poscursor!=ventana-1){

												auxiliar2=ordenEnviados[poscursor+1];
												if(poscursor!=ventana-1){

													auxiliar2=ordenEnviados[poscursor+1];
													while(fin==0){
														auxiliar3=poscursor2;
														if(ordenEnviados[auxiliar3]==auxiliar2){
															fin=1;
														}
														else{
															
															while(auxiliar3<ventana-1){
																ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
																auxiliar3++;
															}
															ordenEnviados[ventana-1]=-1;
														}
													}
												}
			
											}
											
										}

									}
									
									enviar[cursor]=1;
								//ACTUALIZAR NEXTESPERADO
									enviar[cursor]=1;
									
									
									if(nextEsperado==totaleido){
										cuidado=1;
									}
									nextEsperado=ntohl(ventanaMensajes[cursor].numseq)+ntohs(ventanaMensajes[cursor].len);
									
								// LEER
									// Mirar en que lugares se puede enviar
									auxiliar3=0;
									while(auxiliar3<ventana){
										
										fin=0;
										auxiliar2=0;
										while(auxiliar2<ventana){
											if(ordenEnviados[auxiliar2]==auxiliar3){
												fin=1;
												auxiliar2=ventana;
											}
											else{
												auxiliar2++;
											}
										}
										if(auxiliar3==cursor){
											fin=1;
										}
										if(fin==0){
											
										//LEER Y METER EN auxiliar de 3
											while((len=read(0,ventanaMensajes[auxiliar3].buffer,RCFTP_BUFLEN))<0){}
											enviar[auxiliar3]=1;
											totaleido=totaleido+len;
											// EmpaquetarEnStructRCFTP
											ventanaMensajes[auxiliar3].version=RCFTP_VERSION_1;
											ventanaMensajes[auxiliar3].numseq=htonl(nextAenviar);
											ventanaMensajes[auxiliar3].next=htonl(0);		
											salirDelBucle=1;
										// El next esperado no cambia
											nextEsperado=nextEsperado;					
											ventanaMensajes[auxiliar3].len=htons(0);
											ventanaMensajes[auxiliar3].len=htons(len);
											ventanaMensajes[auxiliar3].sum=0;
											nextAenviar=nextAenviar+len;
											if(len==0){
												ventanaMensajes[auxiliar3].flags=F_FIN;
												ventanaMensajes[auxiliar3].sum=htons(xsum((char*)&ventanaMensajes[auxiliar3],sizeof(ventanaMensajes[auxiliar3])));
											}
											else{
												ventanaMensajes[auxiliar3].flags=F_NOFLAGS;
												ventanaMensajes[auxiliar3].sum=htons(xsum((char*)&ventanaMensajes[auxiliar3],sizeof(ventanaMensajes[auxiliar3])));
											}
										}
										auxiliar3++;
									}
										
										enviar[cursor]=1;
										salirDelBucle=1;
										//ACTUALIZAR MENSAJE
										dif=ntohs(ventanaMensajes[cursor].len)+ntohl(ventanaMensajes[cursor].numseq);
										len=dif-nextServidor;
										auxiliar3=nextServidor-ntohl(ventanaMensajes[cursor].numseq);
										memmove(ventanaMensajes[cursor].buffer,ventanaMensajes[cursor].buffer+auxiliar3,len);
										ventanaMensajes[cursor].numseq=htonl(nextServidor);
										ventanaMensajes[cursor].len=htons(0);
										ventanaMensajes[cursor].len=htons(len);
										ventanaMensajes[cursor].sum=htons(xsum((char*)&ventanaMensajes[cursor],sizeof(ventanaMensajes[cursor])));
								}
								else if(nextServidor==auxiliar4){
								// Trama entera.
									if(verb){
										printf("Trama confirmada entera\n");
									}	
							// En el caso de que se reciba el final.
									if(recvbuffer.flags==F_FIN){
										salirDelBucle=1;
										if((nextServidor==totaleido)){
											if(verb){
												printf("Ultimo mensaje confirmado, finalizando programa\n");
											}
											ultimoMensajeConfirmado=1;
											salirDelBucle=1;
										}
										else{
											//Se ignora
										}
									}
									
									else{
									// Recolocar enviados
									////////////////////////////////////////////////////////////MIRAR QUE ACTUALIZA MAL
										if(ordenEnviados[0]==cursor){
											auxiliar3=0;
											fin=0;
									
											while(fin==0){
												if(ordenEnviados[auxiliar3]==cursor2){
													fin=1;
												}
												else{
													auxiliar3++;
												}
											}
									
											while(auxiliar3<ventana){
												ordenEnviados[auxiliar3]=-1;
												auxiliar3++;
											}
										
										}
										else if(ordenEnviados[0]!=cursor){
											auxiliar3=0;
											fin=0;
											poscursor=0;
											poscursor2=0;
											
											while(auxiliar3<ventana){
												if(ordenEnviados[auxiliar3]==cursor){
													poscursor=auxiliar3;
												}
												if(ordenEnviados[auxiliar3]==cursor2){
													poscursor2=auxiliar3;
												}
												auxiliar3++;
											}
										
											if(poscursor<poscursor2){
												auxiliar3=poscursor2;
												while(auxiliar3<ventana){
													ordenEnviados[auxiliar3]=-1;
													auxiliar3++;
												}
										
												auxiliar3=0;
												while(auxiliar3<poscursor){
													ordenEnviados[auxiliar3]=-1;
													auxiliar3++;
												}
						
												auxiliar3=0;
												fin=0;
												while(fin==0){
													if(ordenEnviados[0]==cursor){
														fin=1;
													}
													else{
														auxiliar3=0;
														while(auxiliar3<ventana-1){
															ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
															auxiliar3++;
														}
														ordenEnviados[ventana-1]=-1;
													}
												}
													
											}
											else{
												auxiliar3=poscursor2;
												while(auxiliar3<poscursor){
													ordenEnviados[auxiliar3]=-1;
													auxiliar3++;
												}
												auxiliar3=poscursor2;
												fin=0;
												while(fin==0){
													auxiliar3=poscursor2;
													if(ordenEnviados[auxiliar3]==cursor){
														fin=1;
													}
													else{
														while(auxiliar3<ventana-1){
															ordenEnviados[auxiliar3]=ordenEnviados[auxiliar3+1];
															auxiliar3++;
														}
														ordenEnviados[ventana-1]=-1;
													}
												}
											}
											
										}
									
									// ACTUALIZAR NEXT ESPERADO
										
										
										if(nextEsperado==totaleido){
											cuidado=1;
										}
										nextEsperado=ntohl(ventanaMensajes[cursor].numseq)+ntohs(ventanaMensajes[cursor].len);
										
									// LEER
										// Mirar en que lugares se puede enviar
										auxiliar3=0;
										while(auxiliar3<ventana){
											
											fin=0;
											auxiliar2=0;
											while(auxiliar2<ventana){
												if(ordenEnviados[auxiliar2]==auxiliar3){
													fin=1;
													auxiliar2=ventana;
												}
												else{
													auxiliar2++;
												}
											}
											
											if(auxiliar3==cursor){
												
												fin=1;
											}
											if(fin==0){
												
											//LEER Y METER EN auxiliar de 3
												while((len=read(0,ventanaMensajes[auxiliar3].buffer,RCFTP_BUFLEN))<0){}
												totaleido=totaleido+len;
												enviar[auxiliar3]=1;
												// EmpaquetarEnStructRCFTP
												ventanaMensajes[auxiliar3].version=RCFTP_VERSION_1;
												ventanaMensajes[auxiliar3].numseq=htonl(nextAenviar);
												ventanaMensajes[auxiliar3].next=htonl(0);		
												salirDelBucle=1;
											// El next esperado no cambia
												nextEsperado=nextEsperado;					
												ventanaMensajes[auxiliar3].len=htons(0);
												ventanaMensajes[auxiliar3].len=htons(len);
												ventanaMensajes[auxiliar3].sum=0;
												nextAenviar=nextAenviar+len;
												if(len==0){
													ventanaMensajes[auxiliar3].flags=F_FIN;
													ventanaMensajes[auxiliar3].sum=htons(xsum((char*)&ventanaMensajes[auxiliar3],sizeof(ventanaMensajes[auxiliar3])));
													ultimoMensaje=1;
													auxiliar=ventana;
												}
												else{
													ventanaMensajes[auxiliar3].flags=F_NOFLAGS;
													ventanaMensajes[auxiliar3].sum=htons(xsum((char*)&ventanaMensajes[auxiliar3],sizeof(ventanaMensajes[auxiliar3])));
												}
											}
											auxiliar3++;
										}
										
									}
									
								}
							}// FIN HAY QUE LEER.
						}//FIN IF NEXT A ENVIAR!= NEXT SERVIDOR
						else{
							if(ultimoMensaje!=1){
								int i=0;
								while(i<ventana){
									enviar[i]=0;
									ordenEnviados[i]=-1;
									i++;
								}
								// Llenar la ventana de emision.
								if(verb){
									printf("Leyendo\n");
								}
								cursor=0;
								while (cursor<ventana){
									while((len=read(0,ventanaMensajes[cursor].buffer,RCFTP_BUFLEN))<0){}	
									enviar[cursor]=1;
									totaleido=totaleido+len;
									//EmpaquetarEnStructRCFTP
									if(len==0){
										ultimoMensaje=1;
									}
									ventanaMensajes[cursor].version=RCFTP_VERSION_1;
								
									ventanaMensajes[cursor].numseq=htonl(0);
									ventanaMensajes[cursor].numseq=htonl(nextAenviar);
								
									if(ultimoMensaje==1){
										if(verb){
											printf("Ultimo mensaje\n");
										}
										ventanaMensajes[cursor].flags=F_FIN;
									}
									else if(ultimoMensaje==0){ 
										ventanaMensajes[cursor].flags=F_NOFLAGS;
									}
									ventanaMensajes[cursor].next=htonl(0);
									ventanaMensajes[cursor].len=htons(0);
									ventanaMensajes[cursor].len=htons(len);
									ventanaMensajes[cursor].sum=htons(0);
									ventanaMensajes[cursor].sum=htons(xsum((char*)&ventanaMensajes[cursor],sizeof(ventanaMensajes[cursor])));
									if(verb){
										printf("Mensaje empaquetado\n");
									}
									
									
									if(len==0){
										cursor=ventana;
									}
									cursor++;
									nextAenviar=nextAenviar+len;
								}
							salirDelBucle=1;
							
							}
							else{
								if(recvbuffer.flags==F_FIN){
									salirDelBucle=1;
									ultimoMensajeConfirmado=1;
								}
								else{
									if(verb){
										printf("Falta flag FIN\n");
									}
								}

							}
						}
					}//FIN ELSE TRAMA CONFIRMADA PERDIDA
				}// FIN ELSE SE HA RECIBIDO UN MENSAJE DENTRO DE LA VENTANA
			}// FIN ELSE NO HA SALTADO TIME OUT
		}// FIN BUCLE CONFIRMAR RESPUESTA
	}// FIN BUCLE ULTIMO MENSAJE CONFIRMADO		
}// FIN GO BACK N					
/**
 * Programa principal: procesa argumentos, activa el socket y procesa mensajes
 */
int main(int argc,char *argv[]) {
    int	sock,alg;
    char *port,*dest;
	char verb;
	unsigned int window;

	printf("Autor: Pellicer Magallon, Luis Jesus\n"); // poner nombre
	//printf("Autor: Apellidos, Nombre\n"); // segundo autor, en el mismo formato
	
    initargs(argc,argv,&verb,&alg,&window,&dest,&port);
    /* configurar socket */
    sock=initsocket(dest,port);
	switch(alg) {
		case 1: alg_basico(sock,dest,verb); break;
		case 2: stop_wait(sock,dest,verb); break;
		case 3: go_back_n(sock,dest,verb,window); break;
		default: printf("Algoritmo desconocido\n"); break;
	}

	if (verb)
		fprintf(stderr,"Programa finalizado\n");
    return(0);
}


/**************************************************************************/
/* init -- read command line parameters */
/**************************************************************************/
void initargs(int argc, char **argv, char *verb, int* alg, unsigned int* window,char** dest, char** port) {
    char *progname = *argv;
    char usage[]="Usage: %s [-v] -a[version] [-w[bytes]] -d<address> -p<port>\n  -v\t\tVerbose output\n  -a[version]\tAlgorithm to use (1,2,3)\n  [-w[bytes]]\tSize of emision window (alg 3)\n";


	// default values
	*verb=0;
	*window=2048;
	// error values
	*alg=0;
	*dest=NULL;
    *port=NULL;

    if (argc<2) {
		fprintf(stderr,usage,progname);
		exit(1);   	
    }
    for(argc--,argv++; argc > 0; argc--,argv++) {
    	if (**argv == '-') {
    		switch (*(++*argv)) {
    		case 'v':
    			*verb=1;
    			break;

    		case 'a':
    			*alg=atoi(++*argv);
    			break;

    		case 'w':
    			*window=atoi(++*argv);
    			break;

    		case 'd':
    			*dest=(++*argv);
    			break;

    		case 'p':
    			*port=(++*argv);
    			break;

    		default:
    			fprintf(stderr,usage,progname);
    			exit(1);
    		}
    	}
    	else {
    		fprintf(stderr,usage,progname);
    		exit(1);
    	}
    }
    if (*port==NULL || *dest==NULL || *alg==0) {
		fprintf(stderr,usage,progname);
		exit(1);    	
    }
	if (*verb) {
		fprintf(stderr,"Valores de parámetros: a=%d, w=%d, d=%s, p=%s\n",*alg,*window,*dest,*port);
	}	
}

/**************************************************************************/
/*       configurar y devolver el socket a usar                           */
/**************************************************************************/
int initsocket(char *dest, char *puerto) {
	int sock, status;
	struct addrinfo server;
	memset(&server, 0, sizeof server);
	server.ai_family = AF_UNSPEC; 	
	server.ai_socktype = SOCK_DGRAM; 
	// obtiene  la información del servidor y la almacena en servinfo
	if(f_verbose){
		printf("Obteniendo la información del servidor...\n");
	}
	if((status = getaddrinfo(dest, puerto,&server,&servinfo))!=0){
		fprintf(stderr,"Error en la llamada getaddrinfo: %s\n",gai_strerror(status));
		exit(1);
	}
	// crea un extremo de la comunicación y devuelve un descriptor
	if(f_verbose)
		printf("Creando el socket...\n");
	if((sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) < 0){
		perror("Error en la llamada socket: No se pudo crear el socket");
		exit(1);
	}
	else if(f_verbose){
		printf("Socket creado\n");
	}
	return sock;
}

/**************************************************************************/
/*  algoritmo 1 (practica 3)  */
/**************************************************************************/
void alg_basico(int sock,char * remote,int verb) {
	int ultimoMensaje=0;
	int ultimoMensajeConfirmado=0;
	int respuestaEsperada=0;
	uint32_t nextCliente;
	uint32_t nextServidor;
	struct sockaddr_storage remotee;
	ssize_t recvsize,sentsize;
	if(verb){
		printf("Comunicación con algoritmo básico\n");
	}
	int len=0;
	//Leer
	len=read(0,(void *) sendbuffer.buffer,RCFTP_BUFLEN);
	if(verb){
		printf("Leidos %d bytes\n",len);
	}
	if(len==0){
		ultimoMensaje=1;
	}
	if(verb){
		printf("Empaquetando el mensaje\n");
	}
	//EmpaquetarEnStructRCFTP
	sendbuffer.version=RCFTP_VERSION_1;
	sendbuffer.numseq=htonl(0);
	if(ultimoMensaje==1){
		if(verb){
			printf("Ultimo mensaje\n");
		}
		sendbuffer.flags=F_FIN;
	}
	else if(ultimoMensaje==0){ sendbuffer.flags=F_NOFLAGS;}
	sendbuffer.len=htons(len);
	socklen_t remotelen;
	remotelen=sizeof(remotee);
	sendbuffer.sum=htons(xsum((char*)&sendbuffer,sizeof(sendbuffer)));
	if(verb){
		printf("Mensaje empaquetado\n");
	}
	while(ultimoMensajeConfirmado==0){
		respuestaEsperada=0;
		//Enviar mensaje
		if(verb){
		printf("Enviando\n");
		}
		if((sentsize=sendto(sock,(char *)&sendbuffer,sizeof(sendbuffer),0,servinfo->ai_addr,servinfo->ai_addrlen)) != (sizeof(sendbuffer))){
			if(sentsize !=-1){
				fprintf(stderr,"Error: enviados %d bytes de un mensaje de %d bytes\n",(int)sentsize,(int)sizeof(sendbuffer));	
			}
			else{
				perror("Error en sendto");
			}
			exit(1);
		}
		
		
		//Recibir mensaje
		if(verb){
		printf("Recibir confirmación\n");
		}
		recvsize= recvfrom(sock,(char*)&recvbuffer,sizeof(recvbuffer),0,(struct sockaddr *)&remote,&remotelen);
		if(verb){
			printf("Información recibida\n");
		}
		if(recvsize<0) {
			perror("Error en recvfrom: ");
			exit(2);
		}
		//Si es la respuesta esperada
		nextCliente=len+ntohl(sendbuffer.numseq);
		nextServidor=ntohl(recvbuffer.next);
		if(ultimoMensaje==1){
			if(recvbuffer.flags==F_FIN){
				if(verb){printf("Es el ultimo mensaje y recibes fin\n");}
				respuestaEsperada=1;
			}
			else{respuestaEsperada=0;
				if(verb){
					printf("Ultimo mensaje y recibes flag fin\n");}
			}
		}
		else if((issumvalid(&recvbuffer,recvsize))&&(nextCliente==nextServidor)&&(recvbuffer.version==RCFTP_VERSION_1)){
			respuestaEsperada=1;
		}
		if(respuestaEsperada==1){
				if(verb) {
					printf("RespuestaEsperada\n");
				}
				if(ultimoMensaje){
					ultimoMensajeConfirmado=1;
				}
				else{
					len=read(0,(void *) sendbuffer.buffer,RCFTP_BUFLEN);
					if(len==0){
						ultimoMensaje=1;
					}
	
					//EmpaquetarEnStructRCFTP
					sendbuffer.version=RCFTP_VERSION_1;
					sendbuffer.numseq=htonl(nextServidor);
					sendbuffer.next=htonl(0);
					if(ultimoMensaje==1){
						sendbuffer.flags=F_FIN;
						sendbuffer.len=htons(len);
						sendbuffer.sum=0;
						sendbuffer.sum=htons(xsum((char*)&sendbuffer,sizeof(sendbuffer)));
					}
					else{ 	sendbuffer.flags=F_NOFLAGS;
						sendbuffer.len=htons(len);
						sendbuffer.sum=0;
						sendbuffer.sum=htons(xsum((char*)&sendbuffer,sizeof(sendbuffer)));
					}

				}		
		}
	}
}
/**************************************************************************/
/*  imprime direccion */
/**************************************************************************/
void print_peer(struct sockaddr_storage caddr) {
	struct sockaddr_in * caddr_ipv4;
	struct sockaddr_in6 * caddr_ipv6;
	void *addr;
	char *ipver, ipstr[INET6_ADDRSTRLEN];
	unsigned short port;
	
	if (caddr.ss_family== AF_INET) {//IPv4
        caddr_ipv4=((struct sockaddr_in *)((struct sockaddr *)&caddr));
        addr = &(caddr_ipv4->sin_addr);
        port = htons(caddr_ipv4->sin_port); 
		/*convierte el entero corto sin signo hostshort desde el orden de bytes del host al de la red */
        ipver = "IPv4";
    }
    else if (caddr.ss_family== AF_INET6) {//IPv6
    	caddr_ipv6=((struct sockaddr_in6 *)((struct sockaddr *)&caddr));
        addr = &(caddr_ipv6->sin6_addr);
        port = htons(caddr_ipv6->sin6_port);
        ipver = "IPv6";
    }
    else{
	 	fprintf(stderr, "Error: protoco desconocido");
	 	exit(1);
    }
    //convierte la ip a una string y la imprime
    inet_ntop(caddr.ss_family, addr, ipstr, sizeof ipstr);
    printf("Comunicación con el equipo %s usando %s a través del puerto %d\n", ipstr,ipver,port);
    return;
}
