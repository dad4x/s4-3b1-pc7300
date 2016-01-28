/*
 * System V mkfs, modified to run on 32 and 64 bit systems
 * against a file that will become a filesystem image.
 * Everything but the bare essentials removed.
 */

/*	Copyright (c) 1984 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	mkfs	COMPILE:	cc -O mkfs.c -s -i -o mkfs
 *                   
 * Make a file system 
 *                   
 * usage: s4mkfs [-be|-le] filsys size[:inodes] [gap blocks/cyl]
 */

#include <s4d.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>

/* locate debug printf's */
#define dprintf printf

/* ---------------------------------------------------------------- */

/* the inode we're building later crunched inside a dinode */
struct inode
{
        s4_ino	i_number;	/* i number, 1-to-1 with device address */
        short   i_nlink;       
        int     i_ftype;        /* file type IFREG, IFDIR, etc. */
        ushort	i_mode;         /* permissions */
        ushort	i_uid;		/* owner */
        ushort	i_gid;		/* group of owner */
        s4_off	i_size;		/* size of file */
        s4_daddr i_faddr[S4_NADDR];
};

     
/* ---------------------------------------------------------------- */

/* file system block size */

#define	FSBSIZE S4_BSIZE

/* block 0 size */
#define BBSIZE	512
     
/* super-block size */
#define SBSIZE	512

/* number of sectors in a block */
#define SECTPB  (FSBSIZE/512)

#define	NIDIR	(FSBSIZE/sizeof(s4_daddr))
#define	NFB	(NIDIR+1300)	/* NFB must be greater than NIDIR+LADDR */
#define	NDIRECT	(FSBSIZE/sizeof(struct s4_direct))
#define	NBINODE	(FSBSIZE/sizeof(struct s4_dinode))
     
#define	LADDR       10          /* direct pointers in a address block */
     
#define	STEPSIZE    7
#define	CYLSIZE     400
#define	MAXFN       1500        /* max free list blocks */

/* ----------- */
/* Global data */

time_t	utime;                  /* timestamp for everything. */
int	fsfd;                   /* FD of the fs being built  */
char   *pname;                  /* program name */
char   *fsys;                   /* name of FS being opened */
char   *sizing;                 /* argv[2] with [blocks[:inodes]] */
int	f_n = CYLSIZE;          /* default */
int	f_m = STEPSIZE;         /* defaule */
int	error;                  /* indication of trouble as exit status */
s4_ino	ino;                    /* one we are working on */
int     doswap;                 /* should we byte-swap? */
int     endian;                 /* which endiannes is FS? */
     
/* make sure these are aligned */
int onebuf[FSBSIZE/sizeof(int)];
int twobuf[FSBSIZE/sizeof(int)];
int thrbuf[FSBSIZE/sizeof(int)];

/* pointers to them for use.  */
char              *buf    = (char*)thrbuf;
struct s4_fblk    *fbuf   = (struct s4_fblk *)onebuf;
struct s4_dfilsys *filsys = (struct s4_dfilsys *)twobuf;

/* forward references */

/* read from filesystem a block of type */
void rdfs(s4_daddr bno, char *bf, s4btype type);

/* write to filesystem a block of type */
void wtfs(s4_daddr bno, char *bf, s4btype type);

/* create a file with parent inode */
void mkfile(struct inode *par);
                   
/* allocate a block from superblock cache, refilling as needed */
s4_daddr alloc(void);
     
/* release block to superblock cache, spilling to freelist */
void bfree(s4_daddr bno);                   
     
/* build the freelist from an empty filesystem */
void bflist(void);                   
     
/* make an entry in a directory */
void entry(s4_ino in, char *str, int *adbc, char *db, int *aibc, s4_daddr *ib);
     
/* if block is full, write to disk and setup another */
void newblk(int *adbc, char *db, int *aibc, s4_daddr *ib, s4btype current_type);
     
/* merge memory inode to disk inode block and write */
void iput(struct inode *ip, int *aibc, s4_daddr *ib);
                   
/* ---------------- */
/* MAIN */
                   
int main(int argc, char **argv)
{
        int f, c;
        long n, nb;

        time(&utime);

        pname = argv[0];
        endian = S4_ENDIAN;
        if( argc > 1 && argv[1][0] == '-' )
        {
                if( !strcmp("-be", argv[1]) )
                {
                        doswap = S4_ENDIAN == S4_BE ? 0 : 1;
                        endian = S4_BE;
                }
                else if( !strcmp("-le", argv[1]) )      
                {
                        doswap = S4_ENDIAN == S4_BE ? 1 : 0;
                        endian = S4_LE;
                }
                argv++;
                argc--;
        }

        /*
         * open relevent files
         */
        if(argc < 3) {
                printf("usage: %s filsys blocks[:inodes] [gap blocks/cyl]\n", 
                       pname );
                exit(1);
        }
                   
        fsys   = argv[1];
        sizing = argv[2];
     
        /* Create new file, clobbering old one. */
        fsfd = open(fsys, O_RDWR|O_CREAT|O_TRUNC, 0640 );
        if(fsfd < 0) {
                printf("%s: cannot create\n", fsys);
                exit(1);
        }
     
        nb = n = 0;

        /* if there's an argument, and we can crack it into
           size:inodes, or size, take them, else complain.
           If nothing, take defaults */

        for(f=0; (c=sizing[f]); f++) {
                if(c<'0' || c>'9') {
                        if(c == ':') {
                                nb = n;
                                n = 0;
                                continue;
                        }
                        printf("Mkfs: expected numbers, not '%s'\n", sizing);
                        exit(1);
                }
                n = n*10 + (c-'0');
        }
        if(!nb) {
                nb = n / SECTPB;
                n = nb/4;
        } else {
                nb /= SECTPB;
        }

        /* nb is number of logical blocks in fs,
           n is number of inodes */

        filsys->s_fsize = nb - 2;
                   
        n /= NBINODE;		/* number of logical blocs for inodes */
        if(n <= 0)
                n = 1;
     
        /* cap inodes */
        if(n > 65500/NBINODE)
        {
          printf("Too many inode blocks, %ld max\n", 65500/NBINODE );
          n = 65500/NBINODE;
        }
     
        filsys->s_isize = n + 2;

        /* set magic number for file system type */
        filsys->s_magic = S4_FsMAGIC;
        filsys->s_type = (FSBSIZE == 512) ? S4_Fs1b : S4_Fs2b;
     
        /* cylinder */
        if(argc >= 5) {
                f_m = atoi(argv[3]);
                f_n = atoi(argv[4]);
                if(f_n <= 0 || f_n >= MAXFN)
                        f_n = CYLSIZE;
                if(f_m <= 0 || f_m > f_n)
                        f_m = STEPSIZE;
        }
     
        f_n /= SECTPB;
     
        if( f_n > (filsys->s_fsize / 4) )
                f_n = filsys->s_fsize / 4;
     
        f_m = (f_m +(SECTPB -1))/SECTPB;  /* gap rounded up to the next block */

        /* don't set these until adjustments are made above. */
        filsys->s_vinfo[0] = f_m;
        filsys->s_vinfo[1] = f_n;
     
        printf("Built %s-endian file system\n", S4_BE == endian ? "big" : "little");
        printf("bytes per logical block = %d\n",  FSBSIZE);
        printf("total logical blocks    = %ld\n", (long)filsys->s_fsize);
        printf("total inodes            = %ld\n", n*NBINODE);
        printf("freelist gap            = %d\n",  filsys->s_vinfo[0]);
        printf("freelist cylinder size  = %d \n", filsys->s_vinfo[1]);

        if(filsys->s_isize >= filsys->s_fsize) {
                printf("%ld/%ld: bad file blocks/inode blks ratio\n",
                       (long)filsys->s_fsize, (long)filsys->s_isize-2);
                exit(1);
        }
                   
        /* ---------------------------- */
        /* geometry set up, now prepare */

        ino = 0;
        filsys->s_tinode = 0;
        filsys->s_tfree = filsys->s_fsize;
     
        /* write zeros to the whole inode table */
        memset( buf, 0, FSBSIZE );
        for(n=2; n!=filsys->s_isize; n++) {
                wtfs(n, buf, s4b_ino );
                filsys->s_tinode += NBINODE;
        }
                   
        /* touch end block to set length and ensure writable. */
        wtfs( nb - 1, buf, s4b_raw );

        /* populate the freelist */
        bflist();

        /* create the root directory with no parent inode */
        mkfile((struct inode *)0);

        /* stamp the superblock */
        filsys->s_time = utime;

        /* write super-block onto file system */
        if( doswap )
                s4_fsu_swap( (s4_fsu*)filsys, s4b_super );

        lseek(fsfd, (long)S4_SUPERBOFF, 0);
        if(write(fsfd, (char *)filsys, SBSIZE) != SBSIZE) {
                printf("write error: super-block\n");
                exit(1);
        }
     
        if( doswap )
                s4_fsu_swap( (s4_fsu*)filsys, s4b_super );

        if( error )     
        {
                printf("mkfs: ERROR making filesystem!!!\n");
        }
        else
        {
                printf("mkfs: Available blocks  = %ld  %.fk %.2fM\n",
                       (long)filsys->s_tfree,
                       (double)filsys->s_tfree * FSBSIZE / 1024,
                       (double)filsys->s_tfree * FSBSIZE / 1024 / 1024 );
        }
        exit(error);
}

void mkfile(struct inode *parent)
{
        struct inode in;
        int          dbc, ibc;        /* dir, inode blocks used */
        char         db[FSBSIZE];     /* directory buffer. */
        s4_daddr     ib[NFB];         /* inode bffer? */
        int i;

        /* setup the perms for the inode */
        in.i_ftype  = S_IFDIR;
        in.i_mode   = 0777;        
        in.i_uid    = 0;
        in.i_gid    = 0;

        ino++;
        in.i_number = ino;
     
        memset( db, 0, FSBSIZE );
        memset( ib, 0, NFB );
        in.i_nlink = 1;
        in.i_size = 0;

        /* clear out block addresses */
        for(i=0; i<S4_NADDR; i++)
                in.i_faddr[i] = (s4_daddr)0;
                   
        /* link to self in root */
        if(parent == (struct inode *)0) {
                parent = &in;
                in.i_nlink--;
        }
        dbc = 0;
        ibc = 0;
                   
        /* put the . and .. links into a directory */

        parent->i_nlink++;
        in.i_nlink++;
        entry(in.i_number,      ".",  &dbc, db, &ibc, ib);
        entry(parent->i_number, "..", &dbc, db, &ibc, ib);
     
        in.i_size = 2*sizeof(struct s4_direct);
                   
        /* allocate space for the directory block 
           and write it */
        newblk(&dbc, db, &ibc, ib, s4b_dir);
                   
        /* then put the inode and write that. */
        iput(&in, &ibc, ib);
}



void rdfs(s4_daddr bno, char *bf, s4btype type )
{
        int n;

        lseek(fsfd, (long)(bno*FSBSIZE), 0);
        n = read(fsfd, bf, FSBSIZE);
        if(n != FSBSIZE) {
          printf("read error: %ld\n", (long)bno);
          exit(1);
        }
        if( doswap )
                s4_fsu_swap( (s4_fsu*)bf, type );
}

void wtfs(s4_daddr bno, char *bf, s4btype type)
{
        int n;

        /* swap to disk format */
        if( doswap )
                s4_fsu_swap( (s4_fsu*)bf, type );
                   
        lseek(fsfd, (long)(bno*FSBSIZE), 0);
        n = write(fsfd, bf, FSBSIZE);
        if(n != FSBSIZE) {
          printf("write error: %ld\n", (long)bno);
          exit(1);
        }
                   
        /* return to native */
        if( doswap )
                s4_fsu_swap( (s4_fsu*)bf, type );
}
                   
/* allocate a block from superblock cache, refilling as needed */
s4_daddr alloc(void)
{
        int i;
        s4_daddr bno;

        filsys->s_tfree--;
     
        /* get free block from super cache. */
        bno = filsys->s_free[--filsys->s_nfree];
        if(bno == 0) {
                printf("out of free space\n");
                exit(1);
        }
     
        /* if super cache empty, refill from
           a freelist block  */
        if(filsys->s_nfree <= 0) {
                   
                rdfs(bno, (char *)fbuf, s4b_free);
     
                filsys->s_nfree = fbuf->df_nfree;
                for(i=0; i<S4_NICFREE; i++)
                        filsys->s_free[i] = fbuf->df_free[i];
        }
        return(bno);
}

/* release block to superblock cache, spilling to freelist */
void bfree(s4_daddr bno)
{
        int i;

        /* if super cache is full, replace and refill */
        if(filsys->s_nfree >= S4_NICFREE) {
     
                /* put the super cache into this block
                   as a free list block */
                fbuf->df_nfree = filsys->s_nfree;
                for(i=0; i<S4_NICFREE; i++)
                        fbuf->df_free[i] = filsys->s_free[i];

                wtfs(bno, (char *)fbuf, s4b_free );
                filsys->s_nfree = 0;
        }
        /* and add this block to the super cache,
           either as a direct or as the link to more. */
        filsys->s_free[filsys->s_nfree++] = bno;
                   
        filsys->s_tfree++;
}


/* making a directory entry in db, which might spill */
void entry(s4_ino in, char *str, int *adbc, char *db, int *aibc, 
           s4_daddr *ib)
{
        struct s4_direct *dp;
        int i;

        dp = (struct s4_direct *)db;
        dp += *adbc;
        (*adbc)++;
        dp->d_ino = in;
     
        /* copy what fits of the name */
        memset(dp->d_name, 0, S4_DIRSIZ );
        for(i=0; i<S4_DIRSIZ; i++)
                if((dp->d_name[i] = str[i]) == 0)
                        break;
                   
        /* if dir is full, write it and get a new one */
        if(*adbc >= NDIRECT)
                newblk(adbc, db, aibc, ib, s4b_dir);
}

/* if block is full, write to disk and setup another of current type */
void newblk(int *adbc, char *db, int *aibc, s4_daddr *ib, 
            s4btype current_type)
{
        s4_daddr bno;

        bno = alloc();
                   
        wtfs(bno, db, current_type);
     
        memset( db, 0, FSBSIZE );
     
        *adbc = 0;
        ib[*aibc] = bno;
        (*aibc)++;
        if(*aibc >= NFB) {
                printf("file too large\n");
                error = 1;
                *aibc = 0;
        }
}

/* build the freelist in an empty filesystem */
void bflist(void)
{
        struct    inode in;
        s4_daddr  ib[NFB];            /* indirect block buffers */
        int       ibc;
        char      flg[MAXFN];
        int       adr[MAXFN];
        int       i, j;
        s4_daddr  f, d;

        memset( flg, 0, MAXFN );
     
        /* for block upto cylinder count... */
        // printf("setting up flg and adr\n");     
        i = 0;
        for(j=0; j<f_n; j++) {
                while(flg[i])
                        i = (i+1)%f_n;
                adr[j] = i+1;
     
                // printf("adr[%d] = %d (i+1)\n", j, i+1 );
                flg[i]++;
                i = (i+f_m)%f_n;
     
                // printf("i %d\n", i );
        }

        ino++;
        in.i_number = ino;
        in.i_ftype = S_IFREG;
        in.i_uid = 0;
        in.i_gid = 0;
        in.i_nlink = 0;
        in.i_size = 0;
        in.i_mode = 0;
     
        for(i=0; i<S4_NADDR; i++)
                in.i_faddr[i] = (s4_daddr)0;

        for(i=0; i<NFB; i++)
                ib[i] = (s4_daddr)0;
     
        ibc = 0;
     
        d = filsys->s_fsize-1;
        bfree((s4_daddr)0);
     
        /* reset this; we're building from zero */
        filsys->s_tfree = 0;
                   
        while(d%f_n)
                d++;
     
        // printf("starting at d %d, working backwards\n", d );
     
        for(; d > 0; d -= f_n)
        {
                // printf("d %d\n", d );
                for(i=0; i<f_n; i++) {
                        f = d - adr[i];
                        if(f < filsys->s_fsize && f >= filsys->s_isize)
                        {
                                // printf("bfree %d\n", f );
                                bfree(f);
                        }
                }
        }
        iput(&in, &ibc, ib);
}

/* write the memory-inode out to the inode-block */
void iput(struct inode *ip, int *aibc, s4_daddr *ib)
{
        struct s4_dinode *dp;
        s4_daddr  d;
        int       i,j,k;
        s4_daddr  ib2[NIDIR];	/* a double indirect block */

        filsys->s_tinode--;
        d = itod(ip->i_number);
        if(d >= filsys->s_isize) {
                if(error == 0)
                        printf("ilist too small\n");
                error = 1;
                return;
        }
     
        /* get the existing disk inode block to modify */
        rdfs(d, buf, s4b_ino );
        dp = (struct s4_dinode *)buf;
     
        /* skip to the right entry */
        dp += itoo(ip->i_number);

        /* convert memory to disk format in buffer */
        dp->di_mode  = ip->i_ftype | ip->i_mode;
        dp->di_nlink = ip->i_nlink;
        dp->di_uid   = ip->i_uid;
        dp->di_gid   = ip->i_gid;
        dp->di_size  = ip->i_size;
        dp->di_atime = utime;
        dp->di_mtime = utime;
        dp->di_ctime = utime;

        switch(ip->i_ftype) {

        case S_IFDIR:
        case S_IFREG:
     
                /* handle direct pointers */
                for(i=0; i<*aibc && i<LADDR; i++) {
                        ip->i_faddr[i] = ib[i];
                        ib[i] = 0;
                }
     
                /* handle single indirect block */
                if(i < *aibc)
                {
                        for(j=0; i<*aibc && j<NIDIR; j++, i++)
                                ib[j] = ib[i];
                        for(; j<NIDIR; j++)
                                ib[j] = 0;
                        ip->i_faddr[LADDR] = alloc();

                        wtfs(ip->i_faddr[LADDR], (char *)ib, s4b_idx);
                }
     
                /* handle double indirect block */
                if(i < *aibc)
                {
                        for(k=0; k<NIDIR && i<*aibc; k++)
                        {
                                for(j=0; i<*aibc && j<NIDIR; j++, i++)
                                        ib[j] = ib[i];
                                for(; j<NIDIR; j++)
                                        ib[j] = 0;
                                ib2[k] = alloc();
                                wtfs(ib2[k], (char *)ib, s4b_idx);
                        }
                        for(; k<NIDIR; k++)
                                ib2[k] = 0;
                        ip->i_faddr[LADDR+1] = alloc();
                        wtfs(ip->i_faddr[LADDR+1], (char *)ib2, s4b_idx );
                }
     
                /* triple indirect block? Nope. */
                if(i < *aibc)
                {
                        printf("triple indirect blocks not handled\n");
                }
                break;

        default:
                printf("bogus ftype %o\n", ip->i_ftype);
                exit(1);
        }

        /* convert the address list to correct disk format */
        if( doswap )
                s4ltol3r(dp->di_addr, ip->i_faddr, S4_NADDR);
        else
                s4ltol3(dp->di_addr, ip->i_faddr, S4_NADDR);
                   
        wtfs(d, buf, s4b_ino);
}


