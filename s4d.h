/*
 * Library for access to CTIX / 3b1 / PC7300 / "Safari 4" disk volumes,
 * and filesystem image files
 *
 * Allows exploration of the partitions, bad block mapping, and
 * the System V filesystem.
 *
 * Understanding this is part of making disk and filesystem image files
 * mountable on Linux.
 *
 * A disk here is a sector scrape image of a physical device.
 * Sector 0+1 has a disk header (vhbd), which also contains a
 * partition table.  The VHBD has partition info in sector 0,
 * but other stuff of less interest is in sector 1.
 *
 * Partition 0 has a little space for other things, including the 
 * loader binary, and a bad block table.
 *
 * The disk header contains drive geometry, cylinders, heads, and 
 * sectors per track.  
 *
 * The bad block table is not used by floppies, and by perfect
 * drives.  A perfect drive is one that has the checksum ~0,
 * 0xffffffff as the first entry in the bac block table.
 *
 * If there is bad block mapping, then they are handled by reserving
 * one sector on every track to be used as a spare for a bad sector
 * on that track, so single sector errors are relatively cheap and
 * involve no seeking.
 *
 * However, it means logical block addresses (LBAs) are NOT the same
 * as physical block addresses (PBAs).  Much grief comes from this.
 * 
 */

#ifndef S4LIB_H
#define S4LIB_H

#include <s4fixed.h>

#define S4_BE   (1)
#define S4_LE   (0)

#if linux || vax || i386
#define S4_LITTLE_ENDIAN
#define S4_ENDIAN   S4_LE
#else
#define S4_BIG_ENDIAN
#define S4_ENDIAN   S4_BE
#endif

/* native, local headers */
#include <sys/stat.h>

#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

/* ---------------------------------------------------------------- */

/* how partitions are used on the S4. */
#define S4_LD_PNUM        (0)
#define S4_PAGE_PNUM      (1)
#define S4_FP_FS_PNUM     (1)
#define S4_HD_FS_PNUM     (2)

/* ---------------------------------------------------------------- */
/* replacing <sys/params.h */

#define S4_ROOTINO      2
#define	S4_NICFREE	50    /* number of superblock free blocks */
#define	S4_NICINOD	100   /* number of superblock inodes */

#define S4_SUPERB       1     /* block of FS superblock */
#define S4_SUPERBOFF    512   /* offset of FS superblock */

/* ---------------------------------------------------------------- */
/* replace sys/types.h */

typedef int32_t    s4_daddr;
typedef uint16_t   s4_ino;
typedef int32_t    s4_off;
typedef int32_t    s4_time;
typedef int32_t    s4_dev;

/* ---------------------------------------------------------------- */
/* replace sys/gdisk.h */

#define S4_MAXSLICE	16	/* maximum number of slices */

/*
 *	data describing disk specific information held in the VHB and then
 *	loaded into the gdsw table
 */
struct s4_dswprt {
	char 	 name[6];	/* name, sort of */
	uint16_t cyls;		/* the number of cylinders for this disk */
	uint16_t heads;		/* number of heads per cylinder */
	uint16_t psectrk;	/* number of physical sectors per track */
	uint16_t pseccyl;	/* number of physical sectors per cylinder */
	char	 flags;		/* floppy density and high tech drive flags */
	char	 step;		/* stepper motor rate to controller */
	uint16_t sectorsz;	/* physical sector size in bytes */
}  __attribute__((__packed__));

/*	flags in s4_dswprt	*/
#define S4_FPDENSITY	0x01	/* 0=single 1=double density */
#define S4_FPMIXDENS	0x02	/* 0=all tracks are the same density
				   1= cylinder zero is single density */
#define S4_HITECH	0x04	/* 0= reduced write current is valid,
				   1= head select bit 3 is valid */
#define S4_NEWPARTTAB	0x08	/* 0= old style partition table,
				   1= new style partition table */

/*	disk slice table	*/
struct s4_partit {				/* partition table */
  union {
    uint32_t strk;	/* start track number (new style) */
    struct {
      uint16_t 	strk;	/* start track # */
      uint16_t	nsecs;	/* # logical blocks available to user */
    } old;
  } sz;
};


#define S4_NO_BB_CHECKSUM   (0xffff)


/*	bad block cell on disk	*/
struct s4_bbe {
  uint16_t	cyl;		/* the cylinder of the bad block */
  uint16_t	badblk;		/* the physical sector address of the bad block
                                   within the cylinder cyl */
  uint16_t	altblk;		/* track number of alternate */
  uint16_t	nxtind;		/* index into the cell array for next bad block
r0st				   cell for this cylinder */
};

#define S4_MNAMSZ	40		/* max length of name including null terminator */
struct s4_mntnam {
  char	name[S4_MNAMSZ];	/* full path name of mount point */
};

/* logical blocks are 1K units with bad-sector mapping */
struct s4_resdes {		/* reserved area special files */
  s4_daddr blkstart;            /* start logical block # */
  uint16_t nblocks ;            /* length in logical blocks
			   (zero implies not present) */ /*  */
} __attribute((__packed__));


/*	volume home block on disk	*/
struct  s4_vhbd {
	uint	magic;		/* S4 disk format code */
	int	chksum;		/* adjustment so that the 32 bit sum starting
				   from magic for 512 bytes sums to -1 */
	struct s4_dswprt dsk;	/* specific description of this disk */
	struct s4_partit partab[S4_MAXSLICE];	/* partition table */
	struct s4_resdes resmap[8];
/*	resmap consists of the following entries:
 *		loader area 
 *		bad block table
 *		dump area 
 *		down load image file 
 *		Bootable program, size determined by a.out format. nblocks=1.
 */
	char	fpulled;	/* dismounted last time? */
        char    pad;		/* dbrower 2016 */

        /* this 8 slices * 40 = 500 bytes!? */
	struct	s4_mntnam mntname[S4_MAXSLICE];	/* names for auto mounting.
			  		   null string means no auto mount */
	int	time;		/* time last came on line */
	short	cpioMagic,	/* for cpio backup, restore	*/
		setMagic,
		cpioVol;
} __attribute__((__packed__));

#define S4_VHBMAGIC	0x55515651	/* magic number in disk vhb */
#define S4_VHBMAGIC_BE  0x55515651      /* magic in BE */
#define S4_VHBMAGIC_LE  0x51565155      /* magic in LE */

/*	indexes into resmap	*/
#define	S4_INDLOADER	0
#define S4_INDBBTBL	1
#define S4_INDDUMP      2
#define S4_INDDOWNLOAD	3
#define S4_INDBOOT	4 


/*	bad block table	*/
/*
 *	On the disk the bad block table consists of an array of cells, one per
 *	used alternate physical sector.
 *	cyl	is the cylinder containing the bad block badblk.
 *	badblk	is the physical sector address (within the cylinder cyl)
 *		of a bad block.
 *	altblk	is the location of the alternate physical sector. It is
 *		specified as the track number since the alternate sector is
 *		always the last sector of a track.
 *	nxtind	is a chain of bad blocks on the same cylinder. The number is
 *		the index into this array of bad block cells. A value of zero
 *		means the end of the chain for this cylinder. The zero'th
 *		entry is not used, other than to hold the count of used cells).
 *
 *	In memory a similar array will be held in the appropriate gdbtab
 *	together with an array containing an entry for each cylinder which
 *	holds the index into the above array of cells for the head for that
 *	cylinder.
 *	This means that a zero entry indicates no bad blocks on that cylinder.
 *
 *	This mechanism implies a maximum of 255 bad blocks per disk which is
 *	adequate for disks upto the 250 Mbyte range.
 *	The bad block table is held as a chained set of logical blocks on
 *	track zero. Each block can hold 256 entries.
 *	The first bad block cell in each logical block of the bad block table
 *	contains a chksum (in cyl and badblk) for the 1024 byte block, it is
 *	a 32 bit sum which sums to -1. altblk contains the logical block
 *	number of the next block in the bad block table, zero=end.
 *	On a disk with an odd number of sectors per track, the last sector
 *	is removed from the logical mapping and is used as an alternate area.
 *
 *	Bad block handling is disabled on floppies.
 */
#define S4_HDMAXBADBLK	128	/* only support maximum of 128 bad blocks */


/* ----------------------------------------------------- */
/* replace sys/filsys.h */

/*
 * Structure of the filesystem super-block
 */
struct	s4_dfilsys
{
	uint16_t	 s_isize;	/* size in blocks of i-list */
	s4_daddr s_fsize;	/* size in blocks of entire volume */
	short	 s_nfree;	/* number of addresses in s_free */
	s4_daddr s_free[S4_NICFREE];	/* free block list */
	short	 s_ninode;	/* number of i-nodes in s_inode */
	s4_ino	 s_inode[S4_NICINOD];	/* free i-node list */
	char	 s_flock;	/* lock during free list manipulation */
	char	 s_ilock;	/* lock during i-list manipulation */
	char  	 s_fmod; 	/* super block modified flag */
	char	 s_ronly;	/* mounted read-only flag */
        int	 s_time; 	/* last super block update */
	short	 s_vinfo[4];	/* device information */
	s4_daddr s_tfree;	/* total free blocks */
	s4_ino	 s_tinode;	/* total free inodes */
	char	 s_fname[6];	/* file system name */
	char	 s_fpack[6];	/* file system pack name */
	int 	 s_fill[13];	/* ADJUST to make sizeof filsys be 512 */
	int 	 s_magic;	/* magic number to indicate new file system */
	int 	 s_type;        /* type of FS S4_Fs1b or S4_Fs2b */
	int 	 s_fill2[2];	/* pad so sizeof filsys is 512 */
} __attribute__((__packed__)) ;

#define	S4_FsMAGIC     0xfd187e20	/* s_magic number */
#define S4_FsMAGIC_LE  S4_FsMAGIC       /* layed out in order */
#define S4_FsMAGIC_BE  0x207e18fd       /* layed out reverse  */

#define	S4_Fs1b	1	/* 512 byte block */
#define	S4_Fs2b	2	/* 1024 byte block */

#ifndef S4_FsTYPE
#define S4_FsTYPE 2
#endif

#if S4_FsTYPE==1
#define S4_BSIZE   (512)
#define S4_INOPB   (8)
#define	S4_INOSHIFT 3	/* LOG2(INOPB) if exact */
#define S4_BSHIFT  9		/* LOG2(BSIZE) */
#define	S4_BMASK   0777         /* BSIZE-1 */
#endif

#if S4_FsTYPE==2
#define S4_BSIZE   (1024)
#define S4_INOPB   (16)
#define	S4_INOSHIFT 4           /* LOG2(INOPB) if exact */
#define	S4_BSHIFT  10		/* LOG2(BSIZE) */
#define	S4_BMASK   01777        /* BSIZE-1 */
#endif

#if S4_FsTYPE==3
/* both 1 and 2 supported */
#define	S4_BSIZE   (512)        /* size of file system block (bytes) */
#define S4_INOPB   (16)
#define	S4_INOSHIFT 3	/* LOG2(INOPB) if exact */
#define	S4_BSHIFT   9		/* LOG2(BSIZE) */
#define	S4_BMASK   0777         /* BSIZE-1 */
#endif

#define	S4_NINDIR   (S4_BSIZE/sizeof(s4_daddr))

/* ---------------------------------------------------------------- */
/* replace sys/fblk.h */

struct	s4_fblk
{
	int        df_nfree;
	s4_daddr   df_free[S4_NICFREE];
};

/* ---------------------------------------------------------------- */
/* replace sys/ino.h */
struct s4_dinode
{
	uint16_t  di_mode;          /* mode and type of file */
	int16_t   di_nlink;         /* number of links to file */
	uint16_t  di_uid;           /* owner's user id */
	uint16_t  di_gid;           /* owner's group id */
	s4_off	  di_size;          /* number of bytes in file */
	char  	  di_addr[40];      /* disk block addresses */
	s4_time	  di_atime;         /* time last accessed */
	s4_time	  di_mtime;         /* time last modified */
	s4_time	  di_ctime;         /* time created */
};

/*
 * the 40 address bytes:
 *	39 used; 13 addresses
 *	of 3 bytes each.
 *
 * 0-9 point to data blocks (10k)
 * 10 points to an indirect block -> data blocks 
 * 11 points to an indirect block -> iblocks -> data blocks
 * 12 points to an indirect block -> iblocks -> iblocks -> data blocks
 */

/* ---------------------------------------------------------------- */
/* replace sys/inode.h */

#define S4_NADDR    (13)

/* ---------------------------------------------------------------- */
/* replace sys/dir.h */
#ifndef	S4_DIRSIZ
#define	S4_DIRSIZ	14
#endif

struct	s4_direct
{
    s4_ino	d_ino;
    char	d_name[S4_DIRSIZ];
};

/* ---------------------------------------------------------------- */

/* replace sys/sysmacros.h */
/* inumber to disk address */
#ifdef S4_INOSHIFT
#define	itod(x)	(s4_daddr)(((unsigned)x+(2*S4_INOPB-1))>>S4_INOSHIFT)
#else
#define	itod(x)	(s4_daddr)(((unsigned)x+(2*S4_INOPB-1))/S4_INOPB)
#endif

/* inumber to disk offset */
#ifdef S4_INOSHIFT
#define	itoo(x)	(int)(((unsigned)x+(2*S4_INOPB-1))&(S4_INOPB-1))
#else
#define	itoo(x)	(int)(((unsigned)x+(2*S4_INOPB-1))%S4_INOPB)
#endif


/* ---------------------------------------------------------------- */
/* For compilation on the S4, you only get 7 places of names
   visible in the .o to the loader, so alias here */

#ifdef S4_SHORTNAMES

/* incomplete right now */
#define s4_seek_read        s4skrd
#define s4_seek_write       s4skwr

#define s4_open_vol         s4opv
#define s4_vol_show         s4vsho
#define s4_vol_close        s4vclo

#define s4_vol_open_filsys  s4vopfs
#define s4_open_filesys     s4opfs

#define s4_filsys_get_blk   s4fsgtbk
#define s4_filsys_show      s4fssho
#define s4_filsys_close     s4fsclo

#define s4_fsu_swap         s4fsuswp
#define s4_fsu_show         s4fsuswp
#define s4_fsb_to_diskfmt   s4fsb2d
#define s4_fsb_to_memfmt    s4fsb2m

#endif

/* ---------------------------------------------------------------- */
/* Our Types and constant definitions */

/* errors from s4 api */
typedef enum {

  s4_ok,
  s4_error,
  s4_badmagic,
  s4_open,
  s4_close,
  s4_seek,
  s4_read,
  s4_write,
  s4_range,

} s4err;


typedef enum
{
  s4a_bad = 0,
  s4a_lba = 1,
  s4a_pba = 2

} s4atype;


/* raw filesystem block sizing */

#define S4_NDIRECT (S4_BSIZE/sizeof(struct s4_direct))
#define S4_SPERB   (S4_BSIZE/sizeof(short))
#define S4_NBB     (512/sizeof(struct s4_bbe))

/* All the blocks we know about in one union */
typedef union
{
    char            buf[ S4_BSIZE ]; /* 1K for FS blocks */

    /* from the disk */
    struct s4_vhbd   vhbd;      /* note: bigger than 512 bytes. */

    /* from disk (originally), and FS block 0 new-style */
    struct s4_bbe bbt[S4_NBB];

    /* from the file system */
    struct s4_dinode  dino[ S4_INOPB ];
    struct s4_direct  dir[ S4_NDIRECT ];
    short             links[ S4_SPERB ];
    s4_daddr          indir[ S4_NINDIR ];
    struct s4_dfilsys super;
    struct s4_fblk    free;

} s4_fsu;

typedef enum {
  s4b_unk,
  s4b_first_fs,
  
  s4b_super,
  s4b_ino,
  s4b_idx,
  s4b_dir,
  s4b_linkcnt,
  s4b_free,
  s4b_raw,

  s4b_last_fs,

  /* non-FS blocks after */
  s4b_vhbd,
  s4b_bbt,

  s4b_last
} s4btype;



// partition info
typedef struct
{
  unsigned int	    strk;       /* start track */
  unsigned int      ntrk;       /* number of tracks */
  unsigned int	    pblks;      /* physical blocks */
  unsigned int	    lblks;      /* logical blocks  */
  unsigned int	    partoff;    /* physical offset */
  unsigned int      partlba;    /* Absolute LBA of start */
  unsigned int      partpba;    /* pba of strk */

} s4_part;

typedef struct s4_bbe s4_bbt;

/* Drive & partition information */
typedef struct
{
  char	   *fname;		/* strdup'd */
  int	    fd;                 
  int       doswap;             /* byte swapping needed */

  struct    s4_vhbd vhbd;        /* From disk, swapped */
  
  /* simpler local access */
  s4_fsu    bbt_fsu;            
  int       nbb;                /* number of actual entries */
  s4_bbt   *bbt;                /* pointer into bbt_fsu */

  int	    cyls;
  int	    heads;
  int	    secsz;

  int	    pstrk;		/* sectors/ phys track    */
  int	    lstrk;		/* sectors/ logical track */

  int	    pscyl;		/* physical sectors/cyl */
  int	    lscyl;		/* logical sectors/cyl  */

  int       ptrksz;             /* physical track len */
  int       ltrksz;             /* logical track len  */

  int       pcylsz;             /* physical cyl len */
  int       lcylsz;             /* logical cyl len  */

  int	    pblks;		/* physical blocks */
  int	    lblks;		/* logical blocks  */


  int	    nparts;
  s4_part   parts[ S4_MAXSLICE ];

  int       isfloppy;
  int       fspnum;             /* S4_FP_FS_PNUM or S4_HD_FS_PNUM */
  int       lba_or_pba;         /* LBA for HD w/bad block, PBA floppy  */

  int	    loader_ba;          /* in PBA or LBA   */
  int	    loader_nblks;       /* in 512k sectors */

  int	    bbt_ba;		/* BA is same here PBA and LBA */
  int       bbt_nblks;          /* better be only 1024! */

} s4_vol;


/* Our idea of a file system.  If not in a vol image,
   then "fakevol" is used to set one up. */
typedef struct
{
  s4_vol    *vinfo;     /* fd here */
  s4_part   *part;      /* partition in vinfo */
  int	     bksz;      /* FS block size*/
  int        doswap;

  int        nbb;       /* entries in bbt. */
  s4_fsu     bbt_fsu;   /* bad block for FS     */
  s4_bbt    *bbt;       /* pointer into bbt_fsu */
  s4_fsu     super;     /* usable superblock    */

  s4_vol   fakevol;     /* if opened from file/partition */

} s4_filsys;

/* ---------------------------------------------------------------- */
/* Handy macros */

/* return a printable char, or '.' */
#define s4showc( c ) (isprint(c) ? c : '.')


/* ---------------------------- */
/* Macro functions for geometry */

/*
 * Partition number to physical offset
 */
#define PNUM_TO_OFFSET( d, pn ) (TRK_TO_OFFSET(d, ((d)->parts[pn].strk)))

/*
 * PBA physical block address to ...
 */
#define PBA_TO_OFFSET( d, pba )	((pba) * (d)->secsz)
#define PBA_TO_TRK( d, pba )	((pba) / (d)->pstrk)
#define PBA_TO_CYL( d, pba )	((pba) / (d)->pscyl)
#define PBA_TO_HEAD( d, pba )	((pba) % (d)->heads)
#define PBA_TO_CYLSEC( d, pba ) ((pba) % (d)->pscyl)
#define PBA_TO_HDSEC( d, pba )	((pba) % (d)->pstrk)

#define PBA_TO_VOL_LBA( d, pba ) \
  (s4_pba2lba( pba, (d)->bbt, (d)->nbb, (d)->pstrk, (d)->heads ))

#define PBA_TO_FS_LBA( f, pba ) \
  (s4_pba2lba( pba, (f)->bbt, (f)->nbb, (f)->vinfo->pstrk, (f)->vinfo->heads ))


/*
 * LBA logical block address to ...
 */

/* converting LBA to physical involves looking at bad block table */
#define LBA_TO_VOL_PBA( d, lba ) \
  s4_lba2pba( lba, (d)->bbt, (d)->nbb, (d)->lstrk )

#define LBA_TO_FS_PBA( xfs, lba ) \
  s4_lba2pba( lba, (xfs)->bbt, (xfs)->nbb, (xfs)->vinfo->lstrk )

#define LBA_TO_VOL_OFFSET( d, lba ) \
  PBA_TO_OFFSET(d, LBA_TO_VOL_PBA( d, lba ))

#define LBA_TO_FS_OFFSET( xfs, lba ) \
  PBA_TO_OFFSET( (xfs)->vinfo, LBA_TO_FS_PBA( (xfs), (lba) ) )

#define LBA_TO_TRK( d, lba )    ((lba) / (d)->lstrk)
#define LBA_TO_CYL( d, lba )    ((lba) / (d)->lscyl)
#define LBA_TO_HEAD( d, lba )   (LBA_TO_TRK((d), lba) % (d)->heads)
#define LBA_TO_CYLSEC( d, lba ) ((lba) % (d)->lscyl)
#define LBA_TO_HDSEC( d, lba )	((lba) % (d)->lstrk)

/*
 * Physical offsets to ... (offsets are always physical)
 */
#define OFFSET_TO_PBA( d, off )     ((off) / (d)->secsz )
#define OFFSET_TO_TRK( d, off )     ((off) / (d)->ptrksz)
#define OFFSET_TO_CYL( d, off )     ((off) / (d)->pcylsz)
#define OFFSET_TO_HEAD( d, off )    ((off) / (d)->secsz % (d)->heads )
#define OFFSET_TO_CYLSEC( d, off )  ((off) / (d)->secsz % (d)->pscyl )
#define OFFSET_TO_HDSEC( d, off )   ((off) / (d)->secsz % (d)->pstrk )

/*
 * Track to ...
 */
#define TRK_TO_PBA( d, trk )        ((trk) * (d)->pstrk )
#define TRK_TO_LBA( d, trk )        ((trk) * (d)->lstrk )
#define TRK_TO_CYL( d, trk )        ((trk) / (d)->heads )
#define TRK_TO_HEAD( d, trk )       ((trk) % (d)->heads )
#define TRK_TO_CYLSEC( d, trk )     (TRK_TO_HEAD((d),trk) * (d)->pstrk)
#define TRK_TO_OFFSET( d, trk )     ((trk) * (d)->ptrksz)

/* ---------------------------------------------------------------- */
/* State-free operations */

/* unconditional */
int s4swapi( int i );
int s4swaph( int i );

/* does work on LE machine */
int s4bei( int i );
int s4beh( int i );

/* Dump with hex and char; prefix is absolute addr or relative.
   Relative offsets shown decimal and hex.

   Absolute:
   e4fe00a0: xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxx   abcdefghijklmnop 

   Relative:
     0    0: xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxx   abcdefghijklmnop
   ...
   512  200: xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxx   abcdefghijklmnop

   width 16 gives dumps that fit in 80 chars
   width 32 fits 1k on a screen vertically.

   breaks 0 = none;
   breaks n = breaks after x bytes (128, 256, etc.);
*/
void s4dump( char *b, size_t len, int absolute, int width, int breaks );

/* decode s4err enum */
const char *s4errstr( s4err err );

/* decode s4btype enum */
const char *s4btypestr( s4btype type );

const char *s4atypestr( s4atype atype );

/* do an lseek and a read, issuing errors. */
s4err  s4_seek_read( int fd, int offset, char *buf, int blen );

/* do an lseek and a write, issuing errors. */
s4err  s4_seek_write( int fd, int offset, char *buf, int blen );


/* do bad block mapping given a bad block table and strk */
int s4_lba2pba( int lba, struct s4_bbe *bbt, int nbb, int lstrk );

/* reverse map physical block to lba.  FIXME - May not be useful. */
int s4_pba2lba( int pba, struct s4_bbe *bbt, int nbb, int strk, int heads );

/* ---------------------------------------------------------------- */
/* S4_DISK */

void  s4_init_vol( const char *name, int fd, int cyls, int heads, 
                    int secsz, int scyl, 
                    struct s4_vhbd *opt_vhbd,
                    s4_vol *d );

/* Open disk and populate vinfo. mode O_READ, O_WRITE etc. */
s4err s4_open_vol( const char *dfile, int mode, s4_vol *vinfo );

void s4_vol_set_part( s4_vol *vinfo, int pnum, int scyl, int numtrk );

/* show disk information */
void  s4_vol_show( s4_vol *vinfo );

/* close a disk */
s4err s4_vol_close( s4_vol *vinfo );

/* map LBA to PBA */
int   s4_vol_lba2pba( s4_vol *vinfo, s4_bbt *bbt, int lba, int lstrk );


/* import file to a partition in a volume */
s4err s4_vol_import( s4_vol *ovinfo, int opnum, int ooffblks, int ifd );

/* export cnt blocks from pnum starting at ioffblks to fd. */
s4err s4_vol_export( s4_vol *ivinfo, int ipnum, int ioffblks, int icnt, 
                     int ofd );

/* export icnt blocks from ivinfo partion at ioffblks
   to ovinfo partition at ooffblks */
s4err s4_vol_transfer( s4_vol *ovinfo, int opnum, int ooffblks,
                       s4_vol *ivinfo, int ipnum, int ioffblks,
                       int icnt );

/* ---------------------------------------------------------------- */
/* S4_FILSYS */


/* one way: given open disk, open filsystem on a partition 'volnum */
s4err s4_vol_open_filsys( s4_vol *vinfo, int volnum, s4_filsys *fs );

/* another way: open filesystem from path, no volume header */
s4err s4_open_filsys( const char *path, s4_filsys *fs );

/* given open FS, read LBA from it, scaling blk by bmul;
   if in a partition, add partlba, then convert to PBA  */
s4err s4_filsys_read_blk( s4_filsys *xfs, s4_daddr blk, int bmul,
                          char *buf, int blen );

/* map partition LBA to PBA handling bad blocks */
int  s4_filsys_lba2pba( s4_filsys *xfs, int lba );

/* show FS header info. */
s4err s4_filsys_show( s4_filsys *fs );

/* Stop working on filesystem */
s4err s4_filsys_close( s4_filsys *fs );

/* ---------------------------------------------------------------- */
/* S4_FSU  */

/* unconditional swap typed contents */
void s4_fsu_swap( s4_fsu *fsu, int btype );

/* decode per type; unk and raw give dump */
void s4_fsu_show( s4_fsu *fsu, int btype );


/* ---------------------------------------------------------------- */
/* struct s4_dinode */

/* Get filesystem block number from idx position in inode f_addr,
   doing the 3-byte to 4-byte conversion. */
int s4_dinode_getfblk( struct s4_dinode *inop, int idx );

/* show a single disk inode */
void s4_dinode_show( struct s4_dinode *inop );

/* ---------------------------------------------------------------- */
/* 3 byte disk addresses */

/* get a LE 3 byte address to local  */
int s4_dab2int_le( unsigned char *dab );

/* get a BE 3 byte address to local  */
int s4_dab2int_be( unsigned char *dab );

/* convert array of ints to dab's, out of place */
void s4ltol3(char *cp, int *lp, int n);

/* convert array of dab's to ints, out of place. */
void s4l3tol(int *lp, char *cp, int n);

/* byte-swapping version of ints to dabs  */
void s4ltol3r(char *cp, int *lp, int n);

/* byte-swapping version of dabs to ints  */
void s4l3tolr(int *lp, char *cp, int n);

/* ---------------------------------------------------------------- */

/* returns lenggh of encoded obuf */
int s4rl_encode( const char *inbuf, int ilen, char *obuf, int olen );

/* returns length of decode */
int s4rl_decode( const char *inbuf, int ilen, char *obuf, int olen );

#endif
