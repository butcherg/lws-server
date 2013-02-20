
#include "HierarchicalHash.h"
#include "uthash.h"
#include "utlist.h"

#include <stdio.h>
#include <math.h>


//These are Bad Functions, need much work...
static int extractkey(char * key, const char * fullkey)
{
    int i;
    for (i = 0; i <= (int) strlen(fullkey); ++i) {
        if ((fullkey[i] == '.') | (fullkey[i] == '\0')) {
            key[i] = '\0';
            return i-1;
        }
        else {
            key[i] = fullkey[i];
        }
    }
    return i;
}

static void extractpath(char * path, const char * fullpath)
{
    int p = 0;
    for (int f = 0; f < (int) strlen(fullpath); f++) path[f] = '\0';
    for (int i = 0; i <= (int) strlen(fullpath); i++) {
        if (p > 0) {
            path[p] = fullpath[i];
            if (path[p] == '\0') return;
            p++;
        }
        if (p == 0 && fullpath[i] == '.') {
            i++;
            path[p] = fullpath[i];
            p++;
//          path[p] = '\0';
        }
    }
}


//Listener item:
struct listener_struct {
    HierarchicalHashListener *listener;
    struct listener_struct *next;
};

//Hash item:
struct hierarch_struct {
    char key[STRINGLENGTH];
    char val[STRINGLENGTH];
    HierarchicalHash *child;
    long int timestamp;
    char author[STRINGLENGTH];
    struct listener_struct *listeners;
    UT_hash_handle hh;
};


HierarchicalHash::HierarchicalHash()
{
    h = NULL;
}

HierarchicalHash::HierarchicalHash(const char * jsonstring)
//void HierarchicalHash::LoadJSON(const char *jsonstring)
{
    char numbuf[127];
    double intpart;
    h = NULL;
    cJSON *root = cJSON_Parse(jsonstring);
    if (root) {
        if (root->type == cJSON_Object) {
            cJSON *item = root->child;
            while (item) {
                switch (item->type) {
                    case cJSON_String:
                        Set(item->string, item->valuestring);
                        break;
                    case cJSON_Number:
                        if (modf(item->valuedouble, &intpart) == 0.0)
                            sprintf(numbuf, "%d", item->valueint);
                        else
                            sprintf(numbuf, "%g", item->valuedouble);
                        Set(item->string, numbuf);
                        break;
                    case cJSON_Object:
                        if (item->child) if (item->string) {
                            Set(item->string, new HierarchicalHash(item));
                        }
                    break;
                }
                item = item->next;
            }
        }
        cJSON_Delete(root);
    }
}


HierarchicalHash::~HierarchicalHash()
{
    struct hierarch_struct *current;
    while(h) {
        current = h;
        if (h->child) h->child->~HierarchicalHash();
        HASH_DEL(h, current);
        free(current);
    }
}

        //friend class HierarchicalHashIterator;

bool HierarchicalHash::IsEmpty()
{
    if (h == NULL) return true;
    return false;
}

void HierarchicalHash::Empty()
{
    //HierarchicalHashIterator *i = new HierarchicalHashIterator(h);
    //while (!i->End()) {
    //    i->NextKey()->Delete();
    //}

    struct hierarch_struct *current;
    while(h) {
        current = h;
        if (h->child) h->child->~HierarchicalHash();
        HASH_DEL(h, current);
        free(current);
    }
}


void HierarchicalHash::Set(const char * key, char * val)
{
    char k[STRINGLENGTH];
    char p[STRINGLENGTH];
    extractkey(k, key);
    extractpath(p, key);
//printf("key: %s, path: %s\n", k,p);
    struct hierarch_struct *s;
    HASH_FIND_STR(h, k, s);
    if (s == NULL) {
        s = (hierarch_struct *) malloc(sizeof(hierarch_struct));
        strncpy(s->key, k,STRINGLENGTH);
        if (strlen(p) == 0) strncpy(s->val, val,STRINGLENGTH);
        s->timestamp = 0;
        strncpy(s->author,"",STRINGLENGTH);
        s->child = NULL;
        s->listeners = NULL;
        HASH_ADD_STR(h, key, s);
    }
    if (strlen(p) != 0) {
        if (s->child == NULL) s->child = new HierarchicalHash();
        s->child->Set(p, val);
    }
    else {
        strncpy(s->val, val,STRINGLENGTH);
        FireListeners(s->key, s->val);
    }
}

//needs a little work... maybe don't want to keep....
void HierarchicalHash::Set(const char * key, HierarchicalHash * hash)
{
    char k[STRINGLENGTH];
    char p[STRINGLENGTH];
    extractkey(k, key);
    extractpath(p, key);
    struct hierarch_struct *s;
    HASH_FIND_STR(h, k, s);
    if (s == NULL) {
        s = (hierarch_struct *) malloc(sizeof(hierarch_struct));
        strncpy(s->key, k,STRINGLENGTH);
        if (strlen(p) == 0) s->child = hash;
        strncpy(s->val, "",STRINGLENGTH);
        s->listeners = NULL;
        HASH_ADD_STR(h, key, s);
    }
    if (strlen(p) != 0) {
        if (s->child == NULL) s->child = new HierarchicalHash();
        s->child->Set(p, hash);
    }
    else {
        //strncpy(s->val, val,STRINGLENGTH);
        s->child = hash;
        FireListeners(s->key, s->val);
    }
}

void HierarchicalHash::_Stuff_JSON(cJSON *root)
{
    if (root->type == cJSON_Object) {
        while (root) {
            switch (root->type) {
                case cJSON_String:
                    Set(root->string, root->valuestring);
                    break;
                case cJSON_Object:
                    _Stuff_JSON(root->child);
                    break;
            }
            root = root->next;
        }
    }
}


const char * HierarchicalHash::Get(const char * key)
{
    char k[STRINGLENGTH];
    char p[STRINGLENGTH];
    extractkey(k, key);
    extractpath(p, key);
    struct hierarch_struct *s;
    HASH_FIND_STR(h, k, s);
    if (strlen(p) != 0) {
        if (s == NULL) return "";
        if (s->child == NULL)
            return "";
        else
            return s->child->Get(p);
    }
    else {
        if (s) return s->val;
        return "";
    }
}

int HierarchicalHash::GetAsInt(const char *key)
{
    return atoi(Get(key));
}

double HierarchicalHash::GetAsDouble(const char *key)
{
    return atof(Get(key));
}

int HierarchicalHash::SizeJSON()
{
    int jsize;
    bool first = true;
    struct hierarch_struct *s;
    jsize = 2; //strcat(bigbuf, "{ ");
    for (s=h; s != NULL; s = (struct hierarch_struct *) s->hh.next) {
        if (!first)
            jsize +=2; //strcat(bigbuf, ", ");
        else
            first = false;
        jsize +=1; //strcat(bigbuf, "\""); 
        jsize += strlen(s->key); //strcat(bigbuf, s->key); 
        jsize +=1; //strcat(bigbuf, "\""); 
        jsize +=2; //strcat(bigbuf, ": ");
        if (s->child == NULL) {
            jsize +=1; //strcat(bigbuf, "\"");
	    jsize += strlen(s->val);  //strcat(bigbuf, s->val); 
	    jsize +=1; //strcat(bigbuf, "\"");
        }
        else {
                jsize += s->child->SizeJSON();
        }
    }
    jsize +=3; //strcat(bigbuf, " }"); and \0
    return jsize;
}

void HierarchicalHash::DumpJSON(char * bigbuf)
{
    bool first = true;
    struct hierarch_struct *s;
    strcat(bigbuf, "{ ");
    for (s=h; s != NULL; s = (struct hierarch_struct *) s->hh.next) {
        if (!first)
            strcat(bigbuf, ", ");
        else
            first = false;
        strcat(bigbuf, "\""); strcat(bigbuf, s->key), strcat(bigbuf, "\""); strcat(bigbuf, ": ");
        if (s->child == NULL) {
            strcat(bigbuf, "\"");strcat(bigbuf, s->val); strcat(bigbuf, "\"");
        }
        else {
                s->child->DumpJSON(bigbuf);
        }
    }
    strcat(bigbuf, " }");
}

//you can put a listener on a key before your store a value there. Note
//that if you delete a key, its listeners will disappear...
void HierarchicalHash::Listener(const char * key, HierarchicalHashListener * listener)
{
    char k[STRINGLENGTH];
    char p[STRINGLENGTH];
    extractkey(k, key);
    extractpath(p, key);
    struct hierarch_struct *s;
    HASH_FIND_STR(h, k, s);
    if (s == NULL) {
        s = (hierarch_struct *) malloc(sizeof(hierarch_struct));
        strncpy(s->key, k,STRINGLENGTH);
        s->child = NULL;
        s->listeners = NULL;
        HASH_ADD_STR(h, key, s);
    }
    if (strlen(p) != 0) {
        if (s->child == NULL) s->child = new HierarchicalHash();
        s->child->Listener(p, listener);
    }
    else {
        struct listener_struct *ln;
        ln = (listener_struct *) malloc(sizeof(listener_struct));
        ln->listener = listener;
        LL_APPEND(s->listeners, ln);
    }
}

HierarchicalHash * HierarchicalHash::GetChild(const char * key)
{
    char k[256];
    char p[256];
    extractkey(k, key);
    extractpath(p, key);
    struct hierarch_struct *s;
    HASH_FIND_STR(h, k, s);
    if (strlen(p) != 0) {
        if (s == NULL) return NULL;
        if (s->child == NULL)
            return NULL;
        else
            return s->child->GetChild(p);
    }
    else {
        if (s) return s->child;
        return NULL;
    }
}

//Returns a node of the tree for further access; keeps the hash from
//having to walk the full tree for each child access
HierarchicalHash * HierarchicalHash::With(const char * path)
{
    char k[256];
    char p[256];
    extractkey(k, path);
    extractpath(p, path);
    struct hierarch_struct *s;
    HASH_FIND_STR(h, k, s);
    if (strlen(p) != 0) {
        if (s == NULL) return NULL;
        if (s->child == NULL)
            return NULL;
        else
            return s->child->With(p);
    }
    else {
        if (s) return s->child;
        return NULL;
    }
}

bool HierarchicalHash::Exists(const char * key)
{
    char k[256];
    char p[256];
    extractkey(k, key);
    extractpath(p, key);
    struct hierarch_struct *s;
    HASH_FIND_STR(h, k, s);
    if (s) {
        if (strlen(p) == 0) return true;
        if (s->child == NULL) return false;
        return s->child->Exists(p);
    }
    return false;
}

bool HierarchicalHash::isLeaf(const char * key)
{
	if (Exists(key)) {
		if (GetChild(key) == NULL)
			return true;
		else
			return false;
	}
	else {
		return false;
	}
}

bool HierarchicalHash::Delete(const char * key)
{
    listener_struct *elt, *tmp;
    char k[256];
    char p[256];

    if (Exists(key)) {
        extractkey(k, key);
        extractpath(p, key);
        struct hierarch_struct *s;
        HASH_FIND_STR(h, k, s);
        if (s) {
            if (strlen(p) == 0) {
                HASH_DEL(h, s);
                free(s);
            }
            else
                if (s->child) s->child->Delete(p);
        }
        if (s->listeners) {
            LL_FOREACH_SAFE(s->listeners,elt,tmp) {
                LL_DELETE(s->listeners,elt);
            }
        }
        return true;
    }
    else
        return false;
}

void HierarchicalHash::LoadConfigurationFile(const char * filename)
{
    char line[STRINGLENGTH] = "";
    char section[STRINGLENGTH] = "";
    char path[STRINGLENGTH];
    char *comment;
    FILE *f = fopen(filename, "r");
    while (fgets(line, STRINGLENGTH, f)) {
        int l = strlen(line);
        if (line[l-1]=='\n') line[l-1] = 0;
	if (line[l-2]=='\r') line[l-2] = 0;
        if ((comment=strchr(line,'#'))) comment[0] = '\0';
        if (strlen(line) > 0) {
            if (strchr(line,'=')) {
                char *name = strtok(line, "=");
                char *val  = strtok(NULL, "=");
                for (int i=strlen(val)-1; val[i]==' '; i--) val[i] = '\0';
                if (strlen(section) > 0)
                    sprintf(path,"%s.%s",section,name);
                else
                    strncpy(path,name,STRINGLENGTH);
                Set(path, val);
            }
            else {
                char *lbracket = strchr(line,'[');
                char *rbracket = strchr(line,']');
                if (rbracket) {
                    rbracket[0] = 0;
                    if (lbracket) {
                        strncpy(section,lbracket+1,STRINGLENGTH);
                    }
                }
            }
        }
    }
    fclose(f);
}


void HierarchicalHash::Dump(char * bigbuf, const char * indent)
{
    char nextindent[STRINGLENGTH];
    strncpy(nextindent, indent,STRINGLENGTH);
    strncat(nextindent, "  ",STRINGLENGTH-strlen(nextindent));

    struct hierarch_struct *s;
    for (s=h; s != NULL; s = (struct hierarch_struct *) s->hh.next) {
        strcat(bigbuf, indent); strcat(bigbuf, s->key), strcat(bigbuf, ": ");
        if (s->child == NULL) {
            strcat(bigbuf, s->val); strcat(bigbuf, "\n");
        }
        else {
            strcat(bigbuf, "\n"); s->child->Dump(bigbuf, nextindent);
        }
    }

}


void HierarchicalHash::FireListeners(const char * key, char * val)
{
    struct hierarch_struct *s;
    HASH_FIND_STR(h, key, s);

    if (s->listeners == NULL) return;

    struct listener_struct *ln;
    for (ln = s->listeners; ln != NULL; ln=ln->next) {
        try {
            ln->listener->action(key, val);
        }
        catch (...) {
            //delete reference to non-existing listener?
            LL_DELETE(s->listeners, ln);
        }
    }
}


HierarchicalHash::HierarchicalHash(cJSON * root)  // cJSON_Object
{
    h = NULL;
    cJSON *item = root->child;
    while (item) {
        switch (item->type) {
            case cJSON_String:
            case cJSON_Number:
                Set(item->string, item->valuestring);
                break;
            case cJSON_Object:
                if (item->child) if (item->string) Set(item->string, new HierarchicalHash(item));
                break;
        }
        item = item->next;
    }
}


HierarchicalHashIterator::HierarchicalHashIterator(HierarchicalHash *hash)
{
    iterh = hash->h;
}

char * HierarchicalHashIterator::NextKey()
{
    char *k = iterh->key;
    iterh = (struct hierarch_struct *) iterh->hh.next;
    return k;
}

char * HierarchicalHashIterator::NextValue()
{
    char *v = iterh->val;
    iterh = (struct hierarch_struct *) iterh->hh.next;
    return v;
}

bool HierarchicalHashIterator::End()
{
    if (iterh == NULL) return true;
    return false;
}
