/*
 * SVR1 fsck modified to run on a 64 bit machine against cross-system
   32 bit headers, and to allow checking of a FS image in a file,
   with possible byte swapping

   This is to allow FSCK of 3b1/7300 file system on a 32/64 bit linux
   host with access to a FS image file.

   Recent History:
   dbrower@gmail.com  01/10/16 - created from SVR2 source on archive.org

   Does NOT have the "/etc/checklist" of the original.

   Usage:

   s4fsck fsfile ... [-s][-S][-n][-y|-Y][-D][-f|-F] [-q][-d]

    -s      force freelist salvage
    -S      conditional freelist salvage
    -n      do not write, answer 'no' to everything
    -y      answer 'yes' to all repair questions
    -f      "fast" check
    -D      extensive directory check

    -q      quiet (return status only)
    -d      debug output
*/

  
/* @(#)fsck.c	2.1	 */
#include <stdio.h>
#include <ctype.h>

#include <s4d.h>
#include <pwd.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/vfs.h>

#define	SBSIZE	512

#define NDIRECT	(S4_BSIZE/sizeof(struct s4_direct))
#define SPERB	(S4_BSIZE/sizeof(short))

#define NO	0
#define YES	1

#define	MAXDUP	10		/* limit on dup blks (per inode) */
#define	MAXBAD	10		/* limit on bad blks (per inode) */

#define STEPSIZE	7	/* default step for freelist spacing */
#define CYLSIZE		400	/* default cyl size for spacing */
#define MAXCYL		1000	/* maximum cylinder size */

#define BITSPB	8		/* number bits per byte */
#define BITSHIFT	3	/* log2(BITSPB) */
#define BITMASK	07		/* BITSPB-1 */
#define LSTATE	2		/* bits per inode state */
#define STATEPB	(BITSPB/LSTATE)	/* inode states per byte */

#define USTATE	0		/* inode not allocated */
#define FSTATE	01		/* inode is file */
#define DSTATE	02		/* inode is directory */
#define CLEAR	03		/* inode is to be cleared */
#define EMPT	32		/* empty directory? */
#define SMASK	03		/* mask for inode state */

typedef struct s4_dinode DINODE;
typedef struct s4_direct DIRECT;

#define ALLOC	((dp->di_mode & S_IFMT) != 0)
#define DIR	((dp->di_mode & S_IFMT) == S_IFDIR)
#define REG	((dp->di_mode & S_IFMT) == S_IFREG)
#define BLK	((dp->di_mode & S_IFMT) == S_IFBLK)
#define CHR	((dp->di_mode & S_IFMT) == S_IFCHR)
#define FIFO	((dp->di_mode & S_IFMT) == S_IFIFO)
#define SPECIAL (BLK || CHR)

#define MAXPATH	1500		/* max size for pathname string.
				 * Increase and recompile if pathname
				 * overflows.
				 */

int doswap;                     /* magic numer says we should swap */

#define NINOBLK	11		/* num blks for raw reading */
#define MAXRAW	110		/* largest raw read (in blks) */
s4_daddr startib;		/* blk num of first in raw area */
unsigned niblk;			/* num of blks in raw area */

struct bufarea {
  struct bufarea *b_next;       /* must be first */
  s4_daddr	  b_bno;
  int             b_swapped;    /* when type know, and swapped */
  s4btype         b_type;       /* what is in union below? */
  union {
    char     b_buf[S4_BSIZE];              /* buffer space */
    short    b_lnks[SPERB];                /* link counts */
    s4_daddr b_indir[S4_NINDIR];           /* indirect block */
    struct   s4_dfilsys b_fs;              /* super block */
    struct   s4_fblk b_fb;                 /* free block */
    struct   s4_dinode b_dinode[S4_INOPB]; /* inode block */
    DIRECT   b_dir[S4_NDIRECT];            /* directory */
  } b_un;
  char	b_dirty;
};

typedef struct bufarea BUFAREA;

BUFAREA	inoblk;			/* inode blocks */
BUFAREA	fileblk;		/* other blks in filesys */
BUFAREA	sblk;			/* file system superblock */
#define ftypeok(dp)	(REG||DIR||BLK||CHR||FIFO)
BUFAREA	*poolhead;		/* ptr to first buffer in pool */

#define initbarea(x)	(x)->b_dirty = 0;(x)->b_bno = (s4_daddr)-1;     \
                        (x)->b_type = s4b_unk;(x)->b_swapped = NO;
#undef  dirty
#define dirty(x)	(x)->b_dirty = 1
#define inodirty()	inoblk.b_dirty = 1
#define fbdirty()	fileblk.b_dirty = 1
#define sbdirty()	sblk.b_dirty = 1

#define freeblk		fileblk.b_un.b_fb
#define dirblk		fileblk.b_un.b_dir
#define superblk	sblk.b_un.b_fs

struct filecntl {
  int	rfdes;
  int	wfdes;
  int	mod; 
};

struct filecntl	dfile;		/* file descriptors for filesys */
struct filecntl	sfile;		/* file descriptors for scratch file */

/* typedef unsigned MEMSIZE; */
typedef size_t MEMSIZE;

MEMSIZE	memsize;		/* amt of memory we got */

#ifdef pdp11
#define MAXDATA	((MEMSIZE)54*1024)
#endif
#ifdef u3b
#define MAXDATA ((MEMSIZE)350*1024)
#endif
#ifdef m68k
#define MAXDATA ((MEMSIZE)64*1024)
#endif
#ifdef vax
#ifdef STANDALONE
#define	MAXDATA ((MEMSIZE)256*1024)
#else
#define	MAXDATA ((MEMSIZE)350*1024)
#endif
#endif

#ifndef MAXDATA
#define MAXDATA ((MEMSIZE)600*1024)
#endif

/* two different types of visit functions */
typedef int (*visitdir)(DIRECT *dir, BUFAREA *bp);
typedef int (*visitblk)(s4_daddr blk, int flg);


#define	DUPTBLSIZE	100	/* num of dup blocks to remember */
s4_daddr	duplist[DUPTBLSIZE];	/* dup block table */
s4_daddr	*enddup;		/* next entry in dup table */
s4_daddr	*muldup;		/* multiple dups part of table */

#define MAXLNCNT	20	/* num zero link cnts to remember */
s4_ino	badlncnt[MAXLNCNT];	/* table of inos with zero link cnts */
s4_ino	*badlnp;		/* next entry in table */

char	sflag;			/* salvage free block list */
char	csflag;			/* salvage free block list (conditional) */
char	nflag;			/* assume a no response */
char	yflag;			/* assume a yes response */
char	tflag;			/* scratch file specified */
char	rplyflag;		/* any questions asked? */
char	qflag;			/* less verbose flag */
char    dbgflag;                /* very verbose debug flag */
char	Dirc;			/* extensive directory check */
char	fast;			/* fast check- dup blks and free list check */
char	hotroot;		/* checking root device */
char	rawflg;			/* read raw device */
char	rmscr;			/* remove scratch file when done */
char	fixfree;		/* corrupted free list */
char	*membase;		/* base of memory we get */
char	*blkmap;		/* ptr to primary blk allocation map */
char	*freemap;		/* ptr to secondary blk allocation map */
char	*statemap;		/* ptr to inode state table */
char	*pathp;			/* pointer to pathname position */
char	*thisname;		/* ptr to current pathname component */
char	*srchname;		/* name being searched for in dir */
/*char	*savep;*/			/* save block position */
/*unsigned saven;*/			/* save byte number */
char	pss2done;			/* do not check dir blks anymore */
char	initdone;
char	pathname[MAXPATH];
char	scrfile[80];
char	devname[25];
char	*lfname =	"lost+found";

short	*lncntp;		/* ptr to link count table */

int	cylsize;		/* num blocks per cylinder */
int	stepsize;		/* num blocks for spacing purposes */
int	badblk;			/* num of bad blks seen (per inode) */
int	dupblk;			/* num of dup blks seen (per inode) */

visitdir    dpfunc = NULL;  /* function for dir checks */
visitblk    bpfunc = NULL;; /* function for blk checks */

s4_ino	inum;			/* inode we are currently working on */
s4_ino	imax;			/* number of inodes */
s4_ino	parentdir;		/* i number of parent directory */
s4_ino	lastino;		/* hiwater mark of inodes */
s4_ino	lfdir;			/* lost & found directory */
s4_ino	orphan;			/* orphaned inode */

s4_off	filsize;		/* num blks seen in file */
s4_off	bmapsz;			/* num chars in blkmap */

s4_daddr smapblk;               /* starting blk of state map */
s4_daddr lncntblk;              /* starting blk of link cnt table */
s4_daddr fmapblk;               /* starting blk of free map */
s4_daddr n_free;		/* number of free blocks */
s4_daddr n_blks;		/* number of blocks used */
s4_daddr n_files;               /* number of files seen */
s4_daddr f_min;                 /* block number of the first data block */
s4_daddr f_max;                 /* number of blocks in the volume */

#define clear(x,l)      memset((x),0,(l))

#define minsz(x,y)	(x>y ? y : x)
#define howmany(x,y)	(((x)+((y)-1))/(y))
#define roundup(x,y)	((((x)+((y)-1))/(y))*(y))
#define outrange(x)	(x < f_min || x >= f_max)
#define zapino(x)	clear((x),sizeof(DINODE))

#define setlncnt(x)	dolncnt(x,0)
#define getlncnt()	dolncnt(0,1)
#define declncnt()	dolncnt(0,2)

#define setbmap(x)	domap(x,0)
#define getbmap(x)	domap(x,1)
#define clrbmap(x)	domap(x,2)

#define setfmap(x)	domap(x,0+4)
#define getfmap(x)	domap(x,1+4)
#define clrfmap(x)	domap(x,2+4)

int dostate(int statebit, int noset);

#define setstate(x)	dostate(x,0)
#define getstate()	dostate(0,1)

/* flg values - flgstr() decodes */
#define DATA	1
#define ADDR	0
#define BBLK	2
#define ALTERD	010
#define KEEPON	04
#define SKIP	02
#define STOP	01
#define REM	07

/* Forward declarations */

const char *flgstr( int flg );
const char *cbname(void *f);

#define error0(f)             printf(f)
#define error1(f,a1)          printf(f,a1)
#define error2(f,a1,a2)       printf(f,a1,a2)
#define error3(f,a1,a2,a3)    printf(f,a1,a2,a3)
#define error4(f,a1,a2,a3,a4) printf(f,a1,a2,a3,a4)

#define errexit0(f)             {printf(f);exit(8);}
#define errexit1(f,a1)          {printf(f,a1);exit(8);}
#define errexit2(f,a1,a2 )      {printf(f,a1,a2); exit(8);}
#define errexit3(f,a1,a2,a3)    {printf(f,a1,a2,a3); exit(8);}
#define errexit4(f,a1,a2,a3,a4) {printf(f,a1,a2,a3,a4);exit(8);}

void initmem(void);
void check(char *dev);
void descend(void);

int fsck_getline(FILE *fp, char *loc, int maxlen);
DINODE	*ginode(void);
BUFAREA *getblk( BUFAREA *bp, int blk );
BUFAREA *search(s4_daddr blk);
void flush(struct filecntl *fcp, BUFAREA *bp );
void	catch(int sig);

/* these are visitdir and visitblk callbacks  */
int	nodfunc(DIRECT *dirp, BUFAREA *bp);
int	findino(DIRECT *dirp, BUFAREA *bp);
int     mkentry(DIRECT *dirp, BUFAREA *bp);
int	chgdd(DIRECT *dirp, BUFAREA *bp);

int	dirscan(s4_daddr blk, int flg);
int	chkblk(int blk, int flg);

int	nobfunc(s4_daddr blk, int flg);

int	pass1(s4_daddr blk, int flg);
int	pass1b(s4_daddr blk, int flg);
int	pass2(DIRECT *dirp, BUFAREA *bp);
int	pass3(s4_daddr blk, int flg);
int	pass4(s4_daddr blk, int flg);
int	pass5(s4_daddr blk, int flg);



void blkerr(char *s, s4_daddr blk);
void descent(void);
void adjust(short lcnt);
void clri( char *s, int flg);
void clrinode( DINODE *dp);
void stype( char *p);
void rwerr(char *s, s4_daddr blk);
void sizechk(DINODE *dp);
void ckfini(void);
void pinode(void);

int ckinode(DINODE *dp,  int flg);
int iblock(s4_daddr blk, int ilevel, int flg);
int direrr(char *s);
int chkempt(DINODE *dp);
int setup(char *dev);
int checksb(char *dev);
int reply(char *s);
int getno(FILE *fp);
int domap(s4_daddr blk, int flg);
int dolncnt(short val, int flg);

#define copy(f,t,s) memcpy(t,f,s)

void freechk(void);
void makefree(void);
int linkup(void);

int bread(struct filecntl *fcp, char *buf, s4_daddr blk, MEMSIZE size);
int bwrite(struct filecntl *fcp, char *buf, s4_daddr blk, MEMSIZE size);

/* handle byte swapping of bufarea's */
void bclear( struct bufarea *bp );               /* clear completely */
void bset( struct bufarea *bp, s4btype type );   /* set type and btomem  */
void bsetmem(struct bufarea *bp, s4btype type);
void btomem( struct bufarea *bp );               /* convert to mem fmt */
void btodisk( struct bufarea *bp );              /* convert to disk fmt */

static char	id = ' ';
s4_dev	pipedev = -1;	/* is pipedev (and != -1) iff the standard input
			 * is a pipe, which means we can't check pipedev! */

int main(int argc, char **argv)
{
  register int i;
  int n;
  int svargc, argvix;
  int ix;
  struct stat statbuf;

  if ( argv[0][0] >= '0' && argv[0][0] <= '9' ) id = argv[0][0];

  setbuf(stdin,NULL);
  setbuf(stdout,NULL);
  sync();

  svargc = argc;
  for(i = 1, --argc;  *argv[i] == '-'; i++, --argc) {
    switch(*(argv[i]+1)) {
    case 't':
    case 'T':
      tflag++;
      if(*argv[++i] == '-' || --argc <= 0)
        errexit1("%c Bad -t option\n",id);
      strcpy(scrfile,argv[i]);
      if( (stat(scrfile,&statbuf) >= 0) &&
          ((statbuf.st_mode & S_IFMT) != S_IFREG) )
        errexit2("%c Illegal scratch file <%s>\n",
                 id, scrfile);
      break;
    case 's':	/* salvage flag */
      stype(argv[i]+2);
      sflag++;
      break;
    case 'S':	/* conditional salvage */
      stype(argv[i]+2);
      csflag++;
      break;
    case 'n':	/* default no answer flag */
    case 'N':
      nflag++;
      yflag = 0;
      break;
    case 'y':	/* default yes answer flag */
    case 'Y':
      yflag++;
      nflag = 0;
      break;
    case 'q':
      qflag++;
      break;
    case 'D':
      Dirc++;
      break;
    case 'd':
      dbgflag++;
      break;
    case 'F':
    case 'f':
      fast++;
      break;
    default:
      errexit2("%c %c option?\n",id,*(argv[i]+1));
    }
  }
  if(nflag && sflag)
    errexit1("%c Incompatible options: -n and -s\n",id);
  if(nflag && qflag)
    errexit1("%c Incompatible options: -n and -q\n",id);
  if(sflag && csflag)
    sflag = 0;
  if(csflag) nflag++;

  ix = argvix = svargc - argc;		/* position of first fs argument */
  while(argc > 0) {
    initbarea(&sblk);
    if(checksb(argv[ix]) == NO) {
      argc--; ix++;
      continue;
    }
#if S4_FsTYPE==2
    if(superblk.s_magic != S4_FsMAGIC ||
       (superblk.s_magic == S4_FsMAGIC && superblk.s_type == S4_Fs1b)) {
#else
      if(superblk.s_magic == S4_FsMAGIC && superblk.s_type == S4_Fs2b) {
#endif
        if(dfile.rfdes > 0 )
          close(dfile.rfdes);
        if(argvix < svargc - argc) {
          for(n = 0; n < argc; n++)
            argv[argvix + n] = argv[svargc - argc + n];
          argv[argvix + n] = NULL;
        }
#if S4_FsTYPE==2
        if(execvp("./s4fsck1b",argv) == -1)
          errexit2("%c %sCannot exec ./s4fsck1b\n",
                   id,devname);
#else
        if(execvp("./s4fsck",argv) == -1)
          errexit2("%c %sCannot exec ./s4fsck\n",
                   id,devname);
#endif
#if S4_FsTYPE==2
      } /* match FS2 bracket */
#else
    } /* match else bracket */
#endif

    if(!initdone) {
      initmem();
      initdone++;
    }
    check(argv[ix++]);
    argc--;
  }
  exit(0);
}



void initmem(void)
{
  register int n;
  struct stat statbuf;
  void (*sg)(int);
  void *sbrk();
  void *brk;

  /* number of block buffers */
  memsize = 256 * sizeof(BUFAREA);
  brk = sbrk( memsize );
  if( (long int)brk == -1 )
    errexit1("%c Can't get memory\n",id);

  membase = brk;


  for(n = 1; n < NSIG; n++) {
    if(n == SIGCLD || n == SIGPWR)
      continue;
    sg = signal(n,catch);
    if( sg != SIG_DFL)
      signal(n,sg);
  }

  /* Check if standard input is a pipe. If it is, record pipedev so
   * we won't ever check it */
  if ( fstat( 0, &statbuf) == -1 )
    errexit1("%c Can't fstat standard input\n", id);
  if ( (statbuf.st_mode & S_IFMT) == S_IFIFO ) pipedev = statbuf.st_dev;
}

void check(char *dev)
{
  register DINODE *dp;
  register int n;
  register s4_ino *blp;
  s4_ino savino;
  s4_daddr blk;
  BUFAREA *bp1, *bp2;

  if(pipedev != -1) {
    strcpy(devname,dev);
    strcat(devname,"\t");
  }
  else
    devname[0] = '\0';
  if(setup(dev) == NO)
    return;

  printf("%c %s** Phase 1 - Check Blocks and Sizes\n",id,devname);
  bpfunc = pass1;
  for(inum = 1; inum <= imax; inum++) {
    if((dp = ginode()) == NULL)
      continue;

    if( dbgflag ) {
      printf("Inode %d:\n", inum );
      s4_dinode_show( (struct s4_dinode*)
                      &inoblk.b_un.b_dinode[inum%S4_INOPB] );
    }

    if(ALLOC) {
      lastino = inum;
      if(ftypeok(dp) == NO) {
        printf("%c %sUNKNOWN FILE TYPE I=%u",id,devname,inum);
        if(dp->di_size)
          printf(" (NOT EMPTY)");
        if(reply("CLEAR") == YES) {
          zapino(dp);
          inodirty();
        }
        continue;
      }
      n_files++;
      if(setlncnt(dp->di_nlink) <= 0) {
        if(badlnp < &badlncnt[MAXLNCNT])
          *badlnp++ = inum;
        else {
          printf("%c %sLINK COUNT TABLE OVERFLOW",id,devname);
          if(reply("CONTINUE") == NO)
            errexit0("\n");
        }
      }
      setstate(DIR ? DSTATE : FSTATE);
      badblk = dupblk = 0;
      filsize = 0;
      ckinode(dp,ADDR);
      if((n = getstate()) == DSTATE || n == FSTATE)
        sizechk(dp);
    }
    else if(dp->di_mode != 0) {
      printf("%c %sPARTIALLY ALLOCATED INODE I=%u",id,devname,inum);
      if(dp->di_size)
        printf(" (NOT EMPTY)");
      if(reply("CLEAR") == YES) {
        zapino(dp);
        inodirty();
      }
    }
  }

  if(enddup != &duplist[0]) {
    printf("%c %s** Phase 1b - Rescan For More DUPS\n",id,devname);
    bpfunc = pass1b;
    for(inum = 1; inum <= lastino; inum++) {
      if(getstate() != USTATE && (dp = ginode()) != NULL)
        if(ckinode(dp,ADDR) & STOP)
          break;
    }
  }
  if(rawflg) {
    if(inoblk.b_dirty)
      bwrite(&dfile,membase,startib,niblk*S4_BSIZE);
    inoblk.b_dirty = 0;
    if(poolhead) {
      clear(membase,niblk*S4_BSIZE);
      for(bp1 = poolhead;bp1->b_next;bp1 = bp1->b_next);
      bp2 = &((BUFAREA *)membase)[(niblk*S4_BSIZE)/sizeof(BUFAREA)];
      while(--bp2 >= (BUFAREA *)membase) {
        initbarea(bp2);
        bp2->b_next = bp1->b_next;
        bp1->b_next = bp2;
      }
    }
    rawflg = 0;

  }


  if(!fast) {
    printf("%c %s** Phase 2 - Check Pathnames\n",id,devname);
    inum = S4_ROOTINO;
    thisname = pathp = pathname;
    dpfunc = pass2;
    switch(getstate()) {
    case USTATE:
      errexit2("%c %sROOT INODE UNALLOCATED. TERMINATING.\n",id,devname);
    case FSTATE:
      printf("%c %sROOT INODE NOT DIRECTORY",id,devname);
      if(reply("FIX") == NO || (dp = ginode()) == NULL)
        errexit0("\n");
      dp->di_mode &= ~S_IFMT;
      dp->di_mode |= S_IFDIR;
      inodirty();
      setstate(DSTATE);
    case DSTATE:
      descend();
      break;
    case CLEAR:
      printf("%c %sDUPS/BAD IN ROOT INODE\n",id,devname);
      if(reply("CONTINUE") == NO)
        errexit0("\n");
      setstate(DSTATE);
      descend();
    }


    pss2done++;
    printf("%c %s** Phase 3 - Check Connectivity\n",id,devname);
    for(inum = S4_ROOTINO; inum <= lastino; inum++) {
      if(getstate() == DSTATE) {
        dpfunc = findino;
        srchname = "..";
        savino = inum;
        do {
          orphan = inum;
          if((dp = ginode()) == NULL)
            break;
          filsize = dp->di_size;
          parentdir = 0;
          ckinode(dp,DATA);
          if((inum = parentdir) == 0)
            break;
        } while(getstate() == DSTATE);
        inum = orphan;
        if(linkup() == YES) {
          thisname = pathp = pathname;
          *pathp++ = '?';
          dpfunc = pass2;
          descend();
        }
        inum = savino;
      }
    }


    printf("%c %s** Phase 4 - Check Reference Counts\n",id,devname);
    bpfunc = pass4;
    for(inum = S4_ROOTINO; inum <= lastino; inum++) {
      switch(getstate()) {
      case FSTATE:
        if((n = getlncnt()))
          adjust((short)n);
        else {
          for(blp = badlncnt;blp < badlnp; blp++)
            if(*blp == inum) {
              if((dp = ginode()) &&
                 dp->di_size) {
                if((n = linkup()) == NO)
                  clri("UNREF",NO);
                if (n == REM)
                  clri("UNREF",REM);
              }
              else
                clri("UNREF",YES);
              break;
            }
        }
        break;
      case DSTATE:
        clri("UNREF",YES);
        break;
      case CLEAR:
        clri("BAD/DUP",YES);
      }
    }
    if(imax - n_files != superblk.s_tinode) {
      printf("%c %sFREE INODE COUNT WRONG IN SUPERBLK",id,devname);
      if (qflag) {
        superblk.s_tinode = imax - n_files;
        sbdirty();
        printf("\n%c %sFIXED\n",id,devname);
      }
      else if(reply("FIX") == YES) {
        superblk.s_tinode = imax - n_files;
        sbdirty();
      }
    }
    /* FIXME -- what is the type of fileblk here? */
    flush(&dfile,&fileblk);

  }	/* if fast check, skip to phase 5 */
  printf("%c %s** Phase 5 - Check Free List ",id,devname);
  if(sflag || (csflag && rplyflag == 0)) {
    printf("(Ignored)\n");
    fixfree = 1;
  }
  else {
    printf("\n");
    if(freemap)
      copy(blkmap,freemap,(MEMSIZE)bmapsz);
    else {
      for(blk = 0; blk < fmapblk; blk++) {
        bp1 = getblk(NULL,blk);
        bp2 = getblk(NULL,blk+fmapblk);
        copy(bp1->b_un.b_buf,bp2->b_un.b_buf,S4_BSIZE);
        dirty(bp2);
      }
    }
    badblk = dupblk = 0;
    freeblk.df_nfree = superblk.s_nfree;
    bsetmem(&fileblk, s4b_free);
    for(n = 0; n < S4_NICFREE; n++)
      freeblk.df_free[n] = superblk.s_free[n];
    freechk();
    if(badblk)
      printf("%c %s%d BAD BLKS IN FREE LIST\n",id,devname,badblk);
    if(dupblk)
      printf("%c %s%d DUP BLKS IN FREE LIST\n",id,devname,dupblk);

    if(fixfree == 0) {
      if((n_blks+n_free) != (f_max-f_min)) {
        printf("%c %s%ld BLK(S) MISSING\n",id,devname,
               (long)f_max-f_min-n_blks-n_free);
        fixfree = 1;
      }
      else if(n_free != superblk.s_tfree) {
        printf("%c %sFREE BLK COUNT WRONG IN SUPERBLK",id,devname);
        if(qflag) {
          superblk.s_tfree = n_free;
          sbdirty();
          printf("\n%c %sFIXED\n",id,devname);
        }
        else if(reply("FIX") == YES) {
          superblk.s_tfree = n_free;
          sbdirty();
        }
      }
    }
    if(fixfree) {
      printf("%c %sBAD FREE LIST",id,devname);
      if(qflag && !sflag) {
        fixfree = 1;
        printf("\n%c %sSALVAGED\n",id,devname);
      }
      else if(reply("SALVAGE") == NO)
        fixfree = 0;
    }
  }

  if(fixfree) {
    printf("%c %s** Phase 6 - Salvage Free List\n",id,devname);
    makefree();
    n_free = superblk.s_tfree;
  }
  flush(&dfile,&fileblk);
  flush(&dfile,&inoblk);
  flush(&dfile,&sblk);


#if S4_FsTYPE==2
  printf("%c %s%ld files %ld blocks %ld free\n",id,devname,
         (long)n_files,(long)n_blks*2,(long)n_free*2);
#else
  printf("%c %s%ld files %ld blocks %ld free\n",id,devname,
         n_files,n_blks,n_free);
#endif

  if(dfile.mod) {
    time_t t;       /* local time_t, not time32_t */
    time(&t);
    superblk.s_time = t;
    sbdirty();
  }

  ckfini();

  sync();
  if(dfile.mod && hotroot) {
    printf("%c %s***** BOOT UNIX (NO SYNC!) *****\n",id,devname);
    for(;;);
  }

  if(dfile.mod)
    printf("%c %s***** FILE SYSTEM WAS MODIFIED *****\n",id,devname);
}


int ckinode( DINODE *dp,  int flg )
{
  register s4_daddr *ap;        /* address pointer */
  register int ret;
  visitblk bfunc = nobfunc;
  int      n;
  s4_daddr iaddrs[S4_NADDR];

  if(SPECIAL)
    return(KEEPON);

  /* get 3-byte addresses into 4-byte format, locally */
  if( doswap )
    s4l3tolr(iaddrs,dp->di_addr,S4_NADDR);
  else
    s4l3tol(iaddrs,dp->di_addr,S4_NADDR);

  switch(flg) {
  case ADDR:
    bfunc = bpfunc;
    break;
  case DATA:
    bfunc = dirscan;
    break;
  case BBLK:
    bfunc = chkblk;
  }

  /* check direct blocks */
  for(ap = iaddrs; ap < &iaddrs[S4_NADDR-3]; ap++) {

    /* flag down is first block TRUE, else FALSE */
    /* if return is STOP, break; if not BBLK, return now. */
    if(*ap && (ret = (*bfunc)(*ap,((ap == &iaddrs[0]) ? 1 : 0))) & STOP)
      if(flg != BBLK)
        return(ret);
  }

  /* chase indirects, recursively */
  for(n = 1; n < 4; n++) {
    if(*ap && (ret = iblock(*ap,n,flg)) & STOP) {
      if(flg != BBLK)
        return(ret);
    }
    ap++;
  }
  return(KEEPON);
}

/* chase down an indirect block. */
int iblock( s4_daddr blk, int ilevel, int flg)
{
  register s4_daddr *ap;
  register int n;
  visitblk  bfunc = nobfunc;

  BUFAREA ib;

  if(flg == BBLK) {
    bfunc = chkblk;
  }
  else if(flg == ADDR) {
    bfunc = bpfunc;
    if(((n = (*bfunc)(blk,0)) & KEEPON) == 0)
        return(n);
  }
  else
    bfunc = dirscan;

  if(outrange(blk))		/* protect thyself */
      return(SKIP);

  initbarea(&ib);
  if(getblk(&ib,blk) == NULL)
    return(SKIP);

  /* do we know it is an index? */
  bset(&ib,s4b_idx);

  /* now, for all the blocks in the indir, go deeper */
  ilevel--;
  for(ap = ib.b_un.b_indir; ap < &ib.b_un.b_indir[S4_NINDIR]; ap++) {
    if(*ap) {
      if(ilevel > 0) 
        n = iblock(*ap,ilevel,flg); /* recurse */
      else 
        n = (*bfunc)(*ap,0);

      if(n & STOP && flg != BBLK)
          return(n);
    }
  }
  return(KEEPON);
}


int chkblk(int blk, int flg)
{
  register DIRECT *dirp;
  register char *ptr;
  int zerobyte, baddir = 0, dotcnt = 0;

  if(outrange(blk))
    return(SKIP);
  if(getblk(&fileblk, blk) == NULL)
    return(SKIP);
  bset(&fileblk,s4b_dir);
  for(dirp = dirblk; dirp <&dirblk[S4_NDIRECT]; dirp++) {
    ptr = dirp->d_name;
    zerobyte = 0;
    while(ptr <&dirp->d_name[S4_DIRSIZ]) {
      if(zerobyte && *ptr) {
        baddir++;
        break;
      }
      if(flg) {
        if(ptr == &dirp->d_name[0] && *ptr == '.' &&
           *(ptr + 1) == '\0') {
          dotcnt++;
          if(inum != dirp->d_ino) {
            printf("%c %sNO VALID '.' in DIR I = %u\n",
                   id,devname,inum);
            baddir++;
          }
          break;
        }
        if(ptr == &dirp->d_name[0] && *ptr == '.' &&
           *(ptr + 1) == '.' && *(ptr + 2) == '\0') {
          dotcnt++;
          if(!dirp->d_ino) {
            printf("%c %sNO VALID '..' in DIR I = %u\n",
                   id,devname,inum);
            baddir++;
          }
          break;
        }
      }
      if(*ptr == '/') {
        baddir++;
        break;
      }
      if(*ptr == 0) {
        if(dirp->d_ino && ptr == &dirp->d_name[0]) {
          baddir++;
          break;
        }
        else
          zerobyte++;
      }
      ptr++;
    }
  }
  if(flg && dotcnt < 2) {
    printf("%c %sMISSING '.' or '..' in DIR I = %u\n",id,devname,inum);
    printf("%c %sBLK %ld ",id,devname,(long)blk);
    pinode();
    printf("\n%c %sDIR=%s\n\n",id,devname,pathname);
    return(YES);
  }
  if(baddir) {
    printf("%c %sBAD DIR ENTRY I = %u\n",id,devname,inum);
    printf("%c %sBLK %ld ",id,devname,(long)blk);
    pinode();
    printf("\n%c %sDIR=%s\n\n",id,devname,pathname);
    return(YES);
  }
  return(KEEPON);
}

int pass1(s4_daddr blk, int flg)
{
  register s4_daddr *dlp;

  if(outrange(blk)) {
    blkerr("BAD",blk);
    if(++badblk >= MAXBAD) {
      printf("%c %sEXCESSIVE BAD BLKS I=%u",id,devname,inum);
      if(reply("CONTINUE") == NO)
        errexit0("\n");
      return(STOP);
    }
    return(SKIP);
  }
  if(getbmap(blk)) {
    blkerr("DUP",blk);
    if(++dupblk >= MAXDUP) {
      printf("%c %sEXCESSIVE DUP BLKS I=%u",id,devname,inum);
      if(reply("CONTINUE") == NO)
        errexit0("\n");
      return(STOP);
    }
    if(enddup >= &duplist[DUPTBLSIZE]) {
      printf("%c %sDUP TABLE OVERFLOW.",id,devname);
      if(reply("CONTINUE") == NO)
        errexit0("\n");
      return(STOP);
    }
    for(dlp = duplist; dlp < muldup; dlp++) {
      if(*dlp == blk) {
        *enddup++ = blk;
        break;
      }
    }
    if(dlp >= muldup) {
      *enddup++ = *muldup;
      *muldup++ = blk;
    }
  }
  else {
    n_blks++;
    setbmap(blk); 
    /*		*savep |= saven;*/
  }
  filsize++;
  return(KEEPON);
}

int pass1b(s4_daddr blk, int flg)
{
  register s4_daddr *dlp;

  if(outrange(blk))
    return(SKIP);
  for(dlp = duplist; dlp < muldup; dlp++) {
    if(*dlp == blk) {
      blkerr("DUP",blk);
      *dlp = *--muldup;
      *muldup = blk;
      return(muldup == duplist ? STOP : KEEPON);
    }
  }
  return(KEEPON);
}


int pass2(DIRECT *dirp, BUFAREA *bp)
{
  register char *p;
  register int n;
  register DINODE *dp;

  btomem(bp);
  if((inum = dirp->d_ino) == 0)
    return(KEEPON);
  thisname = pathp;
  if((&pathname[MAXPATH] - pathp) < S4_DIRSIZ) {
    if((&pathname[MAXPATH] - pathp) < strlen(dirp->d_name)) {
      printf("%c %sDIR pathname too deep\n",id,devname);
      printf("%c %sIncrease MAXPATH and recompile.\n",
             id,devname);
      printf("%c %sDIR pathname is <%s>\n",
             id,devname,pathname);
      ckfini();
      exit(4);
    }
  }
  for(p = dirp->d_name; p < &dirp->d_name[S4_DIRSIZ]; )
    if((*pathp++ = *p++) == 0) {
      --pathp;
      break;
    }
  *pathp = 0;
  n = NO;
  if(inum > imax || inum < S4_ROOTINO)
    n = direrr("I OUT OF RANGE");
  else {
  again:
    switch(getstate()) {
    case USTATE:
      n = direrr("UNALLOCATED");
      break;
    case CLEAR:
      if((n = direrr("DUP/BAD")) == YES)
        break;
      if((dp = ginode()) == NULL)
        break;
      setstate(DIR ? DSTATE : FSTATE);
      goto again;
    case FSTATE:
      declncnt();
      break;
    case DSTATE:
      declncnt();
      descend();
    }
  }
  pathp = thisname;
  if(n == NO)
    return(KEEPON);
  dirp->d_ino = 0;
  return(KEEPON|ALTERD);
}


int pass4( s4_daddr blk, int flg )
{
  register s4_daddr *dlp;

  if(outrange(blk))
    return(SKIP);
  if(getbmap(blk)) {
    for(dlp = duplist; dlp < enddup; dlp++)
      if(*dlp == blk) {
        *dlp = *--enddup;
        return(KEEPON);
      }
    clrbmap(blk);
    n_blks--;
  }
  return(KEEPON);
}


/* march through freelist block(s), recursively. */
int pass5(s4_daddr blk, int flg )
{

  if(outrange(blk)) {
    fixfree = 1;
    if(++badblk >= MAXBAD) {
      printf("%c %sEXCESSIVE BAD BLKS IN FREE LIST.",id,devname);
      if(reply("CONTINUE") == NO)
        errexit0("\n");
      return(STOP);
    }
    return(SKIP);
  }
  if(getfmap(blk)) {
    fixfree = 1;
    if(++dupblk >= DUPTBLSIZE) {
      printf("%c %sEXCESSIVE DUP BLKS IN FREE LIST.",id,devname);
      if(reply("CONTINUE") == NO)
        errexit0("\n");
      return(STOP);
    }
  }
  else {
    n_free++;
    setfmap(blk); 
    /*		*savep |= saven;*/
  }

  return(KEEPON);
}


void blkerr(char *s, s4_daddr blk )
{
  printf("%c %s%ld %s I=%u\n",id,devname,(long)blk,s,inum);
  setstate(CLEAR);	/* mark for possible clearing */
}


void descend(void)
{
  register DINODE *dp;
  register char *savname;
  s4_off savsize;

  setstate(FSTATE);
  if((dp = ginode()) == NULL)
    return;
  if(Dirc && !pss2done)
    ckinode(dp,BBLK);
  savname = thisname;
  *pathp++ = '/';
  savsize = filsize;
  filsize = dp->di_size;
  ckinode(dp,DATA);
  thisname = savname;
  *--pathp = 0;
  filsize = savsize;
}


int dirscan(s4_daddr blk, int flg)
{
  register DIRECT *dirp;
  register char *p1, *p2;
  register int n;
  DIRECT direntry;

  if(dbgflag)
    printf("dirscan blk %d\n", blk );

  if(outrange(blk)) {
    filsize -= S4_BSIZE;
    return(SKIP);
  }
  bset(&fileblk,s4b_dir);
  for(dirp = dirblk; dirp < &dirblk[S4_NDIRECT] &&
        filsize > 0; dirp++, filsize -= sizeof(DIRECT)) {

    bset(&fileblk,s4b_dir);
    if(getblk(&fileblk,blk) == NULL) {
      filsize -= (&dirblk[S4_NDIRECT]-dirp)*sizeof(DIRECT);
      return(SKIP);
    }
    bset(&fileblk,s4b_dir);
    p1 = &dirp->d_name[S4_DIRSIZ];
    p2 = &direntry.d_name[S4_DIRSIZ];
    while(p1 > (char *)dirp)
      *--p2 = *--p1;

    if((n = (*dpfunc)(&direntry,&fileblk)) & ALTERD) {
      if(getblk(&fileblk,blk) != NULL) {
        p1 = &dirp->d_name[S4_DIRSIZ];
        p2 = &direntry.d_name[S4_DIRSIZ];
        while(p1 > (char *)dirp)
          *--p1 = *--p2;
        fbdirty();
      }
      else
        n &= ~ALTERD;

      bset(&fileblk,s4b_dir);
    }
    if(n & STOP)
      return(n);
  }
  return(filsize > 0 ? KEEPON : STOP);
}


int direrr( char *s )
{
  register DINODE *dp;
  int n;

  printf("%c %s%s ",id,devname,s);
  pinode();
  if((dp = ginode()) != NULL && ftypeok(dp)) {
    printf("\n%c %s%s=%s",id,devname,DIR?"DIR":"FILE",pathname);
    if(DIR) {
      if(dp->di_size > EMPT) {
        if((n = chkempt(dp)) == NO) {
          printf(" (NOT EMPTY)\n");
        }
        else if(n != SKIP) {
          printf(" (EMPTY)");
          if(!nflag) {
            printf(" -- REMOVED\n");
            return(YES);
          }
          else
            printf("\n");
        }
      }
      else {
        printf(" (EMPTY)");
        if(!nflag) {
          printf(" -- REMOVED\n");
          return(YES);
        }
        else
          printf("\n");
      }
    }
    else if(REG)
      if(!dp->di_size) {
        printf(" (EMPTY)");
        if(!nflag) {
          printf(" -- REMOVED\n");
          return(YES);
        }
        else
          printf("\n");
      }
  }
  else {
    printf("\n%c %sNAME=%s",id,devname,pathname);
    if(!dp->di_size) {
      printf(" (EMPTY)");
      if(!nflag) {
        printf(" -- REMOVED\n");
        return(YES);
      }
      else
        printf("\n");
    }
    else
      printf(" (NOT EMPTY)\n");
  }
  return(reply("REMOVE"));
}


void adjust( short lcnt )
{
  register DINODE *dp;
  register int n;

  if((dp = ginode()) == NULL)
    return;
  if(dp->di_nlink == lcnt) {
    if((n = linkup()) == NO)
      clri("UNREF",NO);
    if(n == REM)
      clri("UNREF",REM);
  }
  else {
    printf("%c %sLINK COUNT %s",id,devname,
           (lfdir==inum)?lfname:(DIR?"DIR":"FILE"));
    pinode();
    printf("\n%c %sCOUNT %d SHOULD BE %d",id,devname,
           dp->di_nlink,dp->di_nlink-lcnt);
    if(reply("ADJUST") == YES) {
      dp->di_nlink -= lcnt;
      inodirty();
    }
  }
}


void clri( char *s, int flg)
{
  register DINODE *dp;
  int n;

  if((dp = ginode()) == NULL)
    return;
  if(flg == YES) {
    if(!FIFO || !qflag || nflag) {
      printf("%c %s%s %s",id,devname,s,DIR?"DIR":"FILE");
      pinode();
    }
    if(DIR) {
      if(dp->di_size > EMPT) {
        if((n = chkempt(dp)) == NO) {
          printf(" (NOT EMPTY)\n");
        }
        else if(n != SKIP) {
          printf(" (EMPTY)");
          if(!nflag) {
            printf(" -- REMOVED\n");
            clrinode(dp);
            return;
          }
          else
            printf("\n");
        }
      }
      else {
        printf(" (EMPTY)");
        if(!nflag) {
          printf(" -- REMOVED\n");
          clrinode(dp);
          return;
        }
        else
          printf("\n");
      }
    }
    if(REG) {
      if(!dp->di_size) {
        printf(" (EMPTY)");
        if(!nflag) {
          printf(" -- REMOVED\n");
          clrinode(dp);
          return;
        }
        else
          printf("\n");
      }
      else
        printf(" (NOT EMPTY)\n");
    }
    if (FIFO && !nflag) {
      if(!qflag)	printf(" -- CLEARED");
      printf("\n");
      clrinode(dp);
      return;
    }
  }
  if(flg == REM)	clrinode(dp);
  else if(reply("CLEAR") == YES)
    clrinode(dp);
}


/* quietly clear inode */
void clrinode( DINODE *dp )
{

  n_files--;
  bpfunc = pass4;
  ckinode(dp,ADDR);
  zapino(dp);
  inodirty();
}

int chkempt(DINODE *dp )
{
  register s4_daddr *ap;
  register DIRECT *dirp;
  s4_daddr blk[S4_NADDR];
  int size;

  size = minsz(dp->di_size, (S4_NADDR - 3) * S4_BSIZE);

  if( doswap )
    s4l3tolr(blk,dp->di_addr,S4_NADDR);
  else
    s4l3tol(blk,dp->di_addr,S4_NADDR);
  for(ap = blk; ap < &blk[S4_NADDR - 3] && size > 0; ap++) {
    if(*ap) {
      if(outrange(*ap)) {
        printf("chkempt: blk %d out of range\n",*ap);
        return(SKIP);
      }
      if(getblk(&fileblk,*ap) == NULL) {
        printf("chkempt: Can't find blk %d\n",*ap);
        return(SKIP);
      }
      bset(&fileblk,s4b_dir);
      for(dirp=dirblk; dirp < &dirblk[S4_NDIRECT] &&
            size > 0; dirp++) {
        if(dirp->d_name[0] == '.' &&
           (dirp->d_name[1] == '\0' || (
                                        dirp->d_name[1] == '.' &&
                                        dirp->d_name[2] == '\0'))) {
          size -= sizeof(DIRECT);
          continue;
        }
        if(dirp->d_ino)
          return(NO);
        size -= sizeof(DIRECT);
      }
    }
  }
  if(size <= 0)	return(YES);
  else	return(NO);
}

int setup( char *dev )
{
  register int n;
  register BUFAREA *bp;
  register MEMSIZE msize;
  char *mbase;
  s4_daddr bcnt, nscrblk;
  s4_dev rootdev;
  extern s4_dev pipedev;	/* non-zero iff standard input is a pipe,
				 * which means we can't check pipedev */
  s4_off smapsz, lncntsz, totsz;

  struct statfs statfsarea;

  struct stat statarea;

  if(stat("/",&statarea) < 0)
    errexit2("%c %sCan't stat root\n",id,devname);
  rootdev = statarea.st_dev;
  if(stat(dev,&statarea) < 0) {
    error3("%c %sCan't stat %s\n",id,devname,dev);
    return(NO);
  }
  hotroot = 0;
  rawflg = 0;
  if((statarea.st_mode & S_IFMT) == S_IFBLK) {
    if(rootdev == statarea.st_rdev)
      hotroot++;
    else if(statfs(dev,&statfsarea) >= 0) {
      if(!nflag) {
        error3("%c %s%s is a mounted file system, ignored\n",
              id,devname,dev);
        return(NO);
      }
      hotroot++;
    }
    if ( pipedev == statarea.st_rdev ) {
      error3( "%c %s%s is pipedev, ignored", id,
               devname, dev);
        return(NO);
      }
  }
  else if((statarea.st_mode & S_IFMT) == S_IFCHR)
    rawflg++;
#if 0
  else {
    error("%c %s%s is not a block or character device\n",id,devname,dev);
    return(NO);
  }
#endif
  printf("\n%c %s",id,dev);
  if((nflag && !csflag) || (dfile.wfdes == -1))
    printf(" (NO WRITE)");
  printf("\n");
  pss2done = 0;
  fixfree = 0;
  dfile.mod = 0;
  n_files = n_blks = n_free = 0;
  muldup = enddup = &duplist[0];
  badlnp = &badlncnt[0];
  lfdir = 0;
  rplyflag = 0;
  initbarea(&fileblk);
  initbarea(&inoblk);
  sfile.wfdes = sfile.rfdes = -1;
  rmscr = 0;
  if(getblk(&sblk,S4_SUPERB) == NULL) {
    ckfini();
    return(NO);
  }
  bset(&sblk,s4b_super);
  imax = ((s4_ino)superblk.s_isize - (S4_SUPERB+1)) * S4_INOPB;
  f_max = superblk.s_fsize;		/* first invalid blk num */
  f_min = (s4_daddr)superblk.s_isize;
  bmapsz = roundup(howmany(f_max,BITSPB),sizeof(*lncntp));

  if(f_min >= f_max || 
     (imax/S4_INOPB) != ((s4_ino)superblk.s_isize-(S4_SUPERB+1))) {
    error4("%c %sSize check: fsize %ld isize %d\n",id,devname,
           (long)superblk.s_fsize,superblk.s_isize);
    ckfini();
    return(NO);
  }
  printf("%c %sFile System: %.6s Volume: %.6s\n\n",id,devname,
         superblk.s_fname,superblk.s_fpack);

  smapsz = roundup(howmany((long)(imax+1),STATEPB),sizeof(*lncntp));
  lncntsz = (long)(imax+1) * sizeof(*lncntp);
  if(bmapsz > smapsz+lncntsz)
    smapsz = bmapsz-lncntsz;
  totsz = bmapsz+smapsz+lncntsz;
  msize = memsize;
  mbase = membase;
  if(rawflg) {
    if(msize < (MEMSIZE)(NINOBLK*S4_BSIZE) + 2*sizeof(BUFAREA))
      rawflg = 0;
    else {
      msize -= (MEMSIZE)NINOBLK*S4_BSIZE;
      mbase += (MEMSIZE)NINOBLK*S4_BSIZE;
      niblk = NINOBLK;
      startib = f_max;
    }
  }
  clear(mbase,msize);
  if((s4_off)msize < totsz) {
    bmapsz = roundup(bmapsz,S4_BSIZE);
    smapsz = roundup(smapsz,S4_BSIZE);
    lncntsz = roundup(lncntsz,S4_BSIZE);
    nscrblk = (bmapsz+smapsz+lncntsz)>>S4_BSHIFT;
    if(tflag == 0) {
      printf("\n%c %sNEED SCRATCH FILE (%ld BLKS)\n",
             id,devname,(long)nscrblk);
      do {
        printf("%c %sENTER FILENAME:\n",id,devname);
        if((n = fsck_getline(stdin,scrfile,sizeof(scrfile))) == EOF)
          errexit0("\n");
      } while(n == 0);
    }
    if(stat(scrfile,&statarea) < 0 ||
       (statarea.st_mode & S_IFMT) == S_IFREG)
      rmscr++;
    if((sfile.wfdes = creat(scrfile,0666)) < 0 ||
       (sfile.rfdes = open(scrfile,0)) < 0) {
      error3("%c %sCan't create %s\n",id,devname,scrfile);
      ckfini();
      return(NO);
    }
    bp = &((BUFAREA *)mbase)[(msize/sizeof(BUFAREA))];
    poolhead = NULL;
    while(--bp >= (BUFAREA *)mbase) {
      initbarea(bp);
      bp->b_next = poolhead;
      poolhead = bp;
    }
    bp = poolhead;
    for(bcnt = 0; bcnt < nscrblk; bcnt++) {
      bp->b_bno = bcnt;
      dirty(bp);
      flush(&sfile,bp);
    }
    blkmap = freemap = statemap = (char *) NULL;
    lncntp = (short *) NULL;
    smapblk = bmapsz / S4_BSIZE;
    lncntblk = smapblk + smapsz / S4_BSIZE;
    fmapblk = smapblk;
  }
  else {
    if(rawflg && (s4_off)msize > totsz+S4_BSIZE) {
      niblk += (unsigned)((s4_off)msize-totsz)>>S4_BSHIFT;
#if S4_FsTYPE==2
      if(niblk > MAXRAW / 2)
        niblk = MAXRAW / 2;
#else
      if(niblk > MAXRAW)
        niblk = MAXRAW;
#endif
      msize = memsize - (niblk*S4_BSIZE);
      mbase = membase + (niblk*S4_BSIZE);
    }
    poolhead = NULL;
    blkmap = mbase;
    statemap = &mbase[(MEMSIZE)bmapsz];
    freemap = statemap;
    lncntp = (short *)&statemap[(MEMSIZE)smapsz];
  }
  return(YES);
}

int checksb(char *dev)
{
  if((dfile.rfdes = open(dev,0)) < 0) {
    error3("%c %sCan't open %s\n",id,devname,dev);
    return(NO);
  }
  if((dfile.wfdes = open(dev,1)) < 0)
    dfile.wfdes = -1;
  if(getblk(&sblk,S4_SUPERB) == NULL) {
    ckfini();
    return(NO);
  }

  /* dbrower -- identify swapped FS here */
  bset(&sblk,s4b_super);
  if(superblk.s_magic == s4swapi(S4_FsMAGIC) )
    {
      printf("%c %s is a byte-swapped filesystem\n", id, dev);
      doswap = YES;
    }
  btomem(&sblk);

  return(YES);
}

DINODE *ginode(void)
{
  register DINODE *dp;
  register char *mbase;
  register s4_daddr iblk;

  if(inum > imax)
    return(NULL);
  iblk = itod(inum);
  if(rawflg) {
    mbase = membase;
    if(iblk < startib || iblk >= startib+niblk) {
      if(inoblk.b_dirty)
        bwrite(&dfile,mbase,startib,niblk*S4_BSIZE);
      inoblk.b_dirty = 0;
      if(bread(&dfile,mbase,iblk,niblk*S4_BSIZE) == NO) {
        startib = f_max;
        return(NULL);
      }
      startib = iblk;
    }
    dp = (DINODE *)&mbase[(unsigned)((iblk-startib)<<S4_BSHIFT)];
  }
  else if(getblk(&inoblk,iblk) != NULL)
    {
      dp = inoblk.b_un.b_dinode;
      bset(&inoblk,s4b_ino);
    }
  else
    return(NULL);
  return(dp + itoo(inum));
}

int reply( char *s )
{
  char line[80];

  rplyflag = 1;
  line[0] = '\0';
  printf("\n%c %s%s? ",id,devname,s);
  if(nflag || dfile.wfdes < 0) {
    printf(" no\n\n");
    return(NO);
  }
  if(yflag) {
    printf(" yes\n\n");
    return(YES);
  }
  while (line[0] == '\0') {
    if(fsck_getline(stdin,line,sizeof(line)) == EOF)
      errexit0("\n");
    printf("\n");
    if(line[0] == 'y' || line[0] == 'Y')
      return(YES);
    if(line[0] == 'n' || line[0] == 'N')
      return(NO);
    printf("%c %sAnswer 'y' or 'n' (yes or no)\n",id,devname);
    line[0] = '\0';
  }
  return(NO);
}


int fsck_getline(FILE *fp, char *loc, int maxlen)
{
  register int n;
  register char *p, *lastloc;

  p = loc;
  lastloc = &p[maxlen-1];
  while((n = getc(fp)) != '\n') {
    if(n == EOF)
      return(EOF);
    if(!isspace(n) && p < lastloc)
      *p++ = n;
  }
  *p = 0;
  return(p - loc);
}


int getno(FILE *fp)
{
  register int n;
  register int cnt;

  cnt = 0;
  while((n = getc(fp)) != EOF) {
    if(n == '\n')
      cnt++;
  }
  return(cnt);
}


void stype( char *p)
{
  if(*p == 0)
    return;
  if (*(p+1) == 0) {
    if (*p == '3') {
      cylsize = 200;
      stepsize = 5;
      return;
    }
    if (*p == '4') {
      cylsize = 418;
      stepsize = 7;
      return;
    }
  }
  cylsize = atoi(p);
  while(*p && *p != ':')
    p++;
  if(*p)
    p++;
  stepsize = atoi(p);
  if(stepsize <= 0 || stepsize > cylsize ||
     cylsize <= 0 || cylsize > MAXCYL) {
    error2("%c %sInvalid -s argument, defaults assumed\n",id,devname);
    cylsize = stepsize = 0;
  }
}


int dostate(int statebit, int noset )
{
  register char *p;
  register unsigned byte, shift;
  BUFAREA *bp;

  byte = ((unsigned)inum)/STATEPB;
  shift = LSTATE * (((unsigned)inum)%STATEPB);
  if(statemap != NULL) {
    bp = NULL;
    p = &statemap[byte];
  }
  else if((bp = getblk(NULL,smapblk+(byte/S4_BSIZE))) == NULL) {
    errexit2("%c %sFatal I/O error\n",id,devname);
  }
  else {
    p = &bp->b_un.b_buf[byte%S4_BSIZE];
  }
  switch(noset) {
  case 0:
    *p &= ~(SMASK<<(shift));
    *p |= statebit<<(shift);
    if(bp != NULL)
      dirty(bp);
    return(statebit);
  case 1:
    return((*p>>(shift)) & SMASK);
  }
  return(USTATE);
}


int domap(s4_daddr blk, int flg)
{
  register char *p;
  register unsigned n;
  register BUFAREA *bp;
  s4_off byte;

  byte = blk >> BITSHIFT;
  n = 1<<((unsigned)(blk & BITMASK));
  if(flg & 04) {
    p = freemap;
    blk = fmapblk;
  }
  else {
    p = blkmap;
    blk = 0;
  }
  if(p != NULL) {
    bp = NULL;
    p += (unsigned)byte;
  }
  else if((bp = getblk(NULL,blk+(byte>>S4_BSHIFT))) == NULL) {
    errexit2("%c %sFatal I/O error\n",id,devname);
  }
  else {
    p = &bp->b_un.b_buf[(unsigned)(byte&S4_BMASK)];
  /*	saven = n;
	savep = p;*/
  }

  switch(flg&03) {
  case 0: /* set */
    *p |= n;
    break;
  case 1: /* get */
    n &= *p;
    bp = NULL;
    break;
  case 2: /* clear */
    *p &= ~n;
  }
  if(bp != NULL)
    dirty(bp);
  return(n);
}


int dolncnt(short val,int flg)
{
  register short *sp;
  register BUFAREA *bp;

  if(lncntp != NULL) {
    bp = NULL;
    sp = &lncntp[(unsigned)inum];
  }
  else if((bp = getblk(NULL,lncntblk+((unsigned)inum/SPERB))) == NULL) {
    errexit2("%c %sFatal I/O error\n",id,devname);
  }
  else {
      bset(bp,s4b_linkcnt);
      sp = &bp->b_un.b_lnks[(unsigned)inum%SPERB];
  }
  switch(flg) {
  case 0:
    *sp = val;
    break;
  case 1:
    bp = NULL;
    break;
  case 2:
    (*sp)--;
  }
  if(bp != NULL)
    dirty(bp);
  return(*sp);
}


BUFAREA *
getblk( BUFAREA *bp, int blk )
{
  register struct filecntl *fcp;

  if(bp == NULL) {
    bp = search(blk);
    fcp = &sfile;
  }
  else
    fcp = &dfile;

  if(bp->b_bno == blk)
    {
      btomem(bp);
      if(dbgflag) printf("getblk had blk %d\n", blk );      
      return(bp);
    }
  if(blk == S4_SUPERB) {
    bset(bp,s4b_super);
    flush(fcp,bp);
    if(lseek(fcp->rfdes,(long)S4_SUPERBOFF,0) < 0)
      rwerr("SEEK",blk);
    else if(read(fcp->rfdes,bp->b_un.b_buf,SBSIZE) == SBSIZE) {
      bp->b_bno = blk;
      btomem(bp);
      if(dbgflag) printf("getblk read blk %d\n", blk );      
      return(bp);
    }
    rwerr("READ",blk);
    bp->b_bno = (s4_daddr)-1;
    return(NULL);
  }
  flush(fcp,bp);
  if(bread(fcp,bp->b_un.b_buf,blk,S4_BSIZE) != NO) {
    bp->b_bno = blk;
    bp->b_swapped = NO;                  
    if(dbgflag) printf("getblk read blk %d\n", blk );      
    return(bp);
  }
  bp->b_bno = (s4_daddr)-1;
  if(dbgflag) printf("getblk no blk %d\n", blk );      
  return(NULL);
}


void flush(struct filecntl *fcp, BUFAREA *bp )
{
  if(bp->b_dirty) {
    if(bp->b_bno == S4_SUPERB) {
      btodisk(bp);
      if(fcp->wfdes < 0) {
        bp->b_dirty = 0;
        return;
      }
      if(lseek(fcp->wfdes,(long)S4_SUPERBOFF,0) < 0)
        rwerr("SEEK",bp->b_bno);
      else if(write(fcp->wfdes,bp->b_un.b_buf,SBSIZE) == SBSIZE) {
        fcp->mod = 1;
        bp->b_dirty = 0;
        return;
      }
      rwerr("WRITE",S4_SUPERB);
      bp->b_dirty = 0;
      btomem(bp);
      return;
    }
    btodisk(bp);
    bwrite(fcp,bp->b_un.b_buf,bp->b_bno,S4_BSIZE);
    btomem(bp);
  }
  bp->b_dirty = 0;
}

void rwerr(char *s, s4_daddr blk)
{
  printf("\n%c %sCAN NOT %s: BLK %ld",id,devname,s,(long)blk);
  if(reply("CONTINUE") == NO)
    errexit2("%c %sProgram terminated\n",id,devname);
}


void sizechk(DINODE *dp)
{
  s4_off size, nblks;

  {
    size = howmany(dp->di_size,S4_BSIZE);
    nblks = size;
    size -= S4_NADDR-3;
    while(size > 0) {
      nblks += howmany(size,S4_NINDIR);
      size--;
      size /= S4_NINDIR;
    }
  }
  if(!qflag) {
    if(nblks != filsize)
      printf("%c %sPOSSIBLE %s SIZE ERROR I=%u\n\n",
             id,devname,DIR?"DIR":"FILE",inum);
    if(DIR && (dp->di_size % sizeof(DIRECT)) != 0)
      printf("%c %sDIRECTORY MISALIGNED I=%u\n\n",id,devname,inum);
  }
}


void ckfini(void)
{
  flush(&dfile,&fileblk);
  flush(&dfile,&sblk);
  flush(&dfile,&inoblk);
  if( dfile.rfdes > 0 )
    close(dfile.rfdes);
  if( dfile.wfdes > 0 )
    close(dfile.wfdes);
  if( sfile.rfdes > 0 )
    close(sfile.rfdes);
  if( sfile.wfdes > 0 )
    close(sfile.wfdes);

  if(rmscr) {
    unlink(scrfile);
  }

}


void pinode(void)
{
  register DINODE *dp;
  register char *p;
  char *ctime();
  time_t t;
  struct passwd *pwd;

  printf(" I=%u ",inum);
  if((dp = ginode()) == NULL)
    return;
  printf(" OWNER=");

  pwd = getpwuid( dp->di_uid );
  if( pwd )
    printf("%s ", pwd->pw_name );
  else
    printf("%d ", dp->di_uid);

  printf("MODE=%o\n",dp->di_mode);
  printf("%c %sSIZE=%ld ",id,devname,(long)dp->di_size);
  t = dp->di_mtime;
  p = ctime(&t);
  printf("MTIME=%12.12s %4.4s ",p+4,p+20);
}




void freechk(void)
{
  register s4_daddr *ap;

  bset(&fileblk,s4b_free);
  if(freeblk.df_nfree == 0)
      return;

  do {

    bset(&fileblk,s4b_free);
    if(freeblk.df_nfree <= 0 || freeblk.df_nfree > S4_NICFREE) {
      printf("%c %sBAD FREEBLK COUNT\n",id,devname);
      fixfree = 1;
      return;
    }

    /* march through blocks in superclock cache */
    ap = &freeblk.df_free[freeblk.df_nfree];
    while(--ap > &freeblk.df_free[0]) {
      if(pass5(*ap,0) == STOP)
        return;
    }
    if(*ap == (s4_daddr)0 || pass5(*ap,0) != KEEPON)
        return;

  } while(getblk(&fileblk,*ap) != NULL);
}


void makefree(void)
{
  register int i, cyl, step;
  int j;
  char flg[MAXCYL];
  short addr[MAXCYL];
  s4_daddr blk, baseblk;

  superblk.s_nfree = 0;
  superblk.s_flock = 0;
  superblk.s_fmod = 0;
  superblk.s_tfree = 0;
  superblk.s_ninode = 0;
  superblk.s_ilock = 0;
  superblk.s_ronly = 0;
  if(cylsize == 0 || stepsize == 0) {
    step = superblk.s_vinfo[0];
    cyl = superblk.s_vinfo[1];
  }
  else {
    step = stepsize;
    cyl = cylsize;
  }
  if(step > cyl || step <= 0 || cyl <= 0 || cyl > MAXCYL) {
    error2("%c %sDefault free list spacing assumed\n",id,devname);
    step = STEPSIZE;
    cyl = CYLSIZE;
  }
  superblk.s_vinfo[0] = step;
  superblk.s_vinfo[1] = cyl;
  clear(flg,sizeof(flg));
#if S4_FsTYPE==2
  step /= 2;
  cyl /= 2;
#endif
  i = 0;
  for(j = 0; j < cyl; j++) {
    while(flg[i])
      i = (i + 1) % cyl;
    addr[j] = i + 1;
    flg[i]++;
    i = (i + step) % cyl;
  }
  baseblk = (s4_daddr)roundup(f_max,cyl);
  bclear(&fileblk);
  bset(&fileblk,s4b_free);
  freeblk.df_nfree++;
  for( ; baseblk > 0; baseblk -= cyl)
    for(i = 0; i < cyl; i++) {
      blk = baseblk - addr[i];
      if(!outrange(blk) && !getbmap(blk)) {
        superblk.s_tfree++;
        if(freeblk.df_nfree >= S4_NICFREE) {
          fbdirty();
          fileblk.b_bno = blk;
          bset(&fileblk,s4b_free);
          flush(&dfile,&fileblk);
	  freeblk.df_nfree = 0;
          memset(freeblk.df_free, 0, sizeof(freeblk.df_free));
        }
        freeblk.df_free[freeblk.df_nfree] = blk;
        freeblk.df_nfree++;
      }
    }
  superblk.s_nfree = freeblk.df_nfree;
  for(i = 0; i < S4_NICFREE; i++)
    superblk.s_free[i] = freeblk.df_free[i];
  sbdirty();
}



BUFAREA *
search(s4_daddr blk)
{
  BUFAREA *pbp, *bp;

  pbp = NULL;

  for(bp = (BUFAREA *) &poolhead; bp->b_next; ) {
    pbp = bp;
    bp = pbp->b_next;
    if(bp->b_bno == blk)
      break;
  }
  pbp->b_next = bp->b_next;
  bp->b_next = poolhead;
  poolhead = bp;
  return(bp);
}


int findino(DIRECT *dirp, BUFAREA *bp)
{
  register char *p1, *p2;

  btomem( bp );

  if(dirp->d_ino == 0)
    return(KEEPON);
  for(p1 = dirp->d_name,p2 = srchname;*p2++ == *p1; p1++) {
    if(*p1 == 0 || p1 == &dirp->d_name[S4_DIRSIZ-1]) {
      if(dirp->d_ino >= S4_ROOTINO && dirp->d_ino <= imax)
        parentdir = dirp->d_ino;
      return(STOP);
    }
  }
  return(KEEPON);
}


int mkentry( DIRECT *dirp, BUFAREA *bp)
{
  register s4_ino in;
  register char *p;

  btomem( bp );

  if(dirp->d_ino)
    return(KEEPON);
  dirp->d_ino = orphan;
  in = orphan;
  p = &dirp->d_name[S4_DIRSIZ];
  while(p != &dirp->d_name[6])
    *--p = 0;
  while(p > dirp->d_name) {
    *--p = (in % 10) + '0';
    in /= 10;
  }
  return(ALTERD|STOP);
}


int chgdd(DIRECT *dirp, BUFAREA *bp)
{
  btomem( bp );

  if(dirp->d_name[0] == '.' && dirp->d_name[1] == '.' &&
     dirp->d_name[2] == 0) {
    dirp->d_ino = lfdir;
    return(ALTERD|STOP);
  }
  return(KEEPON);
}


int linkup(void)
{
  register DINODE *dp;
  register int lostdir;
  register s4_ino pdir;
  register s4_ino *blp;
  int n;

  if((dp = ginode()) == NULL)
    return(NO);
  lostdir = DIR;
  pdir = parentdir;
  if(!FIFO || !qflag || nflag) {
    printf("%c %sUNREF %s ",id,devname,lostdir ? "DIR" : "FILE");
    pinode();
  }
  if(DIR) {
    if(dp->di_size > EMPT) {
      if((n = chkempt(dp)) == NO) {
        printf(" (NOT EMPTY)");
        if(!nflag) {
          printf(" MUST reconnect\n");
          goto connect;
        }
        else
          printf("\n");
      }
      else if(n != SKIP) {
        printf(" (EMPTY)");
        if(!nflag) {
          printf(" Cleared\n");
          return(REM);
        }
        else
          printf("\n");
      }
    }
    else {
      printf(" (EMPTY)");
      if(!nflag) {
        printf(" Cleared\n");
        return(REM);
      }
      else
        printf("\n");
    }
  }
  if(REG) {
    if(!dp->di_size) {
      printf(" (EMPTY)");
      if(!nflag) {
        printf(" Cleared\n");
        return(REM);
      }
      else
        printf("\n");
    }
    else
      printf(" (NOT EMPTY)\n");
  }
  if(FIFO && !nflag) {
    if(!qflag)	printf(" -- REMOVED");
    printf("\n");
    return(REM);
  }
  if(FIFO && nflag)
    return(NO);
  if(reply("RECONNECT") == NO)
    return(NO);
 connect:
  orphan = inum;
  if(lfdir == 0) {
    inum = S4_ROOTINO;
    if((dp = ginode()) == NULL) {
      inum = orphan;
      return(NO);
    }
    dpfunc = findino;
    srchname = lfname;
    filsize = dp->di_size;
    parentdir = 0;
    ckinode(dp,DATA);
    inum = orphan;
    if((lfdir = parentdir) == 0) {
      printf("%c %sSORRY. NO lost+found DIRECTORY\n\n",id,devname);
      return(NO);
    }
  }
  inum = lfdir;
  if((dp = ginode()) == NULL || !DIR || getstate() != FSTATE) {
    inum = orphan;
    printf("%c %sSORRY. NO lost+found DIRECTORY\n\n",id,devname);
    return(NO);
  }
  if(dp->di_size & S4_BMASK) {
    dp->di_size = roundup(dp->di_size,S4_BSIZE);
    inodirty();
  }
  filsize = dp->di_size;
  inum = orphan;
  dpfunc = mkentry;
  if((ckinode(dp,DATA) & ALTERD) == 0) {
    printf("%c %sSORRY. NO SPACE IN lost+found DIRECTORY\n\n",id,devname);
    return(NO);
  }
  declncnt();
  if((dp = ginode()) && !dp->di_nlink) {
    dp->di_nlink++;
    inodirty();
    setlncnt(getlncnt()+1);
    if(lostdir) {
      for(blp = badlncnt; blp < badlnp; blp++)
        if(*blp == inum) {
          *blp = 0L;
          break;
        }
    }
  }
  if(lostdir) {
    dpfunc = chgdd;
    filsize = dp->di_size;
    ckinode(dp,DATA);
    inum = lfdir;
    if((dp = ginode()) != NULL) {
      dp->di_nlink++;
      inodirty();
      setlncnt(getlncnt()+1);
    }
    inum = orphan;
    printf("%c %sDIR I=%u CONNECTED. ",id,devname,orphan);
    printf("%c %sPARENT WAS I=%u\n\n",id,devname,pdir);
  }
  return(YES);
}


int bread(struct filecntl *fcp, char *buf, s4_daddr blk, MEMSIZE size)
{
  if(lseek(fcp->rfdes,blk<<S4_BSHIFT,0) < 0)
    rwerr("SEEK",blk);
  else if(read(fcp->rfdes,buf,size) == size)
    return(YES);
  rwerr("READ",blk);
  return(NO);
}


int bwrite(struct filecntl *fcp, char *buf, s4_daddr blk, MEMSIZE size)
{
  if(fcp->wfdes < 0)
    return(NO);
  if(lseek(fcp->wfdes,blk<<S4_BSHIFT,0) < 0)
    rwerr("SEEK",blk);
  else if(write(fcp->wfdes,buf,size) == size) {
    fcp->mod = 1;
    return(YES);
  }
  rwerr("WRITE",blk);
  return(NO);
}


void catch(int sig)
{
  ckfini();
  exit(4);
}

/* clear the block and it's type */
void bclear( struct bufarea *bp )
{
  memset( &bp->b_un, 0, sizeof(bp->b_un));
  bp->b_type    = s4b_unk;
  bp->b_swapped = NO;
}


/* set the buffer as type, in local mode */
void bsetmem(  struct bufarea *bp, s4btype type )
{
  bp->b_type = type;
  bp->b_swapped = doswap ? 1 : 0;
}

/* set type and convert to local memory */
void bset( struct bufarea *bp, s4btype type )
{
  if( bp->b_type != type && bp->b_type != s4b_unk )
    {
      printf("Confused blk %d is %s, setting to %s!?\n",
             bp->b_bno,
             s4btypestr(bp->b_type),
             s4btypestr(type) );
    }
  bp->b_type = type;
  btomem( bp );
}

/* if block needs swapping to mem, do so. */
void btomem( struct bufarea *bp )
{
  if( doswap && NO == bp->b_swapped )
    {
      s4_fsu_swap( (s4_fsu*)bp->b_un.b_buf, bp->b_type );
      bp->b_swapped = YES;

      if(dbgflag)
        {
          printf("tomem %s\n", s4btypestr( bp->b_type ));
          s4_fsu_show( (s4_fsu*)bp->b_un.b_buf, bp->b_type );
        }
    }
}

/* if block needs swapping to disk format, do so. */
void btodisk( struct bufarea *bp )
{
  if( doswap && YES == bp->b_swapped )
    {
      if(dbgflag)
        printf("to disk %p %s\n", bp, s4btypestr( bp->b_type ));

      s4_fsu_swap( (s4_fsu*)bp->b_un.b_buf, bp->b_type );
      bp->b_swapped = NO;
    }
}


int	nodfunc(DIRECT *dirp, BUFAREA *bp)
{
  printf("No DFUNC should not be called\n");
  abort();
  /*NOTREACHED*/
}

int	nobfunc(s4_daddr blk, int flg)
{
  printf("No BFUNC should not be called\n");
  abort();
  /*NOTREACHED*/
}


union fun {
  visitdir  dirf;
  visitblk  blkf;
  void     *ptr;

};

struct funcmap {
  union fun  what;
  char      *name;
};

const struct funcmap funcnames[] =
  {
    { .what.dirf = nodfunc, "nodfunc" },
    { .what.dirf = findino, "findino" },
    { .what.dirf = mkentry, "mkentry" },
    { .what.dirf = chgdd,   "chgdd"   },
    { .what.dirf = pass2,   "pass2"   },

    { .what.blkf = nobfunc, "nobfunc" },
    { .what.blkf = dirscan, "dirscan" },
    { .what.blkf = chkblk,  "chkblk"  },
    { .what.blkf = pass1,   "pass1"   },
    { .what.blkf = pass1b,  "pass1b"  },
/*  { .what.blkf = pass3,   "pass3"   }, <<-- doesn't exist */
    { .what.blkf = pass4,   "pass4"   },
    { .what.blkf = pass5,   "pass5"   },

    { .what.ptr  = 0, NULL            }
  };

const char *cbname(void*f)
{
  const struct funcmap *fp = funcnames;

  for( ; fp->what.ptr ; fp++ )
    if( f == fp->what.ptr )
      return fp->name;

  return "BAD FUNCPTR";
}

const char *flgstr( int flg )
{
  const char *s;
  switch( flg )
    {
    case DATA:      s = "DATA|STOP";   break;
    case ADDR:      s = "ADDR";        break;
    case BBLK:      s = "BBLK|SKIP";   break;
    case ALTERD:    s = "ALTERD";      break;
    case KEEPON:    s = "KEEPON";      break;
    case REM:       s = "REM";         break;
    default:
      s = "BAD FLG"; break;
    }
  return s;
}


  
