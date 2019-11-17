/* > json.c */
/* (C) Daniel F. Smith, 2019 */
/* Stack-based JSON parser following http://www.json.org charts. */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "json.h"

typedef struct {
        json_valuecontext root; /* must be first in structure; see getsuperlement() */
        json_callbacks callbacks;
        int errcount;
} superelement;

typedef json_valuecontext ctx;

static json_in got_value(ctx *,json_in);

/* -- utility -- */

#define GETTEXT(X) X /* text that might be shown to user */

static json_in startswith(const char *match,json_in p) {
        size_t n=strlen(match);
        if (strncmp(p,match,n)==0) return p+n;
        return NULL;
}

static bool match_nchar(const char *match,const json_nchar *s) {
        size_t n=strlen(match);
        if (s->s==NULL && s->n==0 && match==NULL) return true;
        if (!s->s || !match) return false;
        if (s->n!=n) return false;
        if (strncmp(s->s,match,n)!=0) return false;
        return true;
}

static superelement *getsuperelement(const ctx *c) {
        while(c && c->prev) c=c->prev;
        return (superelement*)c;
}

static json_in not_thing(ctx *c,const char *thing,json_in s,json_in p,const char *msg) {
        /* report invalid type of thing */
        superelement *super=getsuperelement(c);
        if (!super) {printf("remove me %s:%s\n",thing,msg); return NULL;}
        const json_callbacks *cb=&super->callbacks;
        super->errcount++;
        if (super->errcount <= 1)
                cb->error(&super->root,thing,s,p,msg,cb->context);
        return NULL;
}

/* -- default callbacks -- */

static void default_got_value(const json_valuecontext *base,const json_value *v,void *context) {
        json_printpath(base);
        printf(" = ");
        json_printvalue(v);
        printf("\n");
}

static void default_error(const json_valuecontext *c,const char *dtype,json_in s,json_in p,const char *msg,void *context) {
        printf("%s %s (%s):\n",GETTEXT("bad"),dtype,msg);
        superelement *super=getsuperelement(c);
        if (!super) {printf("%s\n",GETTEXT("error reporting failed")); return;}
        json_in q;
        /* highlight error */
        for(q=super->root.value.object;*q;q++)
                printf("%s%s%c%s",
                        (q==s)?"!!!":"" /* highlight element */,
                        (q==p)?"<<<":"" /* highlight character in element */,
                        *q,
                        (q==p)?">>>":"");
        printf("\n");
}

/* -- parser -- */

static json_in eat_whitespace(json_in p) {
        if (!p) return p;
        for(;*p;p++) {
                switch(*p) {
                case ' ':
                case '\n':
                case '\r':
                case '\t':
                        continue;
                }
                break;
        }
        return p;
}

static json_in eat_hex(json_in p,int max) {
        json_in s;
        for(s=p;*s;s++) {
                if (max>0 && s-p>=max) break;
                switch(*s) {
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                case 'A': case 'a':
                case 'B': case 'b':
                case 'C': case 'c':
                case 'D': case 'd':
                case 'E': case 'e':
                case 'F': case 'f':
                        continue;
                default:
                        return s;
                }
        }
        return s;
}

static json_in eat_string(ctx *c,json_in s,json_nchar *str) {
        json_in p,q;
        const char *err=NULL;
        if (*s!='\"') return NULL;
        c->value.type=json_type_string;
        str->s=s+1;
        for(p=str->s;*p && !err;p++) {
                if (*p=='\"') {str->n = p - str->s; return p+1;}
                if (*p!='\\') continue;

                /* control characters */
                p++;
                switch(*p) {
                case '\\':
                case '/':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                        p++;
                        break;
                case 'u':
                        q=eat_hex(p,4);
                        if (q-p!=4) {err=GETTEXT("bad unicode sequence"); break;}
                        p=q;
                        break;
                case '\0':
                default:
                        err=GETTEXT("bad control character");
                        break;
                }
        }
        if (!err) err=GETTEXT("no closing quote");
        return not_thing(c,GETTEXT("string"),s,p,err);
}

static json_in eat_digits(json_in s,double *v,int *digits) {
        json_in p;
        for(p=s;*p>='0' && *p<='9';p++) {
                *v=(*v * 10) + (*p-'0');
                (*digits)++;
        }
        return p;
}

static json_in eat_number(ctx *c,json_in s) {
        json_in p;
        bool neg=false;
        int d=0;
        p=s;
        c->value.number=0.0;
        if (*p=='-') {p++; neg=true;}
        if (*p=='0') p++;
        else {
                p=eat_digits(p,&c->value.number,&d);
                if (d==0) return NULL;
        }

        if (*p=='.') {
                int d=0;
                double f=0;
                p++;
                p=eat_digits(p,&f,&d);
                f*=pow(0.1,d);
                c->value.number+=f;
        }

        if (*p=='e' || *p=='E') {
                double pos=0,neg=0,*exp;
                int d=0;
                p++;
                if (*p=='-') {p++; exp=&neg;}
                else if (*p=='+') {p++; exp=&pos;}
                else exp=&pos;
                p=eat_digits(p,exp,&d);
                if (d==0) return not_thing(c,GETTEXT("number"),s,p,GETTEXT("bad exponent"));
                c->value.number *= pow(10,pos-neg);
        }

        if (neg) c->value.number = -c->value.number;
        c->value.type=json_type_number;
        return p;
}

static json_in eat_bool(ctx *c,json_in p) {
        json_in q;
        do {
                q=startswith("true",p);
                if (q) {c->value.truefalse=true; break;}
                q=startswith("false",p);
                if (q) {c->value.truefalse=false; break;}
                return NULL;
        } while(0);
        c->value.type=json_type_bool;
        return q;
}

static json_in eat_null(ctx *c,json_in p) {
        json_in q;
        q=startswith("null",p);
        if (q) c->value.type=json_type_null;
        return q;
}

static json_in get_value(ctx *c,json_in s) {
        json_in p,q;

        p=eat_whitespace(s);

        if (*p=='{') {
                c->value.type=json_type_object;
                c->value.object=p;
                return p;
        }

        if (*p=='[') {
                c->value.type=json_type_array;
                c->value.array=p;
                return p;
        }

        q=eat_string(c,p,&c->value.string);
        if (q) return eat_whitespace(q);

        q=eat_number(c,p);
        if (q) return eat_whitespace(q);

        q=eat_bool(c,p);
        if (q) return eat_whitespace(q);

        q=eat_null(c,p);
        if (q) return eat_whitespace(q);

        return not_thing(c,GETTEXT("value"),s,p,GETTEXT("invalid value"));
}

static json_in eat_array(ctx *vc,json_in s) {
        json_in p=s;
        ctx c={};
        const char *err=NULL;

        if (*p!='[') return NULL;
        p++;
        c.prev=vc;
        c.name.s=NULL;
        c.name.n=0;
        p=eat_whitespace(p);
        if (*p==']') return p+1;
        if (vc) vc->next=&c;
        for(c.index=0;;c.index++) {
                p=get_value(&c,p);
                if (!p) {err=GETTEXT("bad value"); break;}
                p=got_value(&c,p);
                if (*p==']') break;
                if (*p!=',') {err=GETTEXT("comma or bracket missing"); break;}
                p++;
        }
        if (err) return not_thing(&c,GETTEXT("array"),s,p,err);
        if (vc) vc->next=NULL;
        return p+1;
}

static json_in eat_object(ctx *vc,json_in s) {
        json_in p=s,q;
        ctx c={};
        const char *err=NULL;

        if (*p!='{') return NULL;
        p++;
        c.prev=vc;
        c.index=0;
        p=eat_whitespace(p);
        if (*p=='}') return p+1;
        if (vc) vc->next=&c;
        for(;;) {
                if (!*p) {err=GETTEXT("closure missing"); break;}
                q=eat_string(&c,p,&c.name);
                if (!q) {err=GETTEXT("bad name"); break;}
                p=eat_whitespace(q);
                if (*p!=':') {err=GETTEXT("colon missing"); break;}
                p++;
                q=get_value(&c,p);
                if (!q) {err=GETTEXT("bad value"); break;}
                p=got_value(&c,q);
                if (!p) {err=GETTEXT("bad object value"); break;}
                if (*p=='}') break;

                if (*p!=',') {err=GETTEXT("comma or brace missing"); break;}
                p++;
                p=eat_whitespace(p);
        }
        if (err) return not_thing(&c,GETTEXT("object"),s,p,err);
        if (vc) vc->next=NULL;
        return p+1;
}

static json_in got_value(ctx *c,json_in s) {
        switch(c->value.type) {
        case json_type_object: return eat_whitespace(eat_object(c,s));
        case json_type_array: return eat_whitespace(eat_array(c,s));
        default: break;
        }

        superelement *super=getsuperelement(c);
        const json_callbacks *cb=&super->callbacks;
        cb->got_value(&super->root,&c->value,cb->context);
        return s;
}

const char *json_parse(const json_callbacks *ucb,const char *s) {
        superelement super={};
        if (ucb) super.callbacks=*ucb;
        if (!super.callbacks.got_value) super.callbacks.got_value=default_got_value;
        if (!super.callbacks.error)     super.callbacks.error=default_error;
        super.root.name.s="";
        super.root.name.n=0;

        json_in p;
        const char *err=NULL;
        ctx *c=&super.root;
        do {
                p=get_value(c,s);
                if (!p) {err=GETTEXT("bad string"); break;}
                p=got_value(c,p);
                if (!p) {err=GETTEXT("cannot parse string"); break;}
        } while(0);
        if (err) return not_thing(c,GETTEXT("JSON"),p,p,err);
        return p;
}

/* -- auxiliary functions -- */

const json_valuecontext *json_printpath(const json_valuecontext *c) {
        const json_valuecontext *d,*f;
        superelement *super;

        super=getsuperelement(c);
        if (!super) {printf("<%s>\n",GETTEXT("invalid tree")); return NULL;}
        /* root is an unnamed object, so don't print that */
        for(f=d=super->root.next;d;d=d->next) {
                f=d;
                if (d->name.s) printf("[\"%.*s\"]",d->name.n,d->name.s);
                else printf("[%d]",d->index);
        }
        return f; /* return final element value context */
}

void json_printvalue(const json_value *v) {
        switch(v->type) {
        case json_type_null:
                printf("%s",GETTEXT("null"));
                break;
        case json_type_bool:
                printf("%s",(v->truefalse)?GETTEXT("true"):GETTEXT("false"));
                break;
        case json_type_string:
                printf("\"%.*s\"",v->string.n,v->string.s);
                break;
        case json_type_number:
                printf("%g",v->number);
                break;
        default:
                printf("<%s %d>",GETTEXT("bad type"),v->type);
                break;
        }
}

bool json_matches_name(const json_valuecontext *c,const char *name) {
        if (!c) return false;
        if (!c->name.s) return false;
        return match_nchar(name,&c->name);
}

bool json_matches_index(const json_valuecontext *c,int index) {
        if (!c) return false;
        if (c->name.s) return false;
        if (c->index!=index) return false;
        return true;
}

bool json_matches_path(const json_valuecontext *c,...) {
        superelement *super=getsuperelement(c);
        bool result=false;
        if (!super) return false;

        va_list ap;
        va_start(ap,c);
        for(c=super->root.next;;c=c->next) {
                const char *name;

                name=va_arg(ap,const char *);
                if (name==NULL && c==NULL) {result=true; break;}
                if (!c) break;
                if (strcmp(name,"*")==0) continue; /* match any */
                if (name[0]=='#') {
                        int index;
                        char *end;
                        index=strtol(name+1,&end,0);
                        if (end==name+1 || *end!='\0') break; /* bad index */
                        if (!json_matches_index(c,index)) break;
                }
                else {
                        if (!json_matches_name(c,name)) break;
                }
        }
        va_end(ap);
        return result;
}

/* -- tests and examples -- */

#if 0

/* Minimal example */
#include "json.h"
int main(void) {
        json_parse(NULL,"{\"hello\":\"world\"}");
        return 0;
}

#elif 0

/* Pick off the value ["johnny"][5] */
#include "json.h"
static void condition(const json_valuecontext *root,const json_value *v,void *context) {
        if (json_matches_path(root,"johnny","#5",NULL)) {
                json_printpath(root);
                printf(" is ");
                json_printvalue(v);
                printf("\n");
        }
}

int main(void) {
        json_callbacks cb={.got_value=condition};
        const char *json="{\
                \"johnny\":[\n\
                        \"broken\",\n\
                        \"in pieces\",\n\
                        \"behind shed\",\n\
                        \"upside down\",\n\
                        \"watching tv\",\n\
                        \"alive\",\n\
                        \"passed out\"]\
        }";
        return json_parse(&cb,json)!=NULL;
}

#elif 0

/* test cases */
int main(void) {
        struct {
                bool good;
                char *s;
        } t[]={
                /* simple examples */
                {true,"{}"},
                {true,"{\"hello\":\"there\"}"},
                {true,"[1]"},
                {true,"[1,4.3,9e10]"},
                {true,"{\"list\":[10,11,\"hi\",-3e-10]}"},
                /* real-world example */
                {true,"{\n\
                        \"glossary\": {\n\
                                \"title\": \"example glossary\",\n\
                                \"GlossDiv\": {\n\
                                        \"title\": \"S\",\n\
                                        \"GlossList\": {\n\
                                                \"GlossEntry\": {\n\
                                                        \"ID\": \"SGML\",\n\
                                                        \"SortAs\": \"SGML\",\n\
                                                        \"GlossTerm\": \"Standard Generalized Markup Language\",\n\
                                                        \"Acronym\": \"SGML\",\n\
                                                        \"Abbrev\": \"ISO 8879:1986\",\n\
                                                        \"GlossDef\": {\n\
                                                                \"para\": \"A meta-markup language.\",\n\
                                                                \"GlossSeeAlso\": [\"GML\", \"XML\"]\n\
                                                        },\n\
                                                        \"GlossSee\": \"markup\"\n\
                                                }\n\
                                        }\n\
                                }\n\
                        }\n\
                }"},
                {true,"[0,0.,1e1,1,2,-1,-2,0.0023,-0.0025,1e9,1.0023e9,-123.456e-78]"},
                /* error examples */
                {false,"{hello:3}"},
                {false,"[1,2,3,]"},
        };
        int slen=sizeof(t)/sizeof(*t);
        int i;
        int goodc=0,badc=0;

        for(i=0;i<slen;i++) {
                char *s=t[i].s;
                bool good=t[i].good;
                const char *p;
                const char *pf;

                printf("--------------\n");
                p=json_parse(NULL,s);
                if (p && *p!='\0')
                        p=not_thing(NULL,GETTEXT("rootobject"),s,p,GETTEXT("trailing data"));

                printf("%s -> ",s);
                if (good == !!p) {
                        goodc++; pf=GETTEXT("PASS");
                }
                else {
                        badc++; pf=GETTEXT("FAIL");
                }
                if (p) printf("%zu/%zu (%s)\n",p-s,strlen(s),pf);
                else printf("null (%s)\n",pf);
        }
        printf(GETTEXT("Results:\n"));
        printf(GETTEXT("Content test: check output by eye\n"));
        printf(GETTEXT("Structure parse test: good=%d bad=%d\n"),goodc,badc);
        printf("*** %s ***\n",(badc==0)?GETTEXT("PASS"):GETTEXT("FAIL"));
        return (badc==0)?0:1;
}
#endif
