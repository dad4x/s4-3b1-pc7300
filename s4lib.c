/*
 * Library for safari4 for access, see s4lib.h
 */

/*
  From SV fsck, we see that there are the following block layouts in the FS.

	union {
		char	b_buf[BSIZE];               // buffer space 
		short	b_lnks[SPERB];              // link counts 
		daddr32_t	b_indir[NINDIR];    // indirect block
		struct filsys32 b_fs;               // super block
		struct fblk32   b_fb;               // free block 
		struct s4_dinode b_dinode[INOPB];    // inode block 
                struct direct32 b_dir[NDIRECT];     // directory
        }

   The SYSV filesystem is layed out differntly on different machines.
   On some, block 0 is unused, or used as the bad-block table.  When
   so, the superblock is at block SUPERB(1), which is SUPERBOFF bytes
   from the start.

   The CTIX filesystem is at block 0 of the partition, with no offset?

   We fudge this by considering the first 2 blocks presented when we
   open a filesystem.

   The inode table is continuous blocks starting 2 after the superblock;
   If the superblock is at LBA 0, then the first inode is at LBA 3;.
   If the superblock is at LBA 1, then the first inode is at LBA 4;

   The file system blocks begin at FIXME.

   From there, blocks are considered in FS block sized unita, and the
   inode table begins at ... fixme.

   Following the superblock are s_isize blocks for inodes.

   The root inode of the filesystem is at ROOTINO, which is inode 2.


   PROBLEMS:

   There are bad block mappings that on the 3b1 are handled by the
   operating system, transparently to the user.   It does this
   by reserving a space sector on each track, so if you have 17
   physical sectors, you only get to use 16.

   Something has to keep track of any bad blocks -- preferably
   none (which we can arrange), but we do have to skip one block
   every track.

   If we're not properly mapping, then the LBA to PBA translation
   is:

    	pba = lba + (lba % (sectors-per-track - 1)) - 1;

   which adds an extra skipped block for every preceding track.

   This is why it's good to have partitions be on track boundaries,
   even if not on cylinder boundaries.

   To have this in a file-system image file, we'll use block 0
   as the BBT, and in the superblock, use dinfo[3] as
   sectors per track.   We'll have to construct this
   when we rip a filesystem from a disk image.

   We'll use the CTIX disk data structure for the BBT, but
   we won't use their memory structures.

   Wonder if we can have one track with 400k sectors?

   Maybe have utility to add/delete the spare sectors
   when going to/from native to emulated?
*/

#include <s4lib.h>

#include <time.h>
#include <errno.h>
#include <string.h>             /* strerror */
#include <ctype.h>

/* ------------------------------------------- */
/* Types used here, but not visible to callers */

typedef struct   {

  int         mask;         /* mask to these bits */
  int         eqbits;       /* require == these set */
  const char *name;

} s4_maskname;


/* --------------------- */
/* Private constant data */

static const s4_maskname  s4_typebits[] =
  {
    { IFMT, IFDIR, "d" },
    { IFMT, IFCHR, "c" },
    { IFMT, IFBLK, "b" },
    { IFMT, IFREG, "-" },
    { IFMT, IFIFO, "f" },

    { 0, 0, NULL },              /* no match value */
  };


static const s4_maskname  s4_modebits[] =
  {
    { 0, 0, "-" },              /* no match value */

    { 00400,  000400, "r" },
    { 00200,  000200, "w" },
    { 04100,  000100, "x" },
    { 04100,  004100, "S" },

    { 00040,  000040, "r" },
    { 00020,  000020, "w" },
    { 02010,  000010, "x" },
    { 02010,  020010, "S" },
    
    { 00004,  000004, "r" },
    { 00002,  000002, "w" },
    { 01001,  000001, "x" },
    { 01001,  010001, "t" },

    { 0, 0, NULL }
  };

/* ----------------------------------------- */
/* Forward declarations of private functions */

static void s4_resdes_show( struct s4_resdes *map );
static void s4_vhbd_glorp_show( struct s4_vhbd *vhbd );
static void s4_vhbd_show( struct s4_vhbd *vhbd );

static void s4_disk_decode_partitions( s4_diskinfo *dinfo,
                                       struct s4_vhbd *vhbd );

static void s4_disk_get_bbt( s4_diskinfo *dinfo );
static void s4_disk_bbt_show( s4_diskinfo *dinfo );

static int s4_checksum32( unsigned char *p, int len, int expect );

static void s4_maskname_show( const s4_maskname *bits, int mode );
static void s4_modes_show( int mode );

static s4err s4_filsys_good_super( s4_filsys *xfs );


/* -------------- */
/* Private macros */

// static s4err s4_disk_good_lba( s4_diskinfo *dinfo, int lba );
#define s4_disk_good_lba( d, lba ) \
  ( ((lba)>=0 && (lba) < (d)->nblocks) ? s4_ok : s4_range )

// static s4err s4_filsys_good_inode( s4_filsys *fs, int ino );
#define s4_filsys_good_inode( fs, ino ) \
  ( (((ino)>=0) && (ino) < (fs)->dfs->s_ninode) ? s4_ok : s4_range )

// static s4err s4_filsys_good_fsblk( s4_filsys *fs, int fsblk );
#define s4_filsys_good_fsblk( fs, fsblk ) \
  ( (((fsblk)>=0) && ((fsblk) < (fs)->dfs->s_fsize)) ? s4_ok : s4_range )



/* ================================================================ */
/*  PUBLIC FUNCTIONS */


const char *s4errstr( s4err err )
{
  const char *s = "bad s4err";
  switch( err )
    {
    case s4_ok:         s = "OK";       break;
    case s4_error:      s = "error";    break;
    case s4_badmagic:   s = "badmagic"; break;
    case s4_open:       s = "open";     break;
    case s4_close:      s = "close";    break;
    case s4_seek:       s = "seek";     break;
    case s4_read:       s = "read";     break;
    case s4_write:      s = "write";    break;
    case s4_range:      s = "range";    break;
    default:            break;
    }
  return s;
}


const char *s4btypestr( s4btype btype )
{
  const char *s = "bad s4btype";
  switch( btype )
    {
    case s4b_unk:        s = "unknown";  break;
    case s4b_vhbd:       s = "vhbd";     break;
    case s4b_bbt:        s = "bbt";      break;

    case s4b_super:      s = "super";    break;
    case s4b_ino:        s = "ino";      break;
    case s4b_idx:        s = "indirect"; break;
    case s4b_dir:        s = "dir";      break;
    case s4b_linkcnt:    s = "linkcnt";  break;
    case s4b_free:       s = "free";     break;

    case s4b_raw:        s = "raw";      break;
    case s4b_last_fs:    s = "last_fs";  break;
    default:
      break;
    }
  return s;
}



int s4swapi( int i )
{
  int rv = i;

#ifdef S4_LITTLE_ENDIAN

  unsigned char *src = (unsigned char*)&i;
  unsigned char *dst = (unsigned char*)&rv;

  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];

#endif
  return rv;
};


int s4swaph( int i )
{
  int rv = i;

#ifdef S4_LITTLE_ENDIAN
  
  unsigned char *src = (unsigned char*)&i;
  unsigned char *dst = (unsigned char*)&rv;

  dst[2] = 0;
  dst[3] = 0;
  dst[0] = src[1];
  dst[1] = src[0];

#endif

  return rv;
};

/* does work on LE machine */
int s4bei( int i )
{
#ifdef S4_LITTLE_ENDIAN
  return s4swapi( i );
#else
  return i;
#endif
}

int s4beh( int i )
{
#ifdef S4_LITTLE_ENDIAN
  return s4swaph( i );
#else
  return i;
#endif
}


void s4dump( char *b, size_t len, int absolute )
{
  int i, j;
  int left = len;
  char	*base = absolute ? b : 0;
  int   width = 32;

  /* print header line */
  if( absolute )
    printf("Address ");
  else
    printf( len > 1024 ? "  Dec  Hex" : " Dec Hex" );

  for( j = 0; j < width; j++ )
    {
      if( !(j % 8 ) )
        printf(" ");
      printf("%2x", j & 0xf );
    }
  printf("   ");
  for( j = 0; j < width; j++ )
    if( j & 0xf )
      printf( "%x", j & 0xf );
    else
      printf( " " );
  printf("\n");

  /* print one line at a time until nothing left */
  for( i = 0; left > 0 ; )
    {
      /* prefix */
      if( absolute )
        printf("%8p:", base + i );
      else 
        printf(len > 1024 ? "%5d %04x" : "%4d %03x", i, i );
      
      /* hex parts */
      for( j = 0; j < width && j < left; j++ )
        {
          if( !(j % 8) )
            printf(" ");
          printf("%02x", (b[i + j]) & 0xff );
        }
      
      /* rest of hex part if short */
      for( ; j < width; j++ )
        {
          if( !(j % 8) )
            printf(" ");
          printf("  ");
        }

      /* char part of line */
      printf("   ");
      for( j = 0; j < width && j < left; j++ )
	{
	  printf("%c", s4showc( b[i + j] & 0xff) );
	}
      printf("\n");

      left -= j;
      i    += j;
    }
}



int s4_lba2pba( int lba, s4_bbt *bbt, int nbb, int lstrk )
{
  int pba = lba + (lba/(lstrk)); /* one spare sector per track */
  int npba;

  if( nbb )
    {
      int track = lba / lstrk;
      int hdsec = lba % lstrk;
      int i, cylsec;
      
      for( i = 0 ; i < nbb ; i++ )
        {
          cylsec = bbt[i].cyl;
          if( !cylsec )
            break;

          if( track == (cylsec / (lstrk+1)) && hdsec == (cylsec % (lstrk+1)) )
            {
              npba = (track * (lstrk+1)) + 16;

              printf("Mapping lba %d from pba %d to %d. BB cyl %d trk %d hdsec %d\n",
                     lba, pba, npba, cylsec, track, hdsec, pba, npba );

              break;
            }
        }
    }
  return pba;
}



int s4_pba2lba( int pba, struct s4_bbe *bbt, int nbb, int strk, int heads )
{
  int lba   = pba - (pba/strk);
  int hdsec = pba % strk;

  /* if physical sector is the reserved one, we remapped it. */
  if( hdsec == strk - 1 )
    {
      if( nbb )
        {
          int track = pba / strk;
          int i, cylsec;

          for( i = 0; i < nbb ; i++ )
            {
              cylsec = bbt[i].cyl;
              if( track == (cylsec/strk) )
                {
                  /* this is almost certainly wrong: test & FIXME */
                  lba = (track * (strk-1)) + ((cylsec % strk) % heads);
                }
            }
        }
      else
        {
          printf("Remapped PBA %d, but no bad block table!?\n", pba );
        }
    }
  

  return lba;
}



/* initialize everything to "s4_disk_show"-able state.  
   Do not clear, because fd may be open, vhbd may already be read */

void  s4_init_disk( const char *name, int fd, 
                    int cyls, int heads, int secsz, int pscyl,
                    struct s4_vhbd *opt_vhbd,
                    s4_diskinfo *d )
{
  d->fname  = strdup( name );
  d->fd     = fd;
  d->nbb    = 0;
  d->bbt    = &d->bbt_fsu.bbt[0];

  d->cyls   = cyls;
  d->heads  = heads;
  d->secsz  = secsz;

  d->pstrk  = pscyl / heads;
  d->lstrk  = d->pstrk - 1;

  d->pscyl  = pscyl;
  d->lscyl  = d->pscyl - heads; /* less one per track */

  d->pblks  = d->cyls * d->pscyl;
  d->lblks  = d->cyls * d->lscyl;

  d->ptrksz = d->pstrk * d->secsz;
  d->ltrksz = d->lstrk * d->secsz;
  
  d->pcylsz = d->pscyl * d->secsz;
  d->lcylsz = d->lscyl * d->secsz;
  
  d->loader_lba   = 0;
  d->loader_nblks = 0;

  d->bbt_lba      = 0;
  d->bbt_nblks    = 0;

  d->bbt = &d->bbt_fsu.bbt[0];
  d->nbb = 0;

  /* setup fake partition at the start */
  d->nparts = 1;
  d->parts[0].strk     = 0;
  d->parts[0].ntrk     = 1;
  d->parts[0].pblks    = d->pstrk;
  d->parts[0].lblks    = d->lstrk;
  d->parts[0].partoff  = 0;
  d->parts[0].partlba  = 0;  

  if( opt_vhbd )
    {
      /* vhbd.resmap units are in FS 1k blocks, convert to PBA */
      d->loader_lba   = opt_vhbd->resmap[0].blkstart * 2;
      d->loader_nblks = opt_vhbd->resmap[0].nblocks  * 2;
      d->bbt_lba      = opt_vhbd->resmap[1].blkstart * 2;
      d->bbt_nblks    = opt_vhbd->resmap[1].nblocks * 2;

      /* take real partitions instead */
      s4_disk_decode_partitions( d, opt_vhbd );
    }
}


s4err s4_open_disk( const char *dfile, int mode, s4_diskinfo *dinfo )
{
  s4err	 rv = s4_ok;
  int    fd;

  memset( dinfo, 0, sizeof(*dinfo) );

  fd = open( dfile, mode, 0 );
  if( fd < 0 )
    {
      printf("Unable to open '%s': %s\n", dfile, strerror( errno ));
      rv = s4_open;
      goto done;
    }

  rv =  s4_seek_read( fd, 0, (char*)&dinfo->vhbd, 512 );
  if( s4_ok != rv )
    goto done;

  if( S4_VHBMAGIC == dinfo->vhbd.magic )
    {
      dinfo->doswap = 0;
    }
  else if( S4_VHBMAGIC == s4swapi( dinfo->vhbd.magic ) )
    {
      dinfo->doswap = 1;
      s4_fsu_swap( (s4_fsu*)&dinfo->vhbd, s4b_vhbd );
    }
  else
    {
      s4_fsu_show( (s4_fsu*)&dinfo->vhbd, s4b_vhbd );

      rv = s4_badmagic;
      goto done;
    }

  s4_init_disk( dfile, fd, 
                dinfo->vhbd.dsk.cyls, 
                dinfo->vhbd.dsk.heads,
                dinfo->vhbd.dsk.sectorsz, 
                dinfo->vhbd.dsk.pseccyl, 
                &dinfo->vhbd,
                dinfo );

  if( dinfo->pscyl != dinfo->vhbd.dsk.pseccyl )
    {
      printf("WARNING: computed sectors/cyl %d not reported %d\n",
             dinfo->pscyl, dinfo->vhbd.dsk.pseccyl );
    }

  s4_disk_get_bbt( dinfo );

 done:
  return rv;
}




s4err s4_disk_import( s4_diskinfo *odinfo, int opnum, int ooffblks, int ifd )
{
  int          rv = 0;
  s4err        err = s4_ok;
  s4_diskinfo *d = odinfo;
  long         offset;
  int          lbar, lbaa;      /* relative and absolute blocks */
  int          partlba =  d->parts[opnum].partlba;
  int          len;
  char         buf[ 512 ];

  /* copy everything from the input fd to successive LBA's */
  for( lbar = 0; lbar < d->parts[opnum].lblks && s4_ok == err; lbar++ )
    {
      len = read( ifd, buf, sizeof(buf));
      if( len <= 0 )
        {
          if( len < 0 )
            {
              printf("read error %s\n", strerror(errno));
              err = s4_read;
            }
          break;
        }

      lbaa   = partlba + ooffblks + lbar;
      offset = LBA_TO_DISK_OFFSET(d, lbaa);

      err    = s4_seek_write( odinfo->fd, offset, buf, len );
      if( s4_ok != err )
        {
          printf("seek write error, offset %ld\n", offset );
          break;
        }
    }
 
  printf("Imported %d blocks into partition %d\n", lbar, opnum );

  return err;
}

/* export cnt lba's from pnum starting at ioffblks to fd. */
s4err s4_disk_export( s4_diskinfo *idinfo, int ipnum, int ioffblks,
                      int icnt, int ofd )
{
  s4_diskinfo   *d   = idinfo;
  s4err          err = s4_ok;
  long           offset;
  int            lbar, lbaa, rv;
  long           partlba = d->parts[ipnum].partlba;

  char         buf[ 512 ];
  
  for( lbar = 0; lbar < icnt && s4_ok == err; lbar++ )
   {
     lbaa   = partlba + ioffblks + lbar;
     offset = LBA_TO_DISK_OFFSET(d, lbaa);

     err    = s4_seek_read( d->fd, offset, buf, sizeof(buf) );
     if( s4_ok != err )
       {
         printf("seek read error, offset %d\n", offset );
         break;
       }

     rv = write( ofd, buf, sizeof(buf) );
     if( rv != sizeof(buf) )
       {
         printf("%s writing output\n", strerror(errno));
         err = s4_write;
       }
   }

  if( s4_ok == err )
    printf("Exported %d blocks from partition %d\n", lbar, ipnum );

  return err;
}

/* export icnt lba's from idinfo partion at ioffblks
   to odinfo partition at ooffblks */
s4err s4_disk_transfer( s4_diskinfo *odinfo, int opnum, int ooffblks,
                        s4_diskinfo *idinfo, int ipnum, int ioffblks,
                        int icnt )
{
  s4err   err = s4_ok;

  int     lbar;
  int     ilbaa, ipartlba;
  int     olbaa, opartlba;
  long    ioffset, ooffset;
  
  char    buf[ 512 ];
  
  ipartlba = idinfo->parts[ipnum].partlba;
  opartlba = odinfo->parts[opnum].partlba;

  for( lbar = 0; lbar < icnt && s4_ok == err; lbar++ )  
    {
      ilbaa   = ipartlba + ioffblks + lbar;
      ioffset = LBA_TO_DISK_OFFSET(idinfo, ilbaa);

      err     = s4_seek_read( idinfo->fd, ioffset, buf, sizeof(buf) );
      if( s4_ok != err )
        {
          printf("seek read error, offset %ld\n", ioffset );
          break;
        }

      olbaa   = opartlba + ooffblks + lbar;
      ooffset = LBA_TO_DISK_OFFSET(odinfo, olbaa);

      err     = s4_seek_write( odinfo->fd, ooffset, buf, sizeof(buf) );
      if( s4_ok != err )
        {
          printf("seek write error, offset %ld\n", ooffset );
          break;
        }
    }
  if( s4_ok == err )
    printf("Transferred %d blocks\n", lbar );

  return err;
}




void s4_disk_set_part( s4_diskinfo *dinfo, int pnum, int strk, int numtrk )
{
  s4_part  *part = &dinfo->parts[ pnum ];

  part->strk    = strk;
  part->ntrk    = numtrk;

  part->pblks   = numtrk * dinfo->pstrk;
  part->lblks   = numtrk * dinfo->lstrk;
  part->partoff = TRK_TO_OFFSET( dinfo, strk );
  part->partlba = TRK_TO_LBA( dinfo, strk );
}


static void s4_disk_decode_partitions( s4_diskinfo *dinfo,
                                       struct s4_vhbd *vhbd )
{
  int       i;
  int       strk, ntrk, ltrk;

  dinfo->nparts = 0;
  for( i = 0; i < S4_MAXSLICE ; i++ )
    {
      strk = vhbd->partab[ i ].sz.strk;

      if( i && !strk )
        break;

      /* if there is a next partition... */
      if( i + 1 < S4_MAXSLICE )
        ntrk = vhbd->partab[ i + 1 ].sz.strk;
      else
        ntrk = 0;

      /* either have next track, or take to the end of the disk */
      ltrk = ntrk ? ntrk - 1 : (dinfo->cyls * dinfo->heads) - 1;

      /* ltrk == strk is a one track partition */
      if( ltrk < strk )
        {
          printf("last track %d start %d ntrk %d in partition %d\n",
                 ltrk, strk, ntrk, i );
          abort();
        }
      s4_disk_set_part( dinfo, i, strk, ltrk - strk + 1 );
    }

  dinfo->nparts    = i;
}


static void s4_disk_get_bbt( s4_diskinfo *dinfo )
{
  int rv, i;

  dinfo->nbb = 0;
  dinfo->bbt = &dinfo->bbt_fsu.bbt[0];

  rv =  s4_seek_read( dinfo->fd,
                      LBA_TO_DISK_OFFSET(dinfo, dinfo->bbt_lba ),
                      dinfo->bbt_fsu.buf,
                      sizeof(dinfo->bbt_fsu.buf));
  if( s4_ok == rv )
    {
      if( dinfo->doswap )
        s4_fsu_swap( &dinfo->bbt_fsu, s4b_bbt );

      for( i = 0; i < S4_NBB && dinfo->bbt[i].cyl; i++ )
        dinfo->nbb++;
    }
}


void s4_vhbd_show( struct s4_vhbd *vhbd )
{
  int i;

  printf("  Magic %x %s %x, checksum %x (unchecked)\n",
	 vhbd->magic,
	 vhbd->magic == S4_VHBMAGIC ? "is" : "IS NOT",
	 S4_VHBMAGIC,
	 vhbd->chksum );

  printf("  '%-6s' %d cyls, %d heads, %d sectors/track %d sec/cyl\n",
         vhbd->dsk.name,
         vhbd->dsk.cyls,
         vhbd->dsk.heads,
         vhbd->dsk.psectrk,
         vhbd->dsk.pseccyl );

  printf("  Partitions:\n");
  for( i = 0; i < S4_MAXSLICE; i++ )
    {
      if( i && !vhbd->partab[i].sz.strk )
        break;
      else
        printf("  [%d] start track %d\n", i, vhbd->partab[i].sz.strk );
    }
  printf("\n");

  s4_resdes_show( vhbd->resmap );
}


void s4_disk_show( s4_diskinfo *dinfo )
{
  int	    i;
  s4_part  *part;

  printf("Showing disk header from '%s'\n"
         "Magic %x %s %x, checksum %x (unchecked) %s\n",
         dinfo->fname,
	 dinfo->vhbd.magic,
	 dinfo->vhbd.magic == S4_VHBMAGIC ? "is" : "IS NOT",
	 S4_VHBMAGIC,
	 dinfo->vhbd.chksum,
         dinfo->doswap ? "SWAPPED" : "no swap needed" );

  printf(" %d cyls, %d heads; %d s/trk, %d s/cyl; step %d, %d byte sectors\n",
	 dinfo->cyls,
	 dinfo->heads,
	 dinfo->pstrk,
	 dinfo->pscyl,
	 dinfo->vhbd.dsk.step,
	 dinfo->secsz );

  printf(" %d blocks, %dk bytes %dM bytes\n",
	 dinfo->cyls * dinfo->pscyl,
	 dinfo->cyls * dinfo->pscyl * dinfo->secsz / 1024,
	 dinfo->cyls * dinfo->pscyl * dinfo->secsz / 1024 / 1024 );

  s4_vhbd_glorp_show( &dinfo->vhbd );

  printf("Loader is %d blocks at PBA %d\n", 
	 dinfo->loader_nblks, dinfo->loader_lba );

  printf("Bad Block Table is %d blocks at PBA %d\n", 
	 dinfo->bbt_nblks, dinfo->bbt_lba );

  printf("\nPartitions: (%d)\n", dinfo->nparts );
  printf("Num  STrk  Ntrk  PBA   LBA  Cyl     Offset   ");
  printf("PSects      PSize        LSects      LSize\n");
  printf("---  ----  ---- ----- ----- ----  ---------  ");
  printf("------- ---------------  ------- ---------------\n");

  for( i = 0; i < dinfo->nparts; i++ )
    {
      part = &dinfo->parts[i];

      printf("%3d %5d %5u %5u %5u %4u %10u %8d %6uk %6.2fM %8d %6uk %6.2fM\n",
	     i, 
	     part->strk,
             part->ntrk,
	     TRK_TO_PBA( dinfo, part->strk ),
	     TRK_TO_LBA( dinfo, part->strk ),
	     TRK_TO_CYL( dinfo, part->strk ),
	     PNUM_TO_OFFSET( dinfo, i ),
	     part->pblks,
	     part->pblks * dinfo->secsz / 1024,
	     ((double)part->pblks) * dinfo->secsz / 1024 / 1024,
	     part->lblks,
	     part->lblks * dinfo->secsz / 1024,
	     ((double)part->lblks) * dinfo->secsz / 1024 / 1024 );
    }

  printf("\nSpecial sections in part 0\n");
  s4_resdes_show( dinfo->vhbd.resmap );

  s4_disk_bbt_show( dinfo );
}

static void s4_resdes_show( struct s4_resdes *map )
{
  int i;
  printf("  Resmap:\n");
  for( i = 0; i < 8 ; i++ )
    {
      if( map[i].blkstart )
	{
	  printf("  [%d] at FSBLK %d, blocks %d\n",
		 i,
		 map[i].blkstart,
                 map[i].nblocks );
	}
    }
  printf("\n");
}


s4err s4_disk_close( s4_diskinfo *dinfo )
{
  s4err rv = s4_ok;

  if( dinfo->fname )
    free( dinfo->fname );

  if( dinfo->fd > 0 )
    {
      if( close( dinfo->fd ) < 0 )
        {
          printf("%s closing fd %d\n", strerror(errno), dinfo->fd );
          rv = s4_close;
        }
      dinfo->fd = -1;
    }

  memset( dinfo, 0, sizeof(*dinfo) );

  return rv;
}


s4err s4_seek_read( int fd, int offset, char *buf, int blen )
{
  s4err rv  = s4_ok;
  long  off = lseek( fd, (long)offset, 0 );
  if( off < 0 )
    {
      printf("%s seeking to offset %u in fs %d\n",
             strerror( errno ), offset, fd );
      rv = s4_seek;
    }
  else
    {
      off = read( fd, buf, blen );
      if( off < 0 )
        {
          printf("%s at offset %ld wanted %d got %d\n",
                 strerror( errno ), offset, blen, off );
          rv = s4_read;
        }
    }


#if 0
  printf("seek_read LBA %d offset %d for %d returns %d %s\n",
         offset / 512,
         offset, 
	 blen, rv, s4errstr(rv) );
#endif
  return rv;
}

s4err s4_seek_write( int fd, int offset, char *buf, int blen )
{
  s4err rv  = s4_ok;
  long  off = lseek( fd, (long)offset, 0 );
  if( off < 0 || off != offset )
    {
      printf("%s seeking to offset %u in fs %d\n",
             strerror( errno ), offset, fd );
      rv = s4_seek;
    }
  else
    {
      off = write( fd, buf, blen );
      if( off < 0 || off != blen )
        {
          printf("%s writing at offset %d wanted %d got %ld\n",
                 strerror( errno ), offset, blen, off );
          rv = s4_read;
        }
    }

#if 0
  printf("seek_write LBA %d offset %d for %d returns %d %s\n",
         offset / 512,
         offset, 
	 blen, rv, s4errstr(rv) );
#endif
  return rv;
}


s4err s4_disk_open_filsys( s4_diskinfo *dinfo, int volnum, s4_filsys *fs )
{
  s4err rv;
  int   offset;
  int   block;
  int   fd;

  fs->dinfo   = dinfo;
  fs->part    = &dinfo->parts[ volnum ];
  fd          = dinfo->fd;

  /* FS should be at 512 bytes from 0 */

  for( block = 1; block < 3; block++ )
    {
      offset = fs->part->partoff + PBA_TO_OFFSET( dinfo, block );

      rv = s4_seek_read( fd, offset, fs->super.buf, 512 );
      if( s4_ok != rv )
	goto done;
  
      fs->doswap   = 0;
      rv = s4_filsys_good_super( fs );

      if( s4_ok == rv )
	{
	  fs->bksz     = fs->super.super.s_type == 2 ? 1024 : 512;
#if 0
          printf("FS blocks start at LBAR %d\n", fs->super.super.s_isize);
#endif
	  break;
	}
      else
        {
          printf("FS Superblock at lbar %d offset %d is %s\n", 
                 block, offset, s4errstr(rv) );
        }
    }

 done:
  return rv;
}


s4err s4_open_filsys( const char *path, s4_filsys *fs )
{
  s4err          rv;
  s4_diskinfo   *d = &fs->fakedisk;
  struct stat    sb;

  /* open the file and get it's size  */
  d->fname = strdup( path );
  d->fd    = open( path, 002, 0 );
  if( d->fd < 0 )
    {
      printf("%s opening file '%s'\n", strerror(errno), path );
      rv = s4_open;
      goto done;
    }

  if( fstat( d->fd, &sb ) < 0 )
    {
      printf("%s checking status of '%s'\n", strerror(errno), path );
      rv = s4_error;
      goto done;

    }

  /* Fake disk has one very big track, fs in partition 2, and no BBT */
  s4_init_disk( "fakedisk", d->fd, 1, 1, 512, sb.st_size / 512 + 1, NULL, d );
  s4_disk_set_part( d, 0, 0, 0 );
  s4_disk_set_part( d, 1, 0, 0 );
  s4_disk_set_part( d, 2, 0, 1 );
  d->nparts = 3;

  /* open FS on normal partition 2 of fake disk */
  rv = s4_disk_open_filsys( d, 2, fs );

 done:

  return rv;
}


s4err s4_filsys_show( s4_filsys *xfs )
{
  struct s4_dfilsys *fs = &xfs->super.super;

  if( S4_FsMAGIC != fs->s_magic )
    {
      printf("Bad magic in Filsys\n");
      return s4_badmagic;
    }
  
  s4_fsu_show( &xfs->super, s4b_super );

  return s4_ok;
}


/* is fblk an LBA relative to part start, or a filesystem block? */

s4err s4_filsys_read_blk( s4_filsys *xfs, s4_daddr blk, int bmul,
                          char *buf, int blen  )
{
  int rv     = s4_ok;
  int offset;
  int lbaa   = xfs->part->partlba + (blk * bmul);

  offset = LBA_TO_FILSYS_OFFSET( xfs, lbaa );

  printf("read_blk: lbaa %d\n", lbaa );

  rv     = s4_seek_read( xfs->dinfo->fd, offset, buf, blen );

  printf("read %s %d, LBAA %d, offset %u for %d returns %d %s\n",
         bmul == 1 ? "LBAR" : "FS BLK",
	 blk, lbaa, offset, 
	 blen, rv, s4errstr(rv) );

  return rv;
}



s4err s4_filsys_close( s4_filsys *fs )
{
  s4err rv = s4_ok;

  /* if we opened it, we close it */
  if( fs->dinfo == &fs->fakedisk )
    rv = s4_disk_close( fs->dinfo );

  memset( fs, 0, sizeof(*fs) );

  return rv;
}

/* byte swap values, unconditional */
void s4_fsu_swap( s4_fsu *fsu, int btype )
{
  int i;

  switch( btype )
    {
    case s4b_vhbd:
      {
        fsu->vhbd.magic          = s4swapi( fsu->vhbd.magic );
        fsu->vhbd.chksum         = s4swapi( fsu->vhbd.chksum );
        fsu->vhbd.dsk.cyls       = s4swaph( fsu->vhbd.dsk.cyls );
        fsu->vhbd.dsk.heads      = s4swaph( fsu->vhbd.dsk.heads );
        fsu->vhbd.dsk.psectrk    = s4swaph( fsu->vhbd.dsk.psectrk );
        fsu->vhbd.dsk.pseccyl    = s4swaph( fsu->vhbd.dsk.pseccyl );
        fsu->vhbd.dsk.sectorsz   = s4swaph( fsu->vhbd.dsk.sectorsz );
        for( i = 0; i < S4_MAXSLICE; i++ )
          {
            fsu->vhbd.partab[i].sz.strk =
              s4swapi( fsu->vhbd.partab[i].sz.strk );
          }
        for( i = 0; i < 4 ; i++ )
          {
            fsu->vhbd.resmap[i].blkstart =
              s4swapi( fsu->vhbd.resmap[i].blkstart );
            fsu->vhbd.resmap[i].nblocks =
              s4swaph( fsu->vhbd.resmap[i].nblocks );
          }
      }
      break;

    case s4b_super:
      {
        struct s4_dfilsys *fs = &fsu->super;

        fs->s_magic = s4swapi( fs->s_magic );
        fs->s_isize = s4swaph( fs->s_isize );
        fs->s_fsize = s4swapi( fs->s_fsize );

        fs->s_nfree = s4swaph( fs->s_nfree );
        for( i = 0; i < S4_NICFREE; i++ )
          fs->s_free[i] = s4swapi( fs->s_free[i] );

        fs->s_ninode = s4swaph( fs->s_ninode );
        for( i = 0; i < S4_NICINOD; i++ )
          fs->s_inode[i] = s4swaph( fs->s_inode[i] );

        for( i = 0; i < 4; i++ )
          fs->s_dinfo[i] = s4swaph( fs->s_dinfo[i] );

        fs->s_time   = s4swapi( fs->s_tfree );
        fs->s_tfree  = s4swapi( fs->s_tfree );
        fs->s_tinode = s4swaph( fs->s_tinode );
        fs->s_type   = s4swapi( fs->s_type );
      }
      break;

    case s4b_ino:
      {
        struct s4_dinode *inop;

        for( i = 0; i < S4_INOPB ; i++ )
          {
            inop = &fsu->dino[i];
            inop->di_mode  = s4swaph( inop->di_mode );
            inop->di_nlink = s4swaph( inop->di_nlink );
            inop->di_uid   = s4swaph( inop->di_uid );
            inop->di_gid   = s4swaph( inop->di_gid );
            inop->di_size  = s4swapi( inop->di_size );

            /* block list entries are handled when pulled
               out as expanded from 3 to 4 bytes */

            inop->di_atime = s4swapi( inop->di_atime );
            inop->di_mtime = s4swapi( inop->di_mtime );
            inop->di_ctime = s4swapi( inop->di_ctime);
          }
      }
      break;

    case s4b_idx:
      {
        for( i = 0; i < S4_NINDIR; i++ )
          fsu->indir[ i ] = s4swapi( fsu->indir[ i ] );
      }
      break;

    case s4b_dir:
      {
        for( i = 0; i < S4_NDIRECT ; i++ )
          {
            fsu->dir[i].d_ino = s4swaph( fsu->dir[i].d_ino );
            if( fsu->dir[i].d_ino > 65000 )
              {
                printf("%s: oops?\n", __func__);
                getpid();
              }
          }
      }
      break;

    case s4b_linkcnt:
      {
        for( i = 0; i < S4_SPERB; i++ )
          fsu->links[ i ] = s4swaph( fsu->links[ i ] );
      }
      break;

    case s4b_free:
      {
        fsu->free.df_nfree = s4swaph( fsu->free.df_nfree );        

        for( i = 0; i < S4_NICFREE; i++ )
            fsu->free.df_free[ i ] = s4swapi( fsu->free.df_free[ i ] );        
      }
      break;

    case s4b_bbt:
      {
        for( i = 0; i < S4_NBB && fsu->bbt[ i ].cyl ; i++ )
          {
            fsu->bbt[ i ].cyl    = s4swaph( fsu->bbt[ i ].cyl );
            fsu->bbt[ i ].altblk = s4swaph( fsu->bbt[ i ].altblk );
            fsu->bbt[ i ].nxtind = s4swaph( fsu->bbt[ i ].nxtind );
          }
      }
      break;

    default:
      /* do nothing */
      break;
    }
}


static void s4_maskname_show( const s4_maskname *bits, int mode )
{
  int                i;
  const s4_maskname *defname = NULL;
  
  if( !bits->mask )
    defname = bits++;

  for( i = 0; bits[i].name ; i++ )
    {
      if( ((mode & bits[i].mask) == bits[i].eqbits) )
        printf("%s", bits[i].name );
      else if ( defname )
        printf("%s", defname->name );
    }

  return;
}

static void s4_modes_show( int mode )
{
  s4_maskname_show( s4_typebits, mode );
  s4_maskname_show( s4_modebits, mode );
  printf("\n");
};


int s4_dinode_getfblk( struct s4_dinode *inop, int idx )
{
  int            rv;

  rv = s4_dab2int_le( (unsigned char*)&inop->di_addr[ 3*idx ] );

  return rv;
}


void s4_dinode_show( struct s4_dinode *inop )
{
  int i, x;

  printf("  m:%06o nl:%-3d u:%-3d g:%-3d size %6d  ",
         inop->di_mode,
         inop->di_nlink,
         inop->di_uid,
         inop->di_gid,
         inop->di_size );

  s4_modes_show( inop->di_mode );

  printf("     immediate blocks:");
  for( i = 0; i < (sizeof(inop->di_addr)/3); i++ )
    {
      if( !(i % 16) )
        printf("\n     ");

      x = s4_dinode_getfblk( inop, i );
      printf("%5d ", x );
      if( !x ) 
        break;
    }
  printf("\n");
}


/* show a block, decoded as btype */
void s4_fsu_show( s4_fsu *fsu, int btype )
{
  int i, x;

  switch( btype )
    {
    case s4b_vhbd:
      s4_vhbd_show( &fsu->vhbd );
      break;

    case s4b_super:
      {
        struct s4_dfilsys *fs = &fsu->super;

        printf("FILSYS:\n");
        printf("  Filesystem name '%c%c%c%c%c%c' pack '%c%c%c%c%c%c'\n",
               s4showc(fs->s_fname[0] ),
               s4showc(fs->s_fname[1] ),
               s4showc(fs->s_fname[2] ),
               s4showc(fs->s_fname[3] ),
               s4showc(fs->s_fname[4] ),
               s4showc(fs->s_fname[5] ),

               s4showc(fs->s_fpack[0] ),
               s4showc(fs->s_fpack[1] ),
               s4showc(fs->s_fpack[2] ),
               s4showc(fs->s_fpack[3] ),
               s4showc(fs->s_fpack[4] ),
               s4showc(fs->s_fpack[5] ) );

        printf("  %s magic %x (%x), isize %d, fsize %d ninode %d\n",
               fs->s_magic == S4_FsMAGIC ? "GOOD" : "bad",
               fs->s_magic, S4_FsMAGIC,
               fs->s_isize, fs->s_fsize, fs->s_ninode );
        printf("  tfree %d, tinode %d, type %d\n",
               fs->s_tfree, fs->s_tinode, fs->s_type );

        if( S4_FsMAGIC != fs->s_magic )
          return;

        printf("  FREE BLOCKS:");
        for( i = 0; i < S4_NICFREE; i++ )
          {
            if(  !(i % 8) )
              printf("\n    ");

            x = fs->s_free[i];
            printf("%5d, ", x );
            if( x < 0 || x > 20000 )
              break;
          }
        printf("\n");
        
        printf("  FREE INO:");
        for( i = 0; i < S4_NICINOD; i++ )
          {
            if( !(i % 8) )
              printf("\n    ");
            x = fs->s_inode[i];
            printf("%5d, ", x );
            if( x < 0 || x > 20000 )
              break;
          }
        printf("\n");

        printf("  DISKINFO:\n");
        for( i = 0; i < 4; i++ )
          printf("%8d,", fs->s_dinfo[i] );
        printf("\n");
      }
      break;

    case s4b_ino:
      {
        struct s4_dinode *inop;

        printf("INODE:\n");
        for( i = 0; i < S4_INOPB ; i++ )
          {
            inop =  &fsu->dino[i];

            s4_dinode_show( inop );

            /* break once it stops looking valid */
            if( inop->di_nlink < 0 || inop->di_nlink > 200 ||
                inop->di_size < 0  || inop->di_size > 2*1024*1024 ||
                inop->di_uid < 0 )
              break;
          }
        printf("\n");
      }
      break;

    case s4b_idx:
      {
        printf("INDEX:");
        for( i = 0; i < S4_NINDIR; i++ )
          {
            if( !(i%8) )
              printf("\n    ");
            x = fsu->indir[i];
            printf("%d, ", x );
            if( x <= 0 || x > 20000 )
              break;
          }
        printf("\n");
      }
      break;

    case s4b_dir:
      {
        printf("DIR:\n");
        for( i = 0; i < S4_NDIRECT ; i++ )
          {
            /* skip deleted entries with inode 0 */
            // if( fsu->dir[i].d_ino )
              {
                if( (unsigned)fsu->dir[i].d_ino > 65000 )
                  {
                    printf("dir swap? ino raw %u, swapped %u\n",
                           fsu->dir[i].d_ino,
                           s4swaph( fsu->dir[i].d_ino ));
                    getpid();
                  }

                if( !isprint( fsu->dir[i].d_name[0] ))
                  {
                    printf("  %5u  Bad char in entry %d, probably not a directory\n", 
                           fsu->dir[i].d_ino, i );
                    break;
                  }
                else
                  printf("[%d]  %5u | %-14s\n", 
                         i, fsu->dir[i].d_ino,  fsu->dir[i].d_name );
              }
          }
        printf("\n");
      }
      break;

    case s4b_linkcnt:
      {
        printf("LINKCNT:");
        for( i = 0; i < S4_SPERB; i++ )
          {
            if( !(i%8) )
              printf("\n  ");

            x = fsu->links[ i ];
            printf("%d, ", x );

            /* sanity test */
            if( x < 0 || x > 100 )
              break;
          }
        printf("\n");
      }
      break;

    case s4b_free:
      {
        printf("FREELIST: %d", fsu->free.df_nfree );
        for( i = 0; i < S4_NICFREE && i < fsu->free.df_nfree; i++ )
          {
            if( !(i%8) )
              printf("\n  ");
            x = fsu->free.df_free[i];
            printf("%5d, ", x );

            /* sanity checks */
            if( x < 0 || x > 20000 )
              break;
          }
        printf("\n");
      }
      break;

    case s4b_bbt:
      {
        int              i;
        struct s4_bbe *bbe;
        
        printf("BADBLOCKS:\n");

        printf("   Cyl  cylsec\n");
        printf("  ----  ------\n");

        /* first entry is checksums; ignore */
        bbe = fsu->bbt;
        for( i = 1; i < S4_NBB && bbe->cyl; i++, bbe++ )
          {
            printf("  %4d: %6d\n", bbe->cyl, bbe->badblk );
          }
        printf("%d bad blocks\n\n", i);
      }
      break;

    default:
        printf("\nUnexpected display type %d %s\n", 
               btype, s4btypestr(btype) );
        /* FALL INTO */

    case s4b_unk:
    case s4b_raw:
        printf("RAW DISK BLOCK:\n");
        s4dump( &fsu->buf[0], sizeof(fsu->buf), 0 );
        break;

    case s4b_first_fs:
    case s4b_last_fs:
      /* do nothing */
      break;

    }
}



/* ================================================================ */

static void s4_vhbd_glorp_show( struct s4_vhbd *vhbd )
{
  struct s4_dswprt *gdswprt = &vhbd->dsk;

  printf("Name '%c%c%c%c%c%c' ",
	 s4showc(gdswprt->name[0]),
	 s4showc(gdswprt->name[1]),
	 s4showc(gdswprt->name[2]),
	 s4showc(gdswprt->name[3]),
	 s4showc(gdswprt->name[4]),
	 s4showc(gdswprt->name[5]) );

  printf(" flags 0x%x ", gdswprt->flags );

  if( (gdswprt->flags & S4_FPDENSITY) )
    printf(" DD ");
  else
    printf(" SD ");

  if( (gdswprt->flags & S4_FPMIXDENS) )
    printf(" MIXED ");
  else
    printf(" SAME ");

  if( (gdswprt->flags & S4_HITECH) )
    printf(" HEADSEL3 ");
  else
    printf(" PRECOMP ");

  if( (gdswprt->flags & S4_NEWPARTTAB) )
    printf(" NEWTAB ");
  else
    printf(" OLDTAB ");

  printf("\n");
}


static s4err s4_filsys_good_super( s4_filsys *xfs )
{
  s4err            rv = s4_ok;
  struct s4_dfilsys *fs = &xfs->super.super;

  /* swap contents if needed */
  if( S4_FsMAGIC != fs->s_magic && S4_FsMAGIC == s4swapi( fs->s_magic ) )
    {
      s4_fsu_swap( &xfs->super, s4b_super );
      xfs->doswap = 1;
    }
  else if( S4_FsMAGIC != fs->s_magic )
    {
      rv = s4_badmagic;
      xfs->doswap = 0;
    }

  return rv;
}


static void s4_disk_bbt_show( s4_diskinfo *dinfo )
{
  int	i;
  int   cylsec;
  int   altblk;
  struct s4_bbe *bbe;

  printf("Bad Block table at LBA %d, %d entries\n", 
         dinfo->bbt_lba, dinfo->nbb );
  printf(" Cyl  cylsec  trk  sec     cyl sector nxt\n");
  printf("----  ------  ---  ---    ---- ------ ---\n");

  /* first entry is checksums; ignore */
  bbe = &dinfo->bbt[1];
  for( i = 0; i < dinfo->nbb ; i++, bbe++ )
    {
      if( !bbe->cyl )
	break;

      cylsec = bbe->badblk;
      altblk = bbe->altblk;

      printf("%4d: %6d  %4d %3d ->%5d %6d %3d\n",
	     bbe->cyl,
             cylsec,
             cylsec / dinfo->pstrk,
             cylsec % dinfo->pstrk,
             cylsec / dinfo->pstrk,
             altblk,
             bbe->nxtind );
     }
   printf("%d bad blocks\n\n", i);

   return;
 }


static int s4_checksum32( unsigned char *p, int len, int expect )
{
  int   ssum = 0;
  int   i;

  for( i = 0; i < len ; i++ )
    ssum += p[i];

#if 0
  printf("sum %x, expect %x\n", ssum, expect );
  printf("sum + expect = %x\n", ssum + expect);
  printf("sum + expect = %x\n", ssum + expect);
#endif

  return ssum;
}

                
/* get a LE 3 byte address to local  */
int s4_dab2int_le( unsigned char *dab )
{
  int rv =  ((dab[0] << 16) + (dab[1] << 8) + dab[2]); 

  return rv;
}

/* get a BE 3 byte address to local  */
int s4_dab2int_be( unsigned char *dab )
{
  int rv = ((dab[2] << 16) + (dab[1] << 8) + dab[0]);

  return rv;
}


/*
 * Convert 32 bit ints to and from 3-byte disk addresses,
 * out of place in a batch.
 */
void s4_ltol3(char *cp, int *lp, int n)
{
  unsigned char *dst = (unsigned char *)cp;
  int            val;

  for( ; n-- ; lp++, dst += 3 )
    {
      val = *lp;
#ifdef S4_BIG_ENDIAN
      dst[0] = (val >> 16) & 0xff;
      dst[1] = (val >> 8) & 0xff;
      dst[2] = val & 0xff;
#endif
#ifdef S4_LITTLE_ENDIAN
      dst[0] = val & 0xff;
      dst[1] = (val >> 8) & 0xff;
      dst[2] = (val >> 16) & 0xff;
#endif
    }
}

void s4_l3tol(int *lp, char *cp, int n)
{
  unsigned char *src = (unsigned char *)cp;
        
  for( ; n-- ; src += 3 )
    {
#ifdef S4_BIG_ENDIAN
      *lp++ = ((src[0] << 16) + (src[1] << 8) + src[2]); 
#endif
#ifdef S4_LITTLE_ENDIAN
      *lp++ = ((src[2] << 16) + (src[1] << 8) + src[0]);
#endif
    }
}

/* ================================================================ */


void s4_ltol3r(char *cp, int *lp, int n)
{
  unsigned char *dst = (unsigned char *)cp;
  int            val;

  for( ; n-- ; dst += 3 )
    {
      val = *lp++;
#if S4_ENDIAN == S4_BE
      dst[0] = val & 0xff;
      dst[1] = (val >> 8) & 0xff;
      dst[2] = (val >> 16) & 0xff;
#else
      dst[0] = (val >> 16) & 0xff;
      dst[1] = (val >> 8) & 0xff;
      dst[2] = val & 0xff;
#endif
    }
}

void s4_l3tolr(int *lp, char *cp, int n)
{
  unsigned char *src = (unsigned char *)cp;
        
  for( ; n-- ; src += 3 )
    {
#if S4_ENDIAN == S4_BE
      *lp++ = ((src[2] << 16) + (src[1] << 8) + src[0]);
#else
      getpid();
      *lp++ = ((src[0] << 16) + (src[1] << 8) + src[2]); 
#endif
    }
}


/* The End. */


