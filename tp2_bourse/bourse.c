#include <avr/io.h>
#include <stdint.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <stdarg.h>

//---------------------
// 
// Programme bourse.c
// 
//---------------------


// déclaration des fonctions d'entrée/sortie
// écrites en assembleur dans le fichier io.s
extern void sendbytet0(uint8_t b);
extern uint8_t recbytet0(void);

// variables globales en static ram
uint8_t cla, ins, p1, p2, p3;  // header de commande
uint8_t sw1, sw2;              // status word


#define size_atr 6
const char atr_str[size_atr] PROGMEM = "bourse";



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


#define size_ver 4
const char ver_str[size_ver] PROGMEM = "1.00";

// émission de la version
// t est la taille de la chaîne sv
void version()
{
    	int i;
    	// vérification de la taille
    	if (p3!=size_ver)
    	{
        	sw1=0x6c;	// taille incorrecte
        	sw2=size_ver;	// taille attendue
        	return;
    	}
	sendbytet0(ins);	// acquittement
	// émission des données
	for(i=0;i<p3;i++)
    	{
        	sendbytet0(pgm_read_byte(ver_str+i));
    	}
    	sw1=0x90;
}

// transactions
//-------------

// nombre maximal d'opérations par transaction
#define max_ope		3
// taille maximale totale des données échangées lors d'une transaction
#define max_data	64
// définition de l'état du buffer -- plein est une valeur aléatoire
typedef enum{vide=0, plein=0x1c} state_t;
// la variable buffer de transaction mémorisée en eeprom
struct
{
	state_t state;			// etat
	uint8_t nb_ope;			// nombre d'opération dans la transaction
	uint8_t tt[max_ope];		// table des tailles des transferts
	uint8_t*p_dst[max_ope];		// table des adresses de destination des transferts
	uint8_t buffer[max_data];	// données à transférer
}
ee_trans EEMEM={vide}; // l'état doit être initialisé à "vide"

// validation d'une transaction
void valide()
{
	state_t e;		// état
	uint8_t nb_ope;		// nombre d'opérations dans la transaction
	uint8_t*p_src, *p_dst;	// pointeurs sources et destination
	uint8_t i,j;
	uint8_t tt;		// taille des données à transférer

	// lecture de l'état du buffer
	e=eeprom_read_byte((uint8_t*)&ee_trans.state);
	// s'il y a quelque chose dans le buffer, transférer les données aux destinations
	if (e==plein)	// un état non plein est interprété comme vide
	{
		// lecture du nombre d'opérations
		nb_ope=eeprom_read_byte(&ee_trans.nb_ope);
		p_src=ee_trans.buffer;
		// boucle sur le nombre d'opérations
		for (i=0;i<nb_ope;i++)
		{
			// lecture de la taille à transférer
			tt=eeprom_read_byte(&ee_trans.tt[i]);
			// lecture de la destination
			p_dst=(uint8_t*)eeprom_read_word((uint16_t*)&ee_trans.p_dst[i]);
			// transfert eeprom -> eeprom du buffer vers la destination
			for(j=0;j<tt;j++)
			{
				eeprom_write_byte(p_dst++,eeprom_read_byte(p_src++));
			}
		}
	}
	eeprom_write_byte((uint8_t*)&ee_trans.state,vide);	
}

// engagement d'une transaction
// appel de la forme engage(n1, p_src1, p_dst1, n2, p_src2, p_dst2, ... 0)
// ni : taille des données à transférer
// p_srci : adresse des données à transférer
// p_dsti : destination des données à transférer
void engage(int tt, ...)
{
	va_list args;
	uint8_t nb_ope;
	uint8_t*p_src;
	uint8_t*p_buf;

	// mettre l'état à "vide"
	eeprom_write_byte((uint8_t*)&ee_trans.state,vide);

	va_start(args,tt);
	nb_ope=0;
	p_buf=ee_trans.buffer;
	while(tt!=0)
	{
		// transférer les données dans le buffer
		p_src=va_arg(args,uint8_t*);
		eeprom_write_block(p_src,p_buf,tt);
		p_buf+=tt;
		// écriture de l'adresse de destination
		eeprom_write_word((uint16_t*)&ee_trans.p_dst[nb_ope],(uint16_t)va_arg(args,uint8_t*));
		// écriture de la taille des données
		eeprom_write_byte(&ee_trans.tt[nb_ope],tt);
		nb_ope++;
		tt=va_arg(args,int);	// taille suivante dans la liste
	}
	// écriture du nombre de transactions
	eeprom_write_byte(&ee_trans.nb_ope,nb_ope);
	va_end(args);
	// mettre l'état à "data"
	eeprom_write_byte((uint8_t*)&ee_trans.state,plein);
}

//======================================================================================

// Personnalisation
//-----------------

#define MAX_PERSO 32
uint8_t ee_taille_perso EEMEM=0;
unsigned char ee_perso[MAX_PERSO] EEMEM;

// intro perso
void intro_perso()
{
	int i;
	char perso[MAX_PERSO];
	
	if (p3>MAX_PERSO)
	{
		sw1=0x6c;
		sw2=MAX_PERSO;
		return;
	}
	sendbytet0(ins);	// acquittement
	for (i=0;i<p3;i++)
	{	// réception de la perso
		perso[i]=recbytet0();
	}
	// mémorisation de la perso en eeprom sous transaction
	engage(1,&p3,&ee_taille_perso,p3,perso,ee_perso,0);
	valide();
	sw1=0x90;
}

// lecture perso
void lire_perso()
{
	int i;
	uint8_t t;

	t=eeprom_read_byte(&ee_taille_perso);
	if (p3!=t)
	{
		sw1=0x6c;
		sw2=t;
		return;
	}
	sendbytet0(ins);
	for (i=0;i<p3;i++)
	{
		sendbytet0(eeprom_read_byte(ee_perso+i));
	}
	sw1=0x90;
}


// Gestion du solde

uint16_t ee_solde EEMEM = 0;

// lecture du solde
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
	sendbytet0(s&0xff);	// convention little endian
	sendbytet0(s>>8);
	sw1=0x90;
}

// crédit
void credit()
{
	uint16_t s;
	uint16_t c;

	if (p3!=2)
	{
		sw1=0x6c;
		sw2=2;
		return;
	}
	sendbytet0(ins);
	// lire le montant à créditer
	c=recbytet0();
	c+=(uint16_t)recbytet0()<<8;
	s=eeprom_read_word(&ee_solde);
	s+=c;
	if (s<c)
	{
		sw1=0x61;
		return;
	}
	eeprom_write_word(&ee_solde,s);
	sw1=0x90;
}

// débit
void debit()
{
	uint16_t s;
	uint16_t d;
	if (p3!=2)
	{
		sw1=0x6c;
		sw2=2;
		return;
	}
	sendbytet0(ins);
	d=recbytet0();
	d+=(uint16_t)recbytet0()<<8;
	s=eeprom_read_word(&ee_solde);
	if (d>s)
	{
		sw1=0x61;
		return;
	}
	s-=d;
	engage(2,&s,&ee_solde,0);
	valide();
	sw1=0x90;
}

// Programme principal
//--------------------
int main(void)
{
  	// initialisation des ports
  	ACSR=0x80;
	PRR=0x87;
  	PORTB=0xff;
  	DDRB=0xff;
  	PORTC=0xff;
  	DDRC=0xff;
  	DDRD=0x00;
  	PORTD=0xff;
	ASSR=(1<<EXCLK)+(1<<AS2);

  	
	
	// ATR
  	atr();
  	valide();
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
	  	case 0x81:
		    	switch(ins)
			{
			case 0:
				version();
				break;
			case 1:
				intro_perso();
				break;
			case 2:
				lire_perso();
				break;
			case 3:
				lire_solde();
				break;
			case 4:
				credit();
				break;
			case 5:
				debit();
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


