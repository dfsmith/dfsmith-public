/* > json.c */
/* Daniel F. Smith, 2019 */
/* Stack-based JSON parser following http://www.json.org charts. */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "json.h"

#define DBG(X) X

typedef json_valuecontext ctx;

static json_in got_value(ctx *,json_in);

static json_in startswith(const char *match,json_in p) {
        size_t n=strlen(match);
        if (strncmp(p,match,n)==0) return p+n;
        return NULL;
}

static bool cmp_nchar(const char *match,const json_nchar *s) {
        size_t n=strlen(match);
        if (s->s==NULL && s->n==0 && match==NULL) return true;
        if (!s->s || !match) return false;
        if (s->n!=n) return false;
        if (strncmp(s->s,match,n)!=0) return false;
        return true;
}

static const ctx *getroot(const ctx *c) {
        while(c->prev) c=c->prev;
        return c;
}

static json_in not_thing(const ctx *c,const char *thing,json_in s,json_in p,const char *msg) {
        c=getroot(c);
        c->callbacks->error(c,thing,s,p,msg,c->callbacks->context);
        return NULL;
}

static json_in eat_whitespace(json_in p) {
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

static json_in not_string(ctx *c,json_in s,json_in p,const char *msg) {
        return not_thing(c,"string",s,p,msg);
}

static json_in eat_string(ctx *c,json_in s,json_nchar *str) {
        json_in p,q;
        if (*s!='\"') return NULL;
        c->value.type=json_type_string;
        str->s=s+1;
        for(p=str->s;*p;p++) {
                if (*p=='\"') {str->n = p - str->s; return p+1;}
                if (*p!='\\') continue;
                p++;
                switch(*p) {
                case '\0':
                        return p;
                case '\\':
                case '/':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                        break;
                case 'u':
                        q=eat_hex(p,4);
                        if (q-p!=4) return not_string(c,s,q,"bad unicode sequence");
                        break;
                default:
                        return not_string(c,s,p,"bad control character");
                }
        }
        return not_string(c,s,p,"no closing quote");
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
                if (d==0) return not_thing(c,"number",s,p,"bad exponent");
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
                return p;
        }

        if (*p=='[') {
                c->value.type=json_type_array;
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

        return not_thing(c,"value",s,p,"not a value");
}

static json_in not_array(const ctx *c,json_in s,json_in p,const char *msg) {
        return not_thing(c,"array",s,s,msg);
}

static json_in eat_array(ctx *vc,json_in s) {
        json_in p=s;
        ctx c={};

        if (*p!='[') return NULL;
        p++;
        if (vc) vc->next=&c;
        c.prev=vc;
        c.name.s=NULL;
        c.name.n=0;
        p=eat_whitespace(p);
        if (*p==']') return p+1;
        for(c.index=0;;c.index++) {
                p=get_value(&c,p);
                if (!p) return not_array(&c,s,p,"bad value");
                p=got_value(&c,p);
                if (*p!=',') break;
                p++;
        }
        if (*p!=']') return not_array(&c,s,p,"no closing bracket");
        return p+1;
}

static json_in not_object(const ctx *c,json_in s,json_in p,const char *msg) {
        return not_thing(c,"object",s,p,msg);
}

static json_in eat_object(ctx *vc,json_in s) {
        json_in p=s,q;
        ctx c={};

        if (*p!='{') return NULL;
        p++;
        if (vc) vc->next=&c;
        c.prev=vc;
        c.index=0;
        p=eat_whitespace(p);
        if (*p=='}') return p+1;
        for(;;) {
                if (!*p) return not_object(&c,s,p,"closure missing");
                q=eat_string(&c,p,&c.name);
                if (!q) return not_object(&c,s,p,"bad name");
                p=eat_whitespace(q);
                if (*p!=':') return not_object(&c,s,p,"colon missing");
                p++;
                q=get_value(&c,p);
                if (!q) return not_object(&c,s,p,"bad value");
                p=got_value(&c,q);
                if (*p=='}') return p+1;

                if (*p!=',') return not_object(&c,s,p,"comma missing");
                p++;
                p=eat_whitespace(p);
        }
}

static json_in got_value(ctx *c,json_in s) {
        const json_valuecontext *root;
        switch(c->value.type) {
        case json_type_object: return eat_whitespace(eat_object(c,s));
        case json_type_array: return eat_whitespace(eat_array(c,s));
        default: break;
        }
        root=getroot(c);
        c->next=NULL;
        root->callbacks->got_value(root->next,&c->value,root->callbacks->context);
        return s;
}

void json_printpath(const json_valuecontext *c) {
        const json_valuecontext *root,*d;

        for(root=c;root->prev;root=root->prev);
        for(d=root->next;d;d=d->next) {
                if (d->name.s) printf("[\"%.*s\"]",d->name.n,d->name.s);
                else printf("[%d]",d->index);
        }
}

void json_printvalue(const json_value *v) {
        switch(v->type) {
        case json_type_null: printf("null\n"); break;
        case json_type_bool: printf("%s\n",(v->truefalse)?"true":"false"); break;
        case json_type_string: printf("\"%.*s\"\n",v->string.n,v->string.s); break;
        case json_type_number: printf("%g\n",v->number); break;
        default: printf("<bad type %d>\n",v->type); break;
        }
}

static void default_got_value(const json_valuecontext *base,const json_value *v,void *context) {
        json_printpath(base);
        printf(" = ");
        json_printvalue(v);
}

static void default_error(const json_valuecontext *c,const char *type,json_in s,json_in p,const char *msg,void *context) {
        printf("bad %s: %s\n-> ",type,msg);
        for(;*s;s++)
                printf("%s%c%s",(s==p)?"<<<":"",*s,(s==p)?">>>":"");
        printf("\n");
}

bool json_matches_name(const json_valuecontext *c,const char *name) {
        if (!c) return false;
        if (!c->name.s) return false;
        return cmp_nchar(name,&c->name);
}

bool json_matches_index(const json_valuecontext *c,int index) {
        if (!c) return false;
        if (c->name.s) return false;
        if (c->index!=index) return false;
        return true;
}

const char *json_parse(const json_callbacks *ucb,const char *s) {
        ctx root={};
        json_in p;
        json_callbacks rootcallbacks={};
        if (ucb) rootcallbacks=*ucb;
        if (!rootcallbacks.got_value) rootcallbacks.got_value=default_got_value;
        if (!rootcallbacks.error)     rootcallbacks.error=default_error;

        root.callbacks=&rootcallbacks;
        root.name.s="";
        root.name.n=0;

        switch(*s) {
                case '{': p=eat_object(&root,s); break;
                case '[': p=eat_array(&root,s); break;
                default: p=not_thing(&root,"JSON",s,s,"no opening brace or bracket"); break;
        }
        if (p && *p!='\0') not_thing(&root,"JSON",s,p,"excess data");
        return p;
}

#if 0 DBG(+1)
int main(void) {
        struct {
                char *s;
        } t[]={
                {"{}"},
                {"{\"hello\":\"there\"}"},
                {"[1]"},
                {"[1,4.3,9e10]"},
                {"{\"list\":[10,11,\"hi\",-3e-10]}"},
                {"{\n"
                "        \"glossary\": {\n"
                "                \"title\": \"example glossary\",\n"
                "                \"GlossDiv\": {\n"
                "                        \"title\": \"S\",\n"
                "                        \"GlossList\": {\n"
                "                                \"GlossEntry\": {\n"
                "                                        \"ID\": \"SGML\",\n"
                "                                        \"SortAs\": \"SGML\",\n"
                "                                        \"GlossTerm\": \"Standard Generalized Markup Language\",\n"
                "                                        \"Acronym\": \"SGML\",\n"
                "                                        \"Abbrev\": \"ISO 8879:1986\",\n"
                "                                        \"GlossDef\": {\n"
                "                                                \"para\": \"A meta-markup language.\",\n"
                "                                                \"GlossSeeAlso\": [\"GML\", \"XML\"]\n"
                "                                        },\n"
                "                                        \"GlossSee\": \"markup\"\n"
                "                                }\n"
                "                        }\n"
                "                }\n"
                "        }\n"
                "}"},
                {"[0,0.,1e1,1,2,-1,-2,0.0023,-0.0025,1e9,1.0023e9,-123.456e-78]"},
        };
        int slen=sizeof(t)/sizeof(*t);
        int i;

        for(i=0;i<slen;i++) {
                char *s=t[i].s;
                const char *p;

                printf("--------------\n");
                p=json_parse(NULL,s);
                if (p && *p!='\0') p=not_object(NULL,s,p,"excess data");

                printf("%s -> ",s);
                if (p) printf("%zu/%zu\n",p-s,strlen(s));
                else printf("null\n");
        }
        return 0;
}
#endif
