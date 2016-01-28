
#include <s4d.h>
#include <sys/stat.h>
#include <stdlib.h>

static void dumpfile( char *fn, int width, int breaks )
{
  int   fd;
  long  actual;
  char *buf;
  struct stat sb;

  fd = open( fn, 004, 0 );
  if( fd < 0 )
    {
      printf("can't open '%s': %s\n", fn, strerror(errno));
    }
  else
    {
      fstat( fd, &sb );

      if( !(buf = malloc( sb.st_size ) ) )
        {
          printf("Can't alloc %ld bytes to read '%s'\n", 
                 (long)sb.st_size, fn );
          return;
        }
      actual = read( fd, buf, sb.st_size );
      s4dump( buf, actual, 0, width, breaks );
  
      free( buf );
    }
}



int main( int argc, char **argv )
{
  char *pname = argv[0];
  int   consumed;
  int   width = 0;
  int   breaks = 1024;

  argc--; argv++;
  for( ; argc > 0 ; argc -= consumed, argv += consumed )
    {
      consumed = 2;
      if( argc > 1 )
        {
          if( !strcmp( argv[0], "-w" ) )
            {
              width = atoi( argv[1] );
              continue;
            }
          else if( !strcmp( argv[0], "-b" ) )
            {
              breaks = atoi( argv[1] );
              continue;
            }
        }
      
      consumed = 1;
      if( !strcmp( argv[0], "-h" ))
        {
          printf("Usage: %s [-w width] [-b breaks] file ...\n",
                 pname );
          exit( 0 );
        }

      dumpfile( argv[ 0 ], width, breaks );
    }
  exit(0);
}

