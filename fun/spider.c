#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#define lengthof(X) (sizeof(X)/sizeof(*(X)))
#define STACKS 10

typedef int card;
typedef unsigned int hash;
typedef enum {CALC=0,ADD,SUB} hashop;

typedef struct {
	int n;
	int hidden;
	int pickable;
	card c[104];
} stack;

struct tableau_s;
typedef struct tableau_s tableau;
struct tableau_s {
	stack deck;
	stack s[STACKS];
	stack home[8];
	int nhome;
	hash hash;
	const tableau *prev;
	int depth;
};

static const char facename[]="A23456789TJQK";
static const char suitname[]="CDHS";

#define face(C) ((C) % 13)
#define suit(C) ((C) / 13)
#define topcard(S) ((S)->c[(S)->n - 1])
#define won(T) ((T)->nhome >= lengthof((T)->home))

static int stacknum(const tableau *t,const stack *s) {
	ptrdiff_t n;
	n = s - &t->deck;
	if (n==0) return 0;
	n = s - t->s;
	if (n>=0 && n<STACKS) return n+1;
	n = s - t->home;
	if (n>=0 && n<8) return STACKS+n+1;
	return -1;
}	

static const char *cardtotext(card c) {
	static char n[3];
	if (c==-1 || c>=52) {
		n[0]=n[1]='X';
		n[2]='\0';
		return n;
	}
	n[0]=facename[face(c)];
	n[1]=suitname[suit(c)];
	n[2]='\0';
	return n;
}

static const char *stackcardtotext(const stack *s,int i) {
	static char n[5];
	const char *c;
	
	if (i==-1) {
		snprintf(n,sizeof(n),"%d/%d",s->pickable,s->hidden);
		return n;
	}
	if (i >= s->n) return "    ";
	c=cardtotext(s->c[i]);
	n[1]=c[0];
	n[2]=c[1];
	n[4]='\0';
	if (i < s->hidden) n[0]='(',n[3]=')';
	else if (i < s->n - s->pickable) n[0]='<',n[3]='>';
	else n[0]=n[3]=' ';
	return n;
}

static void showtableau(const tableau *t) {
	int i,j,n;
	const stack *s;
	if (!t) {
		printf("No tableau\n");
		return;
	}
	for(i=0;i<104;i++) {
		for(n=j=0;j<STACKS;j++) {
			s=&t->s[j];
			if (i < s->n) n++;
			printf("%-4s ",stackcardtotext(s,i));
		}
		switch(i) {
		case 0: printf("    Home filled: %d\n",t->nhome); break;
		case 1: printf("    Cards in deck: %d\n",t->deck.n); break;
		case 2: printf("    Hash: %u\n",t->hash); break;
		case 3: printf("    Depth: %d\n",t->depth); break;
		default: printf("\n"); break;
		}
		if (n<1) break;
	}
	for(j=0;j<STACKS;j++) {
		s=&t->s[j];
		printf("%-4s ",stackcardtotext(s,-1));
	}
	printf("\n");
}

static void showstack(const stack *s) {
	int i;
	for(i=0;i<s->n;i++)
		printf("%s",stackcardtotext(s,i));
	printf("\n");
}

static void err(const tableau *t,const char *s) {
	static bool disabled=false;
	const tableau *tc;
	if (!s) {
		disabled=!disabled;
		return;
	}
	if (disabled) return;
	printf("Rewind...\n");
	for(tc=t;tc;tc=tc->prev)
		showtableau(tc);
	printf("\n\n\n%s",s);
	showtableau(t);
	exit(1);
}

static hash calchash(hashop op,const tableau *t,const stack *s,int n,card c,hash hash) {
	bool add=true;
	int i;
//printf("hash %d %u",op,hash);
	switch(op) {
	case CALC:
		hash=0;
		for(i=0;i<STACKS;i++) {
			s = &t->s[i];
			for(n = s->hidden;n < s->n; n++) {
				c = s->c[n];
				hash=calchash(ADD,t,s,n,c,hash);
			}
		}
		break;
	case SUB:
		add=false;
		/* fall through */
	case ADD:
		i=stacknum(t,s);
		if (i<=0) break;
		if (!add) i=-i;
//printf(" stack=%d n=%d card=%d delta=%d ",i,n,c,i*n*c);
//showstack(s);
		if (c == -1) break;
		hash += i * n * c;
		break;
	}
//printf(" -> %u\n",hash);
	return hash;
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
	
	visible = s->n - s->hidden;
	if (visible <= 1) return 1;
	c=topcard(s);
	for(p=1;;p++) {
		d = s->c[s->n - p - 1];
		if (d==-1 || c==-1) break;
//printf("check visible=%d low=%s ",visible,cardtotext(c));
//printf("above=%s\n",cardtotext(d));
		if (suit(c) != suit(d)) break;
		if (face(c)+1 != face(d)) break;
		if (p >= visible) break;
		c=d;
	}
//printf("pickable %d\n",p);
	return p;
}

static bool movecards(tableau *t,stack *dst,stack *src,int n,bool force) {
	int i;
	card s,d;
	hash newhash;

	/* check valid */
	if (!force) {
		if (n > src->pickable) err(t,"bad movecards\n");
		if (dst->n > 0 && dst->hidden < dst->n) {
			d=topcard(dst);
			if (d==-1) err(t,"bad top\n");
			s=src->c[src->n - n];
			if (s==-1) err(t,"bad src\n");
			if (face(d) != face(s)+1) {
				return false;
			}
		}
	}

	/* calc new hash */
	newhash=t->hash;
	for(i=0;i<n;i++) {
		int from = src->n - n + i;
		int to = dst->n + i;
		s = src->c[from];
		newhash=calchash(SUB,t,src,from,s,newhash);
		newhash=calchash(ADD,t,dst,to,s,newhash);
	}
	if (!force) {
		const tableau *tc;
//printf("compare current %u with...\n",newhash);
		for(tc=t->prev; tc; tc = tc->prev) {
//printf("   %u\n",tc->hash);
			if (newhash == tc->hash) {
				/* todo full check */
//printf("repetition\n");
				return false;
			}
		}
//printf("dest covers %s (%zd) ",cardtotext(d),dst-t->s);
//printf("with %s (%d from %zd)? ",cardtotext(s),n,src-t->s);
//printf("yes\n");
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
	src->n-=n;
	t->hash = newhash;

	/* turnover? */
	if (src->hidden >= src->n && src->n > 0) {
		src->hidden = src->n - 1;
		t->hash = calchash(ADD,t,src,src->hidden,src->c[src->hidden],t->hash);
	}
//if (calchash(CALC,t,NULL,0,-1,0)!=t->hash)
//  err(t,"hash error\n");

	/* recalc */
	src->pickable=pickable(src);
	dst->pickable=pickable(dst);
	if (dst->pickable==13) {
		movecards(t,&t->home[t->nhome++],dst,13,true);
		err(t,"got a stack!\n");
	}
	return true;
}

static bool wins(const tableau *t1) {
	tableau newtab,*t2=&newtab;
	stack *src,*dst;
	int i,j,n;
	#define copyt(T2,T1) do{*(T2)=*(T1); (T2)->prev=(T1); (T2)->depth++;} while(0)
static int deepest=0;

//showtableau(t1);
	if (won(t1))
		return true;
	copyt(t2,t1);
if (t2->depth > deepest) {
  showtableau(t2);
  deepest=t2->depth;
}

	/* deal */
//printf("deal from %d remaining at depth %d...\n",t2->deck.n,t2->depth);
	if (t2->deck.n >= STACKS) {
		for(i=0;i<STACKS;i++) {
			dst=&t2->s[i];
			if (!movecards(t2,dst,&t2->deck,1,true)) break;
		}
		if (wins(t2))
			return true;
	}
	/* didn't win from that deal... reset */
	copyt(t2,t1);

	/* try a move */
	for(i=0;i<STACKS;i++) {
		for(j=0;j<STACKS;j++) {
			if (i==j) continue;
			src=&t2->s[i];
			dst=&t2->s[j];
			for(n=1;n <= src->pickable;n++) {
				if (movecards(t2,dst,src,n,false)) {
					if (wins(t2)) {
						return true;
					}
					/* that move didn't win... reset */
					copyt(t2,t1);
				}
			}
		}
	}
//showtableau(&t2);
	return false;
}

static void initstack(stack *s) {
	s->n = s->hidden = 0;
}

int main(void) {
	tableau t;
	const char *layout=
		"xxXXxxXXxxXXxxXXxxXX"
		"xxJSxxXXxxXXxxXXxxXX"
		"xx8SxxXXxxXXxxXXxxXX"
		"xxJHxxXXxxXXxx5S2SXX"
		"xx5HAS7H2H9H2SQH4S5H"
		"KS6H9S7H"
		"AH6H3HAS3STSKS6H8HAH"
		"6STHAS9HKH8S3H4SAHAH"
		"6SKSQH9H2STS8H7S7STH"
		"AS2H6SQS7H9S2S9HKH4H"
		"6S4H3S4SJH7STHJS3H6H";
	const char *p;
	stack *deck=&t.deck;
	int i;
	
	initstack(&t.deck);
	for(i=0;i<STACKS;i++)
		initstack(&t.s[i]);
	for(i=0;i<lengthof(t.home);i++)
		initstack(&t.home[i]);
	t.nhome=0;
	t.hash=0;
	t.prev=NULL;
	t.depth=0;

	/* set up deck */
	deck->n=0;
	for(p=layout + strlen(layout) - 2;p >= layout;p-=2) {
		card c;
		c=cardfromtext(p);
		t.deck.c[t.deck.n++] = c;
	}
	deck->pickable=1;
	deck->hidden=deck->n;
	//showstack(deck);
	
	/* deal initial tableau */
	err(NULL,NULL);
	for(i=0;i<54;i++) {
		stack *s=&t.s[i % STACKS];
		movecards(&t,s,deck,1,true);
		s->hidden = s->n;
	}
	err(NULL,NULL);
	for(i=0;i<STACKS;i++) {
		stack *s=&t.s[i];
		s->hidden = s->n - 1;
		s->pickable = pickable(s);
	}
	t.hash=calchash(CALC,&t,NULL,0,-1,0);
	showtableau(&t);

	/* play */
	if (wins(&t)) printf("Yay!\n");
	else printf("Boo!\n");
	return 0;
}
