// scat Smart CArd Test
// langage de script pour test de carte à puce
// -------------------------------------------
//
//
#define READLINE

#include <stdio.h>
#include <stdlib.h>
#include <winscard.h>
#include <setjmp.h>
#include <string.h>
#include <time.h>
#include <byteswap.h>

#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif


#define VERSION "1.16"

// 17-02-2919 : fonction apdu.
// correction du bug pour des commandes avec p3==0 et sans donnée à transmettre
// inlen doit être dans ce cas réduit à 4 pour indiquer que p3 n'est pas transmis

#ifndef READLINE
#define TAB 9
#define SPACE 32
#endif

#define MSG_ERROR_SIZE 64	// taille du buffer du message d'erreur

#define INPUT_BUFFER_SIZE 500	// taille du buffer d'entrée

//#define VERBOSE

// environnement de capture des erreurs
jmp_buf env;
jmp_buf env_trav;

char *input_string;
int tk_ptr;


// messages d'erreur
static char* error_messages[]={
//    "                                        ",
	"absence de lecteur",
	"lecteur indisponible",
	"la carte a été retirée",
	"l'alimentation de la carte a été coupée, communication impossible",
	"la carte ne repond pas au reset",
	"la carte est inaccessible pour cause d'autre connexion en cours",
	"pas de carte dans le lecteur",
	"commande non réalisée",
	"gestionnaire pcsc absent",
	"lecteur inconnu",
	"protocole demandé incompatible avec celui en cours",
	"un des paramètre fourni n'est pas valide",
	""
};

// buffer pour générer un message d'erreur générique
char msgdata[MSG_ERROR_SIZE];

struct timespec __h;
long int __t;

// rend un pointeur vers un message d'erreur lors d'une opération SCard
char* scard_error_msg(unsigned int e)
{
	switch (e)
	{
	case SCARD_E_NO_READERS_AVAILABLE:
		return error_messages[0];
	case SCARD_E_READER_UNAVAILABLE:
		return error_messages[1];
	case SCARD_W_REMOVED_CARD:
		return error_messages[2];
	case SCARD_W_UNPOWERED_CARD:
		return error_messages[3];
	case SCARD_W_UNRESPONSIVE_CARD:
		return error_messages[4];
	case SCARD_E_SHARING_VIOLATION:
		return error_messages[5];
	case SCARD_E_NO_SMARTCARD:
		return error_messages[6];
	case SCARD_E_NOT_TRANSACTED:
		return error_messages[7];
	case SCARD_E_NO_SERVICE:
		return error_messages[8];
	case SCARD_E_UNKNOWN_READER:
		return error_messages[9];
	case SCARD_E_PROTO_MISMATCH:
		return error_messages[10];
	case SCARD_E_INVALID_VALUE:
		return error_messages[11];
	default:
		sprintf(msgdata,"Erreur (%08x)",e);
		return msgdata;
	}
}

void scard_fatal_error(unsigned int e)
{
	fprintf(stderr,"erreur fatale **%s**\n",scard_error_msg(e));
	longjmp(env,e);
}

// affichage du message d'erreur selon le numéro de la librairie PCSC
void scard_error(unsigned int e)
{
	fprintf(stderr,"erreur **%s**\n",scard_error_msg(e));
	longjmp(env_trav,e);
}


// erreur fatale qui impose l'arrêt du programme
void fatal_error(char*msg)
{
	fprintf(stderr,"erreur fatale **%s**\n",msg);
	longjmp(env,-1);
}

// erreur avec affichage de la position
void error(char*msg)
{
	fprintf(stderr,"%*c\nerreur **%s**\n",tk_ptr+3,'^',msg);
	longjmp(env_trav,-1);
}

// erreur sans affichage de la position -- non liée à l'interprétation du texte
void apdu_error(char*msg)
{
	fprintf(stderr,"erreur **%s**\n", msg);
	longjmp(env_trav,-1);
}

//**************************************************************

// interprétation du status word.


//****************************************************************
// Erreurs iso 7816 - 4 page 11-12
// 90 00 normal processing - command successfully executed
// 61 XX sw2 encodes the number of data bytes still available
// 		"<sw2> bytes still availables please call get response with p3=<sw2>
// 62 XX warning stae of non-volatile memory unchanged
//    00 	no information given
//    80 	part of returned data may be corrupted
//    81	end of file reached before reading N_e bytes
//    83	selected file deactivated
//    84	file control information not formatted
//    85	selected file in termination state
//    86	no input data available from a sensor on the card
//
// 62 XX warning state of non-volatile memory has changed
//    02 to 89 triggering by the card
//
// 63 00 warning state of non volatile memory is unchanged
// 63 81 file filled up by the last write
// 63 Cx a counter has been incremented and now the value is x (usually a PIN)
//
// 64 00 state of non-volatile memory is unchanged
// 64 01 immediate response required by the card
//
// 65 00 state of non-volatile memory has changed
// 65 81 memory failure (eeprom write failure)
//
// 66 00 security related issues
//
// 67 00 wrong length
//
// 68 OO CLA not supported
// 68 81 logical channel not supported
// 68 82 secure messaging not supported
//
// 69 00 command not allowed -- further information in SW2
//
// 6A 00 wrong parameters P1-P2 -- further information in sw2
// 6A 80 incorrects parameters in the command data field
// 6A 81 function not supported
// 6A 82 file or application not found
// 6A 83 record not found
// 6A 84 not enough space in the file
// ...
//
// 6B 00 wrong parameters P1-P2
// ...
//
// 6C XX wrong value of P3 -- sw2 encodes the number of available data bytes
//
// 6D 00 instruction code not supported or invalid
//
// 6E 00 class not supported
//
// 6F 00 fatal error without precise diagnostic
//**************************************************************

static char* iso_msg[]=
{
	"exécution normale",
	"%d octets de données toujours disponibles",
	"warning, mémoire non volatile inchangée",
	"warning, mémoire non volatile modifiée",
	"erreur, mémoire non volatile inchangée",
	"erreur, mémoire non volatile modifiée",
	"problème de sécurité",
	"taille incorrecte",
	"fonction CLA inconnue",
	"commande non autorisée",
	"paramètres P1 ou P2 incorrects",
	"paramètres P1 ou P2 incorrects",
	"P3 incorrect, %02x attendu",
	"INS inconnu ou non valide",
	"CLA inconnu",
	"erreur",
	"erreur non documentée par la norme",
	"status word non documenté par la norme",
	"status word invalide"
};


void sw_msg(char*sw_str,BYTE sw1, BYTE sw2)
{
	char* msg_str;
	switch(sw1)
	{
	case 0x90:
		if (sw2==0)
		{
			msg_str=iso_msg[0];
			break;
		}
		else goto xx;
	case 0x6b:
	case 0x6d:
	case 0x6e:
	case 0x67:
	case 0x6f:
		if (sw2==0) goto yy;
		else goto xx;
	case 0x61:
	case 0x62:
	case 0x63:
	case 0x64:
	case 0x65:
	case 0x66:
	case 0x68:
	case 0x69:
	case 0x6a:
	case 0x6c:
yy:
		msg_str=iso_msg[sw1-0x60];
		break;
xx:
	default:
		if ((sw1&0x90)==0x90)
		{
			msg_str=iso_msg[15];
		}
		else if ((sw1&60)==0x60)
		{
			msg_str=iso_msg[16];
		}
		else
		{
			msg_str=iso_msg[17];
		}
	}
	sprintf(sw_str,msg_str,sw2);
}

//Variables globales
//------------------

// handle qui identifie le lecteur
SCARDHANDLE hCardHandle;
// définition du protocole actif
SCARD_IO_REQUEST pioSendPci;
//
SCARDCONTEXT hContext;

// liste des lecteurs sous forme de multi-string
char pmszReaders[500];
// Nombre de lecteurs -- initialisé par "affiche_lecteurs"
int nb_lecteurs;
// table vers les noms des lecteurs
char * lecteurs[10];
// protocol actif lu par sdcardconnect
DWORD dwActiveProtocol;
// atr
unsigned char bAtr[256];
// longueur de l'atr
DWORD cByte;
// buffer d'entrée
//BYTE inbuffer[512];
DWORD inlen;
// buffer de sortie
//BYTE outbuffer[512];
DWORD outlen;

// chaîne de sortie
char output_str[80];

BYTE mem[10000];
int mem_ptr;
int mem_max;

// alias prédéfinis
int t_sw1, t_sw2, t_reply, t_sw, t_time;
BYTE *b_sw1, *b_sw2, *b_reply, *b_sw, *b_time;

//***************************************************************


int endianness;

void endian_swap(int sa, int a)
{
	int b=a+sa-1;
	unsigned char t;
	while (a<b)
	{
		t=mem[a];
		mem[a++]=mem[b];
		mem[b--]=t;
	}
}


// Manipulation d'entiers
// conversion d'un entier compris entre 0 et 15 en un caractère hexadécimal
char hexa_char(BYTE b)
{
	if (b<10) return b+'0';
	return b-10+'a';
}

// conversion d'un caractère hexadécimal en entier
int get_hexa_value(char c)
{
	if ((c>='0')&&(c<='9')) return c-'0';
	if ((c>='a')&&(c<='f')) return c-'a'+10;
	if ((c>='A')&&(c<='F')) return c-'A'+10;
	return  -1;
}

// multiplication courte avec accumulation
// calcule a * b + *carry, rend le poids faible affecte le poids fort à *carry
BYTE smula(BYTE a, BYTE b, BYTE*carry)
{
	int result;
	result=(int)a*(int)b+(int)*carry;
	*carry=result>>8;
	return result;
}

// idem avec une accumulation supplémentaire a * b + c + *carry
BYTE smulaa(BYTE a, BYTE b, BYTE c, BYTE*carry)
{
	int result;
	result=(int)a * (int)b + (int) c + (int)*carry;
	*carry=result>>8;
	return result;

}


BYTE sdiv(BYTE x, BYTE y, BYTE*carry)
{
	int t;
	t=(*carry<<8)+x;
	*carry=t%y;
	return t/y;
}

int lsmul(BYTE*r, int sa, BYTE*a, BYTE b, BYTE c)
{
	int i;
	if (sa==0)
	{
		if (c==0) return 0;
		r[0]=c;
		return 1;
	}
	for (i=0;i<sa;i++)
	{
		r[i]=smula(a[i],b,&c);
	}
	if (c!=0)
	{
		r[sa]=c;
		return sa+1;
	}
	return sa;
}

int lsdiv(BYTE*r, int sa, BYTE*a, BYTE b, BYTE*rem)
{
	BYTE carry,q;
	int i;
	if (sa==0)
	{
		*rem=0;
		return 0;
	}
	if (a[sa-1]<b)
	{
		carry=a[sa-1];
		sa--;
	}
	else carry=0;
	i=sa;
	while(i)
	{
		i--;
		q=sdiv(a[i],b,&carry);
		if (r) r[i]=q;
	}
	*rem=carry;
	if (r) return sa;
	return 0;
}


void ltoamp(char*s, int sa, BYTE*a,BYTE base)
{
	BYTE rem;
	int i,j;
	char c;
	i=0;
	if (sa)
	{
		do
		{
			sa=lsdiv(a,sa,a,base,&rem);
			s[i++]=hexa_char(rem);
		}
		while(sa);
		s[i]=0;
	}
	else
	{
		s[0]='0';
		s[1]=0;
	}
	j=0;
	while (i>j)
	{
		c=s[--i];
		s[i]=s[j];
		s[j++]=c;
	}
}


int atolmp(int*psr,BYTE*r, char*s, int base)
{
	int d;
	int sr;
	int l;
	d=get_hexa_value(*(s++));
	l=0;
	sr=0;
	while ( (d>=0)&&(d<base) )
	{
		sr=lsmul(r,sr,r,base,d);
		d=get_hexa_value(*s++);
		l++;
	}
	*psr=sr;
	return l;
}

// addition
int  lladd(unsigned char*r,int sa,unsigned char*a,int sb,unsigned char*b)
{
	unsigned char  t;  // pour les resultats intermediaires
	int   i;  // index de boucle
	unsigned char c;  // retenue locale

	if (sa<=sb)
	{ // b a plus ide chiffre que a
		c=0;
		for(i=0;i<sa;i++)
		{ // "t" <- "*a" + "*b" + retenue
			t=a[i]+c;c=((t<a[i])?1:0);
			t+=b[i];
			if (t<b[i]) ++c;
			r[i]=t;
		}
		for(;i<sb;i++)
		{
			r[i]=b[i]+c;
			c=(r[i]<c)?1:0;
		}
		if (c) { r[i]=1; return sb+1; }
		return sb;
	}
	else
	{ // a a plus de chiffres que b
		c=0;
		for(i=0;i<sb;i++)
		{
			t=a[i]+c; c=(t<a[i])?1:0;
			t+=b[i];  if (t<b[i]) ++c;
			r[i]=t;
		}
		for(;i<sa;i++)
		{
			r[i]=a[i]+c;
			c=(r[i]<c)?1:0;
		}
		if (c)
		{
			r[i]=1;
			return sa+1;
		}
		return sa;
	}
}



// Soustraction longue "r" <-- "a" taille "sa" - "b" taille "sb"
// rend la taille de "r"
// "a" est assume superieur a "b"
int llsub(unsigned char*r,int sa,unsigned char*a,int sb,unsigned char*b)
{
	int		i;	// index de boucle
	unsigned char	t;
	unsigned char	c;

	c=0;
	for(i=0;i<sb;i++)
	{
		t=a[i]-c;
		c=((t>a[i])?1:0)+((t<b[i])?1:0);
		r[i]=t-b[i];
	}
	if (i<sa)
	{
		while (1)
		{ // dernier mot ecrit si necessaire seulement
			t=a[i]-c;
			if (i<sa-1)
			{
				c=(t>a[i])?1:0;
				r[i]=t;
			}
			else  // i==sa-1
                        {
				if (t) { r[i]=t; return sa; }
				else { --i; break; }
			}
			++i;
		}
	}
	else --i;
	// ajustement de la taille -- ici, on a i==sa-1
	while ( (i>=0) && (r[i]==0) ) --i;
	return i+1;
}


int llcompare(int sa,unsigned char*a,int sb,unsigned char*b)
{
	// d'abord, comparaison des tailles
	if (sa<sb) return -1;
	if (sa>sb) return 1;
	// ensuite, si les tailles sont égales,
	// comparaison des chiffres depuis le poids fort
	while (sa!=0)  // si "sa" est nul, les deux le sont
	{ // "sa" sert d'index de boucle
		--sa;
		if (a[sa]<b[sa]) return -1; // pas en temps constant !
		if (a[sa]>b[sa]) return 1;
	}
        return 0;
}

// multiplication non signee
// affecte a "r" le produit de "a" de taille "sa" et de "b" de taille "sb"
// rend la taille du resultat
int llmul(unsigned char*r,int sa, unsigned char*a,int sb, unsigned char*b)
{
	int    i;
	int    j;
	unsigned char  m;
	unsigned char carry;
	if ( (sa==0) || (sb==0) )
	{ // si l'un des operandes est nul, le resultat l'est aussi
		return 0;
	}
	if (sb==1) return lsmul(r,sa,a,b[0],0);
	m=*b++;      // multiplicateur simple
	carry=0;
	for (i=0;i<sa;i++)
	{
		r[i]=smula(a[i],m,&carry);
	}
	r[sa]=carry;
	for (j=2;j<sb;++j)
	{
		m=*b++;
		r++;
		carry=0;
		for (i=0;i<sa;++i)
		{
			r[i]=smulaa(a[i],m,r[i],&carry);
		}
		r[sa]=carry;
	}
	// dernier tour special pour ne pas affecter inutilement le poids fort
	m=*b;
	r++;
	carry=0;
	for (i=0;i<sa;++i)
	{
		r[i]=smulaa(a[i],m,r[i],&carry);
	}
	if (carry) { r[sa]=carry; return sa+sb; } else return sa+sb-1;
}

int lland(unsigned char* r, int sa, unsigned char *a, int sb, unsigned char *b)
{
	int i;
	unsigned char* t;
	if (sa<sb)
	{
		i=sa; sa=sb; sb=i;
		t=a; a=b; b=t;
	}
	for (i=0;i<sb;i++) r[i]=a[i]&b[i];
	for (;i<sa;i++) r[i]=0;
	return sa;
}

int llor(unsigned char* r, int sa, unsigned char *a, int sb, unsigned char *b)
{
	int i;
	unsigned char* t;
	if (sa<sb)
	{
		i=sa; sa=sb; sb=i;
		t=a; a=b; b=t;
	}
	for (i=0;i<sb;i++) r[i]=a[i]|b[i];
	for (;i<sa;i++) r[i]=a[i];
	return sa;
}

int llxor(unsigned char* r, int sa, unsigned char *a, int sb, unsigned char *b)
{
	int i;
	unsigned char* t;
	if (sa<sb)
	{
		i=sa; sa=sb; sb=i;
		t=a; a=b; b=t;
	}
	for (i=0;i<sb;i++) r[i]=a[i]^b[i];
	for (;i<sa;i++) r[i]=a[i];
	return sa;
}


// TODO division et modulo


//****************************************************************

void affiche_lecteurs()
{
	char*	p;
	int	l,j;

	p=pmszReaders;
	j=0;
	while(*p!=0)
	{
		lecteurs[j]=p;
//		printf("%s\n",p); fflush(stdout);
		l=strlen(p)+1;
		p+=l;
		j++;
	}
	nb_lecteurs=j;
	// affichage des lecteurs
	fprintf(stderr,"%d lecteur-s connecté-s\n",nb_lecteurs);
	for (j=0;j<nb_lecteurs;j++)
	{
		fprintf(stderr,"%d : %s\n",j,lecteurs[j]);
	}

}

void init_lecteurs()
{
	LONG lv;
	// taille de la liste des lecteurs
	DWORD dwReaders;


	hContext=0;
	hCardHandle=0;

	lv=SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &hContext);
	if (lv!=0)
	{
		scard_fatal_error(lv);
	}
#ifdef VERBOSE
	fprintf(stderr,"establish context(%ld)\n",lv);
#endif
	// liste des lecteurs
	dwReaders=500;
	lv=SCardListReaders(hContext, NULL, pmszReaders, &dwReaders);
	if (lv!=0)
	{
		scard_fatal_error(lv);
	}
	affiche_lecteurs();
}


void fin_lecteur()
{
	LONG lv;

 	// déconnexion de la carte
	if (hCardHandle!=0)
	{
		lv = SCardDisconnect(hCardHandle,SCARD_LEAVE_CARD);
		printf("Déconnection (%ld)\n",lv);
		hCardHandle=0;
	}
	// release context
	if (hContext!=0)
	{
		lv=SCardReleaseContext(hContext);
//		printf("release context (%ld)\n",lv);
		hContext=0;
	}
}

//****************************************************************
//
//


// écrit la table d'octets t en hexa et en ascii
// l : taille de la table - maxi 16
// o = 0 : sortie, =1 : entrée
// inscrit au début '>' pour une sortie et '<' pour une entrée
void ligne(BYTE*t, int l, int o)
{
	int i;
	char*p;

	struct timespec h;
	int ms,s;

	clock_gettime(CLOCK_REALTIME_COARSE,&h);

	int _t;
	_t=(h.tv_nsec/1000000l+1000l*h.tv_sec);
	_t-=__t;
	*(int*)(b_time)=endianness?__bswap_32(_t):_t;


	ms=_t%1000;
	s=_t/1000;

	//	dh=((double)h)/1000;
	p=output_str;
	//p+=sprintf(p,"%08lf %c ",dh,o==0?'>':'<');
	p+=sprintf(p,"%3d.%03d %c",s,ms,o==0?'>':'<');

//	output_str[0]=o==0?'>':'<';
//	output_str[i]=0;
	for (i=0;i<l;i++)
	{
		p+=sprintf(p," %02x",t[i]);
	}
	p+=sprintf(p," %*c",3*(16-l)+4,' ');
	for (i=0;i<l;i++)
	{
		p+=sprintf(p,"%c",t[i]<32?'.':t[i]);
	}
//	p[l+58]=0;
}


// affichage d'un buffer en séparant par lignes de 16 octets max
void print(int len, BYTE* buff,int o)
{
	int k;
	k=len;
	while (k>16)
	{
		ligne(buff,16,o);
		fprintf(stderr,"%s\n",output_str);
		k-=16;
		buff+=16;
	}
	if (k!=0)
	{
		ligne(buff,k,o);
		fprintf(stderr,"%s\n",output_str);
	}
}

void get_atr();

// connecte le lecteur numéro i
void lecteur(int i)
{

	LONG lv;
	char msg[80];
//	char szReaderName[100];
#ifdef VERBOSE
	fprintf(stderr,"lecteur(%d)\n",i);
#endif
	// contrôle de i
	if ((i<0)||(i>=nb_lecteurs))
	{
		sprintf(msg,"numéro de lecteur hors borne [O..%d]",nb_lecteurs-1);
		error(msg);
	}
	if (hCardHandle!=0)
	{
		/*lv=SCardStatus(hCardHandle,
			szReaderName,
			NULL,
			NULL, // state
			&dwActiveProtocol,
			bAtr,
			&cByte);
//		if (lv!=0) scard_error(lv);
		if (strcmp(szReaderName,lecteurs[i])!=0)
		{*/
			lv = SCardDisconnect(hCardHandle,SCARD_LEAVE_CARD);

		/*}*/
		hCardHandle=0;

	}
	lv=SCardConnect(hContext,
			lecteurs[i],
			SCARD_SHARE_EXCLUSIVE,
			SCARD_PROTOCOL_T0|SCARD_PROTOCOL_T1,
			&hCardHandle,
			&dwActiveProtocol); // active protocol
	if (lv!=0)
	{
		scard_error(lv);
	}

#ifdef VERBOSE
	fprintf(stderr,"protocole actif : ");
	switch(dwActiveProtocol)
	{
	case SCARD_PROTOCOL_T0:
		fprintf(stderr,"T=0\n");
		break;
	case SCARD_PROTOCOL_T1:
		fprintf(stderr,"T=1\n");
		break;
	case SCARD_PROTOCOL_UNDEFINED:
		fprintf(stderr,"indéfini\n");
		break;
	}
#endif
	get_atr();
}


/* Combinaisons possibles de l'état rendu par SCardStatus

#define SCARD_UNKNOWN                   0x0001 // *< Unknown state
#define SCARD_ABSENT                    0x0002 // *< Card is absent
#define SCARD_PRESENT                   0x0004  // < Card is present
#define SCARD_SWALLOWED                 0x0008  // *< Card not powered
#define SCARD_POWERED                   0x0010  // *< Card is powered
#define SCARD_NEGOTIABLE                0x0020  // *< Ready for PTS
#define SCARD_SPECIFIC                  0x0040  // *< PTS has been set/
*/
//	char pmszReaders[500];
//	DWORD dwState;

void get_atr()
{
//	int k;
	LONG lv;
	cByte=256;
	// lecture du statut de la carte
	lv=SCardStatus(hCardHandle,
			NULL, //pmszReaders,	// nom du lecteur
			NULL,		// taille du nom du lecteur
			NULL,	// état, ou de plusieurs combinaisons
			&pioSendPci.dwProtocol,	// protocol
			bAtr,		// les octets de l'atr
			&cByte);	// longueur de l'atr
	if (lv!=0)
	{
		scard_fatal_error(lv);
	}
	// initialisation
	pioSendPci.cbPciLength=sizeof(DWORD)*2;
//	fprintf(stderr,"dwState=%lx, cByte=%ld\n", dwState, cByte);
//#ifdef VERBOSE
	// affichage de l'ATR
	print(cByte,bAtr,0);
//#endif
}

void reset()
{
	LONG lv;
//	fprintf(stderr,"reset\n");
	if (hCardHandle==0)
	{
		// si lecteur n'a pas été appelé, prendre le lecteur par défaut
		lecteur(0);
	}
	// appelle SCardReconnect
	lv=SCardReconnect(hCardHandle,
			SCARD_SHARE_EXCLUSIVE,
			SCARD_PROTOCOL_T0|SCARD_PROTOCOL_T1,
			SCARD_RESET_CARD,
			&dwActiveProtocol); // n'accepte pas NULL !
	if (lv!=0)
	{
		scard_fatal_error(lv);
	}
	clock_gettime(CLOCK_REALTIME_COARSE,&__h);
	__t=__h.tv_nsec/1000000l+1000l*__h.tv_sec;
	*(int*)(b_time)=0;
	get_atr();
}


// ferme les lecteurs et quitte le programme.
void fin()
{
	fin_lecteur();
	longjmp(env,-1);
}


void apdu()
{
	DWORD lv;
	int i;
#ifdef VERBOSE
	fprintf(stderr,"apdu\n");
#endif
	print(inlen,mem,1);
	// contrôle de la taille
	// inbuffer[4] doit être égal à inlen+5 ou inlen=5
	if ( (inlen!=5) && (mem[4]!=inlen-5) )
	{
		apdu_error("valeur de p3 non valide");
	}
	if (hCardHandle==0)
	{	// chercher un lecteur avec une carte insérée
		i=0;
		do
		{
	//	printf("test lecteur %d\n",i); fflush(stdout);
			lv=SCardConnect(hContext,
				lecteurs[i],
				SCARD_SHARE_EXCLUSIVE,
				SCARD_PROTOCOL_T0|SCARD_PROTOCOL_T1,
				&hCardHandle,
				&dwActiveProtocol); // active protocol
			if (lv==0)
			{
				break;
			}
			i++;
		}
		while (i<nb_lecteurs);
		if (i==nb_lecteurs)
		{
			scard_error(SCARD_E_NO_SMARTCARD);
		}
		pioSendPci.cbPciLength=sizeof(DWORD)*2;
		pioSendPci.dwProtocol=dwActiveProtocol;

	}

	outlen=512;
	// si p3 == 0 et aucune donnée n'est à introduire dans le lecteur,
	// p3 n'est pas transmis et inlen doit être réduit à 4 pour indiquer cela
	if ((inlen==5)&&(mem[4]==0)) inlen=4;
	lv=SCardTransmit(hCardHandle,
			&pioSendPci,
			mem,
			inlen,
			NULL,
			b_reply,
			&outlen);
	if (lv!=0)
	{
		scard_error(lv);
	}
	print(outlen,b_reply,0);
	b_sw[0]=*b_sw1=b_reply[outlen-2];
	b_sw[1]=*b_sw2=b_reply[outlen-1];
	// affichage du message concernant le status word
	//ooooo//
	sw_msg(output_str,b_sw[0],b_sw[1]);
	fprintf(stderr,"%s\n", output_str);
	fflush(stderr);

}

//**********************************************************************
// pool des chaînes
#define POOL_SIZE 5000
char pool[POOL_SIZE];
// premier emplacement libre dans la partie basse du pool, initialisé à 0
int pool_ptr;
// premier emplacement occupé dans la partie haute du pool, initialisé à POOL_SIZE
int pool_max;

void init_pool()
{
	pool_ptr=0;
	pool_max=POOL_SIZE;
}

char* new_string(char*text, int l)
{
	int i,j;
/*
	j=pool_ptr;
	if (pool_ptr+l>=POOL_SIZE) fatal_error("mémoire pleine");
	for (i=0;i<l;i++)
		pool[pool_ptr++]=text[i];
	pool[pool_ptr++]=0;	// zéro terminal dans le pool
	return pool+j;
*/
	i=pool_max-=l+1;
	if (i<=pool_ptr)
		fatal_error("mémoire pleine");
	for (j=0;j<l;j++,i++) pool[i]=text[j];
	return pool+pool_max;

}

//*********************************************************************
// la pile d'entrée
#define INPUT_STACK_SIZE 10


typedef struct
{
	FILE* file;
	char* save_input_str;
	void (*gl)();
}
inputstackentry_t;

int istack_ptr; // pointeur dans la pile d'entrée -- initialisé à 0
inputstackentry_t input_stack[INPUT_STACK_SIZE];

void (*get_line)();

char input_buffer[INPUT_BUFFER_SIZE];

// A static variable for holding the line.
#ifdef READLINE
static char *line_read = (char *)NULL;

char prompt[3]="* ";

// Read a string, and return a pointer to it.
//    Returns NULL on EOF.
char * rl_gets ()
{
	  // If the buffer has already been allocated,
	  //      return the memory to the free pool.
	if (line_read)
	{
		free (line_read);
		line_read = (char *)NULL;
	}
	// Get a line from the user.
	line_read = readline(prompt);
	// If the line has any text in it,
	//      save it on the history.
	if (line_read && *line_read)
		add_history (line_read);
        return (line_read);
}

void cp_line(char*d, char*s)
{
	char c;
	do c=*d++=*s++; while (c);
	d[-1]=10;
	*d=0;
}

#endif


// rend le prochain caractère qui n'est pas un espace
int skip_blanks()
{
	int tk;
	if (input_string==NULL)
	{
		return -1;
	}
	while ((input_string[tk_ptr]==SPACE)||(input_string[tk_ptr]==TAB)) tk_ptr++;
	if (input_string[tk_ptr]=='#')
	{
		do
			tk_ptr++;
		while ((input_string[tk_ptr]!=10)&&(input_string[tk_ptr]!=0));
		return 0;	// équivaut à une fin de ligne
	}
	tk=input_string[tk_ptr];
	if (tk==10) tk=0;
	return tk;
}

int next_token()
{
	tk_ptr++;
	return skip_blanks();
}

int token()
{
	if (input_string==NULL) return -1;
	return input_string[tk_ptr];
}

//récupération d'une ligne par la console
// c'est le get_line par défaut.
void rl_get_line()
{
//	input_string=input_buffer;
	do
	{
		input_string=rl_gets();
//		cp_line(input_string,rl_gets());
		tk_ptr=0;
	}
	while (skip_blanks()==0);
}

void pop_input();
int is_eol();

int low_case(char c)
{
	if ((c>='A')&&(c<='Z'))
		c-='A'-'a';
	return c;
}

int is_digit(char c)
{
	if ((c>='0')&&(c<='9')) return 1;
	return 0;
}



// rend true si c est un caractère autorisé pour un identifieur
int is_idchar(char c)
{
	if ((c>='a')&&(c<='z')) return 1;
	if ((c>='0')&&(c<='9')) return 1;
	if (c=='_') return 1;
	return 0;
}


// rend true si le mot sur le flux d'entrée est le mot m de longueur l
int check_word(char*m, int l)
{
	int i;
	for (i=0;i<l;i++)
	{
		if (m[i]!=low_case(input_string[tk_ptr+i]))
		{
			return 0;
		}
	}
	//printf("[%d]",input_string[tk_ptr+i]); fflush(stdout);
	if (is_idchar(low_case(input_string[tk_ptr+i]))==1)
	{
		return 0;
	}
	return 1;
}
// comme ckeck_word, mais positionne l'entrée à la fin du mot si le test est positif
int is_word(char*m,int l)
{
	int r=check_word(m,l);
	if (r) tk_ptr+=l;
	return r;
}


// récupération d'une ligne sur un fichier
void file_get_line()
{
	do
	{
		input_string=fgets(input_buffer,INPUT_BUFFER_SIZE,input_stack[istack_ptr-1].file);
		if (input_string==NULL)
		{
			pop_input();
			get_line();
			break;
		}
		//fprintf(stderr, "* %s",input_string);
		tk_ptr=0;
	}
	while (is_eol());
}

// renvoie la ligne qui suit p dans la macro
char* next_line(char*p)
{
	// aller en fin de ligne
	while (*p) p++;
	// sauter les lignes vides
	while (*p==0) p++;
	return p;
}

void macro_get_line()
{
	input_string=next_line(input_string+tk_ptr);

	tk_ptr=0;

	//fprintf(stderr,"* %s\n",input_string);
	if (check_word("end",3))
	{
		pop_input();
		get_line();
	}
}

// rend true si les deux chaînes a et b sont égales sur l caractères
int eq_str(char* a, char*b, int l)
{
	do
	{
		--l;
		if (a[l]!=b[l]) return 0;
	}
	while (l);
	return 1;
}

// la ligne courante est-elle la dernière de la macro ?
int last_line()
{
	char*p;
	p=next_line(input_string+tk_ptr);
	return eq_str(p,"end",3);
}

// save current position input, open file and initialize input
void push_input_file(char* name)
{
	FILE*f;
	if (istack_ptr>=INPUT_STACK_SIZE)
		fatal_error("débordement de la pile d'entrée");
	f=input_stack[istack_ptr].file=fopen(name,"r");
	if (f==NULL)
	{
		sprintf(msgdata,"le fichier \"%s\" semble ne pas exister",name);
		fprintf(stderr,"**  %s\n", msgdata);
		return;
	}
	// sauvegarde du buffer d'entrée
	input_stack[istack_ptr].save_input_str=input_string;
	// sauvegarde de la métode de lecture
	input_stack[istack_ptr].gl=get_line;
	// nouveau buffer d'entrée
	input_string=NULL;
	// nouvelle méthode de lecture
	get_line=file_get_line;
	// empilement
	istack_ptr++;
}


void push_input_macro(char*base)
{
	if (last_line())
	{
		if (input_stack[istack_ptr-1].file==NULL)
		{
			// récursion terminale -- ne pas empiler l'adresse de retour
			input_string=base;
			tk_ptr=0;
			return;
		}
	}
	if (istack_ptr>=INPUT_STACK_SIZE)
	{
		fatal_error("débordement de la pile d'entrée");
	}
	input_stack[istack_ptr].file=NULL;
	input_stack[istack_ptr].save_input_str=input_string;
	input_stack[istack_ptr].gl=get_line;
	input_string=base;//+strlen(base); // pour l'instant, on ignore la première ligne de paramètres
	get_line=macro_get_line;
	tk_ptr=0;
	istack_ptr++;
}

// dépile l'entrée
void pop_input()
{
	if (istack_ptr==0)
	{
		fatal_error("interne : pile d'entrée vide");
	}
	istack_ptr--;
	input_string=input_stack[istack_ptr].save_input_str;
	if (input_stack[istack_ptr].file !=NULL) fclose(input_stack[istack_ptr].file);
	// restauration de la méthode de lecture
	get_line=input_stack[istack_ptr].gl;
}



//**********************************************************************
// La pile

typedef struct
{
	int base;
	int len;
} sentry_t;

// pile montante
#define STACK_SIZE 100
sentry_t stack[STACK_SIZE];
int stack_ptr; // premier emplacement libre, initialisé à 0

// vide la pile
void clear_stack()
{
	stack_ptr=0;
	mem_ptr=0;
}

void check_stack()
{
	if (stack_ptr==STACK_SIZE)
	{
		fatal_error("pile pleine");
	}
}

//**********************
//
// La table de hachage
//
//**********************
typedef struct
{
	int link;	// chaînage des éléments de la table en cas de collistion
	char* text;	// symble
	unsigned char* base;	// index de base dans la mémoire
	int len;	// len >0 pour un alias
			// len = 0 pour une macro
} hentry_t;

// taille de la table de hachage = nombre maximum de symboles
#define H_SIZE 500
// un nombre premier environ égal à 80% de h_size
#define H_PRIME 397

// variables pour la h_table
// la h_table elle même
hentry_t h_table[H_SIZE];
// dernière entrée de la h_table utilisée. initialisé à H_SISE
int h_used;


void init_htable()
{
	int i;
	// initialisation de H_SIZE
	h_used=H_SIZE;
	// initialisation des champs de la table à zéro
	for (i=0;i<H_SIZE;i++)
	{
		h_table[i].link=0;
		h_table[i].text=NULL;
		h_table[i].base=NULL;
		h_table[i].len=0;
	}
}


// vérifie si la chaîne a de longueur l correspond à la chaîne b à zéro terminal
int match_string(char*a, char*b, int l)
{
	int i;
	for (i=0;i<l;i++)
	{
		if (a[i]!=b[i]) return 0;
	}
	if (b[i]!=0) return 0; 	// test le zéro terminal de b
	return 1;
}

// rend l'index dans la table du symbole dont le nom commence à "name" de l caractères
int look_up(char* text, int l)
{
	int i;
	int key; 	// clé de hachage

	key=0;
	// calcul de la clé de hachage
	for (i=0;i<l;i++)
	{
		key+=key+text[i];
		key%=H_PRIME;
	}
	// recherche du symbole qui a le text
	for (;;)
	{
		if (h_table[key].text==NULL)
		{
			// aucun symbole avec cette clé n'a ce texte
			// créer un nouveau symbole
		found:
			h_table[key].text=new_string(text,l);
			return key;
		}
		else
		{
			// il existe un symbole avec cette clé
			if (match_string(text,h_table[key].text,l))
			{
				return key;
			}

		}
		// ici, il y a une entrée sur key qui existe, mais qui ne correpond pas au texte
		if (h_table[key].link==0)
		{
			//rechercher une entrée libre à lier ici
			do
			{
				h_used--;
				if (h_used==0) fatal_error("trop de symboles");
			}
			while (h_table[h_used].text!=NULL);
			h_table[key].link=h_used;
			key=h_used;
			goto found;
		}
		// rechercher le symbole suivant dans la chaîne
		key=h_table[key].link;
	}
}


int new_alias(char*text,int l, int len)
{
	int t = look_up(text,l);
	if (h_table[t].base!=NULL)
	{
		error("symbole déjà défini");
	}
	if (mem_max-len<mem_ptr)
	{
		error("pas de place");
	}
	mem_max-=len;
	h_table[t].base=mem+mem_max;
	h_table[t].len=len;
	return t;
}

int new_macro(char*text, int l)
{
	int t=look_up(text,l);
	if (h_table[t].base!=NULL)
	{
		error("symbole déjà défini");
	}
	h_table[t].base=(unsigned char*)pool+pool_ptr;
	h_table[t].len=0;
	return t;
}

void check_mem(int l)
{
	if (mem_ptr+l>mem_max)
	{
		error("mémoire pleine");
	}
}

void push_in(BYTE b)
{
	check_mem(1);
	mem[mem_ptr++]=b;
}


//***********************************************************************


int is_eol()
{
	switch (skip_blanks())
	{
	case '#':	// le début d'un commentaire signifie une fin de ligne
	case 10:	// caractère de fin de ligne
	case 0:		// caractère de fin de fichier
		return 1;
	}
	return 0;
}

void check_eol()
{
	char error_msg[30];
	if (is_eol()==0)
	{
		//fprintf(stderr,"%*c\n",tk_ptr+3,'^');
		sprintf(error_msg,"fin de ligne attendue (%d)",token());
		error(error_msg);
	}
}

int curr_char()
{
	if (input_string==NULL)
	{
		return -1;
	}
	return input_string[tk_ptr];
}


int is_char(char c)
{
	if (c==10)
	{
		c=0;
	}
	return c==curr_char();
}

void unexpected_char(char c)
{
	char msg[30];
	if ((c==10)||(c==0)) error("fin de ligne inattendue");
	else if (c<32)
	{
		sprintf(msg,"caractère '\\%d' inattendu",c);
		error(msg);
	}
	else
	{
		sprintf(msg,"caractère '%c' inattendu",c);
		error(msg);
	}
}


void check_char(char c)
{
	char msg[50];
	if (skip_blanks()!=c)
	{
		//fprintf(stderr,"%*c\n",tk_ptr+3,'^');
		sprintf(msg,"caractère '%c' attendu",c);
		error(msg);
	}
	tk_ptr++;
}


//f (inlen>=261)
//error("trop de données d'entrée");
//nbuffer[inlen++]=b;


void push_bytes(BYTE*t,int l)
{
	int i;

	check_mem(l);
	for (i=0;i<l;i++)
	{
		mem[mem_ptr++]=*t++;
	}
}


void check_noteol()
{

	//printf("noteol");
	switch(input_string[tk_ptr])
	{
	case 0:
	case 10:
		error("fin de ligne inattendue");
	}
}

int get_string()
{
	int l;
	tk_ptr++;
	l=0; //tk_ptr;
	char c;
	stack[stack_ptr].base=mem_ptr;
	// recherche le \" final
	while (is_char('\"')==0)
	{
		check_noteol();
		if (is_char('\\'))
		{	// traite les codes d'échappement
			tk_ptr++;
			check_noteol();
			switch (input_string[tk_ptr])
			{
			case 'a':
				c=7;
				break;
			case 'n':
				c=10;
				break;
			case 'e':
				c=27;
				break;
			case 'f':
				c=12;
				break;
			case 'b':
				c=9;
				break;
			case 't':
				c=8;
				break;
			case 'r':
				c=13;
				break;
			case 'v':
				c=11;
				break;
			default:
				c=input_string[tk_ptr];
			}
		}
		else c=input_string[tk_ptr];
		push_in(c);
		l++;
		tk_ptr++;
	}
	tk_ptr++;
	stack[stack_ptr++].len=l;
	return l;
}

static int push_byte()
{
	int b,c;
	char x;

	skip_blanks();
	b=get_hexa_value(x=input_string[tk_ptr]);
	if (b<0)
	{
		unexpected_char(x);
	}
	tk_ptr++;
	c=get_hexa_value(input_string[tk_ptr]);
	if (c>=0)
	{
		b=b*16+c;
		tk_ptr++;
	}
	stack[stack_ptr].base=mem_ptr;
	stack[stack_ptr++].len=1;
	push_in(b);
	return 1;
}


int get_id(char** pps)
{
	int l=tk_ptr;
	char c;
	if (!(is_idchar(c=input_string[tk_ptr])))
	{
		unexpected_char(c);
	}
	tk_ptr++;
	while (is_idchar(input_string[tk_ptr])) tk_ptr++;
	*pps=input_string+l;
	return tk_ptr-l;
}

void not_alias(int t)
{
	char error_msg[80];
	sprintf(error_msg,"le symbole \"%s\" n'est pas un alias",h_table[t].text);
error(error_msg);
}

void not_macro(int t)
{
	char error_msg[80];
	sprintf(error_msg,"le symbole \"%s\" n'est pas une macro", h_table[t].text);
	error(error_msg);
}

void undef_symb(int t)
{
	char error_msg[80];
	sprintf(error_msg,"symbole \"%s\" non défini", h_table[t].text);
	error(error_msg);
}


int push_alias()
{
	//char error_msg[80];
	tk_ptr++;
	BYTE*p;
	char*n;
	int l;
	l=get_id(&n);
	int t=look_up(n,l);
	p=h_table[t].base;
	if (p==NULL)
	{
		undef_symb(t);
//		sprintf(error_msg,"alias %s non défini",h_table[t].text);
//		error(error_msg);
	}
	if (h_table[t].len<=0)
	{
		not_alias(t);
	}

	l=h_table[t].len;
	stack[stack_ptr].base=mem_ptr;
	stack[stack_ptr++].len=l;
	push_bytes(p,l);
	return l;
}

int push_int(int base)
{
	int d;
	int s;
	int l;
	int i,j;
	char c;
	BYTE t;
	BYTE v[500];
	tk_ptr++;
	d=get_hexa_value(c=input_string[tk_ptr]);
	if ((d>=base)||(d<0))
	{
		unexpected_char(c); //error("caractère inattendu");
	}
		l=atolmp(&s,v,input_string+tk_ptr,base);
	if (s==0)
	{
		v[0]=0;
		s=1;
	}
	tk_ptr+=l;
	if (endianness)
	{
		i=0;
		j=s;
		while (i<j)
		{
			t=v[i];
			v[i++]=v[--j];
			v[j]=t;
		}

	}
	stack[stack_ptr].base=mem_ptr;
	stack[stack_ptr++].len=s;
	push_bytes(v,s);
	return s;

}

// empile un entier selon son format
int push_format()
{
	tk_ptr++;
	if (is_char('d')) return push_int(10);
	else if (is_char('x')) return push_int(16);
	else error("spécification de format inconnue");
	return 0;
}

int push_data()
{
	switch(skip_blanks())
	{
	case '"':
		return get_string();
	case '$':
		return push_alias();
	case '%':
		return push_format();
	default:
		return push_byte();
	}
}

// analyse syntaxique des expressions
// ---------------------------------
void push_expr();
void push_terme();

void push_factor()
{
	int l;
	int i;
	int b;
	//char c;
	switch (skip_blanks())
	{
	case '(':
		tk_ptr++;
		push_expr();
		check_char(')');
		return;
	}
	if (is_word("sizeof",6))
	{
		check_char('(');
		push_expr();
		check_char(')');

		l=stack[stack_ptr-1].len;
xx:
		if (l<256)
		{
			mem_ptr=stack[stack_ptr-1].base+1;
			mem[mem_ptr-1]=l;
			stack[stack_ptr-1].len=1;
		}
		else // l<65536
		{
			mem_ptr=stack[stack_ptr-1].base+2;
			if (endianness)
			{
				mem[mem_ptr-2]=(l>>8);
				mem[mem_ptr-1]=l;
			}
			else
			{
				mem[mem_ptr-1]=(l>>8);
				mem[mem_ptr-2]=l;
			}
			stack[stack_ptr-1].len=2;
		}

		return;
	}
	if (is_word("length",6))
	{
		check_char('(');
		push_expr();
		check_char(')');
		i=stack[stack_ptr-1].len;
		b=stack[stack_ptr-1].base;
		l=0;
		while ((l<i)&&(mem[b+l]!=0)) l++;
		goto xx;
	}
	//default:
	push_data();
//	}
}

// utilisé pour lire des paramètres -- ne devrait jamais dépasser deux octets.
int check_param()
{
	int n;
	int l;
	int i;
	stack_ptr--;

	mem_ptr=stack[stack_ptr].base;
	n=0;
	l=stack[stack_ptr].len;
	if (endianness)
	{	// big endian
		i=0;
		while ((i<l)&&(mem[mem_ptr+i]==0)) i++;
		while (i<l)
		{
			// TODO, utiliser check_mem
			n=(n*256)+mem[mem_ptr+i];
			if (n>10000) error("débordement");
			i++;
		}
	}
	else
	{	// little endian
		i=l-1;
		while ((i>=0)&&(mem[mem_ptr+i]==0)) i--;
		while (i>=0)
		{
			// TODO utiliser check_mem
			n=(n*256)+mem[mem_ptr+i];
			if (n>10000) error("débordement");
			i--;
		}
	}
//fprintf(stderr,"param=%d\n",n);
	return n;
}

int get_param()
{
	skip_blanks();
	push_factor();
	return check_param();
}

int get_param_expr()
{
	skip_blanks();
	push_expr();
	return check_param();
}

void duplicate()
{
	int n;
	int l;
	int i;
	int b;
	n=get_param();
	if (n==0)
	{
		stack[stack_ptr-1].len=1;
		b=stack[stack_ptr-1].base;
		mem[b]=0;
		mem_ptr=b+1;
		return;
	}
	l=stack[stack_ptr-1].len;
	stack[stack_ptr-1].len=n*l;
	while (--n)
	{
		for (i=0;i<l;i++)
			push_in(mem[mem_ptr-l]);
//		n--;
	}
}

void pad_left()
{
	int n,l,b;
	int i;
	n=get_param();
	l=stack[stack_ptr-1].len;
	b=stack[stack_ptr-1].base;
	if (n<=l)
	{
		mem_ptr=b+n;
	}
	else
	{
		for (i=n-l;i;i--) push_in(0);

	}
	stack[stack_ptr-1].len=n;
}

void pad_right()
{
	int n,b,l;
	int i;
	n=get_param();
	l=stack[stack_ptr-1].len;
	if (n==l) return;
	stack[stack_ptr-1].len=n;
	b=stack[stack_ptr-1].base;
	if (n<l)
	{
		for (i=0;i<l;i++)
		{
			mem[b+i]=mem[b+i+l-n];
		}
	}
	else
	{
		if (b+l>mem_max) error("mémoire pleine");
		for (i=l-1;i>=0;i--) mem[b+i+n-l]=mem[b+i];
		for (i=0;i<n-l;i++) mem[b+i]=0;
	}
	mem_ptr=b+n;

}

void shift_base()
{
	int n;
	int l;
	int i;
	int p;
	int nl;
	n=get_param_expr();
	l=stack[stack_ptr-1].len;
	p=stack[stack_ptr-1].base;
	for (i=n;i<l;i++)
	{
		mem[p+i-n]=mem[p+i];
	}
	nl=l-n;
	if (nl<=0)
	{
		nl=1;
		mem[p]=0;
	}
	mem_ptr=p+nl;
	stack[stack_ptr-1].len=nl;
}


void push_factora()
{

	push_factor();
	for (;;)
	{
		switch(skip_blanks())
		{
		case '.':
			tk_ptr++;
			duplicate();
			break;
		case ';':
			tk_ptr++;
			pad_left();
			break;
		case ':':
			tk_ptr++;
			pad_right();
			break;
		case '[':
			tk_ptr++;
			shift_base();
			break;
		default: return;
		}
	}
}

// élimine les zéros inutile en mode little_endian
int adjust(int sx, int x)
{
	while ((sx>0) && (mem[x+sx-1]==0)) sx--;
	return sx;
}

void operate(int op)
{
	int a=stack[stack_ptr-2].base;
	int sa=stack[stack_ptr-2].len;
	int b=stack[stack_ptr-1].base;
	int sb=stack[stack_ptr-1].len;
	int i;



	if (endianness)
	{
		endian_swap(sa,a);
		endian_swap(sb,b);
	}
	sa=adjust(sa,a);
	sb=adjust(sb,b);
	switch(op)
	{
	case'+':
		sa=lladd(mem+a,sa,mem+a,sb,mem+b);
		goto xx;
	case '&':
		sa=lland(mem+a,sa,mem+a,sb,mem+b);
		goto yy;
	case '|':
		sa=llor(mem+a,sa,mem+a,sb,mem+b);
		goto xx;
	case '^':
		sa=llxor(mem+a,sa,mem+a,sb,mem+b);
		goto yy;
	case '-':
		if (llcompare(sa,mem+a,sb,mem+b)<0) error("soustraction impossible");
		sa=llsub(mem+a,sa,mem+a,sb,mem+b);
	yy:
		// si la taille du résultat est nulle, rétablir une taille de 1 octet nul
		if (sa==0)
		{
			sa=1;
			mem[a]=0;
		}
	xx:
		stack_ptr--;
		stack[stack_ptr-1].len=sa;
		mem_ptr=a+sa;
		break;
	case '*':
		check_mem(sa+sb);
//		if (mem_ptr+sa+sb>10000) error("mémoire pleine");
		sa=llmul(mem+mem_ptr, sa, mem+a, sb, mem+b);
		for (i=0;i<sa;i++)
		{
			mem[a+i]=mem[mem_ptr+i];
		}
		goto yy;
	case '/':
	case '\\':
		error("opérateur non implémenté");

	}
	if (endianness)
	{
		endian_swap(sa,a);
	}
}


// vérifie si le caractère est un opérateur constitué d'un caractère c et non pas deux de suite
// par exemple & et pas &&
// en cas de réponse positive, avance le pointeur de lecture
int scan_char1(char c)
{
	if (input_string[tk_ptr]!=c) return 0;
	if (input_string[tk_ptr+1]==c) return 0;
	tk_ptr++;
	return 1;
}


// rend 1 si le caractère courant est c -- dans ce cas, incrémente le pointeur de lecture
// rend 0 sans modifier le pointeur de lecture dans le cas contraire
int scan_char(char c)
{
	if (input_string[tk_ptr]!=c) return 0;
	tk_ptr++;
	return 1;
}


// empile un terme
void push_terme()
{
	char c;
	skip_blanks();
	push_factora();
	for(;;)
	{
		c=skip_blanks();
		if (scan_char1('&') || scan_char('*') || scan_char('/') || scan_char('\\') )
		{
			push_factora();
			operate(c);
			continue;
		}
		else
		{
			return;
		}
	}
}


void push_terml()
{
	char c;
	skip_blanks();
	push_terme();
	for(;;)
	{
		c=skip_blanks();
		if (scan_char('+')||
			scan_char('-')||
			scan_char1('|')||
			scan_char1('^'))
		{
			push_terme();
			operate(c);
			continue;
		}
		else break;
	}
}


// test si le caractère c est dans la liste des caractères de fin possible
// pour une expression
// rend 0 si c n'est pas un caractère de fin
// rend 1 si c est un caractère de fin
int isinends(char c)
{
	static char ends[]=")]<>!=|*+-/\\&^\012";
	//static char ends[]=")]<>!=|*+-/\\&^%\012";
	char *p;
	// la fin de chaîne est un caractère de fin
	if (c==0)
	       return 1;
	p=ends;
	while (*p)
		if (c==*p++)
			return 1;
	return 0;
}



// Analyse d'expression, empile le résultat
// ends contient la liste des caractères attendus à la fin de l'expression
void push_sexpr() //(char* ends)
{
	skip_blanks();
	push_terml();
	while (isinends(skip_blanks())==0)
	{
		push_terml();
		// concaténer
		stack[stack_ptr-2].len+=stack[stack_ptr-1].len;
		stack_ptr--;
	}
}



// ****************************************************************


// expressions booléennes

// rend 1 et modifie le pointeur de lecture si les caractères courants sont ceux de ope
int scan_ope(char*ope)
{

	if (input_string[tk_ptr]!=ope[0])
		return 0;
/*	if (ope[1]==0)
	{
		tk_ptr++;
		return 1;
	}
*/
	if (input_string[tk_ptr+1]!=ope[1])
		return 0;
	tk_ptr+=2;
	return 1;
}


void push_expr()
{
	int ope;
	int a,sa, b,sb;
	int r;
	push_sexpr();
	if (scan_ope("==")) ope=0;
	else if (scan_ope("!=")) ope=1;
	else if (scan_ope(">=")) ope=2;
	else if (scan_ope("<=")) ope=3;
	else if (scan_char('>')) ope=4;
	else if (scan_char('<')) ope=5;
	else return;
	push_sexpr();
	// évaluation de l'opération sur les deux éléments de la pile
	sa=stack[stack_ptr-2].len;
	a=stack[stack_ptr-2].base;
	sb=stack[stack_ptr-1].len;
	b=stack[stack_ptr-1].base;
	// swap éventuel
	if (endianness)
	{
		endian_swap(sa,a);
		endian_swap(sb,b);
	}
	// ajuste la taille selon les zéros inutiles
	sa=adjust(sa,a);
	sb=adjust(sb,b);

	stack_ptr--;
	r=llcompare(sa,mem+a,sb,mem+b);
	switch(ope)
	{
	case 0:
		mem[a]=(r==0);
		break;
	case 1:
		mem[a]=(r!=0);
		break;
	case 2:
		mem[a]=(r>=0);
		break;
	case 3:
		mem[a]=(r<=0);
		break;
	case 4:
		mem[a]=(r>0);
		break;
	default:
		mem[a]=(r<0);
		break;
	}
	stack[stack_ptr-1].len=1;
	mem_ptr=a+1;
}


// interprète l'expression et rend une valeur booléenne
// qui est nulle si l'expression est nulle et non nulle dans le cas contraire
int bool_expr()
{
	int a,sa;

	push_expr();
	stack_ptr--;
	sa=stack[stack_ptr].len;
	a=stack[stack_ptr].base;
	sa=adjust(sa,a);
	mem_ptr=a;
	return sa;
}
//******************************************************************

void display_bytes()
{
	int i;
	for (i=0;i<mem_ptr;i++)
	{
		fprintf(stderr, " %02x", mem[i]);
	}
}


void display_string()
{
	int i;
	int c;
	char a[8];
//	mem[mem_ptr]=0;
	fputc('\"',stderr);
	for (i=0;i<mem_ptr;i++)
	{
		switch(c=mem[i])
		{
		case 0:
			break;
		case 7:
			fputs("\\a",stderr);
			break;
		case 8:
			fputs("\\t",stderr);
			break;
		case 9:
			fputs("\\b",stderr);
			break;
		case 10:
			fputs("\\n",stderr);
			break;
		case 11:
			fputs("\\v",stderr);
			break;
		case 12:
			fputs("\\f",stderr);
			break;
		case 13:
			fputs("\\r",stderr);
			break;
		case 27:
			fputs("\\e",stderr);
			break;
		default:
			if (c<32)
			{
				fprintf(stderr,"\\%03o",c);
			}
			else if (c<128) fputc(c,stderr);
			else
			{
				a[0]=c;
				a[1]=mem[++i];
				if (c<0xdf)
				{
					a[2]=0;
					fputs(a,stderr);
				}
				else
				{
					a[2]=mem[++i];
					if (c<0xef)
					{
						a[3]=0;
						fputs(a,stderr);
					}
					else
					{
						a[3]=mem[++i];
						a[4]=0;
						fputs(a,stderr);
					}
				}
			}
		}
	}
	fputc('\"',stderr);
}

void display_int(int base)
{
	char msg[700];   // TODO trouver un autre emplacement comme buffer d'affichage
	int i,j;
	BYTE t;
	i=mem_ptr;
	if (endianness)
	{
		j=0;
		while (j<i)
		{
			t=mem[--i];
			mem[i]=mem[j];
			mem[j++]=t;
		}
	}
	i=mem_ptr;
	while ((i>0)&&(mem[i-1]==0)) i--;
	ltoamp(msg,i,mem,base);
	fprintf(stderr,"%s",msg);
}

void say()
{

	int format;
	skip_blanks();
	format=0;
	if (is_char('-'))
	{
		tk_ptr++;
		if (is_char('d')) format=1;
		else if (is_char('x')) format=2;
		else if (is_char('s')) format=3;
		else error("format inconnu");
		tk_ptr++;
	}
	mem_ptr=0;
	push_expr();
	switch(format)
	{
	case 0:
		display_bytes();
		break;
	case 1:
		display_int(10);
		break;
	case 2:
		display_int(16);
		break;
	case 3:
		display_string();
		break;
	}
	fprintf(stderr,"\n"); fflush(stderr);
}


// déclaration d'un alias
void alias()
{
	char*n;
	skip_blanks();
	int t=get_id(&n);
	int l;
	switch (skip_blanks())
	{
	case '[':
		tk_ptr++;
		l=get_param_expr();
		if (l==0) error("un alias ne peut pas avoir une taille nulle");
		tk_ptr++;
		break;
	case 10:
	case 0:
		l=1;
		break;
	default:
		unexpected_char(skip_blanks());
	}
	check_eol();
	new_alias(n,t,l);
}

int get_alias()
{
	char error_msg[80];
	char* n;
	skip_blanks();
	int t=get_id(&n);
	t=look_up(n,t);
	unsigned char* p=h_table[t].base;
	if (p==NULL)
	{
		sprintf(error_msg,"alias %s non défini",h_table[t].text);
		error(error_msg);
	}
	if (h_table[t].len<=0)
	{
		not_alias(t);
	}
	return t;
}

// analyse une alias à gauche d'un paramètre
// alias[offset];len
// Rend l'alias et affecte l'offset et la longnueur
int alias_left(int*poffs, int*plen)
{
	int offs;
	int len;
	int t;

	t=get_alias();
	int n=len=h_table[t].len;
	if (n<=0)
	{
		not_alias(t);
	}
	offs=0;
	if (skip_blanks()=='[')
	{
		tk_ptr++;
		offs=get_param_expr();
		n=len-offs;
	}
	if (skip_blanks()==';')
	{
		tk_ptr++;
		n=get_param();
	}

	if (offs+n>len) error("débordement des paramètres");

	*poffs=offs;
	*plen=n;
	return t;
}

void random_alias()
{
	int t,offs,len;
	unsigned char*p;
	int i;

	t=alias_left(&offs,&len);
	check_eol();

	p=h_table[t].base+offs;
	for (i=0;i<len;i++) p[i]=random();
}

// assigne le sommet de pile à l'alias t
void assign(int t, int offs, int n)
{
	int i;
	unsigned char*p;
	stack_ptr--;
	int l=stack[stack_ptr].len;	// longueur de l'expression
	int b=stack[stack_ptr].base;
	p=h_table[t].base+offs;
	if (l>n) error("expression trop grande");
	if (l==n)
	{
		for (i=0;i<l;i++)
		{
			p[i]=mem[b+i];
		}
	}
	else
	{
		if (endianness)
		{
			for (i=0;i<n-l;i++) p[i]=0;
			for (;i<n;i++) p[i]=mem[b-n+l+i];
		}
		else
		{
			for (i=0;i<l;i++) p[i]=mem[b+i];
			for (;i<n;i++) p[i]=0;
		}
	}

}

void set()
{
	int offs;	// offset sur l'alias
	int t;
	int n;
	t=alias_left(&offs,&n);
	push_expr();
	check_eol();
	assign(t,offs,n);
}

void input()
{
	char c;
	int d,f;
	tk_ptr++;
	d=tk_ptr;
	c=skip_blanks();
	while ((c>32)&&(c!='#'))
	{
		tk_ptr++;
		c=token();
	}
	f=tk_ptr;
	check_eol();
	input_string[f]=0;
	push_input_file(input_string+d);
}

// recopie une ligne de macro dans le pool
// rend 1 si c'est la dernière ligne
int line_in_pool()
{
	char c;
	int l;
	c=skip_blanks();
	// ne pas mémoriser les lignes vide
	if ((c==10)||(c==0)||(c=='#')) return 0;
	// vérifier si c'est la dernière ligne
	l=check_word("end",3);
	// mémoriser la ligne dans le pool jusqu'à la fin de ligne
	do
	{
		if (pool_ptr>=pool_max-1) fatal_error("mémoire pleine");
		pool[pool_ptr++]=c;
		c=input_string[++tk_ptr];
	}
	while((c!=10)&&(c!=0)&&(c!='#'));
	// purge les espaces en fin de ligne
	while ((c==' ')||(c==8))
	{
		pool_ptr--;
		c=pool[pool_ptr-1];
	}
	// zéro terminal de la ligne
	pool[pool_ptr++]=0;
	return l;
}

// lecture de la définition d'une macro
// mémorise le texte jusqu'à la commande "end"
void macro()
{
	int l;	// longueur du nom de la macro
	char*n;	// adresse du nom de la macro
	// lire le nom de la macro
	skip_blanks();
	l=get_id(&n);
	check_eol();
	new_macro(n,l);
	// première ligne vide pour que get-line puisse s'enclencher
	pool[pool_ptr++]=0;
	for(;;)
	{
		if (line_in_pool()) break;
		get_line();
	}

}

int scan_macro()
{
	char*n;
	int t;
	skip_blanks();
	t=get_id(&n);
	t=look_up(n,t);
	if (h_table[t].base==NULL)
	{
		undef_symb(t);
	}
	if (h_table[t].len>0)
	{
		not_macro(t);
	}
	return t;
}

void call()
{
	int t;
	t=scan_macro();
	push_input_macro((char*)h_table[t].base);
}

void __exit()
{
	//int t;
	if (is_eol())
	{
		fin();
	}
	//t=scan_macro();
	// recherche dans la pile un retour de cette macro

}



void __if();

void parse_ligne()
{
	skip_blanks();
	if (is_word("exit",4))
	{
		__exit();
		return;
	}
	if (is_word("reader",6))
	{
		int n;
		//n=get_number();
		n=get_param();
		check_eol();
		lecteur(n);
		return;
	}
	if (is_word("reset",5))
	{
		reset();
		check_eol();
		return;
	}
	if (is_word("say",3))
	{
		say();
		return;
	}
	if (is_word("little_endian",13))
	{
		endianness=0;
		return;
	}
	if (is_word("big_endian",10))
	{
		endianness=1;
		return;
	}
	if (is_word("alias",5))
	{
		alias();
		return;
	}
	if (is_word("set",3))
	{
		set();
		return;
	}
	if (is_word("random",6))
	{
		random_alias();
		return;
	}
	if (is_word("input",5))
	{
		input();
		return;
	}
	if (is_word("macro",5))
	{
		strcpy(prompt,"> ");
		macro();
		strcpy(prompt,"* ");
		return;
	}
	if (is_word("call",4))
	{
		call();
		return;
	}
	if (is_word("if",2))
	{
		__if();
		return;
	}
	mem_ptr=0;
	push_expr();
//		push_datas();
	inlen=mem_ptr;
	if (inlen<5)
	{
		error("commande incomplète");
	}
	//print(inlen,inbuffer,1);
	apdu();

}


void __if()
{
	int t;
	check_char('(');
	t=bool_expr();
	check_char(')');
	if (t!=0)
	{
	//	printf("suite de la ligne %s\n", input_string+tk_ptr);
	       	parse_ligne();
	}
}


void travail()
{

	//input_string[0]=0;
	tk_ptr=0;

	for (;;) if (setjmp(env_trav)==0)
	{
		clear_stack();
		mem_ptr=0;
		get_line();
		fprintf(stderr,"* %s\n",input_string);
		parse_ligne();
		continue;
	}
	else
	{
		while (!is_eol()) tk_ptr++;
	}
}

//****************************************************************



void initialize()
{
	istack_ptr=0;
	get_line=rl_get_line;
	init_pool();
	init_htable();
	mem_ptr=0;
	mem_max=10000;
	endianness=0;
	clear_stack();
	clock_gettime(CLOCK_REALTIME_COARSE,&__h);
	__t=__h.tv_nsec/1000000l+1000l*__h.tv_sec;
	t_sw1=new_alias("sw1",3,1);
	t_sw2=new_alias("sw2",3,1);
	t_reply=new_alias("reply",5,258);
	t_sw=new_alias("sw",2,2);
	t_time=new_alias("time",4,4);
	b_sw1=h_table[t_sw1].base;
	b_sw2=h_table[t_sw2].base;
	b_reply=h_table[t_reply].base;
	b_sw=h_table[t_sw].base;
	b_time=h_table[t_time].base;
	*(int*)(b_time)=0;
}

/*
char* input_p;
char input_buffer[256];

int skip_blanks;
FILE*file;

int next_tk()
{

	return skip_blanks=input_p[tk_ptr++];
}

char* file_gets()
{
	fgets(input_buffer,256,file);
	return input_buffer;
}

void get_line()
{
	input_p=file_gets(); //rl_gets();
	tk_ptr=0;
}

void print_line()
{
	do
	{
		printf("(%d)",next_tk());
	}
	while ((skip_blanks!=0)&&(token!=10));
	printf("\n");
}

void test()
{
	file=fopen("script","r");
	do
	{
		get_line();
		print_line();
	}
	while (!feof(file));
	fin();

}

*/
int main(int argc, char*argv[])
{
	fprintf(stderr,"Scat version " VERSION "\n");



	initialize();

	if (setjmp(env)==0)
	{
		init_lecteurs();
		while (--argc)
		{
			push_input_file(argv[argc]);
		}
		travail();
		//test();
	}

	fin_lecteur();
	return 0;
}
