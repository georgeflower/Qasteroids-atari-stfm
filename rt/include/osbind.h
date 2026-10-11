/* Stand-in for the MiNT <osbind.h>: only the TOS calls the game uses (implemented in rt/sys.S). */
#ifndef ST_OSBIND_H
#define ST_OSBIND_H

long Super(void *ssp);
int Getrez(void);
void *Physbase(void);
void *Logbase(void);
void Setscreen(void *logical, void *physical, int rez);
void Ikbdws(int count, const char *ptr);
long Cconws(const char *text);

#endif
