/*
 * s4export.c
 *
 * Tool for exporting a filesystem image from a disk image,
 * removing bad blocks.
 *
 * Usage:  s4export -i volfile -o fsfile -v fspartnum -F -p
 */

#include <s4d.h>

int main( int argc, char **argv )
{
  int	      rv = 0;

  char       *pname   = argv[0];
  char       *volfile = NULL;
  char       *fsfile  = NULL;
  int         fd;
  s4_vol      vinfo;
  s4_vol    *d = &vinfo;
  s4_filsys   lfs;
  s4err       err = s4_ok;
  int         help = 0;
  int         consumed;
  int         blks;
  int         dbgflag = 0;

  for( argc--, argv++; argc > 0 && !help ; argc -= consumed, argv += consumed)
    {
      consumed = 2;
      if( argc > 1 )
        {
          if( !strcmp( "-i", argv[0] ))
            {
              volfile = argv[1]; continue;
            }
          else if( !strcmp( "-o", argv[0] ) )
            {
              fsfile = argv[1];
              continue;
            }
        }
      consumed = 1;
      if( !strcmp( "-d", argv[0] ) )
        {
          dbgflag++;
          continue;
        }
      else 
        {
          printf("Unexpected argument or missing value to '%s'\n", argv[0]);
          help = 1;
        }
    }

  if( help || !volfile || !*volfile || !fsfile || !*fsfile)
   {
      printf("usage: %s -i volfile -o fsfile\n", pname );
      exit( 1 );
    }
  printf("Volume file:   %s\n",     volfile );
  printf("FS-image file: %s\n",     fsfile );

  /* open the volfile */
  err = s4_open_vol( volfile, 004, d );
  if( s4_ok != err )
    {
      printf("Got '%s' opening %s\n", s4errstr(err), volfile );
      exit( 1 ) ;
    }
  printf("FS is on partition %d\n", d->fspnum );

  /* make sure there's a filesystem in the volume */
  err = s4_vol_open_filsys( d, d->fspnum, &lfs );
  if( s4_ok != err )
    {
      printf("Can't find filesystem in '%s'  -- %s\n",
             volfile, s4errstr(err) );
      exit( 1 );
    }

  /* 002 = write */
  if( (fd = creat( fsfile, 0640 )) < 0 )
  {
    printf("Can't open '%s' for write: %s\n", 
           fsfile, strerror(errno));
    exit( 1 );
  }

  if( s4a_lba == d->lba_or_pba )
    blks = d->parts[d->fspnum].lblks;
  else
    blks = d->parts[d->fspnum].pblks;

  err = s4_vol_export( d, d->fspnum, 0, blks, fd );

  close( fd );
  if( s4_ok != err )
   {
     printf("Errors exporting filesystem\n");
     unlink( fsfile );
     rv = 1;
   }

  s4_filsys_close( &lfs );
  s4_vol_close( d );

  return  rv;
}

