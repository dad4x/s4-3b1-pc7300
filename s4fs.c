/*
 * s4fs
 *
 * Tool for exploring AT&T UNIX PC 7300/3b1 "Safari 4" filesystems.
 *
 * Explores a file that is supposed to be a file system image from
 * s4export.
 */

#include <s4d.h>

/* Addressing mode: lba 'b', fblk 'F'|'B', inode 'I' */
typedef enum { 

  albar,                        /* FS relative logical */
  afblk,                        /* FS block */
  ainode                        /* FS inode number */

} s4fs_amode;

static const char *s4fs_amodestr( s4fs_amode amode )
{
  const char *s = "BAD AMODE";
  switch( amode )
    {
    case albar:  s = "LBAR"; break;
    case afblk:  s = "FBLK"; break;
    case ainode: s = "INO";  break;
    }
  return s;
}

s4err s4fs_getint( const char *prompt, int *valp )
{
  char buf[ 128 ];

  printf("%s", prompt);
  fflush(stdout);
  if( !fgets( buf, sizeof(buf), stdin ) )
    return s4_error;
              
  *valp = atoi(buf);
  return s4_ok;
}



int main( int argc, char **argv )
{
  char       *fsfile = argv[1];
  int	      rv;
  int         j, x;
  int         btype, curadr, lastadr;
  int         ioffset = 0;
  s4_vol     *vinfo;
  s4_filsys   fs;
  s4_fsu      disk_fsu;
  s4_fsu      mem_fsu;
  char	      buf[ 1024 ];
  
  int         bmul = 1;
  s4fs_amode  amode = albar;
  int         fblk, lbar, offset;

  if( !fsfile || !*fsfile )
    {
      printf("usage: %s fsfile\n", argv[0] );
      exit( 1 );
    }

  printf("Filesystem image %s\n", fsfile );
  if( s4_ok != s4_open_filsys( fsfile, &fs ) )
    {
      printf("Failed as filesystem image\n");
      /* Try as disk image instead */
      exit( 1 );
    }

  curadr = 1;
  amode  = albar;
  vinfo  = fs.vinfo;

  printf("Opened OK, FS block size is %d\n", fs.bksz );

  btype     = s4b_super;
  s4_fsu_show( &fs.super, btype );
  lastadr = -1;
  for(;;)
    {
      /* prompt */
      printf("%s %d %s: ",
             s4fs_amodestr(amode), curadr, s4btypestr(btype) );

      fflush(stdout);
       
      /* get input line */
      buf[0] = 0;
      if( !fgets( buf, sizeof(buf), stdin ) )
	break;

      /* process line */
      x = buf[0];
      if( '-' == x || (x >= '0' && x <= '9') )
        {
          x = atoi( buf );
          if( x < 0 )
            {
              printf("NEG %d!\n", x);
              curadr += x;
            }
          else
            curadr = x;
        }
      else
        {
          switch( x )
            {
            case 'q':
            case 'Q':
              goto done;

              /* change addressing forces re-read */
            case 'b':   amode = albar;  bmul = 1; lastadr = -1; break;
            case 'F':   /* fall into 'B' */
            case 'B':   amode = afblk;  bmul = 2; lastadr = -1; break;
            case 'I':   amode = ainode; bmul = 1; lastadr = -1;
                        btype = s4b_ino; break;

              /* Change display */
            case 'a':   btype = s4b_unk;    break;
            case 's':   btype = s4b_super;  break;
            case 'i':   btype = s4b_ino;    break;
            case 'd':   btype = s4b_dir;    break;
            case 'x':   btype = s4b_idx;    break;
            case 'k':   btype = s4b_linkcnt;break;
            case 'f':   btype = s4b_free;   break;
            case 'r':   btype = s4b_raw;    break;

            case 'h':
            case '?':
              printf("NEWLINE:  next address\n");
              printf("<number>: specific address\n");
              printf("-<number>: backwards address(es)\n\n");

              printf("a:        show all formats\n");
              printf("u:        unknown, raw dump\n");
              printf("s:        superblock\n");
              printf("i:        inode\n");
              printf("d:        directory\n");
              printf("x:        indirect index\n");
              printf("l:        linkcount\n");
              printf("f:        freelist\n\n");
              printf("r:        raw disk dump\n\n");

              printf("b:        Address as 512 byte blocks\n");
              printf("B:        Address as FS-size blocks\n");
              printf("I:        Address Inode number\n\n");

              printf("D:        Goto directory block from inode\n");
              printf("j:        jump to inode from dir, dir from inodes\n");

              printf("q:        quit\n");
              continue;

            case 'D':
              if( ainode == amode && s4b_ino == btype )
                {
                  struct s4_dinode *inop = &mem_fsu.dino[ ioffset ];

                  if( (inop->di_mode & S_IFDIR)  )
                    {
                      curadr = s4_dinode_getfblk( inop, 0 );
                      amode  = afblk;
                      bmul   = 2;
                      btype  = s4b_dir;
                      printf("D curadr %d, bmul %d\n", curadr, bmul );
                    }
                  else
                    {
                      printf("INO %d is not a directory!\n\n", curadr );
                    }
                }
              else
                {
                  printf("\n\nWrong mode for D!\n");
                }
              break;

            case 'j':
              {
                switch( btype )
                  {
                  case s4b_dir:
                    if( s4_ok != s4fs_getint( "Jump to ino: ", &curadr ) )
                      continue;
                    btype   = s4b_ino;
                    amode   = ainode;
                    bmul    = 1;
                    lastadr = -1;
                    printf("jumping to inode %d\n", curadr );
                    break;
                    
                  case s4b_ino:
                    if( s4_ok != s4fs_getint( "Jump to DIR FBLK: ", &curadr ) )
                      continue;
                    btype   = s4b_dir;
                    amode   = afblk;
                    bmul    = 2;
                    lastadr = -1;

                    printf("jumping to directory at fblk %d\n", curadr );
                    break;
                  }
                break;
              }
              break;

            case '\n':
            default:
              curadr = lastadr + 1;  
              break;
            }
        }

      /* always compute offset, in case mode changes */
      ioffset = (curadr % S4_INOPB) - 1;

      /* compute equivalent units */
      switch( amode )
        {
        case albar:
          lbar   = curadr;
          fblk   = lbar / 2;
          break;

        case ainode:
          fblk    = ((curadr) / S4_INOPB) + 2;
          lbar    = fblk * 2;
          break;

        case afblk:
          fblk   = curadr;
          lbar   = fblk * 2;
        default:
          printf("oops\n");
          abort();
        }
      offset = lbar * 512;

      /* Print interesting info */
      printf("\nLBAR: %d  FBLK: %d offset %d\n",
             lbar, fblk, offset);

      /* reads always from FS relative LBA */
      if( lastadr != curadr || ainode == amode )
        {
          rv = s4_seek_read( vinfo->fd, offset, disk_fsu.buf, S4_BSIZE);
          if( s4_ok != rv )
            {
              printf("err %s reading %s block %d at %d\n",
                     s4errstr(rv), bmul == 1 ? "LBA" : "FS",
                     curadr, offset );
              break;
            }
          lastadr = curadr;
        }

      /* Pick pre-display header based on btype and amode */
      if( s4b_ino == btype && ainode == amode )
        {
          printf("\nShowing INO %d from FBLK %d LBAR %d idx %d\n", 
                 curadr, fblk, lbar, ioffset );
        }
      else if( s4b_ino == btype && ainode != amode )
        {
          printf("\nShowing FBLK %d LBAR %d starting at inode %d:\n",
                 fblk, lbar, (fblk - 2) * S4_INOPB );
        }
      else                      /* everything else */
        {
          printf("\nShowing %s %d %s: \n",
                 s4fs_amodestr(amode), curadr, s4btypestr(btype) );
        }

      /* swap and show as if it were this kind of block */
      if( s4b_unk == btype )
        {
          /* try all of 'em */
          for( j = s4b_first_fs; j < s4b_last_fs; j++ )
            {
              /* copy it, then modify copy */
              memcpy( &mem_fsu, &disk_fsu, sizeof(mem_fsu) );
              
              if( fs.doswap )
                s4_fsu_swap( &mem_fsu, j );
              s4_fsu_show( &mem_fsu, j );
            }
        }

      if( s4b_raw == btype )
        {
          /* dump disk buffer, not the mem buffer */
          s4_fsu_show( &disk_fsu, btype );
        }
      else
        {
          /* copy it, then modify copy */
          memcpy( &mem_fsu, &disk_fsu, sizeof(mem_fsu) );

          if( fs.doswap )
            s4_fsu_swap( &mem_fsu, btype );

          /* show single inode */
          if( ainode == amode && s4b_ino == btype )
            {
              s4_dinode_show( &mem_fsu.dino[ioffset] );
            }
          else
            {
              s4_fsu_show( &mem_fsu, btype );
            }
        }
    }
 done:
  
  // s4_filsys_show_inodes( &fs );
  // s4_filsys_show_freelist( &fs );
  s4_filsys_close( &fs );

  return 0;
}

