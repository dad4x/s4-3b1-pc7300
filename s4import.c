
/*
 * s4export.c
 *
 * Tool for importing a filesystem image into a volume image,
 * replacing the old filesystem.
 * 
 * Usage:
 *
 *  s4import -i fsfile -o volfile -F
 *
 *  volfile and fsfile must already exist; volfile will
 *  be over-written.
 */

#include <s4d.h>
#include <errno.h>
#include <fcntl.h>

int main( int argc, char **argv )
{
  char       *pname     = argv[0];
  char       *volfile   = NULL;
  char       *fsfile    = NULL;
  int         ifd;
  s4_vol vinfo;
  s4_vol *d = &vinfo;
  int	      rv;
  s4err       err = s4_ok;
  struct stat sb;
  int         help = 0;
  int         consumed;
  int         dbgflag = 0;
  int         avail;
  
  for( argc--, argv++; argc > 0 ; argc -= consumed, argv += consumed )
    {
      if( argc > 1 )
        {
          consumed = 2;
          if( !strcmp( "-i", argv[0] ))
            {
              fsfile = argv[1];
              continue;
            }
          else if( !strcmp( "-o", argv[0] ) )
            {
              volfile = argv[1];
              continue;
            }
        }
      consumed = 1;
      if( !strcmp( "-F", argv[0] ) )
        {
          printf("Floppy! FIXME\n");
        }
      else if( !strcmp( "-d", argv[0] ) )
        {
          dbgflag++;
          continue;
        }
      else
        help = 1;
    }

  if( help || !volfile || !*volfile || !fsfile  || !*fsfile)
    {
      printf("usage: %s -i fsfile -o volimage [-F][-d]\n\n"
             "-i fsfile           filesystem image to import\n"
             "-o volfile          volume file to modify\n\n"
             "-F                  volume is a floppy\n"
             "-d                  increase debug output\n",
             pname );
      exit( 1 );
    }


  /* 006 = READ/WRITE */
  err =  s4_open_vol( volfile, 006, d );
  if( s4_ok != err )
    {
      printf("Can't open volume file %s, %s\n",
             volfile, s4errstr(err));
      exit( 1 ) ;
    }

  printf("Volume file to update: %s\n", volfile );
  printf("Filesystem image file: %s\n", fsfile );
  printf("using partition %d\n", d->fspnum );
  printf("Output volume is %s\n", d->isfloppy ? "floppy" : "HD");

  /* 004 = read the fsimage file */
  if( (ifd = open( fsfile, 004 )) < 0 )
    {
      printf("Can't open filesystem image '%s' for read: %s\n", 
             fsfile, strerror(errno));
      rv = 1;
      goto done;
    }

  if( s4a_lba == d->lba_or_pba )
    avail = d->parts[d->fspnum].lblks;
  else
    avail = d->parts[d->fspnum].pblks;

  fstat( ifd, &sb );
  if( (sb.st_size + 511) / 512 > avail )
    {
      printf("Filesytem %dk is too big for the partition of %dk\n",
             (int)(sb.st_size +1023)/ 1024,
             (int)(avail * 512 / 1024 ));
      rv = 1;
      goto done;
    }

  err = s4_vol_import( d, d->fspnum, 0, ifd );
  if( s4_ok != err )
    {
      printf("Error in import\n");
      rv = 1;
    }
     
 done:

  if( ifd > 0 )
    close( ifd );

  s4_vol_close( d );

  return  rv;
}




