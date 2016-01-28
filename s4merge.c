/*
 * s4merge -- merge multiple disk images into one
 *            choosing sectors that vary.
 *
 * Usage:  s4merge image-file ... -o output-image-file.
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <s4d.h>

# define S4MERGE_MAX_FILES  4

/* merge file */
typedef struct
{
  char      *fn;
  int        fd;
  char       buf[ 512 ];

} s4mf;


int main( int argc, char **argv )
{
  char *pname;
  char *outfn = NULL;
  int   ofd;                    /* output fd */
  int   nf = 0;                 /* number of input files */
  int   fr = 0;                 /* number of files read  */
  int   consumed;
  int   blk;                    /* current block */
  int   actual;                 /* size read/written */
  int   i, j;
  int   dif;                    /* current dif count */
  s4mf *ef;                     /* infput file */
  char  lbuf[ 128 ];            
  s4mf  infiles[ S4MERGE_MAX_FILES ];
  
  pname = argv[0];
  argc--;
  argv++;

  for( ; argc > 0 ; argv += consumed, argc -= consumed )
    {
      consumed = 2;
      if( argc > 1 )
        {
          if( !strcmp( "-o", argv[0] ) )
            {
              outfn = argv[1];
              continue;
            }
        }

      consumed = 1;
      if( nf < S4MERGE_MAX_FILES )
        {
          infiles[ nf ].fn = argv[0];
          infiles[ nf ].fd = open( argv[0], 004, 0 );
          if( infiles[ nf ].fd < 0 )
            {
              printf("%s opening input '%s' for read\n", strerror(errno), argv[0] );
              exit( 1 );
            }
          nf++;
        }
      continue;
    }
  if( argc > 0 )
    {
      printf("Usage %s infile ... -o outfile\n", pname );
      exit( 0 );
    } 
  ofd = open( outfn, 002| O_CREAT, 0640 );

  if( ofd < 0 )
    {
      printf("%s opening output '%s' for write\n", strerror(errno), outfn );
      exit( 1 );
    }

  /* until we break out */
  for( blk = 0; ; blk++ )
    {
      printf("%d...\r", blk );

      /* read all */
      for( fr = i = 0; i < nf ; i++ )
        {
          ef = &infiles[i];
          if( ef->fd > 0 )
            {
              actual = read( ef->fd, ef->buf, 512 );
              if( actual != 512 )
                {
                  printf("got %d from '%s', closing\n", actual, ef->fn );
                  close( ef->fd );
                  ef->fd = -1;
                }
              else
                {
                  fr++;
                }
            }
        }
      if( !fr )
        {
          printf("\nHit the end, nothing read\n");
          break;
        }

      /* compare what we got */
      /* find first open file */
      for( i = 0; i < nf && infiles[i].fd < 0; i++ )
        continue;
        
      for( dif = 0, j = i; j < nf; j++ )
        {
          if( infiles[j].fd > 0 )
            if( memcmp( infiles[i].buf, infiles[j].buf, 512 ) )
              dif++;
        }

      i = 0;                    /* assume block infile 0 */
      if( dif )
        {
          printf("\n");

        again:
          printf("%d diffs on block %d\n", dif, blk );
          printf("RESOLVE> ");
          fflush(stdout);
          while( fgets(lbuf, sizeof(lbuf), stdin) )
            {
              if( '\n' == lbuf[0] )
                goto again;

              i = atoi( lbuf );
              if( i )
                {
                  i--;
                  break;
                }
              else
                {
                  switch( lbuf[ 0 ] )
                    {
                    case 'l':
                      for( i = 0; i < nf ; i++ )
                        printf("[%d] %s\n", i + 1, infiles[i].fn );
                      break;

                    case 'b':
                      for( i = 0; i < nf ; i++ )
                        {
                          if( infiles[i].fd > 0 )
                            {
                              printf("\n[%d] %s:\n", i + 1, infiles[i].fn );
                              s4dump( infiles[i].buf, 512, 0, 0, 0 );
                            }
                        }
                      break;

                    case 'q':
                      exit( 0 );
                      break;

                    default:
                      printf("l -- list files\n"
                             "b -- show buffers\n"
                             "q -- quit\n");
                    }
                    goto again;
                }
            }
        }

      actual = write( ofd, infiles[i].buf, 512 );
      if( actual != 512 )
        {
          printf("%s writing output '%s'\n", strerror(errno), outfn );
          exit( 1 );
        }
    }

  close( ofd );
  for( i = 0; i < nf ; i++ )
    {
      if( infiles[i].fd > 0 )
        close( infiles[i].fd > 0 );
    }

  return 0;
}
