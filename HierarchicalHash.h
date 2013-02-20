#ifndef HIERARCHICALHASH_H
#define HIERARCHICALHASH_H

#include <stdlib.h>
#include <stdio.h>
#include "cJSON.h"

#define STRINGLENGTH 256


class HierarchicalHash;
class HierarchicalHashIterator;
class HierarchicalHashListener;


class HierarchicalHashListener
{
    public:
        virtual ~HierarchicalHashListener() { }
        virtual void action(const char * key, char * value) = 0;
};


/**
 * \brief a hierarchical string-keyed hash.
 *
 * Keys are formed using a dot notation, e.g., "person.tom.age", "person.joe.age".
 * In this example, "person" is a HierarchicalHash itself, containing HierarchicalHashes
 * for "tom" and "joe", each containing one name-value pair, "age". A JSON string 
 * constructor is provided.  Listeners can be attached to any node, which will be fired
 * when the item is updated.
 *
*/
class HierarchicalHash
{
    friend class HierarchicalHashIterator;

    public:
        HierarchicalHash();
        HierarchicalHash(const char * jsonstring);
//        void LoadJSON(const char * jsonstring);
        ~HierarchicalHash();

        bool IsEmpty();
        void Empty();
        void Set(const char * key, char * val);
        void Set(const char * key, HierarchicalHash * hash);
        const char * Get(const char * key);
        int GetAsInt(const char *key);
        double GetAsDouble(const char *key);
        void Listener(const char * key, HierarchicalHashListener * listener);
        HierarchicalHash * GetChild(const char * key);
        HierarchicalHash * With(const char * path);
        bool Exists(const char * key);
	bool isLeaf(const char * key);
        bool Delete(const char * key);

        void Dump(char * bigbuf, const char * indent);
	int SizeJSON();
        void DumpJSON(char * bigbuf);
        void LoadConfigurationFile(const char * filename);
	HierarchicalHash(cJSON * root);

    protected:
        struct hierarch_struct *h;
        void FireListeners(const char * key, char * val);
	void _Stuff_JSON(cJSON *root);

};

/**
 * \brief provides an iterator for HierarchicalHashes.
 *
 * Use as follows:
 * \code
 * HierarchicalHashIterator *i = new HierarchicalHash(h);
 * while (!i->End()) {
 *	//use i->NextKey() or i->NextValue() to get the iterated item, then do stuff with it.
 * }
 * \endcode
*/
class HierarchicalHashIterator
{
    public:
        HierarchicalHashIterator(HierarchicalHash *hash);
        char * NextKey();
        char * NextValue();
	
        bool End();

    private:
        struct hierarch_struct *iterh;

};



#endif

