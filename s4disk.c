/*
 * s4disk
 *
 * Tool for exploring AT&T UNIX PC 7300/3b1 "Safari 4" disks.
 *
 * Explores the partition table and starts to decode the filesystem
 *
 * Usage:
 *
 *      s4disk <volfile> [-fs] [-dump] -steal
 *
 */

#include <s4d.h>

static void s4_dump_vol( s4_vol *vinfo );
static void s4_steal_vol_header( s4_vol *vinfo );

int main( int argc, char **argv )
{
  char    *pname = argv[0];
  char    *devfile = NULL;
  s4_vol   vinfo;
  s4err    err;
  int      fsflag = 0;
  int      dumpflag = 0;
  int      stealflag = 0;
  int      consumed;

  for( argc--, argv++ ; argc > 0 ; argc -= consumed, argv += consumed )
    {
      consumed = 1;
      if( !strcmp( argv[0], "-fs" ) )
        {
          fsflag = 1;
          continue;
        }
      else if( !strcmp( argv[0], "-dump" ) )
        {
          dumpflag = 1;
          continue;
        }
      else if( !strcmp( argv[0], "-steal" ) )
        {
          stealflag = 1;
          continue;
        }
      else if( !strcmp( argv[0], "-h" ) )
        {
          printf("Usage: %s [-fs][-dump] volfile\n", pname );
          exit( 0 );
        }
      else
        {
          devfile = argv[0];
          continue;
        }
    }

  if( !devfile || !*devfile )
    {
      printf("usage: %s devicefile\n", pname );
      exit( 1 );
    }
  printf("Device file %s\n", devfile );

  /* 004 = O_READ */
  if( (err = s4_open_vol( devfile, 004, &vinfo )) )
    {
      printf("Problem opening '%s' -- %s\n", 
             devfile, s4errstr(err) );
      exit( 1 ) ;
    }

  s4_vol_show( &vinfo );

  if( fsflag )
    {
      s4_filsys lfs;
      s4_vol_open_filsys( &vinfo, 2, &lfs );
      s4_filsys_show( &lfs );
      s4_filsys_close( &lfs );
    }


  if( dumpflag )
    s4_dump_vol( &vinfo );

  if( stealflag )
    s4_steal_vol_header( &vinfo );
      
  s4_vol_close( &vinfo );

  return 0;
}


static void s4_dump_vol( s4_vol *vinfo )
{
  char     buf[ 512 ];
  int      offset;
  int      blk;
  int      pnum;
  int      ppbase;
  int      plbase;
  int      lba;


  pnum = ppbase = plbase = 0;
  for( blk = 0; blk < vinfo->pblks; blk++ )
    {
      if( blk == vinfo->parts[pnum + 1].partpba )
        {
          pnum++;
          ppbase = vinfo->parts[pnum].partpba;
          plbase = vinfo->parts[pnum].partlba;
        }

      offset = blk * 512;
      if( s4_ok != s4_seek_read( vinfo->fd, offset, buf, 512 ))
        break;

      lba = PBA_TO_VOL_LBA( vinfo, blk );
      printf("\nPBAA %-7d LBAA %-7d "
             "Part %d: PBAR %-7d LBAR %-7d FSPBAR %-7d FSLBAR %-7d "
             "offset %d -- HDSEC %d/%d -- %s\n",
             blk, lba, 
             pnum, 
             blk - ppbase, 
             lba - plbase, 
             (blk - ppbase) / 2, 
             (lba - plbase) / 2, 
             offset,
             PBA_TO_HDSEC( vinfo, blk ), vinfo->pstrk - 1,
             PBA_TO_HDSEC( vinfo, blk ) == 
             (vinfo->pstrk - 1) ? "SPARE" : "data");

      s4dump( buf, sizeof(buf), 0, 0, 0);
    }
}




static void s4buf_to_c( FILE *fp, char *name, char *inbuf, int len )
{
  int i;

  fprintf(fp, "static const char %s[] = {\n", name );

  fprintf(fp, "    ");
  for( i = 0; len ; i++, len-- )
    {
      if( i && !(i % 8) )
        fprintf(fp, ",\n    ");

      fprintf(fp, !(i % 8) ? "0x%02X" : ", 0x%02X", inbuf[i] & 0xff );
    }
  fprintf(fp, "\n};\n\n");
}



static void s4_steal_vol_header( s4_vol *vinfo )
{
  FILE *fp;
  char  nbuf[ 80 ];
  char  fnbuf[ 80 ];
  char  ebuf[ 1024 ];           /* encoded */
  char  pbuf[ 1024 ];           /* plain back from encoding */
  int   elen, plen;

  /* cyls-hds-secs-ldtrks-pgtrks */
  snprintf( nbuf, sizeof(nbuf), "s4v_%d_%d_%d_%d_%d",
            vinfo->cyls,
            vinfo->heads,
            vinfo->pstrk,
            vinfo->parts[1].strk - 1,
            vinfo->fspnum == S4_FP_FS_PNUM ? 0 : vinfo->parts[2].strk - 1 );
  
  snprintf( fnbuf, sizeof(fnbuf), "%s.c", nbuf );

  fp = fopen( fnbuf, "w" );
  if( !fp )
    {
      printf("can't open '%s'\n", fnbuf );
      exit( 1 );
    }

#ifdef S4_LITTLE_ENDIAN
  s4_fsu_swap( (s4_fsu*)&vinfo->vhbd, s4b_vhbd );
#endif
  elen = s4rl_encode( (char*)&vinfo->vhbd, sizeof(vinfo->vhbd),
                      ebuf, sizeof(ebuf));
  if( elen < 0 )
    {
      printf("Encode failed\n");
      exit( 1 );
    }

  /* verify the encoded stuff decodes correctly */
  memset( pbuf, 0xff, sizeof(pbuf) );
  plen = s4rl_decode( ebuf, elen, pbuf, sizeof(pbuf) );
  if( plen != sizeof(vinfo->vhbd) )
    {
      printf("decode len error plen %d, orig %d\n",
             plen, (int)sizeof(vinfo->vhbd) );
    }

  if( memcmp(  (char*)&vinfo->vhbd, pbuf, sizeof(vinfo->vhbd) ))
    {
      printf("mismatch!\n");
      s4dump( pbuf, sizeof(pbuf), 0, 16, 0 );
    }


#ifdef S4_LITTLE_ENDIAN
  /* put it back! */
  s4_fsu_swap( (s4_fsu*)&vinfo->vhbd, s4b_vhbd );
  s4_fsu_swap( (s4_fsu*)pbuf, s4b_vhbd );
#endif
  s4_fsu_show( (s4_fsu*)pbuf, s4b_vhbd );
  


  s4buf_to_c( fp, nbuf, ebuf, elen );

  fclose( fp );
}





