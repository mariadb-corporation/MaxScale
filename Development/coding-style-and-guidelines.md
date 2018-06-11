The purpose of a coding style and accompanying guidelines is to make the
reading and comprehension of the code easier. Reading prose would also be
much harder, should the punctuation rules differ from chapter and paragraph
to the next.

## Coding Style
### astyle
MaxScale comes with a Astyle configuration file used to format the source
code. To use it, run the following in the source root.
```
astyle --options=astylerc <path to source>
```
This will format the source file according to the MaxScale coding style.

## Consistency
Be consistent. In a particular context, everything should style wise look
the same.

### In Rome do as the romans do.
If something is done in a particular way in some context, then follow that
same way when doing modifications, even if it violates what is stated
here. If you want to fix the style of a file, then do so in one separate
change; not as part of other modifications.

## General
* Only spaces, no tabs.
* Indentation depth 4 spaces.
* No trailing white space.
* Maximum line length 110 characters.

## Indentation Style

We follow the
[Allman](https://en.wikipedia.org/wiki/Indentation_style#Allman_style)
indentation style.

Braces are always used and the brace associated with a control statement is
placed on the next line, indented to the same level as the control
statement. Statements within the braces are indented to the next level.
```
if (something)
{
    do_this();
}
else
{
    do_that();
}
```

## Punctuation
### Keywords are followed by a space.
```
if (something)
{
   ...;
}

while (something)
{
   ...;
}
```
Exceptions are `sizeof`, `typeof` and `alignof`.

### Function name and opening parenthesis are not separated by a space.
```
some_function();
```
### Operators

Use one space around (on each side of) most binary and ternary operators,
such as any of these:
```
=  +  -  <  >  *  /  %  |  &  ^  <=  >=  ==  !=  ?  :
```
but no space after unary operators:
```
&  *  +  -  ~  !  sizeof  typeof  alignof  __attribute__  defined
```
no space before the postfix increment & decrement unary operators:
```
++  --
```
no space after the prefix increment & decrement unary operators:
```
++  --
```
and no space around the `.` and `->` structure member operators.

### Comma is always followed by a space.
```
x = some_function(a, b, c + d);
```
### Opening parenthesis and square bracket are not followed by a space, and closing parenthesis and square bracket are not preceded by a space.
```
int len = strlen(name);

a = b[5];
```

## Naming
### enums
```
enum gwbuf_type { ... };
typedef enum { ... } gwbuf_type_t;
typedef enum gwbuf_type { ... } gwbuf_type_t;
```
### structs
```
struct gw_protocol { ... };
typedef struct { ... } GW_PROTOCOL;
typedef struct gw_protocol { ... } GW_PROTOCOL;
```

### functions

Small caps and words separated by underscores
```
void server_set_status(SERVER *, int);
```
with the exception of names of function pointers in plugin interfaces
```
typedef struct filter_object {
    FILTER *(*createInstance)(char **options, FILTER_PARAMETER **);
    ...
} FILTER_OBJECT;
```
that are _camelCase_.

## C++ naming
### namespaces

Small caps and words separated by underscores
```
namespace maxscale
{

class ...

}
...
namespace xyz_filter
{
...
}
```
Note that symbols within a namespace are not indented. Note also that the
namespace `maxscale` can only be used by classes belonging to the MaxScale
core. An exception is when a template in the MaxScale namespace is
specialized for a non MaxScale core class.

### classes

To distinguish them from plain-old-data, aka structs, class names use camel-case.
```
class CacheFilterSession
{
public:
    int some_member_function();
    ...
};
```
Note that the naming of member functions follows the same conventions as the
naming of free functions, that is, snake-case is used.

### member variables

Member variables are prefixed with `m_` and static member variables are
prefixed with `s_`.
```
class MyClass
{
    ...
private:
    int m_size;
    static int s_max_size;
};
```
In general, a class should have no public member variables but all access
should be via public functions that can be inline.

### variable prefixes

The following prefixes are "standardized" and can be used when applied
consistently:
```
int *pAge;                        // p, for pointers.
shared_ptr<NiftyBuffer> sBuffer;  // s, for smart pointers
unsigned nItems;                  // n, when a variable denotes a count.
```
Note that there is no underscore between the prefix and the actual variable
name, and that the character following the prefix is in large caps. Note
also that the prefixes can be stacked.
```
class SomeClass
{
private:
    int* m_pIndex;
    int** m_ppIndex;
    shared_ptr<SomeClass> m_sNext;
};
```
## Guidelines
These are suggestions.

### Try to have only a single exit-point in each function.

**Rationale**: With multiple exit points it is very easy to overlook the release
of some resource in one of those places.

Reasonable exceptions to the single exit point rule are:

* Checking of preconditions at the beginning of the function.
```
 int function(int a, int b)
 {
     if (a < 0)
         return -1;

     if (b > 5)
         return -1;

     int rv;
     ...
     return rv;
}
```
* Functions that are basically only big switches.
```
char* toString(enum Color c)
{
    switch (c)
    {
        case RED:
            return "RED";

        case GREEN:
            return "GREEN";

        default:
            return "Unknown";
   }
}
```
### Try to keep the line length less than 80 characters.

**Rationale**: In typography the optimal line length for readability is
considered to be around 60 characters. With 5 levels of nesting of 4
spaces each, that gives 80 characters. If it is hard to stay within that
limit, consider refactoring the function.

### Use parenthesis to make the precedence explicit, even if precedence rules would not require it.
```
if (a != b && !ready()) ...;

if ((a != b) && !ready()) ...;
```
**Rationale**: There can never be any doubt about what the _intended_ precedence is.

### Try to keep the length of each function less than what fits in one screen full (roughly 60 lines).

**Rationale**: That way it is possible with one glance to get a grasp of
what the function does. If it's hard to keep within the limit it may be a
sign that the function ought to be refactored.

### Use vertical space (i.e. empty lines) to introduce paragraphs and sections.

**Rationale**: Makes the reading easier.