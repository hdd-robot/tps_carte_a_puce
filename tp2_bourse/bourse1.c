#include <avr/io.h>
#include <stdint.h>
#include <avr/eeprom.h>

//------------------------------------------------
// 
// Programme bourse
// 
//------------------------------------------------


// déclaration des fonctions d'entrée/sortie
// écrites en assembleur dans le fichier io.s
extern void sendbytet0(uint8_t b);
extern uint8_t recbytet0(void);

// variables globales en static ram
uint8_t cla, ins, p1, p2, p3;  // header de commande
uint8_t sw1, sw2;              // status word

#define MAX_PERSO 32
char ee_perso[MAX_PERSO] EEMEM;
uint8_t ee_taille_perso EEMEM;

#define size_atrstr 6
char atrstr[size_atrstr]="bourse";

// Procédure qui renvoie l'ATR
void atr()
{
	int i;
    
    sendbytet0(0x3b);	// définition du protocole
    uint8_t n = 0x60 + size_atr;
    sendbytet0(n);		// nombre d'octets d'historique
    sendbytet0(0x1b); 
    sendbytet0(0x00);
    
	for (i=0;i<size_atr;i++)		// Boucle d'envoi des octets d'historique
	{
    	sendbytet0(pgm_read_byte(atr_str+i));
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
void intro_perso()
{
    	int i;
	unsigned char data[MAX_PERSO];
     	// vérification de la taille
    	if (p3>MAX_PERSO)
	{
	   	sw1=0x6c;	// P3 incorrect
        	sw2=MAX_PERSO;	// sw2 contient l'information de la taille correcte
		return;
    	}
	sendbytet0(ins);	// acquitement

	for(i=0;i<p3;i++)	// boucle d'envoi du message
	{
	    data[i]=recbytet0();
	}
	eeprom_write_block(data,ee_perso,p3);
	eeprom_write_byte(&ee_taille_perso,p3);
	sw1=0x90;
}



void lire_perso()
{
	int i;
	uint8_t taille;
	taille=eeprom_read_byte(&ee_taille_perso);
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


uint16_t ee_solde EEMEM = 0;

void lire_solde()
{
	uint16_t s;
	if (p3!=2)
	{
		sw1=0x6c;
		sw2=2;
		return;
	}
	sendbytet0(ins);
	s=eeprom_read_word(&ee_solde);
	sendbytet0(s>>8); // big endian
	sendbytet0(s&0xff);
	sw1=0x90;
}

void credit()
{
	uint16_t c;
	uint16_t s;
	if (p3!=2)
	{
	}
	sendbytet0(ins);
	c=(uint16_t)recbytet0()<<8;
	c+=(uint16_t)recbytet0();
	s=eeprom_read_word(&ee_solde);
	s+=c;
	// contrôle de débordement
	// ...
	eeprom_write_word(&ee_solde,s);
}


// Programme principal
//--------------------
int main(void)
{
  	// initialisation des ports
  	ACSR=0x80;
  	PORTB=0xff;
  	DDRB=0xff;
  	PORTC=0xff;
  	DDRC=0xff;
  	DDRD=0x00;
  	PORTD=0xff;
  	
	// ATR
  	atr();
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
		    	switch(ins)
			{
			case 0:
				version(4,"1.00");
				break;
		  	case 1:
	        		intro_data();
	        		break;
			case 2:
				sortir_data();
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

