/* > minmax.c */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

typedef unsigned int uint;

typedef struct {
	double minmax[2];
	uint inited:1;
} data;
const data data_reset={{0.0,0.0},0};

typedef struct {
	int columns;
	data *data;
} results;

static const char *progname="minmax";

static const char *process(results *r,int column,double value) {
	data *d;
	if (r->columns < column+1) {
		r->data=realloc(r->data,(column+1)*sizeof(*r->data));
		if (!r->data) return "nomem";
		for(;r->columns < column+1;r->columns++)
			r->data[r->columns]=data_reset;
	}
	d=&r->data[column];
	if (!d->inited) {
		d->minmax[0] = d->minmax[1] = value;
		d->inited=1;
	}
	else {
		if (value < d->minmax[0]) d->minmax[0]=value;
		if (value > d->minmax[1]) d->minmax[1]=value;
	}
	return NULL;
}

static void eatline(FILE *in) {
	int c;
	do {
		c=getc(in);
		if (c==EOF) return;
	} while(c!='\n');
}

static int eatspace(FILE *in,int sol) {
	int nl=0;
	int c;
	do {
		c=getc(in);
		if (c==EOF) break;
		if (c=='\n') nl=1;
		if (c=='#' && sol) {eatline(in); c='\n'; continue;}
		sol=0;
	} while(isspace(c));
	if (c!=EOF) ungetc(c,in);
	return nl;
}

static void getword(FILE *in,char *w,size_t mw) {
	int c;
	if (mw<1) return;
	do {
		c=getc(in);
		if (c==EOF) break;
		if (mw>0) {*(w++)=c; mw--;}
	} while(!isspace(c));
	if (c!=EOF) {ungetc(c,in); w--;}
	*w='\0';
}

static void fout(FILE *out,uint *first,uint known,double value) {
	char *space=" ";
	if (*first) {space=""; *first=0;}
	if (!known) {fprintf(out,"%s?",space); return;}
	fprintf(out,"%s%f",space,value);
}
	

static void printresults(FILE *out,results *r,uint flags) {
	int column;
	data *d;
	uint first=1;
	for(column=0;column < r->columns;column++) {
		d=&r->data[column];
		if (flags & 1) fout(out,&first,d->inited,d->minmax[0]);
		if (flags & 2) fout(out,&first,d->inited,d->minmax[1]);
	}
	printf("\n");
}

static int syntax(int retval) {
	fprintf(stderr,"Syntax: %s [-s|-r]\n",progname);
	return 1;
}

int main(int argc,char *argv[]) {
	results res={0,NULL};
	int column=0;
	FILE *in=stdin;
	double value;
	char word[128],*end;
	enum {cols,rows} format=rows;
	
	progname=*argv++; argc--;
	for(;argc>0;argv++,argc--) {
		if ((*argv)[0]!='-') return syntax(1);
		switch((*argv)[1]) {
		case 's': format=cols; break;
		case 'r': format=rows; break;
		default: return syntax(1);
		}
	}
	
	while(!feof(in)) {
		if (eatspace(in,column==0)) column=0;
		getword(in,word,sizeof(word));
		value=strtod(word,&end);
		if (end!=word) process(&res,column,value);
		column++;
	}

	switch(format) {
	case rows:
		/* print min row then max row with header column */
		printf("#min ");
		printresults(stdout,&res,1);
		printf("#max ");
		printresults(stdout,&res,2);
		break;
	case cols:
		/* double up columns with min the max */
		printresults(stdout,&res,3);
		break;
	}

	return 0;	
}
