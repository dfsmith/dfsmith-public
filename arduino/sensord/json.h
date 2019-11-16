/* > json.h */
/* Daniel F. Smith, 2019 */
/* Stack-based in-place JSON parser. */

/* Because parse runs on the JSON string in-place, most
 * strings are handled as a json_nchar, which is a pointer
 * and string length.  These strings should be printed as
 * printf("%.*s",str.n,str.c);
 */

#ifndef STACK_JSON_H
#define STACK_JSON_H



#if 0

/* Minimal example: */
#include "json.h"
int main(void) {
        json_parse(NULL,"{\"hello\":\"world\"}");
        return 0;
}

/* Pick off the value ["johnny"][5] */
#include "json.h"
void condition(const json_valuecontext *root,const json_value *v,void *context) {
        const json_valuecontext *c=root;
        if (!json_matches_name(c,"johnny")) return;
        c=c->next;
        if (!json_matches_index(c,5)) return;
        if (&c->value!=v || v->type!=json_type_string) return;
        printf("%.*s\n",v->string.n,v->string.s);
}
int main(void) {
        json_callbacks cb={.got_value=condition};
        return json_parse(&cb,"{\"johnny\":[0,1,2,3,4,\"is alive\",6]}")!=NULL;
}


#endif




#include <stdbool.h>

/* Somewhere inside the JSON string in memory. */
typedef const char *json_in;

/* A pointer,length string type. */
typedef struct {
        json_in s;
        int n;
} json_nchar;

/* A value that a JSON entity can have.  Note that json_type_object
 * and json_type_array are composite types, that are never returned.
 */
typedef struct {
        enum {
                json_type_null,
                json_type_bool,
                json_type_number,
                json_type_string,
                json_type_array,
                json_type_object,
        } type;
        union {
                /* set if... */
                bool truefalse;    /* ... json_type_bool */
                double number;     /* ... json_type_number */
                json_nchar string; /* ... json_type_string */
        };
} json_value;

typedef struct json_valuecontext_s json_valuecontext;

/* A set of user-provided callback functions. If functions are NULL,
 * some suitable stdout functions will be used: see the default
 * values for these functions in the main file for an example.
 */
typedef struct {
        /* user context */
        void *context;

        /* called when a value is found */
        void (*got_value)(
                const json_valuecontext *base,
                const json_value *v,
                void *context);

        /* called when an error is found */
        void (*error)(
                const json_valuecontext *c,
                const char *type,
                json_in start,json_in hint,
                const char *msg,
                void *context);
} json_callbacks;

/* A list of the names that contain the value */
struct json_valuecontext_s {
        json_valuecontext *prev,*next;
        json_callbacks *callbacks;

        json_nchar name;  /* name of the JSON entity, or NULL if an array */
        int index;        /* index into an JSON array, if name.s==NULL */
        json_value value; /* the value of the entity */
};

/* Print the chain of path variables to the context. */
extern void json_printpath(const json_valuecontext *c);
/* Print a value. */
extern void json_printvalue(const json_value *v);

/* Returns true if the name matches the context. See example. */
extern bool json_matches_name(const json_valuecontext *c,const char *name);
/* Returns true if the index matches the context. See example. */
extern bool json_matches_index(const json_valuecontext *c,int index);

/* Parse a JSON text object with optional callback functions. */
extern const char *json_parse(const json_callbacks *cb,const char *json_string);

#endif
