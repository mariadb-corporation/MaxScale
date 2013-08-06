#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/hashtable.h"

static int hfun(void* key);
static int cmpfun (void *, void *);

static int hfun(
        void* key)
{
        return *(int *)key;
}


static int cmpfun(
        void* v1,
        void* v2)
{
        int i1;
        int i2;

        i1 = *(int *)v1;
        i2 = *(int *)v2;

        return (i1 < i2 ? -1 : (i1 > i2 ? 1 : 0));
}




/** 
 * @node Simple test which creates hashtable and frees it. Size and number of entries
 * sre specified by user and passed as arguments.
 *
 * Parameters:
 * @param argc - <usage>
 *          <description>
 *
 * @param argv - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
int main(int argc, char** argv)
{
        HASHTABLE* h;
        int        nelems;
        int        i;
        int*       val_arr;
        int        argsize;
        int        hsize;
        int        argelems;
        int        longest;

        if (argc != 3) {
            fprintf(stderr, "\nWrong number of arguments. Usage "
                    ":\n\n\ttesthash <# of elements> <# hash size> "
                    "<hash function> <compare function>\n\n");
            return 1;
        }

        argelems = strtol(argv[1], NULL, 10);
        argsize  = strtol(argv[2], NULL, 10);

        ss_dfprintf(stderr,
                    "testhash : creating hash table of size %d, including %d "
                    "elements in total.",
                    argsize,
                    argelems); 
        
        val_arr = (int *)malloc(sizeof(void *)*argelems);
        
        h = hashtable_alloc(argsize, hfun, cmpfun);

        ss_dfprintf(stderr, "\t..done\nAdd %d elements to hash table.", argelems);
        
        for (i=0; i<argelems; i++) {
            val_arr[i] = i;
            hashtable_add(h, (void *)&val_arr[i], (void *)&val_arr[i]);
        }

        ss_dfprintf(stderr, "\t..done\nRead hash table statistics.");
        
        hashtable_get_stats((void *)h, &hsize, &nelems, &longest);

        ss_dfprintf(stderr, "\t..done\nValidate read values.");
        
        ss_info_dassert(hsize == argsize, "Invalid hash size");
        ss_info_dassert((nelems == argelems) || (nelems == 0 && argsize == 0),
                        "Invalid element count");
        ss_info_dassert(longest <= nelems, "Too large longest list value");

        ss_dfprintf(stderr, "\t\t..done\n\nTest completed successfully.\n\n");
        
        CHK_HASHTABLE(h);
        hashtable_free(h);
        return 0;
}




