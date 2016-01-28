/*
 * s4test.c -- trying things w/s4d
 */

#include <s4d.h>

typedef struct s4_vhbd    myvhbd;
typedef struct s4_dfilsys myfilsys;

static void s4_vol_find_fs( s4_vol *vinfo );
static void s4_vol_find_rootdir( s4_vol *vinfo );
static void s4_vol_find_fblks( s4_vol *vinfo );


static void s4_conv_test( s4_vol *d,
			  int pnum,
                          int pba, 
			  int lba, int offset, 
			  int track, 
			  int cyl, int pcylsec, int lcylsec,
			  int head, int hdsec );

static void s4_rll_test(void);

static int tracks_to_test[] = { 0, 7, 8, 15, 16,
                                632, 633, 634, 635, 636, 637, 639, 640, 769, 
                                -1 };

static int pbas_to_test[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                              11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                              134, 135, 136, 137, 138, 139, 140,
                              276, 277, 278, 279, 280,
                              2008, 2009, 2010, 2011, 2012,
                              10761, -1 };

static int lbas_to_test[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 
                              9, 10, 11, 12, 13, 14, 15, 16, 17, 
                              125, 126, 127, 128, 129, 130, 131, 132,
                              133, 134, 135, 136, 137, 138, 139,
                              260, 261, 262, 
                              1890, 1891, 1892,
                              10110, 10111, 10112, 10113, 10114,
                              10115, 10116, 10117, 10118, 10119,
                              10120, 10121, 10122, 10123, 10124,
                              10125, 10126, 10127, 10128, 10129,
                              10130, 10131, 10132, 10133, 10134,
                              -1 };

static int offsets_to_test [] = { 0, 136, 512, 1024, 
                                  68608, 69632, 70144,
                                  5500928, 5509632, -1 };

static int fslbas_to_test[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                11, 12, 13, 14, 15, 16, 17, 18,
                                112, 128, 144, 160, 176, 
                                -1 };


int main( int argc, char **argv )
{
  char        *devfile = argv[1];
  s4_vol  vinfo;
  s4_vol *d = &vinfo;

  printf("\nChecking math:\n");

  s4_init_vol( "s4test fake", -1, 1024, 8, 512, 17*8, NULL, d );
  s4_vol_set_part( d, 0, 0,      8 );
  s4_vol_set_part( d, 1, 8,    625 );
  s4_vol_set_part( d, 2, 633, 7559 );
  d->nparts = 3;

  s4_vol_show( d );
  printf("\n");

  s4_conv_test( d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 );

  /* where we think the FS is */
  s4_conv_test( d, 2, 10761, 10128, 5509632, 633, 79, 16, 17, 1, 0 );

  if( devfile && *devfile )
    {
      printf("\nLooking for filesystem in %s...\n", devfile );

      /*  004 = O_READ */
      if( s4_open_vol( devfile, 004, d ) )
        {
          printf("didn't open %s\n", devfile);
          exit( 1 );
        }

      s4_vol_show( d );

      s4_vol_find_fs( d );
      s4_vol_find_rootdir( d );
      s4_vol_find_fblks( d );

      // s4_vol_close( d );
    }

  s4_rll_test();


  return 0;
}

void s4_test_macros(void)
{
  int	      i, lba, pba, fblk, offset, fslba;
  int         track;
  s4_vol   vinfo;
  s4_vol  *d = &vinfo;
  s4_filsys     filsys;
  s4_filsys    *fs = &filsys;

  printf("Converting TRACK to ...\n");
  printf(    "Track    Offset   PBA   LBA  Cyl  CS  HD\n");
  for( i = 0; (track = tracks_to_test[i]) >= 0 ; i++ )
    {
      printf(" %5u %8u %5u %5u %4u %3u %3u\n",
             track,
             TRK_TO_OFFSET( d, track ),
             TRK_TO_PBA(    d, track ),
             TRK_TO_LBA(    d, track ),
             TRK_TO_CYL(    d, track ),
             TRK_TO_CYLSEC( d, track ),
             TRK_TO_HEAD(   d, track )
             );
    }
  printf("\n");

  printf("Converting PBA's foo:\n");
  printf("  PBA     OFFSET   LBA   TRK  CYL HD HS  CS\n");
  for( i = 0; (pba = pbas_to_test[ i ]) >= 0; i++ )
    {
      printf("%5u %10u %5u %5u %4u %2u %2u %3u\n",
             pba,
             PBA_TO_OFFSET(   d, pba ),
             PBA_TO_VOL_LBA(  d, pba ),
             PBA_TO_TRK(      d, pba ),
             PBA_TO_CYL(      d, pba ),
             PBA_TO_HEAD(     d, pba ),
             PBA_TO_HDSEC(    d, pba ),
             PBA_TO_CYLSEC(   d, pba ) );
    }
  printf("pba %d \n", pba);

  printf("Converting LBA's:\n");
  printf("    LBA    PBA   TRK  CYL HD HS LCS\n");
  for( i = 0; (lba = lbas_to_test[ i ]) >= 0; i++ )
    {
      printf("%6u  %6u %5u %4u %2u %2u %3u\n",
             lba,
             LBA_TO_VOL_PBA( d, lba ),
             LBA_TO_TRK(      d, lba ),
             LBA_TO_CYL(      d, lba ),
             LBA_TO_HEAD(     d, lba ),
             LBA_TO_HDSEC(    d, lba ),
             LBA_TO_CYLSEC(   d, lba ) );
    }
  printf("\n");

  printf("Converting offsets's:\n");
  printf("    OFFSET    PBA    CYL   TRK HD  CS HS\n");
  for( i = 0; (offset = offsets_to_test[ i ]) >= 0; i++ )
    {
      printf("%10u  %5u  %5u %5u %2u %3u %2u\n",
             offset,
             OFFSET_TO_PBA(        d, offset ),
             OFFSET_TO_CYL(        d, offset ),
             OFFSET_TO_TRK(        d, offset ),
             OFFSET_TO_HEAD(       d, offset ),
             OFFSET_TO_CYLSEC(     d, offset ),
             OFFSET_TO_HDSEC(      d, offset ));
    }
  printf("\n");

  fs->vinfo = d;
  fs->part  = &d->parts[2];
  fs->bksz  = 1024;
  fs->nbb   = 0;
  fs->bbt   = &fs->bbt_fsu.bbt[0];
  fs->bbt[0].cyl = 0;
  fs->bbt[0].altblk = 0;
  fs->bbt[0].nxtind = 0;
  fs->bbt[1].cyl = 0;
  fs->bbt[1].altblk = 0;
  fs->bbt[1].nxtind = 0;

  printf("New fake disk info:\n");
  s4_vol_show( d );
  printf("\n");

  fslba    = TRK_TO_LBA( d, 633 );

  printf("\nFS LBA to ...\n" );
  printf(     "FSLBA  FBLK ABSLBA   PBA  CYL   TRK  HD  LS  PS (* = remapped)\n");
  for( i = 0; (lba = fslbas_to_test[i]) >= 0; i++ )
    {
      fblk = i/2;
      pba = LBA_TO_FS_PBA( fs, fslba + lba ),
      printf( "%5u %5u  %5u %5u %4u %5u  %2u  %2u  %2u%c\n",
              lba,
              fblk,
              lba + fslba,
              pba,
              LBA_TO_CYL( d, lba ),
              LBA_TO_TRK( d, lba ),
              LBA_TO_HEAD( d, lba ),
              LBA_TO_HDSEC( d, lba ),
              PBA_TO_HDSEC( d, pba ),
              LBA_TO_HDSEC( d, lba ) != PBA_TO_HDSEC( d, pba ) ? '*' : ' ');
    }


  return;
}




#define TESTCASE( n, m, d, a, b )		\
  { if ( (x = m( d, a )) != b )			\
      { printf( "%-20s F:%12u != C:%-12u %10d\n", n, x, b, a ); ++errs; } }

/* given canonical values, convert from one to another and check */
static void s4_conv_test( s4_vol *d,
			  int pnum,
                          int pba,
			  int lba,
                          int offset,
			  int trk,
			  int cyl, 
                          int lcylsec,
                          int pcylsec,
			  int head, 
                          int hdsec )
{
  int	x;  			/* used by TESTCASE macro above */
  int   errs = 0;

  printf("\nconvert: prt:%u p:%8u l:%8u o:%10u t:%5u c:%4u lcs:%3u pcs:%3u h:%2u hs:%2u\n",
	 pnum, pba, lba, offset, trk, cyl, lcylsec, pcylsec, head, hdsec );

  TESTCASE( "PNUM_TO_OFFSET", PNUM_TO_OFFSET, d, pnum, offset );

  TESTCASE( "PBA_TO_OFFSET",   PBA_TO_OFFSET,   d, pba,  offset );
  TESTCASE( "PBA_TO_TRK",      PBA_TO_TRK,      d, pba,  trk    );
  TESTCASE( "PBA_TO_CYL",      PBA_TO_CYL,      d, pba,  cyl    );
  TESTCASE( "PBA_TO_HEAD",     PBA_TO_HEAD,     d, pba,  head   );
  TESTCASE( "PBA_TO_CYLSEC",   PBA_TO_CYLSEC,   d, pba,  pcylsec );
  TESTCASE( "PBA_TO_HDSEC",    PBA_TO_HDSEC,    d, pba,  hdsec    );
  TESTCASE( "PBA_TO_VOL_LBA", PBA_TO_VOL_LBA, d, pba,  lba    );

  TESTCASE( "LBA_TO_VOL_OFFSET", LBA_TO_VOL_OFFSET, d, lba, offset );
  TESTCASE( "LBA_TO_VOL_PBA",    LBA_TO_VOL_PBA,    d, lba, pba    );
  TESTCASE( "LBA_TO_TRK",         LBA_TO_TRK,         d, lba, trk    );
  TESTCASE( "LBA_TO_CYL",         LBA_TO_CYL,         d, lba, cyl    );
  TESTCASE( "LBA_TO_HEAD",        LBA_TO_HEAD,        d, lba, head   );
  TESTCASE( "LBA_TO_CYLSEC",      LBA_TO_CYLSEC,      d, lba, lcylsec );
  TESTCASE( "LBA_TO_HDSEC",       LBA_TO_HDSEC,       d, lba, hdsec  );

  TESTCASE( "OFFSET_TO_PBA",    OFFSET_TO_PBA,    d, offset, pba    );
  TESTCASE( "OFFSET_TO_TRK",    OFFSET_TO_TRK,    d, offset, trk    );
  TESTCASE( "OFFSET_TO_CYL",    OFFSET_TO_CYL,    d, offset, cyl    );
  TESTCASE( "OFFSET_TO_HEAD",   OFFSET_TO_HEAD,   d, offset, head   );
  TESTCASE( "OFFSET_TO_CYLSEC", OFFSET_TO_CYLSEC, d, offset, pcylsec );
  TESTCASE( "OFFSET_TO_HDSEC",  OFFSET_TO_HDSEC,  d, offset, hdsec  );

  TESTCASE( "TRK_TO_PBA",    TRK_TO_PBA,    d, trk, pba    );
  TESTCASE( "TRK_TO_LBA",    TRK_TO_LBA,    d, trk, lba    );
  TESTCASE( "TRK_TO_CYL",    TRK_TO_CYL,    d, trk, cyl    );
  TESTCASE( "TRK_TO_HEAD",   TRK_TO_HEAD,   d, trk, head   );
  TESTCASE( "TRK_TO_CYLSEC", TRK_TO_CYLSEC, d, trk, pcylsec );
  TESTCASE( "TRK_TO_OFFSET", TRK_TO_OFFSET, d, trk, offset );

  printf("%2d errs: prt:%u p:%8u l:%8u o:%10u t:%5u c:%4u lcs:%3u pcs:%3u h:%2u hs:%2u\n\n",
	 errs, pnum, pba, lba, offset, trk, cyl, lcylsec, pcylsec, head, hdsec );
}






/* locate possible freelist blocks by inspection */
static void s4_vol_find_fblks( s4_vol *vinfo )
{
  int   i, j;
  char  buf[ 512 ];
  int	offset;
  int	rv;
  int	nfree;
  int	dat;
  int	nope;

  struct s4_fblk *fblk = (struct s4_fblk*)buf;

  printf("\nFinding fblks, scanning %d blocks\n", vinfo->pblks);

  /* start at very beginning */
  for( offset = i = 0; i < vinfo->pblks; i++, offset += vinfo->secsz )
    {
      rv = s4_seek_read( vinfo->fd, (long)offset, buf, sizeof(buf));
      if( rv )
        break;

      nope = 0;

      nfree = s4swapi( fblk->df_nfree );
      if( nfree > 0 && nfree < S4_NICFREE )
        {
          for( j = 0 ; j < S4_NICFREE; j++ )
            {
              dat = s4swapi( fblk->df_free[ j ] );
              if( 0 == dat )
                break;

              if( dat < 0 && dat > 30000 )
                {
                  /* out of range for valid */
                  nope = 1;
                }
            }
          if( j != nfree )
            {
              printf("apparent %3d not viable claimed %3d in lba %u\n",
                     j, nfree, i );
              nope = 0;
            }
        }
      else
        {
          nope = 1;
        }

      if( !nope )
        {
          printf(" off %8u, PBA %5u, cyl %4u csec %3u trk %4u hd %2u hs %2u \n",
                 offset,
                 OFFSET_TO_PBA(	   vinfo, offset ),
                 OFFSET_TO_CYL(    vinfo, offset ),
                 OFFSET_TO_CYLSEC( vinfo, offset ),
                 OFFSET_TO_TRK(    vinfo, offset ),
                 OFFSET_TO_HEAD(   vinfo, offset ),
                 OFFSET_TO_HDSEC(  vinfo, offset ) );
          break;
        }
    }
  printf("Stopped at offset %u, PBA %u\n",
         offset, OFFSET_TO_PBA( vinfo, offset ) );
}



static void s4_vol_find_fs( s4_vol *vinfo )
{
  int   i, j;
  int  *ip;
  char  buf[ 512 ];
  int	offset;
  int	rv;
  int   found;

  printf("Finding FS the hard way, scanning %d blocks, should be near PBA %d\n",
         vinfo->pblks, TRK_TO_PBA( vinfo, vinfo->parts[2].strk ) );

  /* start at very beginning */
  for( found = offset = i = 0; !found && i < vinfo->pblks; i++, offset += vinfo->secsz )
    {
      rv = s4_seek_read( vinfo->fd, (long)offset, buf, sizeof(buf));
      if( rv )
	break;

      /* 128 is ints in a 512 byte block */
      ip = (int*)buf;
      for( j = 0; !found && j < 128 && !found ; j++ )
	{
	  if( S4_FsMAGIC == s4swapi(ip[j]) )
	    {
	      printf("\n\nMaybe found in PBA %d %u offset %d at %d!!!\n", 
                     i, 
		     OFFSET_TO_PBA( vinfo, offset),
		     offset,
		     (int)(j * sizeof(int)));

              printf("   offset %8u, PBA %i %5u, cyl %4u csec %3u trk %4u hd %2u hs %2u\n\n",
                     offset,
                     i, 
                     OFFSET_TO_PBA(    vinfo, offset ),
                     OFFSET_TO_CYL(    vinfo, offset ),
                     OFFSET_TO_CYLSEC( vinfo, offset ),
                     OFFSET_TO_TRK(    vinfo, offset ),
                     OFFSET_TO_HEAD(   vinfo, offset ),
                     OFFSET_TO_HDSEC(  vinfo, offset ) );


              if( i >= TRK_TO_PBA( vinfo, vinfo->parts[2].strk ) )
                found++;
            }
	}
    }
  printf("Stopped at offset %u, PBA %u\n",
	 offset, OFFSET_TO_PBA( vinfo, offset ) );
}



static const char *rootdirs[] =
  {
    "lost+found",
    "mnta",
    "mntb",
    "UNIX3.5",
    "Filecabinet",
    NULL
  };

static void s4_vol_find_rootdir( s4_vol *vinfo )
{
  int                 pba, fsblk;
  char                buf[ 512 ];
  const char        **np;
  char               *bp;
  int                 hits;
  int                 len;

  printf("\nFind ROOTDIR:\n"
         "Psrtition 2 starts at track %d, LBAA %d, PBA %d abs offset %d\n",
         vinfo->parts[2].strk,
         vinfo->parts[2].partlba,
         TRK_TO_PBA( vinfo, vinfo->parts[2].strk ),
         TRK_TO_OFFSET( vinfo, vinfo->parts[2].strk ) );

  fsblk = 0;
  for( pba = vinfo->parts[2].partoff/512; pba < vinfo->lblks; pba++, fsblk++ )
    {
      if( s4_ok != s4_seek_read( vinfo->fd, pba * 512, buf, sizeof(buf) ))
        break;

      hits = 0;

      /* for each name, see if it's in the block */
      for( np = rootdirs; *np ; np++ )
        {
          len = strlen( *np );
          for( bp = buf; bp < &buf[ 512 - 18 ]; bp++ )
            {
              if( !memcmp( bp, *np, len ) )
                {
                  printf("%s found in PBA %d LBAR %d, FBLK %d\n",
                         *np, pba, 
                         PBA_TO_VOL_LBA( vinfo, pba ) - vinfo->parts[2].partlba,
                         fsblk / 2 );
                  hits++;
                }
            }
        }
      if( hits > 3 )
        {
          printf("Absolute offset %d\n", PBA_TO_OFFSET( vinfo, pba ) );
          s4dump( buf, sizeof(buf), 0, 0, 0 );

          s4_fsu_swap( (s4_fsu*)buf, s4b_dir );
          s4_fsu_show( (s4_fsu*)buf, s4b_dir );
          break;
        }
    }
}




static char rll_to_test[] = { 
  0, 1, 2, 3, 0, 0, 0, 1,
  2, 3, 0, 0, 0, 0, 0, 0,
  4, 4, 4, 4, 9
};


static void s4_rll_test(void)
{
  char  ebuf[80];
  char  pbuf[80];
  int   olen, elen, plen;

  olen = (int)sizeof(rll_to_test);

  elen = s4rl_encode( rll_to_test, olen, ebuf, sizeof(ebuf));
  printf("\nEncoded:  %d\n", elen );
  s4dump( ebuf, elen, 0, 16, 0 );

  plen = s4rl_decode( ebuf, elen, pbuf, sizeof(pbuf) );

  printf("Orig: %d\n", olen);
  s4dump( rll_to_test, olen, 0, 16, 0 );


  printf("\nDecoded: %d plen\n", plen);
  s4dump( pbuf, plen, 0, 16, 0 );
  
}
