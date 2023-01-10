#include <io.h>
#include <inttypes.h>
#include <avr/eeprom.h>

//------------------------------------------------
// Programme "hello world" pour carte à puce
// 
//------------------------------------------------


// déclaration des fonctions d'entrée/sortie définies dans "io.c"
void sendbytet0(uint8_t b);
uint8_t recbytet0(void);

// variables globales en static ram
uint8_t cla, ins, p1, p2, p3;	// entête de commande
uint8_t sw1, sw2;		// status word

int taille;		// taille des données introduites -- est initialisé à 0 avant la boucle
#define MAXI 128	// taille maxi des données lues
uint8_t data[MAXI];	// données introduites

// Procédure qui renvoie l'ATR
void atr(uint8_t n, char* hist)
{
  sendbytet0(0x3b);	// définition du protocole
  n = 0x60 + n;
  sendbytet0(n);		// nombre d'octets d'historique
  sendbytet0(0x1b); 
  sendbytet0(0x00);

  
  while(n--)		// Boucle d'envoi des octets d'historique
    {
      sendbytet0(*hist++);
    }
}


// émission de la version
// t est la taille de la chaîne sv
void version(int t, char* sv)
{
    	int i;
    	// vérification de la taille
    	if (p3!=t)
    	{
        	sw1=0x6c;	// taille incorrecte
        	sw2=t;		// taille attendue
        	return;
    	}
	sendbytet0(ins);	// acquittement
	// émission des données
	for(i=0;i<p3;i++)
    	{
        	sendbytet0(sv[i]);
    	}
    	sw1=0x90;
}


// commande de réception de données
void intro_data()
{
    	int i;
     	// vérification de la taille
    	if (p3>MAXI)
        {
          sw1=0x6c;	// P3 incorrect
        	sw2=MAXI;	// sw2 contient l'information de la taille correcte
          return;
        }
      sendbytet0(ins);	// acquitement

      for(i=0;i<p3;i++)	// boucle d'envoi du message
        {
          data[i]=recbytet0();
        }
      taille=p3; 		// mémorisation de la taille des données lues
      sw1=0x90;
}


void sortir_data()
{
	int i;
	if (p3!=taille)
    {
      sw1=0x6c;
      sw2=taille;
      return;
    }
	sendbytet0(ins);
	for(i=0;i<p3;i++)
    {
      sendbytet0(data[i]);
    }
  sw1=0x90;
}


#define MAX_PERSO 32
uint16_t ee_taille EEMEM=0;
uint8_t ee_perso[MAX_PERSO] EEMEM;

void intro_perso()
{
	char buffer[MAX_PERSO];
	int i;
	// contrôle p3
	if (p3>MAX_PERSO)
    {
      sw1=0x6c;
      sw2=MAX_PERSO;
      return;
    }
	// acquittement
	sendbytet0(ins);
	// traitement de la commande
	for (i=0;i<p3;i++)
    {	// lecture des données
      buffer[i]=recbytet0();
    }
	// recopie en eeprom
	eeprom_write_block(buffer,ee_perso,p3);
	// écriture de la taille
	eeprom_write_word(&ee_taille,p3);
	// status word
	sw1=0x90;
}

void lire_perso()
{
	int i;
	uint8_t taille;
	taille=eeprom_read_byte(&ee_taille);
	if (p3!=taille)
    {
      sw1=0x6c;
      sw2=taille;
      return;
    }
	sendbytet0(ins);
	for (i=0;i<p3;i++)
    {
      sendbytet0(eeprom_read_byte(data+i));
    }
	sw1=0x90;
}


// Programme principal
//--------------------
int main(void)
{
  // initialisation des ports
	ACSR=0x80;
	PORTB=0xff;
	DDRB=0xff;
	DDRC=0xff;
	DDRD=0;
	PORTC=0xff;
	PORTD=0xff;
	ASSR=(1<<EXCLK)+(1<<AS2);
	//TCCR2A=0;
  //	ASSR|=1<<AS2;
	PRR=0x87;


	// ATR
  atr(11,"Hello scard");

	taille=0;
	sw2=0;		// pour éviter de le répéter dans toutes les commandes
  // boucle de traitement des commandes
  for(;;)
  	{
      // lecture de l'entête
      cla=recbytet0();
      ins=recbytet0();
      p1=recbytet0();
      p2=recbytet0();
      p3=recbytet0();
      sw2=0;
      switch (cla)
        {
        case 0x80:
		    	switch(ins){
            case 0:
              version(4, "1.00");
              break;

            case 1:
	        		intro_data();
	        		break;

            case 2:
              sortir_data();
              break;

            case 3:
               intro_perso();
               break;

          case 4:
               lire_perso();
               break;

            default:
              sw1=0x6d; // code erreur ins inconnu
        		}
			break;
      		default:
        		sw1=0x6e; // code erreur classe inconnue
		}
		sendbytet0(sw1); // envoi du status word
		sendbytet0(sw2);
  	}
  	return 0;
}

