#ifndef _HAVE_SPCAT_OBJ_H
#define _HAVE_SPCAT_OBJ_H

typedef struct spcat_state spcs_t;

#include "calpgm.h"
#include "spinit.h"

#define NFILE 6

extern const char *spcat_ext[NFILE];
enum spcat_efile {eint, evar, eout, ecat, estr, eegy};


typedef struct {  /* vibrational information */ /* From spinv.c */
  /*@dependent@*/ short *spt;
  int nspstat;
  short knmin[4], knmax, wt[5], ewt[2], lvqn, lvupper, gsym, nqn;
} SVIB;
typedef struct str_spar {  /* local parameter information */ /* From spinv.c */
  /*@null@*/ /*@owned@*/ struct str_spar *next;
  double zfac;
  int ip;
  unsigned int ipsym;
  short flags;
  signed char ksq, fc, kavg, msi1, msi2, mldel;
  unsigned char njq, mln, mld, mkdel, mins, msznz, euler, alpha;
} SPAR;
typedef /*@null@*/ /*@owned@*/ SPAR *PSPAR; /* From spinv.c */
typedef struct struct_ssp {	/* From spinv.c */
  /*@null@*/ /*@owned@*/ /*@reldef@*/ struct struct_ssp *next;
  /*@notnull@*//*@owned@*/ short *sspt;
  int ssize, nitot;
} SSP;
typedef struct { /* local dipole information */ /* From spinv.c */
  double fac;
  short  flg;
  signed char kd, ld, ldel, fc, kavg;
} SDIP;
#define MAXVIB  999
#define MAXII   20  /* I <= 10 */
#define MAXNS   20  /* 2 * NSPINS + 2 */
#define NDXCOM  8   /* XCOM size, XCOM enum: */
#define NSBCD   (2 * (NDECPAR + MAXVDEC + 1))
#define NDECPAR 5   /* number of digit pairs for idpar not including vib */ 
#define MAXVDEC 3
#define NSPOP 5
#define MAXN_DIRCOS 359 /* MAX K in DIRCOS */
#define NFAC_DIRCOS 20


struct spcat_state {
  /* SPCAT static variables moved here to allow thread/session safety */

  /* from spcat-obj.c ibufof, uninitialized (i.e. to zero) */
  FILE *scratch;		/* Close me when freeing */
  long maxrec, lsizb;
  int mempos, orgpos, nbsav;
  unsigned int maxdm;

  /* from spinit.c */
  /*@null@*/ ITMIX *itmix_head/* = NULL*/;
  /*@null@*/ SITOT *ithead/* = NULL*/;
  int fc_neqi;
  double zrfc[MAXITOT], zifc[MAXITOT];
  /* nb: itpairs switched from int[] to int* */
  int *itpairs; /* documentation and default value is itpairs in init_spcs */
  int *itpair/* = itpairs*/;

  /* From spinv.c */
  SVIB vinfo1;
  /*@owned@*/ SVIB *vinfo/* = &vinfo1*/;
  PSPAR spar_head[MAXVIB];
  short sptzero[2]/* = { 1, 0 }*/; /* default for no spins */
  SSP ssp_head/* = { NULL, sptzero, 1 , 0}*/;
  struct {   /* save data for getqn */
    int cblk, cnblk, csblk, cff, cwt[5];
  } *cgetq, cgetv[2];
  SDIP dipinfo0;
  /*@owned@*/ SDIP *dipinfo/* = &dipinfo0*/;
  double zero/* = 0.*/;
  double zwk/* = 0.*/;
  double spfac[MAXII];
  double spfac2[MAXII];
  int zmoldv/* = 0*/;
  int zblkptr/* = 0*/;
  int zivs/* = 0*/;
  int zipder/* = 0*/;
  int ixphase[4]/* = {0, 1, 2, 3}*/;
  int is_esym[MAXITOT];
  int lscom[MAXNS], iscom[MAXNS], jscom[MAXNS], ismap[MAXNS];
  int ixcom[NDXCOM], jxcom[NDXCOM];
  int itptr, itsym, nspin, nsqmax, ndmx, ndmax, nddip;
  short szero/* = 0*/;
  short zidx/* = 0*/;
  short zjdx/* = 0*/;
  short ziqnsep/* = 0*/;
  short zibkptr/* = 0*/;
  short zikmin/* = 0*/;
  struct {
    int mxspin, idiag, nvib, nfit, nbkpj, ixz, nqnn, nqn, maxqn, vibfac;
    int parinit, maxblk, nitot, vibdec, esymdec, msmask, nqn0, iqfmt0, maxwt;
    int stdphase, phasemask, g12;
    unsigned int msshft;
    BOOL lsym, esym, oblate, vibfmt, newlz, nofc;
  } glob;
  char sbcd[NSBCD];
  /* pointers to dynamically allocated arrays */
  /*@owned@*/ int *moldv;
  /*@owned@*/ int *blkptr;
  /*@owned@*/ int *ipder, *ivs;
  /*@owned@*/ double *wk;
  /*@owned@*/ short *idx, *jdx, *iqnsep, *ibkptr, *ikmin;

  /* Moved from spinv.c hamx */
  double sqj[10];
  int nold/* = -1*/;
  /* Moved from spinv.c specop */
  double aden[NSPOP], bden[NSPOP];
  /* Moved from spinv.c specfc */
  double pfac[MAXVIB];
  short ipfac[MAXVIB];
  /* Moved from spinv.c intens */
  int idipoff;
  /* Moved from spinv.c setblk */
  char csym[4]/* = { 'x', 'c', 'b', 'a' }*/;
  /* Moved from spinv.c pasort */
  int idmy[NDXCOM], initl;
  bcd_t idunit[NDECPAR];
  /* Moved from spinv.c setgsym */
  int oldgsym/* = -1*/;
  int nsym;
  /* Moved from spinv.c getqn */
  int last;
  /* Moved from spinv.c dircos */
  double ff[MAXN_DIRCOS], fac[NFAC_DIRCOS];
  /* Moved from spinv.c ffcal */
  int nlast/* = -1*/;
  int klast/* = 0*/;
  double sq;
};

int init_spcs(spcs_t *s);
int free_spcs(spcs_t *s);
int spcat(spcs_t *spcs, char *filebufs[], size_t bufsizes[]);

#endif
