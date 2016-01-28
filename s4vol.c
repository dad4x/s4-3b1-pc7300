/*
 * s4vol -- make or modify a s4 volume to give to exp2emu
 *
 *
 * Usage:
 *    s4vol [-i in-volfile] [-o out-volfile] [-io inout-volfile]
 *          [-l loader] [-f filsys] [-h heads] [-c cyls] [-s sectors]
 *          [ -p paging] [-v fspartnum] [-v fspartnum]
 *          [-x][-d][-F][-3][-nobb][-bb]
 *
 * Create, modify or consdider volume file
 * 
 * With no arguments, describe a hypothetical volume
 *
 * With -i, work with an existing volume, read-only.
 * With -o, write a new volume
 * With -io, modify existing volume.
 *
 * Sec cyls, heads and sectors/track with -c -h and -s.
 * -l, use a new loader.  -- CHECKSUM problems
 * -f, install a new filesystem image.
 *
 * -bb      allow bad block mapping.
 * -nobb    do not allow bad block mapping
 *
 * -F sets cyls, heads, sectors for a vanilla 400k floppy.
 * -3 sets cyls, heads, sectors for 3-1/2" 800 floppy.
 *
 * -x, eXplain what's on the -i and -o disks
 *
 * -d can be used multiple times to increase debug ouput.
 *
 * Default cyls, heads, sectors is { 1400, 8, 17 }, which are the
 * the maximums for the stock 7300.
 */

#include <s4d.h>
#include <ctype.h>
#include <time.h>

/* These defaults are maxed out to work with vanilla systems */

#define DEF_SECTRK   17           /* -maybe- able to increase this. */
#define DEF_CYLS     1400         /* see HDMAXCYL in sys/gdisk.h    */
#define DEF_HEADS    8            /* Hardware limit.                */
#define DEF_PLEN    (4*1024*1024) /* full 4M machine VM space limit */

#define DEF_LOADER_TRKS (4)       /* about 32k, down from 8 */

/* Context for this program */
typedef struct
{
  char    *pname;          /* program name */
  int      xflag;          /* eXplore vol */
  int      dflag;          /* debug level */

  s4_vol   ivinfo;         /* inputdisk info */
  s4_vol   ovinfo;         /* output disk info */

  struct s4_vhbd *ovhbd;        /* ptr into ovinfo */

  /* from args, for intended output */
  int           heads;
  int           cyls;
  int           pstrk;          /* physical sectors per trk */
  int           fspnum;         /* when no input vol to use */
  int           bbflag;         /* on = yes. */
  int           lba_or_pba;     /* when no input vol to use */
  int           inout;          /* if in is same as output */

  char         *infile;
  char         *outfile;
  char         *ldfile;
  char         *fsfile;
  char         *pagestr;

  /* from existing vol or file lengths or args */

  long          ldlen;          /* loader len           */
  long          fslen;          /* file system len      */
  unsigned long plen;           /* paging partition len */

  /* file descriptors (except for old volume, in vinfo  */
  int           ldfd;           /* loader fd            */
  int           fsfd;           /* fs image fd          */

  /* computed values */
  int           ldtrks;         /* tracks for vhbd+loader */
  int           pagetrks;       /* tracks for paging      */
  int           fstrks;         /* tracks for filesystem  */
  long          fsavail;        /* available for fs       */

  long          trks;           /* total tracks         */
  long          pblks;          /* physical blocks      */
  int           pscyl;          /* physical sectors/cyl */

  long          lblks;          /* logical blocks      */
  int           lscyl;          /* logical sectors/cyl */
  int           lstrk;          /* logical sectors/trk */

} s4volcx;

static void s4volcx_init( s4volcx *cx );
static void s4volcx_term( s4volcx *cx );

static int s4vol_parse_args( int argc, char **argv, s4volcx *cx );
static int s4vol_consider( s4volcx *cx );
static int s4vol_execute( s4volcx *cx );

static int s4vol_new_vhbd( s4volcx *cx );

static int s4vol_read_open( char *fn, int *fd, long *len );

static s4err s4vol_import_or_transfer( s4_vol *ovinfo,
                                       s4_vol *ivinfo,
                                       int pnum, int resnum, 
                                       int ifd );

int main( int argc, char **argv )
{
  s4volcx       cx;
  int rv = 0;

  s4volcx_init( &cx );

  if( (rv = s4vol_parse_args( argc, argv, &cx )) )
    goto done;

  if( (rv = s4vol_consider( &cx )) )
    goto done;

  rv = s4vol_execute( &cx );

 done:
  s4volcx_term( &cx );

  return rv;
}


static void s4volcx_init( s4volcx *cx )
{
  cx->pname   = NULL;

  memset( &cx->ivinfo, 0, sizeof( cx->ivinfo ));
  memset( &cx->ovinfo, 0, sizeof( cx->ovinfo ));

  cx->ovhbd = &cx->ovinfo.vhbd;

  cx->heads   = 0;
  cx->cyls    = 0;
  cx->pstrk   = 0;
  cx->fspnum      = S4_HD_FS_PNUM;
  cx->lba_or_pba  = s4a_pba;    /* always PBA for output */

  cx->infile = NULL;
  cx->outfile = NULL;
  cx->ldfile  = NULL;
  cx->fsfile  = NULL;
  cx->pagestr = NULL;

  cx->ldlen   = 0;
  cx->fslen   = 0;
  cx->plen    = 0;

  cx->ldfd    = -1;
  cx->fsfd    = -1;

  cx->ldtrks   = 0;
  cx->pagetrks = 0;
  cx->fstrks   = 0;
  cx->fsavail  = 0;

  cx->trks     = 0;
  cx->pblks    = 0;
  cx->pscyl    = 0;
  cx->lblks    = 0;
  cx->lscyl    = 0;
  cx->lstrk    = 0;
}


static void s4vol_fd_close( int *fd )
{
  if( *fd > 0 )
    {
      close( *fd );
      *fd = 0;
    }
}

static void s4volcx_term( s4volcx *cx )
{
  s4_vol_close( &cx->ovinfo );
  s4_vol_close( &cx->ivinfo );

  s4vol_fd_close( &cx->ldfd );
  s4vol_fd_close( &cx->fsfd );
}


static int s4vol_parse_args( int argc, char **argv, s4volcx *cx )
{
  int rv = 0;
  int help = 0;
  int consume = 0;

  cx->pname = argv[0];
  argv++;
  argc--;

  for( ; argc > 0 ; argc -= consume, argv += consume )
    {
      /* consider two arg options if possible*/
      consume = 2;
      if( argc > 1 )
        {
          if( !strcmp( argv[0], "-l" ) )
            {
              cx->ldfile = argv[1]; continue;
            }
          else if( !strcmp( argv[0], "-f" ) )
            {
              cx->fsfile = argv[1]; continue;
            }
          else if( !strcmp( argv[0], "-h" ) )
            {
              cx->heads = atoi( argv[1] );  continue;
            }
          else if( !strcmp( argv[0], "-c" ) )
            {
              cx->cyls = atoi( argv[1] ); continue;
            }
          else if( !strcmp( argv[0], "-s" ) )
            {
              cx->pstrk = atoi( argv[1] ); continue;
            }
          else if( !strcmp( argv[0], "-p" ) )
            {
              cx->pagestr = argv[1]; continue;
            }
          else if( !strcmp( argv[0], "-i" ) )
            {
              cx->infile = argv[1]; 
              continue;
            }
          else if( !strcmp( argv[0], "-o" ) )
            {
              cx->outfile = argv[1];
              continue;
            }
          else if( !strcmp( argv[0], "-io" ) )
            {
              /* same file. */
              cx->infile = argv[1]; 
              cx->outfile = argv[1];
              cx->inout  = 1;
            }
        }

      /* now consider single options */
      consume = 1;
      if( !strcmp( argv[0], "-h" ) )
        {
          help = 1;
          break;
        }
      else if( !strcmp( argv[0], "-x" ) )
        {
          cx->xflag = 1; continue;
        }
      else if( !strcmp( argv[0], "-bb" ) )
        {
          cx->bbflag = 1; continue;
        }
      else if( !strcmp( argv[0], "-nobb" ) )
        {
          cx->bbflag = 0; continue;
        }
      else if( !strcmp( argv[0], "-F" ) )
        {
          printf("Floppy!\n");
          cx->cyls    = 40;
          cx->heads   = 2;
          cx->pstrk   = 10;
          cx->ldtrks  = 1;
          cx->fspnum  = S4_FP_FS_PNUM;
          continue;
        }
      else if( !strcmp( argv[0], "-3" ) )
        {
          printf("3-1/4\" Floppy!\n");
          cx->cyls    = 80;
          cx->heads   = 2;
          cx->pstrk   = 10;
          cx->ldtrks  = 1;
          cx->fspnum  = S4_FP_FS_PNUM;
          continue;
        }
      else if( !strcmp( argv[0], "-d" ) )
        {
          cx->dflag++; continue;
        }
      else 
        {
          printf("unexpected arg '%s' or missing value\n", argv[0] );
          help = 1;
          rv++;
          break;
        }
    }

  if( rv || help )
    {
      printf("\nUsage: %s -i ivol -o ovol -io modvol\n"
             "         -f fs -l loader -h heads -c cyls -s seccyl\n"
             "         -p pagespace -F -3 -bb -nobb -x -d\n\n"
             "-i input-volume         opt: copy source\n"
             "-o output-volume        opt: show info\n\n"
             "-io modify-volume       opt: show info\n\n"
             "-f fs-image-file        opt: image to import\n"
             "-l loader-file          opt: loader to import\n\n"
             "-h heads                default: 8\n"
             "-c cylinders            default: 1400\n"
             "-s sectors-per-track    default: 17\n"
             "-p paging-space         default: 4M\n\n"
             "-bb                     default: invol or nobb\n"
             "-nobb                   default: invol\n"
             "-F                      use 5\" floppy defaults\n\n"
             "-3                      use 3-1/4\" floppy defaults\n\n"
             "-x                      eXpanded volume output\n"
             "-d ...                  increase debug output\n\n",
             cx->pname );
    }


  /* catch simple case, not if they resolve to the same inode */
  if( cx->infile && cx->outfile && !strcmp( cx->infile, cx->outfile ) )
    cx->inout = 1;

  if( cx->infile )
    {
      if( s4_ok != s4_open_vol( cx->infile,
                                cx->inout ? 006 : 004,
                                &cx->ivinfo ) )
        {
          printf( "Can't open %s volume '%s'\n",
                  cx->inout ? "in-out" : "input",
                  cx->infile);
          rv++;
        }
    }

  return rv;
}


/* how many tracks needed to hold something in blocks based on strk.  */
static int s4vol_tracks_needed( s4volcx *cx, unsigned long len, int strk )
{
  int rv;
  long l;
  
  l = ((len / 512) + (strk - 1));
  l /= strk;

  if( len > 0 )
    rv = (int)l;
  else
    rv = DEF_LOADER_TRKS;

  return rv;
}

static double s4vol_trks_to_len( int trks, int strk )
{
  return (double)trks * strk * 512;
}


static void s4vol_describe_part( int pnum, int trks, int strk, char *what )
{
  double d = s4vol_trks_to_len( trks, strk );

  printf("Part[%d] %5d tracks, %8.f k %7.2f M for %s\n",
         pnum, trks,
         d / 1024, d / 1024 / 1024, what );
}




static int s4vol_consider( s4volcx *cx )
{
  int           rv = 0;
  long          sects;
  char         *p;

  /* check files for existence and get len & params from them. */
  if( cx->ldfile && s4vol_read_open( cx->ldfile, &cx->ldfd, &cx->ldlen ) )
    {
      rv++; 
      goto done;
    }
  if( cx->fsfile && s4vol_read_open( cx->fsfile, &cx->fsfd, &cx->fslen ) )
    {
      rv++; 
      goto done;
    }

  if( cx->infile )
    {
      if( cx->ivinfo.fd > 0 )
        {
          if( cx->xflag )
            {
              printf("\n\nInformation about input volume '%s':\n", 
                     cx->infile );
              s4_fsu_show( (s4_fsu*)&cx->ivinfo.vhbd, s4b_vhbd );
              printf("\n\n");
            }

          /* arguments override the existing values */
          if( !cx->cyls )
            cx->cyls = cx->ivinfo.cyls;

          if( !cx->heads )
            cx->heads = cx->ivinfo.heads;

          if( !cx->pstrk )
            cx->pstrk = cx->ivinfo.pstrk;

          if( !cx->ldfile )
            {
              cx->ldlen   = cx->ivinfo.loader_nblks * 512;
              cx->ldtrks  = cx->ivinfo.parts[S4_LD_PNUM].ntrk;
            }

          if( !cx->fsfile )
            cx->fslen = cx->ivinfo.parts[cx->ivinfo.fspnum].lblks * 512;

          if( !cx->pagestr )
            {
              cx->plen     =  cx->ivinfo.parts[S4_PAGE_PNUM].lblks * 512;
              cx->pagetrks =  cx->ivinfo.parts[S4_PAGE_PNUM].ntrk;
            }
        }
    }

  if( !cx->cyls )
    cx->cyls = DEF_CYLS;
  if( !cx->heads )
    cx->heads = DEF_HEADS;
  if( !cx->pstrk )
    cx->pstrk = DEF_SECTRK;

  /* determine simple parts of drive virtual geometry */
  cx->trks  = cx->cyls  * cx->heads;
  cx->pblks = cx->heads * cx->cyls;
  cx->pscyl = cx->heads * cx->pstrk;

  /* one less sector per track when bad block mapped */
  cx->lblks = cx->pblks - cx->trks;
  cx->lscyl = cx->pscyl - 1;
  cx->lstrk = cx->pstrk - 1;

  /* tracks needed in partition[0] for the loader -- FIXME strk lba */
  if( !cx->ldtrks && cx->ldlen )
    cx->ldtrks   = s4vol_tracks_needed( cx, cx->ldlen + 1024, cx->pstrk );
  else if( !cx->ldtrks )
    cx->ldtrks   = DEF_LOADER_TRKS;

  if( !cx->pagestr && !cx->plen )
    cx->plen = DEF_PLEN;
  if( !cx->pagetrks )
    cx->pagetrks = s4vol_tracks_needed( cx, cx->plen, cx->pstrk );

  /* determine paging partition space */
  if( cx->pagestr )
    {
      cx->plen = atoi(cx->pagestr);
      /* skip past number */
      for( p = cx->pagestr; isdigit(*p) ; p++ )
        continue;
      if( 'k' == *p || 'K' == *p )
        cx->plen *= 1024;
      else if( 'm' == *p || 'M' == *p )
        cx->plen *= (1024 * 1024);
    }

  /* but if floppy, forget paging */
  if( S4_FP_FS_PNUM == cx->fspnum )
    {
      cx->plen = 0;
      cx->pagetrks = 0;
    }

  cx->fstrks  = cx->trks - cx->ldtrks - cx->pagetrks;

  /* we always turn off BB-mapping, so use pstrk. */
  cx->fsavail = s4vol_trks_to_len( cx->fstrks,  cx->pstrk );

  printf("\nComputed values:\n");

  s4vol_describe_part( 0, cx->ldtrks,   cx->lstrk, "loader" );
  if( S4_FP_FS_PNUM == cx->fspnum )
    {
      s4vol_describe_part( 1, cx->fstrks, cx->lstrk, "floppy FS" );
    }
  else
    {
      s4vol_describe_part( 1, cx->pagetrks, cx->lstrk, "paging" );
      s4vol_describe_part( 2, cx->fstrks,   cx->lstrk, "filesystem" );
    }
  sects = cx->pstrk * cx->trks;
  printf("Raw volume is %ld sectors, %ld k %.2f M\n",
         sects, 
         sects * 512 / 1024,
         (double)sects * 512 / 1024 / 1024 );
         
  printf("Volume can hold a %ld block, %.f k %.2f M filesystem\n",
         cx->fsavail / 512,
         (double)cx->fsavail / 1024, (double)cx->fsavail / 1024 /1024 );
    
  if( !cx->fslen )
    {
      printf("Suggest:\n        s4mkfs <fs-file-name> %ld %d %d\n",
             cx->fsavail / 512, 0, cx->pscyl/2 );
    }

  if( cx->fslen && cx->fsavail < cx->fslen )
    {
      printf("\nFS %.fk %.2fM too big for available disk %.fk %.2fM\n",
             (double)cx->fslen / 1024, (double)cx->fslen / 1024 /1024,
             (double)cx->fsavail / 1024, (double)cx->fsavail / 1024 /1024 );
      rv++;
    } 
  
 done:

  return rv;
}

static int s4vol_execute( s4volcx *cx )
{
  int          rv = 0;
  s4_fsu       fsu;
  s4err        err;

  if( !cx->outfile )
    return rv;

  /* open output file read/write.  Might be the in file. */
  if( (cx->ovinfo.fd = open( cx->outfile, 006|O_CREAT, 0640 )) < 0 )
    {
      printf("Can't open output file '%s', %s\n", 
             cx->outfile, strerror(errno));
      rv++;
      goto done;
    }

  /* construct and write new volume header */

  if( s4_ok != (err = s4vol_new_vhbd( cx )))
    goto done;

  /* setup geometry in ovinfo with new vhbd */
  s4_init_vol( "s4vol",  cx->ovinfo.fd, cx->cyls, cx->heads, 512, 
               cx->pscyl, cx->ovhbd, &cx->ovinfo );

  /* Write the bad block table */
  memset( fsu.buf, 0, sizeof(fsu.buf) );
  fsu.bbt[0].cyl    = S4_NO_BB_CHECKSUM;  /* turn off BBT and LBA mapping */
  fsu.bbt[0].badblk = S4_NO_BB_CHECKSUM; 
  if( s4_ok != s4_seek_write( cx->ovinfo.fd, cx->ovinfo.bbt_ba/512, 
                              fsu.buf, sizeof(fsu.buf) ) )
    {
      rv++;
      goto done;
    }
  printf("Wrote anti-bad block table\n");

  /* Write the loader, offset from the resource map(s) */
  if( cx->ldlen )
    {
      printf("Installing loader...\n");
      err = s4vol_import_or_transfer( &cx->ovinfo, &cx->ivinfo, 
                                      S4_LD_PNUM, S4_INDLOADER,
                                      cx->ldfd );
      if( s4_ok != err )
        goto done;
    }

  /* write FS image */
  if( cx->fslen )
    {
      printf("Installing filesystem...\n");
      err = s4vol_import_or_transfer( &cx->ovinfo, &cx->ivinfo, 
                                      cx->fspnum, 0,
                                      cx->fsfd );
    }

  if( cx->xflag )
    {
      printf("\n\nInformation about output volume '%s':\n", 
             cx->outfile );
      s4_fsu_show( (s4_fsu*)&cx->ovinfo.vhbd, s4b_vhbd );
      printf("\n\n");
    }


 done:

  return rv;
}


static s4err s4vol_import_or_transfer( s4_vol *ovinfo,
                                       s4_vol *ivinfo,
                                       int pnum, int resnum, 
                                       int ifd )
{
  s4err err = s4_ok;

  struct s4_resdes *ires = &ovinfo->vhbd.resmap[0];
  struct s4_resdes *ores = &ivinfo->vhbd.resmap[0];

  int ooffblks = 0;
  int oblks;

  /* handle ugly loader case. */
  if( S4_LD_PNUM == pnum )
    ooffblks = ores[resnum].blkstart * 2;

  /* if external, import */
  if( ifd > 0  )
    {
      struct stat sb;

      fstat( ifd, &sb );
      if( (sb.st_size + 511) / 512 >
          ovinfo->parts[pnum].pblks - ooffblks )
        {
          printf("File %ldk is too big for partition %dk\n",
                 (sb.st_size +1023)/ 1024,
                 ovinfo->parts[pnum].lblks * 512 / 1024 );
          err = s4_error;
          goto done;
        }
      err = s4_vol_import( ovinfo, pnum, ooffblks, ifd );
    }
  else                      /* transfer from vol to vol */
    {
      int ioffblks, iblks;

      if( S4_LD_PNUM == pnum )
        {
          ioffblks = ires[resnum].blkstart * 2;          
          iblks = (ires[resnum].nblocks * 2);
        }
      else
        {
          ioffblks = 0;
          if( s4a_lba == ivinfo->lba_or_pba )
            iblks = ivinfo->parts[pnum].lblks;
          else
            iblks = ivinfo->parts[pnum].pblks;
        }

      printf("Source has %d blocks, %dk\n",
             iblks, iblks * 512 / 1024 );

      if( s4a_lba == ovinfo->lba_or_pba )
        oblks = ovinfo->parts[pnum].lblks;
      else
        oblks = ovinfo->parts[pnum].pblks;

      if( iblks > (oblks - ooffblks) )
        {
          printf("Source %d blks too big for destination %d blks\n",
                 iblks, oblks - ooffblks );
          err = s4_error;
          goto done;
        }
      err = s4_vol_transfer( ovinfo, pnum, ooffblks,
                             ivinfo, pnum, ioffblks,
                             iblks );
    }
 done:

  return err;
}



/* open for read and get length */
static int s4vol_read_open( char *fn, int *fd, long *len )
{
  int rv = 0;
  struct stat sb;

  *fd = open( fn, 004, 0 );
  if( *fd > 0 )
    {
      fstat( *fd, &sb );
      *len = sb.st_size;
      printf("Opened '%s', length %ld\n", fn, *len );
    }
  else
    {
      printf("Can't open '%s' - %s\n", 
             fn, strerror( errno ));
      rv++;
    }
  return rv;
}



static int s4vol_new_vhbd( s4volcx *cx )
{
  int              rv = 0;
  struct s4_vhbd  *vh = cx->ovhbd;

  memset(vh, 0, sizeof(*vh));

  /* ----------------- */
  /* build up new vhbd */
  vh->magic    = S4_VHBMAGIC;
  vh->chksum   = 0;
  snprintf( vh->dsk.name, sizeof(vh->dsk.name), "%s", "s4vol" );
  vh->dsk.cyls       = cx->cyls;
  vh->dsk.heads      = cx->heads;
  vh->dsk.psectrk    = cx->pstrk;
  vh->dsk.pseccyl    = cx->pstrk * cx->heads;
  vh->dsk.flags      = S4_NEWPARTTAB;
  vh->dsk.step       = 1;
  vh->dsk.sectorsz   = 512;

  vh->partab[0].sz.strk = 0;
  vh->partab[1].sz.strk = cx->ldtrks;
  if( cx->fspnum == S4_HD_FS_PNUM )
    vh->partab[2].sz.strk = cx->ldtrks + cx->pagetrks;
  else
    vh->partab[2].sz.strk = 0;
  vh->partab[3].sz.strk = 0;

  vh->resmap[S4_INDLOADER].blkstart = 2;
  vh->resmap[S4_INDLOADER].nblocks  = (cx->ldlen + 1023) / 1024;
  vh->resmap[S4_INDBBTBL].blkstart  = 1;
  vh->resmap[S4_INDBBTBL].nblocks   = 1;
  vh->resmap[3].blkstart  = 0;
  vh->resmap[3].nblocks   = 0;

  vh->fpulled = 0;
  memset( vh->mntname, 0, sizeof(vh->mntname) );
  vh->mntname[2].name[0] = '/'; /* mount on root */
  vh->time = (s4_time)time(0);
  vh->cpioMagic = 0;
  vh->setMagic  = 0;
  vh->cpioVol   = 0;

  /* Write the volume header, swapped */
  printf("\n");

  s4_fsu_show( (s4_fsu*)vh, s4b_vhbd );  

  s4_fsu_swap( (s4_fsu*)vh, s4b_vhbd );
  if( s4_ok != s4_seek_write( cx->ovinfo.fd, 0, (char*)vh, 512 ))
    rv++;

  if( !rv  )
    printf("Wrote volume header\n");

  /* make it memory order again */
  s4_fsu_swap( (s4_fsu*)vh, s4b_vhbd );
  return rv;
}



static const char s4v_1040_8_16_6_588[] = {
    0x55, 0x51, 0x56, 0x51, 0x06, 0x41, 0x4F, 0x33,
    0x57, 0x49, 0x4E, 0x43, 0x48, 0x45, 0x04, 0x10,
    0x00, 0x08, 0x00, 0x10, 0x00, 0x80, 0x08, 0x00,
    0x02, 0xEE, 0x08, 0x00, 0x07, 0x00, 0x00, 0x02,
    0x4D, 0xEE, 0x37, 0x00, 0x02, 0x00, 0x17, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x01, 0xEE, 0xFF, 0x00,
    0xEE, 0xFF, 0x00, 0xEE, 0xB2, 0x00
};

static const char s4v_1024_8_17_7_632[] = {
    0x55, 0x51, 0x56, 0x51, 0x06, 0x0C, 0x4F, 0x42,
    0x57, 0x49, 0x4E, 0x43, 0x48, 0x45, 0x04, 0x00,
    0x00, 0x08, 0x00, 0x11, 0x00, 0x88, 0x08, 0x00,
    0x02, 0xEE, 0x08, 0x00, 0x08, 0x00, 0x00, 0x02,
    0x79, 0xEE, 0x37, 0x00, 0x02, 0x00, 0x17, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x01, 0xEE, 0xFF, 0x00,
    0xEE, 0xFF, 0x00, 0xEE, 0xB2, 0x00
};

static const char s4v_612_4_17_3_628[] = {
    0x55, 0x51, 0x56, 0x51, 0x06, 0x5C, 0x50, 0xDE,
    0x57, 0x49, 0x4E, 0x43, 0x48, 0x45, 0x02, 0x64,
    0x00, 0x04, 0x00, 0x11, 0x00, 0x44, 0x08, 0x00,
    0x02, 0xEE, 0x08, 0x00, 0x04, 0x00, 0x00, 0x02,
    0x75, 0xEE, 0x37, 0x00, 0x02, 0x00, 0x17, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x01, 0xEE, 0xFF, 0x00,
    0xEE, 0xFF, 0x00, 0xEE, 0xB2, 0x00
};





static const struct {
  const int   len;
  const char *val;

} s4_known_vols[] = {

  { sizeof(s4v_1040_8_16_6_588),  s4v_1040_8_16_6_588 },
  { sizeof(s4v_612_4_17_3_628),   s4v_612_4_17_3_628  },
  { sizeof(s4v_1024_8_17_7_632),  s4v_1024_8_17_7_632 },
  { 0, NULL }

};



s4err s4_vol_checksum( s4_vol *vol )
{
  int i;
  struct s4_vhbd   vhbd;

  for( i = 0 ; s4_known_vols[i].len ; i++ )
    {
      s4rl_decode( s4_known_vols[i].val,
                   s4_known_vols[i].len,
                   (char*)&vhbd, sizeof(vhbd) );
                   
      s4_fsu_swap( (s4_fsu*)&vhbd, s4b_vhbd );

      /* if what we care about geometry matches, take it */
      if( vhbd.dsk.cyls == vol->vhbd.dsk.cyls &&
          vhbd.dsk.heads == vol->vhbd.dsk.heads &&
          vhbd.dsk.psectrk == vol->vhbd.dsk.psectrk &&
          vhbd.partab[1].sz.strk  == vol->vhbd.partab[1].sz.strk &&
          vhbd.partab[2].sz.strk  == vol->vhbd.partab[2].sz.strk )
        {
          memcpy( &vol->vhbd, (char*)&vhbd, sizeof(vhbd) );
          return s4_ok;
        }
    }
  printf("No checksum known for this geometry\n");
  return s4_error;
}


