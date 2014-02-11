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


static bool do_hashtest(
        int argelems,
        int argsize)
{
        bool       succp = true;
        HASHTABLE* h;
        int        nelems;
        int        i;
        int*       val_arr;
        int        hsize;
        int        longest;
        
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
        
return_succp:
        return succp;
}

/** 
 * @node Simple test which creates hashtable and frees it. Size and number of entries
 * sre specified by user and passed as arguments.
 *
 *
 * @return 0 if succeed, 1 if failed.
 *
 * 
 * @details (write detailed description here)
 *
 */
int main(void)
{
        int rc = 1;

        if (!do_hashtest(0, 1))         goto return_rc;
        if (!do_hashtest(10, 1))        goto return_rc;
        if (!do_hashtest(1000, 10))     goto return_rc;
        if (!do_hashtest(10, 0))        goto return_rc;
        if (!do_hashtest(1500, 17))     goto return_rc;
        if (!do_hashtest(1, 1))         goto return_rc;
        if (!do_hashtest(10000, 133))   goto return_rc;
        if (!do_hashtest(1000, 1000))   goto return_rc;
        if (!do_hashtest(1000, 100000)) goto return_rc;
        
        rc = 0;
return_rc:
        return rc;
}
