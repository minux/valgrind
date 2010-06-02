
/* This program handles linking the tool executables, statically and
   at an alternative load address.  Linking them statically sidesteps
   all sorts of complications to do with having two copies of the
   dynamic linker (valgrind's and the client's) coexisting in the same
   process.  The alternative load address is needed because Valgrind
   itself will load the client at whatever address it specifies, which
   is almost invariably the default load address.  Hence we can't
   allow Valgrind itself (viz, the tool executable) to be loaded at
   that address.

   Unfortunately there's no standard way to do 'static link at
   alternative address', so this program handles the per-platform
   hoop-jumping.
*/

/* What we get passed here is:
   first arg
      the alternative load address
   all the rest of the args
      the gcc invokation to do the final link, that
      the build system would have done, left to itself

   We just let assertions fail rather than do proper error reporting.
   We don't expect the users to run this directly.  It is only run
   from as part of the build process, with carefully constrained
   inputs.
*/

#if defined(VGO_linux)

// Don't NDEBUG this; the asserts are necesary for
// safety checks.
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main ( int argc, char** argv )
{
   int    i;
   size_t reqd = 0;

   // expect at least: alt-load-address gcc -o foo bar.o
   assert(argc > 5);

   // check for plausible-ish alt load address
   char* ala = argv[1];
   assert(ala[0] == '0');
   assert(ala[1] == 'x');

   // We'll need to invoke this to do the linking
   char* gcc = argv[2];

   // and the 'restargs' are argv[3 ..]

   // so, build up the complete command here:
   // 'gcc' -static -Ttext='ala' 'restargs'

   // first, do length safety checks
   reqd += 1+ strlen(gcc);
   reqd += 1+ 100/*let's say*/ + strlen(ala);
   for (i = 3; i < argc; i++)
      reqd += 1+ strlen(argv[i]);

   reqd += 1;
   char* cmd = calloc(reqd,1);
   assert(cmd);

   char ttext[100];
   assert(strlen(ala) < 30);
   memset(ttext, 0, sizeof(ttext));
   sprintf(ttext, " -static -Wl,-Ttext=%s", ala);

   strcpy(cmd, gcc);
   strcat(cmd, ttext);
   for (i = 3; i < argc; i++) {
     strcat(cmd, " ");
     strcat(cmd, argv[i]);
   }

   assert(cmd[reqd-1] == 0);

   if (0) printf("\n");
   printf("link_tool_exe: %s\n", cmd);
   if (0) printf("\n");

   int r = system(cmd);

   free(cmd);

   // return the result of system.  Note, we should handle it
   // properly; that would involve using WEXITSTATUS on the
   // value system gives back to us.
   return r;
}

#elif defined(VGO_darwin)

#error Daaaawin

#else
#  error "Unsupported OS"
#endif
