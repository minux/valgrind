// This test is for testing that the --threshold options in both Massif and
// ms_print work as they should.  A threshold of 10% is a good choice for
// this file, because in some parts of the tree it renders all children
// insignificant, and in others parts of the tree it renders only some
// children insignificant.
//
// Also, it's deliberate that the 'malloc(2000)' and 'my_malloc1(500)' calls 
// are in 'main' -- at one point, ms_print was failing to connect some
// children arrows when a more significant child didn't have any children of
// its own, eg:
//
//   |   
//   ->20.00% (2000B) 0x804846A: main (thresholds.c:43)
//   
//   ->13.00% (1300B) 0x80483A4: my_malloc2 (thresholds.c:16)
//
// (There must be a '|' between the '->'s.)

#include <stdlib.h>

void my_malloc1(int n)
{
   malloc(n);
}

void my_malloc2(int n)
{
   malloc(n);
}

void my_malloc3(int n)
{
   malloc(n);
}

void a7550(void)
{
   my_malloc1(6000);
   my_malloc2( 900);
}

void a450(void)
{
   my_malloc2( 300);
   my_malloc1( 100);
   my_malloc2( 100);
   my_malloc1(  50);
}

int main(void)
{
   a7550(); 
   a450(); 
   my_malloc1(500);
   malloc(2000);
   my_malloc3(50);
   return 0;
}
