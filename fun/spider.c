#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#define lengthof(X) ((int)(sizeof(X)/sizeof(*(X))))
#define STACKS 10
#define HASHTABSIZE 150000

typedef struct {
	int dest,src,n;
} move;

#include "spidercheck.h"
#ifdef CHECKPOINT
static bool seeking=true;
#else
static bool seeking=false;
static move checkpoint[]={{-1,-1,-1},};
#endif

typedef signed char card;

typedef struct stack_s {
	int n;
	int hidden;
	int pickable;
	card c[128 - 3*sizeof(int)];
} stack;

struct tableau_s;
typedef struct tableau_s tableau;
struct global_s;
typedef struct global_s global;
struct vistab_s;
typedef struct vistab_s vistab;

struct vistab_s {
	const vistab *nexthash;
	int cards;
	card c[];
};

struct tableau_s {
	stack deck;
	stack s[STACKS];
	stack home;
	global *global;
	int depth;

	const tableau *prev;
	move last;
};

struct global_s {
	vistab *tabtab[HASHTABSIZE];
	tableau *first;
	int maxdepth;
	unsigned int matched,unmatched;
	long long int playscount;
	bool seeking;

	/* own malloc */
	char *memblock;
	size_t maxmem;
	size_t used;
	size_t lastused;
};

static const char facename[]="A23456789TJQK";
static const char suitname[]="CDHS";

#define face(C) ((C) % 13)
#define suit(C) ((C) / 13)
#define topcard(S) ((S)->c[(S)->n - 1])

#if 0
static int stacknum(const tableau *t,const stack *s) {
	ptrdiff_t n;
	n = s - &t->deck;
	if (n==0) return STACKS;
	n = s - t->home;
	if (n==0) return STACKS+1;
	n = s - t->s;
	if (n>=0 && n<STACKS) return n;
	return -1;
}
#endif

static const char *cardtotext(card c) {
	static char n[3];
	if (c==-1 || c>=52) {
		n[0]=n[1]='-';
		n[2]='\0';
		return n;
	}
	n[0]=facename[face(c)];
	n[1]=suitname[suit(c)];
	n[2]='\0';
	return n;
}

static const char *stackcardtotext(const stack *s,int i,const char *mark) {
	static char n[5];
	const char *c;

	if (i==-1) {
		snprintf(n,sizeof(n),"%d/%d",s->pickable,s->hidden);
		return n;
	}
	if (i >= s->n) {
		if (!mark || mark[0]=='\0') mark="  ";
		n[0]=' ';
		n[1]=mark[0];
		n[2]=mark[1];
		n[3]=' ';
		n[4]='\0';
		return n;
	}
	c=cardtotext(s->c[i]);
	n[1]=c[0];
	n[2]=c[1];
	n[4]='\0';
	if (i < s->hidden) n[0]='(',n[3]=')';
	else if (i < s->n - s->pickable) n[0]=':',n[3]=':';
	else n[0]=n[3]=' ';
	return n;
}

static void showtableau(const tableau *t,FILE *f) {
	int i,j,n;
	const stack *s,*ps;
	const char *mark;

	if (!f) f=stdout;
	if (!t) {
		fprintf(f,"No tableau\n");
		return;
	}
	for(i=0;i<lengthof(t->deck.c);i++) {
		n=0;
		for(j=0;j<STACKS;j++) {
			s=&t->s[j];
			ps = (t->prev) ? &t->prev->s[j] : s;
			if (i < s->n) n++;
			if (ps->n < s->n) {
				mark="^ ";
			}
			else if (ps->n > s->n && ps->n > i) {
				mark="\\/";
			}
			else
				mark=NULL;
			fprintf(f,"%-4s ",stackcardtotext(s,i,mark));
		}

		n++; /* see default below */
		switch(i) {
		case 0: fprintf(f,"    Home filled: %d\n",t->home.n); break;
		case 1: fprintf(f,"    Cards in deck: %d\n",t->deck.n); break;
		case 2: fprintf(f,"    Depth: %d\n",t->depth); break;
		case 3:
			if (t->last.n==-1) fprintf(f,"    Move: deal\n");
			else fprintf(f,"    Move: %d from %d->%d\n",t->last.n,t->last.src,t->last.dest);
			break;
		default: fprintf(f,"\n"); n--; break;
		}
		if (n<1) break;
	}
	#if 0
	for(j=0;j<STACKS;j++) {
		s=&t->s[j];
		fprintf(f,"%-4s ",stackcardtotext(s,-1,'\0'));
	}
	fprintf(f,"\n");
	#endif
}

static void showrewind(const tableau *t,FILE *f) {
	if (!f) f=stdout;
	fprintf(f,"Rewind...\n");
	for(;t;t=t->prev)
		showtableau(t,f);
}

#if 0
static void showstack(const stack *s) {
	int i;
	for(i=0;i<s->n;i++)
		printf("%s",stackcardtotext(s,i,'\0'));
	printf("\n");
}
#endif

static void err(const tableau *t,const char *msg) {
	static bool disabled=false;
	FILE *f;

	if (!msg) {
		disabled=!disabled;
		return;
	}
	if (disabled) return;
	printf("error reported: %s\n",msg);
	showrewind(t,NULL);
	f=fopen("spiderlog","w");
	if (f) {
		showrewind(t,f);
		fclose(f);
	}
	printf("\n\n\n%s",msg);
	showtableau(t,NULL);
	exit(1);
}

static card cardfromtext(const char *n) {
	const char *p;
	card c;
	do {
		p=strchr(facename,n[0]);
		if (!p) break;
		c=p-facename;
		p=strchr(suitname,n[1]);
		if (!p) break;
		c+=13*(p-suitname);
		return c;
	} while(0);
	return -1; /* invalid card */
}

static int pickable(const stack *s) {
	card c,d;
	int p;
	int visible;

	if (s->n == 0) return 0;
	visible = s->n - s->hidden;
	if (visible <= 1) return 1;
	c=topcard(s);
	for(p=1;;p++) {
		d = s->c[s->n - p - 1];
		if (d==-1 || c==-1) break;
		if (suit(c) != suit(d)) break;
		if (face(c)+1 != face(d)) break;
		if (p >= visible) break;
		c=d;
	}
	return p;
}

static bool movecards(tableau *t,stack *dst,stack *src,int n,bool special) {
	int i;
	card s,d;

	/* check valid */
	if (!special) {
		if (n > src->pickable) err(t,"bad movecards\n");
		if (dst->n > 0 && dst->hidden < dst->n) {
			d=topcard(dst);
			if (d==-1) err(t,"bad top\n");
			s=src->c[src->n - n];
			if (s==-1) err(t,"unknown src\n");
			if (face(d) != face(s)+1) {
				return false;
			}
		}
	}

	/* move the cards */
	for(i=0;i<n;i++) {
		int from = src->n - n + i;
		int to = dst->n;
		s = src->c[from];
		if (s==-1) err(t,"bad card\n");
		dst->c[to] = s;
		dst->n++;
	}
	src->n -= n;

	/* turnover? */
	if (src->hidden >= src->n && src->n > 0) {
		src->hidden = src->n - 1;
	}

	/* recalc */
	src->pickable=pickable(src);
	dst->pickable=pickable(dst);
	if (!special && dst->pickable==13) {
		movecards(t,&t->home,dst,dst->pickable,true);
		//showrewind(t,NULL);
		printf("got a 13 stack!\n");
		showtableau(t,NULL);
	}
	return true;
}

#if 0
static bool matchtableau(const tableau *t1,const tableau *t2) {
	const stack *s1,*s2;
	int i,j;

	if (t1->hash != t2->hash) return false;
	if (t1->deck.n != t2->deck.n) return false;
	if (t1->home.n != t2->home.n) return false;
	for(i=0;i<STACKS;i++) {
		s1=&t1->s[i];
		s2=&t2->s[i];
		if (s1->n != s2->n) return false;
		if (s1->hidden != s2->hidden) return false;
		for(j = s1->hidden;j < s1->n; j++)
			if (s1->c[j] != s2->c[j]) return false;
	}
	return true;
}
#endif

static void analysis(global *g) {
	/* minimal analysis */
	unsigned int maxhash=0;
	int chain,maxchain=0;
	int used=0,entries=0;
	int maxchainloc=0;
	const vistab *v;
	unsigned int i;

	for(i=0;i<HASHTABSIZE;i++) {
		v=g->tabtab[i];
		if (!v) continue;
		if (i>maxhash) maxhash=i;
		used++;
		chain=0;
		while(v) {
			v=v->nexthash;
			entries++;
			chain++;
		}
		if (chain > maxchain) {maxchain=chain; maxchainloc=i;}
	}
	printf("Hash table %d/%d filled with %d plays (%gx), maxhash=%u\n",
	  used,HASHTABSIZE,entries,(double)entries/HASHTABSIZE,maxhash);
	printf("Longest chain: %d at %d\n",maxchain,maxchainloc);
	printf("Size of tableau: %zu\n",sizeof(tableau));
	printf("Matched %u Unmatched %u (%.1f%%)\n",
	  g->matched,g->unmatched,g->matched*100.0/(g->matched+g->unmatched));
}

static vistab *vtalloc(global *g,int cards) {
	vistab *v;
	size_t s,as;
	const size_t alignment=sizeof(v);

	s=sizeof(*v) + sizeof(*v->c) * cards;
	as = alignment * ((s + alignment-1)/alignment);

	g->lastused = g->used;
	g->used += as;
	if (g->used >= g->maxmem) {
		/* throw away cache and start again */
		int i;
		analysis(g);
		g->used=as;
		g->lastused=0;
		for(i=0;i<HASHTABSIZE;i++)
		  g->tabtab[i]=NULL;
		printf("reset hash cache\n");
	}
	v=(void*)(g->memblock + g->used);
	v->cards=cards;
	return v;
}

static void vtunalloc(const tableau *t) {
	/* deallocate previous alloc */
	global *g=t->global;
	g->used=g->lastused;
}

#if 0
static void showvistab(const vistab *v) {
	printf("vistab @ %p has %d cards\n",v,v->cards);
	for(int i=0;i<v->cards;i++)
		printf("%s ",cardtotext(v->c[i]));
	printf("\n");
}
#endif

static vistab *newvistab(const tableau *t,unsigned int *hash) {
	vistab *v;
	card *c;
	int n=0,i,j;

	for(i=0;i<STACKS;i++) {
		n += 1 + t->s[i].n - t->s[i].hidden;
	}
	v=vtalloc(t->global,n);
	if (!v) err(t,"out of memory\n");

	*hash=0;
	c=v->c;
	for(i=0;i<STACKS;i++) {
		const stack *s=&t->s[i];
		for(j=s->hidden;j<s->n;j++) {
			card x = s->c[j];
			*c++ = x;
			*hash += (x<<i) + x*(c - v->c) + 19*j;
		}
		*c++ = -1;
		*hash += 53 * (c - v->c);
	}
	return v;
}

static bool matchvistab(const vistab *v1,const vistab *v2) {
	if (v2->cards != v1->cards) return false;
	if (memcmp(v2->c,v1->c,v1->cards * sizeof(*v1->c))!=0) return false;
	return true;
}

static bool findorinserttableau(const tableau *t) {
	vistab *v;
	const vistab *hv;
	unsigned int hash;
	unsigned int h;
	global *g=t->global;

	v=newvistab(t,&hash);
	h=hash % HASHTABSIZE;
	/* find? */
	for(hv=g->tabtab[h];hv;hv=hv->nexthash) {
		if (matchvistab(hv,v)) {
			g->matched++;
			vtunalloc(t);
			return true;
		}
	}

	/* no... insert */
	g->unmatched++;
	v->nexthash=g->tabtab[h];
	g->tabtab[h]=v;
	return false;
}

static void playforward(const tableau *t1,void (*func)(const tableau *,void *,int),void *context) {
	int depth,d;
	const tableau *t;

	depth=t1->depth;
	for(d=0; d<=depth; d++) {
		for(t=t1; t && t->depth > d; t=t->prev) {}
		func(t,context,d);
	}
}

static void printdest(const tableau *t,void *vf,int d) {
	FILE *f=vf;
	if (t->last.dest==-1) fprintf(f,"D");
	else fprintf(f,"%d",t->last.dest);
}

static void showstatus(const tableau *t1) {
	/* update */
	printf("Plays: %llu\n",t1->global->playscount);
	printf("Route: ");
	playforward(t1,printdest,stdout);
	printf("\n");
}

static void printmove(const tableau *t,void *vf,int d) {
	FILE *f=vf;
	if (d==0) return;
	d--;
	fprintf(f,"{%d,%d,%d},",t->last.dest,t->last.src,t->last.n);
	if (d%10==9) fprintf(f,"\n");
}

static void showcheckpoint(const tableau *t1,bool record) {
	/* checkpoint */
	char filename[16];
	static int count=0;
	FILE *f;

	if (record) {
		snprintf(filename,sizeof(filename),"spidercheck-%d.h",count++);
		if (count>=4) count=0;
		f=fopen(filename,"w");
		if (!f) f=stdout;
		printf("checkpointing...\n");
		fprintf(f,"/*\n");
		showrewind(t1,f);
	}
	else
		f=stdout;

	fprintf(f,"Checkpoint at %llu plays, depth=%d\n",t1->global->playscount,t1->depth);
	fprintf(f,"*/\n");
	fprintf(f,"#define CHECKPOINT checkpoint\n");
	fprintf(f,"static move checkpoint[]={ {-1,-1,-1}, /* initial deal */\n");
	playforward(t1,printmove,f);
	fprintf(f,"};\n");
	if (f!=stdout) fclose(f);
}

static bool wins(const tableau *t1) {
	tableau newtableau,*t2=&newtableau;
	stack *src,*dst;
	int i,j,n;
	#define copyt(T2,T1) do{*(T2)=*(T1); (T2)->prev=(T1); (T2)->depth++;} while(0)

	if (t1->global->seeking) {
		int d=t1->depth;
		if (d >= lengthof(checkpoint)) {
			t1->global->seeking=false;
			return false;
		}
		showcheckpoint(t1,false);
		if (t1->last.dest != checkpoint[d].dest)
			return false;
		if (t1->last.src != checkpoint[d].src)
			return false;
		if (t1->last.n != checkpoint[d].n)
			return false;
	}
	else if (findorinserttableau(t1)) {
		return false;
	}
	if (t1->depth > t1->global->maxdepth) return false;

	if (t1->home.n >= 104) /* wins! */
		return true;

	t1->global->playscount++;
	if ((t1->global->playscount & (1024*1024-1))==0) showstatus(t1);
	if ((t1->global->playscount & (1024*1024*4-1))==0) showcheckpoint(t1,true);

	copyt(t2,t1);
	t2->last.src=-1;
	t2->last.dest=-1;
	t2->last.n=-1;

	/* deal */
	if (t2->deck.n >= STACKS) {
		for(i=0;i<STACKS;i++) {
			dst=&t2->s[i];
			if (!movecards(t2,dst,&t2->deck,1,true)) break;
		}
		if (wins(t2)) {
			return true;
		}
	}
	/* didn't win from that deal... reset */
	copyt(t2,t1);

	/* try a move */
	for(i=0;i<STACKS;i++) /* dest stack */ {

		/* progress update */
		if (false && t1->depth < 6) {
			const tableau *t;
			printf("depth %.*s%d stack %d",t1->depth,"         ",t1->depth,i);
			for(t=t1; t->prev; t=t->prev) {
				if (t->last.dest==-1) printf(", deal");
				else printf(", %d",t->last.dest);
			}
			printf("\n");
			showtableau(t1,NULL);
		}

		t2->last.dest=i;

		for(j=0;j<STACKS;j++) /* src stack */ {
			if (i==j) continue;
			dst=&t2->s[i];
			src=&t2->s[j];
			t2->last.src=j;
			for(n = src->pickable;n>0;n--) {
				t2->last.n=n;

				if (t1->last.dest == t2->last.src && t1->last.n == t2->last.n)
				  continue;

				if (movecards(t2,dst,src,n,false)) {
					if (wins(t2)) {
						return true;
					}
					/* that move didn't win... reset */
					t2->s[i]=t1->s[i];
					t2->s[j]=t1->s[j];
					t2->home.n=t1->home.n;
					//copyt(t2,t1);
				}
			}
		}
	}
	return false;
}

static void initstack(stack *s) {
	s->n = s->hidden = 0;
}

static void fillstack(stack *s,const char *from) {
	/* fill stack backwards because it makes layout easier. */
	const char *p;
	s->n=0;
	for(p=from + strlen(from) - 2;p >= from;p-=2) {
		s->c[s->n++] = cardfromtext(p);
	}
	s->pickable=1;
	s->hidden=s->n;
	//showstack(s);
}

#if 0
static void shuffle(stack *s) {
	int i,j;
	card c;
	for(i=0;i < s->n; i++) {
		j=rand() % s->n; /* not very */
		c = s->c[j];
		s->c[j] = s->c[i];
		s->c[i] = c;
	}
}
#endif

int main(void) {
	global mainglobal,*g=&mainglobal;
	tableau initialtableau,*t=&initialtableau;
	int i;

	memset(g,0,sizeof(*g));
	memset(t,0,sizeof(*t));
	g->seeking=seeking;
	g->first=t;
	g->maxmem=4000000000;
	g->maxdepth=150;
	g->memblock=malloc(g->maxmem);
	if (!g) err(NULL,"cannot malloc\n");

	#if 1
	const char *layout=
		"----JHTS--5H--4H--QS"
		"--JS4HQS9SQH--KH7S7H"
		"--8SJS2HKS3H--8HTH8S"
		"--JH3S4S8S5H--5S2S5S"
		"--5HAS7H2H9H2SQH4S5H"
		"KS6H9S7H"
		"AH6H3HAS3STSKS6H8HAH"
		"6STHAS9HKH8S3H4SAHAH"
		"6SKSQH9H2STS8H7S7STH"
		"AS2H6SQS7H9S2S9HKH4H"
		"6S4H3S4SJH7STHJS3H6H";
		/* unknown 2H 8H JH QH KH 3S 5S 5S 9S TS JS QS */
	fillstack(&t->deck,layout);
	#else
        const char *pack=
		"AH2H3H4H5H6H7H8H9HTHJHQHKH"
		"AH2H3H4H5H6H7H8H9HTHJHQHKH"
		"AH2H3H4H5H6H7H8H9HTHJHQHKH"
		"AH2H3H4H5H6H7H8H9HTHJHQHKH"
		"AS2S3S4S5S6S7S8S9STSJSQSKS"
		"AS2S3S4S5S6S7S8S9STSJSQSKS"
		"AS2S3S4S5S6S7S8S9STSJSQSKS"
		"AS2S3S4S5S6S7S8S9STSJSQSKS";
	fillstack(&t->deck,pack);
	shuffle(&t->deck);
	#endif

	for(i=0;i<STACKS;i++)
		initstack(&t->s[i]);
	initstack(&t->home);
	t->prev=NULL;
	t->depth=0;
	t->global=g;
	t->last.src = t->last.dest = t->last.n = -1;

	/* deal initial tableau */
	err(NULL,NULL);
	for(i=0;i<54;i++) {
		stack *s=&t->s[i % STACKS];
		movecards(t,s,&t->deck,1,true);
		s->hidden = s->n;
	}
	err(NULL,NULL);
	for(i=0;i<STACKS;i++) {
		stack *s=&t->s[i];
		s->hidden = s->n - 1;
		s->pickable = pickable(s);
	}
	printf("Dealt tableau\n");
	showtableau(t,NULL);

	/* play */
	if (wins(t)) printf("Yay!\n");
	else printf("Boo!\n");

	analysis(g);
	free(g->memblock);
	return 0;
}
