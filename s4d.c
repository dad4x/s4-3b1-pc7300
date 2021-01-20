/*
 * Library for safari4 for access, see s4d.h
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
   as the BBT, and in the superblock, use vinfo[3] as
   sectors per track.   We'll have to construct this
   when we rip a filesystem from a disk image.

   We'll use the CTIX disk data structure for the BBT, but
   we won't use their memory structures.

   Wonder if we can have one track with 400k sectors?

   Maybe have utility to add/delete the spare sectors
   when going to/from native to emulated?
*/

#include <s4d.h>

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
    { S_IFMT, S_IFDIR, "d" },
    { S_IFMT, S_IFCHR, "c" },
    { S_IFMT, S_IFBLK, "b" },
    { S_IFMT, S_IFREG, "-" },
    { S_IFMT, S_IFIFO, "f" },

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

static void s4_vol_decode_partitions( s4_vol *vinfo,
                                       struct s4_vhbd *vhbd );

static void s4_vol_get_bbt( s4_vol *vinfo );
static void s4_vol_bbt_show( s4_vol *vinfo );

static int s4_checksum32( signed char *p, int len, int expect );

static void s4_maskname_show( const s4_maskname *bits, int mode );
static void s4_modes_show( int mode );

static s4err s4_filsys_good_super( s4_filsys *xfs );


/* -------------- */
/* Private macros */

// static s4err s4_vol_good_lba( s4_vol *vinfo, int lba );
#define s4_vol_good_lba( d, lba ) \
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

const char *s4atypestr( s4atype atype )
{
  const char *s = "bad s4atype";
  
  switch( atype )
    {
    case s4a_bad:  s = "bad";       break;
    case s4a_lba:  s = "logical";   break;
    case s4a_pba:  s = "physical";  break;
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


void s4dump( char *b, size_t len, int absolute, int width, int breaks )
{
  int i, j;
  int line = 0;
  int left = len;
  int pos  = 0;
  char	*base = absolute ? b : 0;

  if( !width )
     width = 32;

  /* print header line */
  if( absolute )
    printf("Address        ");
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
  for( i = 0; left > 0 ; line++ )
    {
      pos = len - left;
      if( breaks && pos && !(pos % breaks) )
        printf("\n");

      /* prefix */
      if( absolute )
        printf("%14p:", base + i );
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



int s4_lba2pba( int lba, s4_bbt *bbt, int nbb, int lstrk, int heads )
{
  int pba = lba + (lba/(lstrk)); /* one spare sector per track */
  int npba;

  if( nbb )
    {
      int track = lba / lstrk;
      int hdsec = lba % lstrk;
      int i, cylsec;
      
      /* First entry is checksum so start at 1 */
      for( i = 1 ; i < nbb ; i++ )
        {
          cylsec = bbt[i].badblk + bbt[i].cyl * heads * (lstrk + 1);
          if( !cylsec )
            break;

          if( track == (cylsec / (lstrk+1)) && hdsec == (cylsec % (lstrk+1)) )
            {
              /* Badblk is alternate track, convert to block. Last sector on
                 track is alternate */
              npba = bbt[i].altblk * (lstrk + 1) + lstrk;

              printf("Mapping lba %d from pba %d to %d.\n",
                     lba, pba, npba );

              pba = npba;
              break;
            }
        }
    }

  return pba;
}



/* strk is physical sectors per track */
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
          int i;

          /* First entry is checksum so start at 1 */
          for( i = 1; i < nbb ; i++ )
            {
              /* If track is alternate block location map to bad sector
                 location */
              if( track == bbt[i].altblk ) 
                {
                  /* Convert remapped sector to pba then lba */
                  lba = bbt[i].cyl * heads * strk + bbt[i].badblk;
                  lba = lba - (lba / strk);
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



/* initialize everything to "s4_vol_show"-able state.  
   Do not clear, because fd may be open, vhbd may already be read */

void  s4_init_vol( const char *name, int fd, 
                   int cyls, int heads, int secsz, int pscyl,
                   struct s4_vhbd *opt_vhbd,
                   s4_vol *v )
{
  v->fname  = strdup( name );
  v->fd     = fd;
  v->nbb    = 0;
  v->bbt    = &v->bbt_fsu.bbt[0];

  v->cyls   = cyls;
  v->heads  = heads;
  v->secsz  = secsz;

  v->pstrk  = pscyl / heads;
  v->lstrk  = v->pstrk - 1;

  v->pscyl  = pscyl;
  v->lscyl  = v->pscyl - heads; /* less one per track */

  v->pblks  = v->cyls * v->pscyl;
  v->lblks  = v->cyls * v->lscyl;

  v->ptrksz = v->pstrk * v->secsz;
  v->ltrksz = v->lstrk * v->secsz;
  
  v->pcylsz = v->pscyl * v->secsz;
  v->lcylsz = v->lscyl * v->secsz;
  
  /* assume HD/LBA */
  v->lba_or_pba    = s4a_lba;
  v->fspnum        = S4_HD_FS_PNUM;
  v->isfloppy      = 0;

  v->loader_ba    = 0;
  v->loader_nblks = 0;

  v->bbt_ba       = 0;
  v->bbt_nblks    = 0;

  v->bbt = &v->bbt_fsu.bbt[0];
  v->nbb = 0;

  /* setup fake partition at the start */
  v->nparts = 1;
  v->parts[0].strk     = 0;
  v->parts[0].ntrk     = 1;
  v->parts[0].pblks    = v->pstrk;
  v->parts[0].lblks    = v->lstrk;
  v->parts[0].partoff  = 0;
  v->parts[0].partlba  = 0;  
  v->parts[0].partpba  = 0;  

  if( opt_vhbd )
    {
      /* vhbd.resmap units are in FS 1k blocks, convert to PBA */
      v->loader_ba   = opt_vhbd->resmap[0].blkstart * 2;
      v->loader_nblks = opt_vhbd->resmap[0].nblocks  * 2;
      v->bbt_ba      = opt_vhbd->resmap[1].blkstart * 2;
      v->bbt_nblks    = opt_vhbd->resmap[1].nblocks * 2;

      /* take real partitions instead */
      s4_vol_decode_partitions( v, opt_vhbd );
    }
}


s4err s4_open_vol( const char *vfile, int mode, s4_vol *vinfo )
{
  s4err	 rv = s4_ok;
  int    fd;

  memset( vinfo, 0, sizeof(*vinfo) );

  fd = open( vfile, mode, 0 );
  if( fd < 0 )
    {
      printf("Unable to open '%s': %s\n", vfile, strerror( errno ));
      rv = s4_open;
      goto done;
    }

  rv =  s4_seek_read( fd, 0, (char*)&vinfo->vhbd, sizeof(vinfo->vhbd) );
  if( s4_ok != rv )
    goto done;

  if( S4_VHBMAGIC == vinfo->vhbd.magic )
    {
      vinfo->doswap = 0;
    }
  else if( S4_VHBMAGIC == s4swapi( vinfo->vhbd.magic ) )
    {
      vinfo->doswap = 1;
      s4_fsu_swap( (s4_fsu*)&vinfo->vhbd, s4b_vhbd );
    }
  else
    {
      s4_fsu_show( (s4_fsu*)&vinfo->vhbd, s4b_vhbd );
      rv = s4_badmagic;
      goto done;
    }

  /* see cheksum w/o the magic and sum itself */
  s4_checksum32( (signed char*)&vinfo->vhbd.dsk, sizeof(vinfo->vhbd) - 8,
                 vinfo->vhbd.magic );

  s4_init_vol( vfile, fd, 
               vinfo->vhbd.dsk.cyls, 
               vinfo->vhbd.dsk.heads,
               vinfo->vhbd.dsk.sectorsz, 
               vinfo->vhbd.dsk.pseccyl, 
               &vinfo->vhbd,
               vinfo );

  if( vinfo->pscyl != vinfo->vhbd.dsk.pseccyl )
    {
      printf("WARNING: computed sectors/cyl %d not reported %d\n",
             vinfo->pscyl, vinfo->vhbd.dsk.pseccyl );
    }

  if( !memcmp( vinfo->vhbd.dsk.name, "Floppy", 6 ) )
    {
      vinfo->fspnum = S4_FP_FS_PNUM;
      vinfo->lba_or_pba = s4a_pba;
      vinfo->isfloppy = 1;
    }
  else
    {
      vinfo->fspnum = S4_HD_FS_PNUM;
      vinfo->lba_or_pba = s4a_lba;
      vinfo->isfloppy = 0;

      /* HD's have BBTs.  But they may say no bb mapping. */
      s4_vol_get_bbt( vinfo );
    }

 done:
  return rv;
}


s4err s4_vol_import( s4_vol *ovinfo, int opnum, int ooffblks, int ifd )
{
  s4err      err = s4_ok;
  s4_vol    *d = ovinfo;
  long       offset;
  int        bar, baa;      /* relative and absolute blocks */
  int        partba;
  int        blks;
  int        len;
  char       buf[ 512 ];

  if( s4a_lba == ovinfo->lba_or_pba )
    {
      partba = d->parts[opnum].partlba;
      blks   = d->parts[opnum].lblks;
    }
  else
    {
      partba = TRK_TO_PBA(d, d->parts[opnum].strk);
      blks   = d->parts[opnum].pblks;
    }

  /* copy everything from the input fd to successive LBA's */
  printf("Importing...\n");
  for( bar = 0; bar < blks && s4_ok == err; bar++ )
    {
      if( bar && !(bar % 100) )
        printf("BLK %d\r", bar );

      len = read( ifd, buf, sizeof(buf));
      if( len == 0 )
        break;

      if( len <= 0 )
        {
          if( len < 0 )
            {
              printf("read error %s\n", strerror(errno));
              err = s4_read;
            }
          break;
        }

      baa = partba + ooffblks + bar;
      if( s4a_lba == ovinfo->lba_or_pba )
        offset = LBA_TO_VOL_OFFSET(d, baa);
      else
        offset = PBA_TO_OFFSET(d, baa);

      err = s4_seek_write( ovinfo->fd, offset, buf, len );
      if( s4_ok != err )
        {
          printf("seek write error, offset %ld\n", offset );
          break;
        }
    }
 
  printf("Imported %d %s blocks into partition %d\n", 
         bar, s4atypestr( ovinfo->lba_or_pba ), opnum );
         
  return err;
}

/* export cnt lba's from pnum starting at ioffblks to fd. */
s4err s4_vol_export( s4_vol *ivinfo, int ipnum, int ioffblks,
                     int icnt, int ofd )
{
  s4_vol   *d   = ivinfo;
  s4err     err = s4_ok;
  long      offset;
  int       bar, baa, rv;
  long      partba;
  char      buf[ 512 ];
  
  if( s4a_lba == ivinfo->lba_or_pba )
    partba = d->parts[ipnum].partlba;
  else
    partba = TRK_TO_PBA(d, d->parts[ipnum].strk);

  for( bar = 0; bar < icnt && s4_ok == err; bar++ )
   {
     if( bar && !(bar % 100) )
       printf("BLK %d\r", bar );

     baa = partba + ioffblks + bar;

     if( s4a_lba == ivinfo->lba_or_pba )
       offset = LBA_TO_VOL_OFFSET(d, baa);
     else
       offset = PBA_TO_OFFSET(d, baa);

     err = s4_seek_read( d->fd, offset, buf, sizeof(buf) );
     if( s4_ok != err )
       {
         printf("seek read error, offset %ld\n", offset );
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
    printf("Exported %d %s blocks from partition %d\n", 
           bar, s4atypestr( ivinfo->lba_or_pba ), ipnum );

  return err;
}

/* export icnt lba's from ivinfo partion at ioffblks
   to ovinfo partition at ooffblks */
s4err s4_vol_transfer( s4_vol *ovinfo, int opnum, int ooffblks,
                       s4_vol *ivinfo, int ipnum, int ioffblks,
                       int icnt )
{
  s4err   err = s4_ok;

  int     bar;
  int     ibaa, ipartba;
  int     obaa, opartba;
  long    ioffset, ooffset;
  
  char    buf[ 512 ];
  
  if( s4a_lba == ivinfo->lba_or_pba )
    ipartba = ivinfo->parts[ipnum].partlba;
  else
    ipartba = TRK_TO_PBA( ivinfo, ivinfo->parts[opnum].strk);

  if( s4a_lba == ovinfo->lba_or_pba )
    opartba = ovinfo->parts[opnum].partlba;
  else
    opartba = TRK_TO_PBA( ovinfo, ovinfo->parts[opnum].strk);

  for( bar = 0; bar < icnt && s4_ok == err; bar++ )  
    {
      if( bar && !(bar % 100) )
        printf("BLK %d\r", bar );

      ibaa = ipartba + ioffblks + bar;
      if( s4a_lba == ivinfo->lba_or_pba )
        ioffset = LBA_TO_VOL_OFFSET(ivinfo, ibaa);
      else
        ioffset = PBA_TO_OFFSET( ivinfo, ibaa );

      err = s4_seek_read( ivinfo->fd, ioffset, buf, sizeof(buf) );
      if( s4_ok != err )
        {
          printf("seek read error, offset %ld\n", ioffset );
          break;
        }

      obaa = opartba + ooffblks + bar;
      if(  s4a_lba == ovinfo->lba_or_pba )
        ooffset = LBA_TO_VOL_OFFSET(ovinfo, obaa);
      else
        ooffset = PBA_TO_OFFSET( ovinfo, obaa );

      err = s4_seek_write( ovinfo->fd, ooffset, buf, sizeof(buf) );
      if( s4_ok != err )
        {
          printf("seek write error, offset %ld\n", ooffset );
          break;
        }
    }
  if( s4_ok == err )
    printf("Transferred %d %s blocks\n", 
           bar, s4atypestr( ovinfo->lba_or_pba ));

  return err;
}



void s4_vol_set_part( s4_vol *vinfo, int pnum, int strk, int numtrk )
{
  s4_part  *part = &vinfo->parts[ pnum ];

  part->strk    = strk;
  part->ntrk    = numtrk;
  part->pblks   = numtrk * vinfo->pstrk;
  part->lblks   = numtrk * vinfo->lstrk;
  part->partpba = TRK_TO_PBA( vinfo, strk );
  part->partoff = TRK_TO_OFFSET( vinfo, strk );
  part->partlba = TRK_TO_LBA( vinfo, strk );
}


static void s4_vol_decode_partitions( s4_vol *vinfo,
                                       struct s4_vhbd *vhbd )
{
  int       i;
  int       strk, ntrk, ltrk;

  vinfo->nparts = 0;
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
      ltrk = ntrk ? ntrk - 1 : (vinfo->cyls * vinfo->heads) - 1;

      /* ltrk == strk is a one track partition */
      if( ltrk < strk )
        {
          printf("last track %d start %d ntrk %d in partition %d\n",
                 ltrk, strk, ntrk, i );
          abort();
        }
      s4_vol_set_part( vinfo, i, strk, ltrk - strk + 1 );
    }

  vinfo->nparts    = i;
}


static void s4_vol_get_bbt( s4_vol *vinfo )
{
  int rv, i;

  vinfo->nbb = 0;
  vinfo->bbt = &vinfo->bbt_fsu.bbt[0];

  if( vinfo->bbt_ba == 0 )
    return;

  rv =  s4_seek_read( vinfo->fd,
                      LBA_TO_VOL_OFFSET(vinfo, vinfo->bbt_ba ),
                      vinfo->bbt_fsu.buf,
                      sizeof(vinfo->bbt_fsu.buf));
  if( s4_ok == rv )
    {
      /* if bb turned off, go physical. Don't do this for real disks with
         17 sectors per track */
      printf("BBT CHKSUM %x\n", vinfo->bbt[0].cyl );
      if( vinfo->bbt[0].cyl == S4_NO_BB_CHECKSUM &&
          vinfo->bbt[0].badblk == S4_NO_BB_CHECKSUM &&
          vinfo->pstrk != 17)
        {
          printf("PHYSICAL!\n");
          vinfo->lba_or_pba = s4a_pba;
        }

      if( vinfo->doswap )
        s4_fsu_swap( &vinfo->bbt_fsu, s4b_bbt );

      for( i = 0; i < S4_NBB && vinfo->bbt[i].cyl; i++ )
        vinfo->nbb++;
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


void s4_vol_show( s4_vol *vinfo )
{
  int	    i;
  s4_part  *part;

  printf("Showing disk header from '%s'\n"
         "Magic %x %s %x, checksum %x (unchecked) %s\n",
         vinfo->fname,
	 vinfo->vhbd.magic,
	 vinfo->vhbd.magic == S4_VHBMAGIC ? "is" : "IS NOT",
	 S4_VHBMAGIC,
	 vinfo->vhbd.chksum,
         vinfo->doswap ? "SWAPPED" : "no swap needed" );

  printf(" %d cyls, %d heads; %d s/trk, %d s/cyl; step %d, %d byte sectors\n",
	 vinfo->cyls,
	 vinfo->heads,
	 vinfo->pstrk,
	 vinfo->pscyl,
	 vinfo->vhbd.dsk.step,
	 vinfo->secsz );

  printf(" %d blocks, %dk bytes %dM bytes\n",
	 vinfo->cyls * vinfo->pscyl,
	 vinfo->cyls * vinfo->pscyl * vinfo->secsz / 1024,
	 vinfo->cyls * vinfo->pscyl * vinfo->secsz / 1024 / 1024 );

  s4_vhbd_glorp_show( &vinfo->vhbd );

  printf("Loader is %d blocks at PBA %d\n", 
	 vinfo->loader_nblks, vinfo->loader_ba );

  printf("Bad Block Table is %d blocks at PBA %d\n", 
	 vinfo->bbt_nblks, vinfo->bbt_ba );

  printf("\nPartitions: (%d)\n", vinfo->nparts );
  printf("Num  STrk  Ntrk  PBA   LBA  Cyl     Offset   ");
  printf("PSects      PSize        LSects      LSize\n");
  printf("---  ----  ---- ----- ----- ----  ---------  ");
  printf("------- ---------------  ------- ---------------\n");

  for( i = 0; i < vinfo->nparts; i++ )
    {
      part = &vinfo->parts[i];

      printf("%3d %5d %5u %5u %5u %4u %10u %8d %6uk %6.2fM %8d %6uk %6.2fM\n",
	     i, 
	     part->strk,
             part->ntrk,
	     TRK_TO_PBA( vinfo, part->strk ),
	     TRK_TO_LBA( vinfo, part->strk ),
	     TRK_TO_CYL( vinfo, part->strk ),
	     PNUM_TO_OFFSET( vinfo, i ),
	     part->pblks,
	     part->pblks * vinfo->secsz / 1024,
	     ((double)part->pblks) * vinfo->secsz / 1024 / 1024,
	     part->lblks,
	     part->lblks * vinfo->secsz / 1024,
	     ((double)part->lblks) * vinfo->secsz / 1024 / 1024 );
    }

  printf("\nSpecial sections in part 0\n");
  s4_resdes_show( vinfo->vhbd.resmap );

  s4_vol_bbt_show( vinfo );
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


s4err s4_vol_close( s4_vol *vinfo )
{
  s4err rv = s4_ok;

  if( vinfo->fname )
    free( vinfo->fname );

  if( vinfo->fd > 0 )
    {
      if( close( vinfo->fd ) < 0 )
        {
          printf("%s closing fd %d\n", strerror(errno), vinfo->fd );
          rv = s4_close;
        }
      vinfo->fd = -1;
    }

  memset( vinfo, 0, sizeof(*vinfo) );

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
          printf("%s at offset %ld wanted %ld got %ld\n",
                 strerror( errno ), (long)offset, (long)blen, off );
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


s4err s4_vol_open_filsys( s4_vol *vinfo, int volnum, s4_filsys *fs )
{
  s4err rv;
  int   fd;
  long  offset;

  fs->vinfo   = vinfo;
  fs->part    = &vinfo->parts[ volnum ];
  fd          = vinfo->fd;

  /* FS should be at block 1,  512 bytes from 0 */
  offset = fs->part->partoff + PBA_TO_OFFSET( vinfo, 1 );

  rv = s4_seek_read( fd, offset, fs->super.buf, 512 );
  if( s4_ok != rv )
    goto done;
  
  fs->doswap = 0;
  rv = s4_filsys_good_super( fs );
  if( s4_ok == rv )
    {
      fs->bksz = fs->super.super.s_type == 2 ? 1024 : 512;
    }
  else
    {
      printf("FS Superblock is %s\n", s4errstr(rv) );
    }
 done:
  return rv;
}


s4err s4_open_filsys( const char *path, s4_filsys *fs )
{
  s4err          rv;
  s4_vol   *d = &fs->fakevol;
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
  s4_init_vol( "fakevol", d->fd, 1, 1, 512, sb.st_size / 512 + 1, NULL, d );
  s4_vol_set_part( d, 0, 0, 0 );
  s4_vol_set_part( d, 1, 0, 0 );
  s4_vol_set_part( d, 2, 0, 1 );
  d->nparts = 3;

  /* open FS on normal partition 2 of fake disk */
  rv = s4_vol_open_filsys( d, 2, fs );

 done:

  return rv;
}


s4err s4_filsys_show( s4_filsys *xfs )
{
  struct s4_dfilsys *fs = &xfs->super.super;

  if( S4_FsMAGIC_LE == fs->s_magic )
    {
      printf("FS is swapped\n");
      return s4_badmagic;
    }
  else if( S4_FsMAGIC != fs->s_magic )
    {
      printf("Bad magic in Filsys\n");
      return s4_badmagic;
    }
  
  if( S4_FsMAGIC != fs->s_magic )
    {
      printf("Bad magic 0x%08x in Filsys\n", fs->s_magic );
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

  offset = LBA_TO_FS_OFFSET( xfs, lbaa );

  printf("read_blk: lbaa %d\n", lbaa );

  rv     = s4_seek_read( xfs->vinfo->fd, offset, buf, blen );

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
  if( fs->vinfo == &fs->fakevol )
    rv = s4_vol_close( fs->vinfo );

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
          fs->s_vinfo[i] = s4swaph( fs->s_vinfo[i] );

        fs->s_time   = s4swapi( fs->s_time );
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
          fsu->dir[i].d_ino = s4swaph( fsu->dir[i].d_ino );
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
        fsu->free.df_nfree = s4swapi( fsu->free.df_nfree );

        for( i = 0; i < S4_NICFREE; i++ )
            fsu->free.df_free[ i ] = s4swapi( fsu->free.df_free[ i ] );        
      }
      break;

    case s4b_bbt:
      {
        for( i = 0; i < S4_NBB && fsu->bbt[ i ].cyl ; i++ )
          {
            fsu->bbt[ i ].cyl    = s4swaph( fsu->bbt[ i ].cyl );
            fsu->bbt[ i ].badblk = s4swaph( fsu->bbt[ i ].badblk );
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
  printf("  (%d)\n", i);
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

        printf("  %d FREE BLOCKS:", fs->s_nfree);
        for( i = 0; i < S4_NICFREE && i < fs->s_nfree; i++ )
          {
            if(  !(i % 8) )
              printf("\n    ");

            x = fs->s_free[i];
            printf("%5d, ", x );
            if( x < 0 || x > 20000 )
              break;
          }
        printf("\n");
        
        printf("  %d FREE INO:", fs->s_ninode);
        for( i = 0; i < S4_NICINOD && i < fs->s_ninode; i++ )
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
          printf("%8d,", fs->s_vinfo[i] );
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
        printf("  (%d)\n", i);
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
        printf("  (%d)\n", i);
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
        printf("  (%d)\n", i);
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
        printf("  (%d)\n", i);
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
        s4dump( &fsu->buf[0], sizeof(fsu->buf), 0, 0, 0 );
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
      s4_fsu_show( &xfs->super, s4b_raw );
    }


  return rv;
}


static void s4_vol_bbt_show( s4_vol *vinfo )
{
  int	i;
  int   cylsec;
  int   altblk;
  struct s4_bbe *bbe;

  if( vinfo->bbt[0].cyl == S4_NO_BB_CHECKSUM &&
      vinfo->bbt[0].badblk == S4_NO_BB_CHECKSUM )
    {
      printf("Bad block mapping is turned OFF\n");
    }
  else
    {
      printf("Bad Block table at blk %d sum 0x%x %d entries\n",
             vinfo->bbt_ba, vinfo->bbt[0].cyl, vinfo->nbb );
      printf(" Cyl  cylsec  head  sec     cyl  head  nxt\n");
      printf("----  ------  ----  ---     ---  ----  ---\n");

      /* first entry is the checksum; ignore */
      bbe = &vinfo->bbt[1];
      for( i = 0; i < vinfo->nbb ; i++, bbe++ )
        {
          if( !bbe->cyl )
            break;

          cylsec = bbe->badblk;
          altblk = bbe->altblk;

          printf("%4d: %6d  %4d %4d -> %4d %5d %4d\n",
                 bbe->cyl,
                 cylsec,
                 cylsec / vinfo->pstrk,
                 cylsec % vinfo->pstrk,
                 altblk / vinfo->heads,
                 altblk % vinfo->heads,
                 bbe->nxtind );
        }
      printf("%d bad blocks\n\n", i);
    }

   return;
 }


static int s4_checksum32( signed char *p, int len, int expect )
{
  int   ssum = 0;
  int   i;

  printf("Checksumming %d\n", len );

  for( i = 0; i < len ; i++ )
      ssum += p[i];

#if 0
  printf("sum 0%08x, expect 0%08x\n", ssum, expect );
  printf("sum + expect = 0%08x\n", ssum + expect);
  printf("sum + expect = 0%08x\n", ssum + expect);
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
void s4ltol3(char *cp, int *lp, int n)
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

void s4l3tol(int *lp, char *cp, int n)
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


void s4ltol3r(char *cp, int *lp, int n)
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

void s4l3tolr(int *lp, char *cp, int n)
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


/* no need to be public */
#define S4_RL_MARKER 0xEE


/* returns lenggh of encoded obuf */
int s4rl_encode( const char *inbuf, int ilen, char *obuf, int olen )
{
  int c;
  int ipos;
  int opos = 0;
  int cursor;
  int cnt;
  
  for( ipos = 0; ipos < ilen ; )
    {
      c = inbuf[ipos] & 0xff ;

      /* count runs of the same value, up to 255 */
      for( cnt = 1, cursor = ipos + 1; cursor < ilen && cnt < 255; cnt++ )
          if( c != (inbuf[cursor++] & 0xff) )
            break;

      /* if enough repeats to save space... */
      if( cnt > 3 )
        {
          if( opos < olen - 4 )
            {
              obuf[ opos++ ] = S4_RL_MARKER;
              obuf[ opos++ ] = (char)cnt;
              obuf[ opos++ ] = (char)c;
              ipos += cnt;
            }
          else
            {
              printf("no room to encode rle %d!?\n", cnt);
              return -1;
            }
        }
      else if( opos < olen )
        {
          obuf[ opos++ ] = c;
          ipos++;
        }
      else 
        {
          printf("no room to encode one!?\n");
          return -1;
        }
    }

  return opos;
}

/* returns length of decode */
int s4rl_decode( const char *inbuf, int ilen, char *obuf, int olen )
{
  int ipos, opos;
  int c;
  int cnt;
  
  /* eats varying amounts of input each time */
  for( ipos = opos = 0; ipos < ilen ; )
    {
      c = inbuf[ ipos ] & 0xff;

      if( S4_RL_MARKER == c )
        {
          if( ipos < ilen - 2 )
            {
              cnt = inbuf[ ipos + 1 ] & 0xff;
              c = inbuf[ ipos + 2 ] & 0xff;
              ipos += 3;
            }
          else
            {
              printf("end of input during run\n");
              return -1;
            }                  

          if( (opos + cnt) < olen )
            {
              while( cnt-- )
                obuf[ opos++ ] = (char)c;
            }
          else
            {
              printf("no room for rll decode\n");
              return -1;
            }
        }
      else if( opos < olen )
        {
          obuf[ opos++ ] = (char)c;
          ipos++;
        }
      else
        {
          printf("no room for decode\n");
          return -1;
        }
    }

  /* clear remaining output buffer */
  if( (olen - opos) > 0 )
    memset( obuf + opos, 0, olen - opos );

  return opos;
}

/* The End. */


