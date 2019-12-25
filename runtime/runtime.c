/* Runtime library */

# include <stdio.h>
# include <stdio.h>
# include <string.h>
# include <stdarg.h>
# include <stdlib.h>
# include <sys/mman.h>
# include <assert.h>
# include <errno.h>

# define __ENABLE_GC__
# ifndef __ENABLE_GC__
# define alloc malloc
# endif

/*# define DEBUG_PRINT 1 */

/* GC pool structure and data; declared here in order to allow debug print */
typedef struct {
  size_t * begin;
  size_t * end;
  size_t * current;
  size_t   size;
} pool;

static pool from_space;
static pool to_space;
size_t      *current;
/* end */

# ifdef __ENABLE_GC__

/* GC extern invariant for built-in functions */
extern void __pre_gc  ();
extern void __post_gc ();

# else

# define __pre_gc __pre_gc_subst
# define __post_gc __post_gc_subst

void __pre_gc_subst () {}
void __post_gc_subst () {}

# endif
/* end */

# define STRING_TAG  0x00000001
# define ARRAY_TAG   0x00000003
# define SEXP_TAG    0x00000005
# define CLOSURE_TAG 0x00000007 

# define LEN(x) ((x & 0xFFFFFFF8) >> 3)
# define TAG(x)  (x & 0x00000007)

# define TO_DATA(x) ((data*)((char*)(x)-sizeof(int)))
# define TO_SEXP(x) ((sexp*)((char*)(x)-2*sizeof(int)))
#ifdef DEBUG_PRINT // GET_SEXP_TAG is necessary for printing from space
# define GET_SEXP_TAG(x) (LEN(x))
#endif

# define UNBOXED(x)  (((int) (x)) &  0x0001)
# define UNBOX(x)    (((int) (x)) >> 1)
# define BOX(x)     ((((int) (x)) << 1) | 0x0001)

typedef struct {
  int tag; 
  char contents[0];
} data; 

typedef struct {
  int tag; 
  data contents; 
} sexp; 

extern void* alloc (size_t);

extern int Blength (void *p) {
  data *a = (data*) BOX (NULL);
  a = TO_DATA(p);
  return BOX(LEN(a->tag));
}

char* de_hash (int n) {
  static char *chars = (char*) BOX (NULL);
  static char buf[6] = {0,0,0,0,0,0};
  char *p = (char *) BOX (NULL);
  chars =  "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  p = &buf[5];

#ifdef DEBUG_PRINT
  printf ("de_hash: tag: %d\n", n); fflush (stdout);
#endif
  
  *p-- = 0;

  while (n != 0) {
#ifdef DEBUG_PRINT
    printf ("char: %c\n", chars [n & 0x003F]); fflush (stdout);
#endif
    *p-- = chars [n & 0x003F];
    n = n >> 6;
  }
  
  return ++p;
}

static void vfailure (char *s, va_list args) {
  fprintf  (stderr, "*** FAILURE: ");
  vfprintf (stderr, s, args); // vprintf (char *, va_list) <-> printf (char *, ...)
  exit     (255);
}

static void failure (char *s, ...) {
  va_list args;

  va_start (args, s);
  vfailure (s, args);
}

typedef struct {
  char *contents;
  int ptr;
  int len;
} StringBuf;

static StringBuf stringBuf;

# define STRINGBUF_INIT 128

static void createStringBuf () {
  stringBuf.contents = (char*) malloc (STRINGBUF_INIT);
  stringBuf.ptr      = 0;
  stringBuf.len      = STRINGBUF_INIT;
}

static void deleteStringBuf () {
  free (stringBuf.contents);
}

static void extendStringBuf () {
  int len = stringBuf.len << 1;

  stringBuf.contents = (char*) realloc (stringBuf.contents, len);
  stringBuf.len      = len;
}

static void vprintStringBuf (char *fmt, va_list args) {
  int     written = 0,
          rest    = 0;
  char   *buf     = (char*) BOX(NULL);

 again:
  buf     = &stringBuf.contents[stringBuf.ptr];
  rest    = stringBuf.len - stringBuf.ptr;
  written = vsnprintf (buf, rest, fmt, args);
  
  if (written >= rest) {
    extendStringBuf ();
    goto again;
  }

  stringBuf.ptr += written;
}

static void printStringBuf (char *fmt, ...) {
  va_list args;

  va_start (args, fmt);
  vprintStringBuf (fmt, args);
}

static void printValue (void *p) {
  data *a = (data*) BOX(NULL);
  int i   = BOX(0);
  if (UNBOXED(p)) printStringBuf ("%d", UNBOX(p));
  else {
    a = TO_DATA(p);

    switch (TAG(a->tag)) {      
    case STRING_TAG:
      printStringBuf ("\"%s\"", a->contents);
      break;

    case CLOSURE_TAG:
      printStringBuf ("<closure ");
      for (i = 0; i < LEN(a->tag); i++) {
	if (i) printValue ((void*)((int*) a->contents)[i]);
	else printStringBuf ("%x", (void*)((int*) a->contents)[i]);
	
	if (i != LEN(a->tag) - 1) printStringBuf (", ");
      }
      printStringBuf (">");
      break;
      
    case ARRAY_TAG:
      printStringBuf ("[");
      for (i = 0; i < LEN(a->tag); i++) {
        printValue ((void*)((int*) a->contents)[i]);
	if (i != LEN(a->tag) - 1) printStringBuf (", ");
      }
      printStringBuf ("]");
      break;
      
    case SEXP_TAG: {
#ifndef DEBUG_PRINT
      char * tag = de_hash (TO_SEXP(p)->tag);
#else
      char * tag = de_hash (GET_SEXP_TAG(TO_SEXP(p)->tag));
#endif      
      
      if (strcmp (tag, "cons") == 0) {
	data *b = a;
	
	printStringBuf ("{");

	while (LEN(a->tag)) {
	  printValue ((void*)((int*) b->contents)[0]);
	  b = (data*)((int*) b->contents)[1];
	  if (! UNBOXED(b)) {
	    printStringBuf (", ");
	    b = TO_DATA(b);
	  }
	  else break;
	}
	
	printStringBuf ("}");
      }
      else {
	printStringBuf ("%s", tag);
	if (LEN(a->tag)) {
	  printStringBuf (" (");
	  for (i = 0; i < LEN(a->tag); i++) {
	    printValue ((void*)((int*) a->contents)[i]);
	    if (i != LEN(a->tag) - 1) printStringBuf (", ");
	  }
	  printStringBuf (")");
	}
      }
    }
    break;

    default:
      printStringBuf ("*** invalid tag: %x ***", TAG(a->tag));
    }
  }
}

static void stringcat (void *p) {
  data *a;
  int i;
  
  if (UNBOXED(p)) ;
  else {
    a = TO_DATA(p);

    switch (TAG(a->tag)) {      
    case STRING_TAG:
      printStringBuf ("%s", a->contents);
      break;
      
    case SEXP_TAG: {
#ifndef DEBUG_PRINT
      char * tag = de_hash (TO_SEXP(p)->tag);
#else
      char * tag = de_hash (GET_SEXP_TAG(TO_SEXP(p)->tag));
#endif   
      if (strcmp (tag, "cons") == 0) {
	data *b = a;
	
	while (LEN(a->tag)) {
	  stringcat ((void*)((int*) b->contents)[0]);
	  b = (data*)((int*) b->contents)[1];
	  if (! UNBOXED(b)) {
	    b = TO_DATA(b);
	  }
	  else break;
	}
      }
      else printStringBuf ("*** non-list tag: %s ***", tag);
    }
    break;

    default:
      printStringBuf ("*** invalid tag: %x ***", TAG(a->tag));
    }
  }
}

extern int Lcompare (void *p, void *q) {
# define COMPARE_AND_RETURN(x,y) do if (x != y) return BOX(x - y); while (0)
  if (UNBOXED(p)) {
    if (UNBOXED(q)) return BOX(UNBOX(p) - UNBOX(q));    
    else return BOX(-1);
  }
  else if (UNBOXED(q)) return BOX(1);
  else {
    data *a = TO_DATA(p), *b = TO_DATA(q);
    int ta = TAG(a->tag), tb = TAG(b->tag);
    int la = LEN(a->tag), lb = LEN(b->tag);
    int i;
    
    COMPARE_AND_RETURN (ta, tb);
      
    switch (ta) {
    case STRING_TAG:
      return BOX(strcmp (a->contents, b->contents));
      
    case CLOSURE_TAG:
      COMPARE_AND_RETURN (((void**) a->contents)[0], ((void**) b->contents)[0]);
      COMPARE_AND_RETURN (la, lb);
      i = 1;
      break;
      
    case ARRAY_TAG:
      COMPARE_AND_RETURN (la, lb);
      i = 0;
      break;

    case SEXP_TAG: {
#ifndef DEBUG_PRINT
      int ta = TO_SEXP(p)->tag, tb = TO_SEXP(q)->tag;      
#else
      int ta = GET_SEXP_TAG(TO_SEXP(p)->tag), tb = GET_SEXP_TAG(TO_SEXP(q)->tag);
#endif      
      COMPARE_AND_RETURN (ta, tb);
      COMPARE_AND_RETURN (la, lb);
      i = 0;
      break;
    }

    default:
      failure ("invalid tag %d in compare *****\n", ta);
    }

    for (; i<la; i++) {
      int c = Lcompare (((void**) a->contents)[i], ((void**) b->contents)[i]);
      if (c != BOX(0)) return BOX(c);
    }
    
    return BOX(0);
  }
}

extern void* Belem (void *p, int i) {
  data *a = (data *)BOX(NULL);
  a = TO_DATA(p);
  i = UNBOX(i);
  
  if (TAG(a->tag) == STRING_TAG) {
    return (void*) BOX(a->contents[i]);
  }
  
  return (void*) ((int*) a->contents)[i];
}

extern void* LmakeString (int length) {
  int   n = UNBOX(length);
  data *r;

  __pre_gc () ;
  
  r = (data*) alloc (n + 1 + sizeof (int));

  r->tag = STRING_TAG | (n << 3);

  __post_gc();
  
  return r->contents;
}

extern void* Bstring (void *p) {
  int   n = strlen (p);
  void *s;

  __pre_gc ();
  
  s = LmakeString (BOX(n));  
  strncpy (s, p, n + 1);

  __post_gc ();
  
  return s;
}

extern void* Lstringcat (void *p) {
  void *s;

  __pre_gc ();
  
  createStringBuf ();
  stringcat (p);

  s = Bstring (stringBuf.contents);
  
  deleteStringBuf ();

  __post_gc ();

  return s;  
}

extern void* Bstringval (void *p) {
  void *s = (void *) BOX (NULL);

  __pre_gc () ;
  
  createStringBuf ();
  printValue (p);

  s = Bstring (stringBuf.contents);
  
  deleteStringBuf ();

  __post_gc ();

  return s;
}

extern void* Bclosure (int n, void *entry, ...) {
  va_list args = (va_list) BOX (NULL);
  int     i    = BOX(0),
          ai   = BOX(0);
  data    *r   = (data*) BOX (NULL);

  __pre_gc ();
  
#ifdef DEBUG_PRINT
  printf ("Bclosure: create n = %d\n", n); fflush(stdout);
#endif
  r = (data*) alloc (sizeof(int) * (n+2));

  r->tag = CLOSURE_TAG | ((n + 1) << 3);
  ((void**) r->contents)[0] = entry;
  
  va_start(args, n);
  
  for (i = 0; i<n; i++) {
    ai = va_arg(args, int);
    ((int*)r->contents)[i+1] = ai;
  }
  
  va_end(args);

  __post_gc();

  return r->contents;
}

extern void* Barray (int n, ...) {
  va_list args = (va_list) BOX (NULL);
  int     i    = BOX(0),
          ai   = BOX(0);
  data    *r   = (data*) BOX (NULL);

  __pre_gc ();
  
#ifdef DEBUG_PRINT
  printf ("Barray: create n = %d\n", n); fflush(stdout);
#endif
  r = (data*) alloc (sizeof(int) * (n+1));

  r->tag = ARRAY_TAG | (n << 3);
  
  va_start(args, n);
  
  for (i = 0; i<n; i++) {
    ai = va_arg(args, int);
    ((int*)r->contents)[i] = ai;
  }
  
  va_end(args);

  __post_gc();
  
  return r->contents;
}

extern void* Bsexp (int n, ...) {
  va_list args = (va_list) BOX (NULL);
  int     i    = BOX(0);
  int     ai   = BOX(0);
  size_t * p   = NULL;
  sexp   *r    = (sexp*) BOX (NULL);
  data   *d    = (data *) BOX (NULL);

  __pre_gc () ;
  
#ifdef DEBUG_PRINT
  printf("Bsexp: allocate %zu!\n",sizeof(int) * (n+1)); fflush (stdout);
#endif
  r = (sexp*) alloc (sizeof(int) * (n+1));
  d = &(r->contents);
  r->tag = 0;
    
  d->tag = SEXP_TAG | ((n-1) << 3);
  
  va_start(args, n);
  
  for (i=0; i<n-1; i++) {
    ai = va_arg(args, int);
    
    p = (size_t*) ai;
    ((int*)d->contents)[i] = ai;
  }

  r->tag = va_arg(args, int);

#ifdef DEBUG_PRINT
  r->tag = SEXP_TAG | ((r->tag) << 3);
#endif

  va_end(args);

  __post_gc();

  return d->contents;
}

extern int Btag (void *d, int t, int n) {
  data *r = (data *) BOX (NULL);
  if (UNBOXED(d)) return BOX(0);
  else {
    r = TO_DATA(d);
#ifndef DEBUG_PRINT
    return BOX(TAG(r->tag) == SEXP_TAG && TO_SEXP(d)->tag == t && LEN(r->tag) == n);
#else
    return BOX(TAG(r->tag) == SEXP_TAG &&
	     GET_SEXP_TAG(TO_SEXP(d)->tag) == t && LEN(r->tag) == n);
#endif
  }
}

extern int Barray_patt (void *d, int n) {
  data *r = BOX(NULL);
  if (UNBOXED(d)) return BOX(0);
  else {
    r = TO_DATA(d);
    return BOX(TAG(r->tag) == ARRAY_TAG && LEN(r->tag) == n);
  }
}

extern int Bstring_patt (void *x, void *y) {
  data *rx = (data *) BOX (NULL),
       *ry = (data *) BOX (NULL);
  if (UNBOXED(x)) return BOX(0);
  else {
    rx = TO_DATA(x); ry = TO_DATA(y);

    if (TAG(rx->tag) != STRING_TAG) return BOX(0);
    
    return BOX(strcmp (rx->contents, ry->contents) == 0 ? 1 : 0);
  }
}

extern int Bclosure_tag_patt (void *x) {
  if (UNBOXED(x)) return BOX(0);
  
  return BOX(TAG(TO_DATA(x)->tag) == CLOSURE_TAG);
}

extern int Bboxed_patt (void *x) {
  return BOX(UNBOXED(x) ? 0 : 1);
}

extern int Bunboxed_patt (void *x) {
  return BOX(UNBOXED(x) ? 1 : 0);
}

extern int Barray_tag_patt (void *x) {
  if (UNBOXED(x)) return BOX(0);
  
  return BOX(TAG(TO_DATA(x)->tag) == ARRAY_TAG);
}

extern int Bstring_tag_patt (void *x) {
  if (UNBOXED(x)) return BOX(0);
  
  return BOX(TAG(TO_DATA(x)->tag) == STRING_TAG);
}

extern int Bsexp_tag_patt (void *x) {
  if (UNBOXED(x)) return BOX(0);
  
  return BOX(TAG(TO_DATA(x)->tag) == SEXP_TAG);
}

extern void* Bsta (void *v, int i, void *x) {
  if (TAG(TO_DATA(x)->tag) == STRING_TAG)((char*) x)[UNBOX(i)] = (char) UNBOX(v);
  else ((int*) x)[UNBOX(i)] = v;

  return v;
}

static void fix_unboxed (char *s, va_list va) {
  size_t *p = va;
  int i = 0;
  
  while (*s) {
    if (*s == '%') {
      size_t n = p [i];
      if (UNBOXED (n)) {
	p[i] = UNBOX(n);
      }
      i++;
    }
    s++;
  }
}

extern void Lfailure (char *s, ...) {
  va_list args;
  
  va_start    (args, s);
  fix_unboxed (s, args);
  vfailure    (s, args);
}

extern void Bmatch_failure (void *v, char *fname, int line, int col) {
  createStringBuf ();
  printValue (v);
  failure ("match failure at %s:%d:%d, value '%s'\n", fname, line, col, stringBuf.contents);
}

extern void* /*Lstrcat*/ i__Infix_4343 (void *a, void *b) {
  data *da = (data*) BOX (NULL);
  data *db = (data*) BOX (NULL);
  data *d  = (data*) BOX (NULL);

  da = TO_DATA(a);
  db = TO_DATA(b);

  __pre_gc () ;
  
  d  = (data *) alloc (sizeof(int) + LEN(da->tag) + LEN(db->tag) + 1);

  d->tag = LEN(da->tag) + LEN(db->tag);

  strcpy (d->contents, da->contents);
  strcat (d->contents, db->contents);

  __post_gc();
  
  return d->contents;
}

extern void* Lsprintf (char * fmt, ...) {
  va_list args;
  void *s;

  
  va_start (args, fmt);
  fix_unboxed (fmt, args);
  
  createStringBuf ();

  vprintStringBuf (fmt, args);

  __pre_gc ();

  s = Bstring (stringBuf.contents);

  __post_gc ();
  
  deleteStringBuf ();

  return s;
}

extern void Lfprintf (FILE *f, char *s, ...) {
  va_list args = (va_list) BOX (NULL);

  va_start    (args, s);
  fix_unboxed (s, args);
  
  if (vfprintf (f, s, args) < 0) {
    failure ("fprintf (...): %s\n", strerror (errno));
  }
}

extern void Lprintf (char *s, ...) {
  va_list args = (va_list) BOX (NULL);

  va_start    (args, s);
  fix_unboxed (s, args);
  
  if (vprintf (s, args) < 0) {
    failure ("fprintf (...): %s\n", strerror (errno));
  }
}

extern FILE* Lfopen (char *f, char *m) {
  FILE* h = fopen (f, m);

  if (h)
    return h;

  failure ("fopen (\"%s\", \"%s\"): %s, %s, %s\n", f, m, strerror (errno));
}

extern void Lfclose (FILE *f) {
  fclose (f);
}

extern void* LreadLine () {
  char *buf;

  if (scanf ("%m[^\n]", &buf) == 1) {
    void * s = Bstring (buf);
    
    free (buf);
    return s;
  }
  
  if (errno != 0)
    failure ("readLine (): %s\n", strerror (errno));

  return LmakeString (0);
}

extern void* Lfread (char *fname) {
  FILE *f = fopen (fname, "r");

  if (f) {
    if (fseek (f, 0l, SEEK_END) >= 0) {
      long size = ftell (f);
      void *s   = LmakeString (size);
      
      rewind (f);

      if (fread (s, 1, size, f) == size) {
	fclose (f);
	return s;
      }
    }
  }

  failure ("fread (\"%s\"): %s\n", fname, strerror (errno));
}

extern void Lfwrite (char *fname, char *contents) {
  FILE *f = fopen (fname, "w");

  if (f) {
    if (fprintf (f, "%s", contents) < 0);
    else {
      fclose (f);
      return;
    }
  }

  failure ("fwrite (\"%s\"): %s\n", fname, strerror (errno));
}

extern void* Lfst (void *v) {
  return Belem (v, BOX(0));  
}

extern void* Lsnd (void *v) {
  return Belem (v, BOX(1));  
}

extern void* Lhd (void *v) {
  return Belem (v, BOX(0));  
}

extern void* Ltl (void *v) {
  return Belem (v, BOX(1));  
}

/* Lread is an implementation of the "read" construct */
extern int Lread () {
  int result = BOX(0);

  printf ("> "); 
  fflush (stdout);
  scanf  ("%d", &result);

  return BOX(result);
}

/* Lwrite is an implementation of the "write" construct */
extern int Lwrite (int n) {
  printf ("%d\n", UNBOX(n));
  fflush (stdout);

  return 0;
}

/* GC starts here */

extern const size_t __start_custom_data, __stop_custom_data;

# ifdef __ENABLE_GC__

extern void L__gc_init ();

# else

# define L__gc_init __gc_init_subst
void __gc_init_subst () {}

# endif

extern void __gc_root_scan_stack ();

/* ======================================== */
/*           Mark-and-copy                  */
/* ======================================== */

//static size_t SPACE_SIZE = 128;
static size_t SPACE_SIZE = 1024 * 1024;
# define POOL_SIZE (2*SPACE_SIZE)

static void swap (size_t ** a, size_t ** b) {
  size_t * t = *a;
  *a = *b;
  *b = t;
}

static void gc_swap_spaces (void) {
  swap (&from_space.begin, &to_space.begin);
  swap (&from_space.end  , &to_space.end  );
  from_space.current = current;
  to_space.current   = to_space.begin;
}

# define IS_VALID_HEAP_POINTER(p)\
  (!UNBOXED(p) &&		 \
   from_space.begin <= p &&	 \
   from_space.end   >  p)

# define IN_PASSIVE_SPACE(p)	\
  (to_space.begin <= p	&&	\
   to_space.end   >  p)

# define IS_FORWARD_PTR(p)			\
  (!UNBOXED(p) && IN_PASSIVE_SPACE(p))

extern size_t * gc_copy (size_t *obj);

static void copy_elements (size_t *where, size_t *from, int len) {
  int    i = 0;
  void * p = NULL;
  for (i = 0; i < len; i++) {
    size_t elem = from[i];
    if (!IS_VALID_HEAP_POINTER(elem)) {
      *where = elem;
      where++;
    }
    else {
      p = gc_copy ((size_t*) elem);
      *where = p;
#ifdef DEBUG_PRINT
      printf ("copy_elements: fix %p: %p\n", from, *where); fflush (stdout);
#endif
      where ++;
    }
  }
}

static void extend_spaces (void) {
  void *p1 = (void *) BOX (NULL), *p2 = (void *) BOX (NULL);
  size_t old_space_size = SPACE_SIZE        * sizeof(size_t),
         new_space_size = (SPACE_SIZE << 1) * sizeof(size_t);
  p1 = mremap(from_space.begin, old_space_size, new_space_size, 0);
  p2 = mremap(to_space.begin  , old_space_size, new_space_size, 0);
  if (p1 == MAP_FAILED || p2 == MAP_FAILED) {
    perror("EROOR: extend_spaces: mmap failed\n");
    exit (1);
  }
#ifdef DEBUG_PRINT
  printf ("extend: %p %p %p %p\n", p1, p2, from_space.begin, to_space.begin);
  printf ("extend: %p %p %p\n"   , from_space.end, to_space.end, current);
  fflush (stdout);
#endif
  from_space.end  += SPACE_SIZE;
  to_space.end    += SPACE_SIZE;
  SPACE_SIZE      =  SPACE_SIZE << 1;
  from_space.size =  SPACE_SIZE;
  to_space.size   =  SPACE_SIZE;
}

extern size_t * gc_copy (size_t *obj) {
  data   *d    = TO_DATA(obj);
  sexp   *s    = NULL;
  size_t *copy = NULL;
  int     i    = 0;
#ifdef DEBUG_PRINT
  int len1, len2, len3;
  void * objj;
  void * newobjj = (void*)current;
  printf ("gc_copy: %p cur = %p starts\n", obj, current);
  fflush (stdout);
#endif

  if (!IS_VALID_HEAP_POINTER(obj)) {
#ifdef DEBUG_PRINT
    printf ("gc_copy: invalid ptr: %p\n", obj); fflush (stdout);
#endif
    return obj;
  }

  if (!IN_PASSIVE_SPACE(current) && current != to_space.end) {
#ifdef DEBUG_PRINT
    printf("ERROR: gc_copy: out-of-space %p %p %p\n", current, to_space.begin, to_space.end);
    fflush(stdout);
#endif
    perror("ERROR: gc_copy: out-of-space\n");
    exit (1);
  }

  if (IS_FORWARD_PTR(d->tag)) {
#ifdef DEBUG_PRINT
    printf ("gc_copy: IS_FORWARD_PTR: return! %p\n", (size_t *) d->tag);
    fflush(stdout);
#endif
    return (size_t *) d->tag;
  }

  copy = current;
#ifdef DEBUG_PRINT
  objj = d;
#endif
  switch (TAG(d->tag)) {
    case CLOSURE_TAG:
#ifdef DEBUG_PRINT
      printf ("gc_copy:closure_tag; len =  %zu\n", LEN(d->tag)); fflush (stdout);
#endif
      current += (LEN(d->tag) + 1) * sizeof (int);
      *copy = d->tag;
      copy++;
      i = LEN(d->tag) - 1;
      d->tag = (int) copy;
      *copy = obj[0];
      copy_elements (copy, obj++, i);
      break;
    
    case ARRAY_TAG:
#ifdef DEBUG_PRINT
      printf ("gc_copy:array_tag; len =  %zu\n", LEN(d->tag)); fflush (stdout);
#endif
      current += (LEN(d->tag) + 1) * sizeof (int);
      *copy = d->tag;
      copy++;
      i = LEN(d->tag);
      d->tag = (int) copy;
      copy_elements (copy, obj, i);
      break;

    case STRING_TAG:
#ifdef DEBUG_PRINT
      printf ("gc_copy:string_tag; len = %d\n", LEN(d->tag) + 1); fflush (stdout);
#endif
      current += LEN(d->tag) * sizeof(char) + sizeof (int);
      *copy = d->tag;
      copy++;
      d->tag = (int) copy;
      strcpy (&copy[0], (char*) obj);
      break;

  case SEXP_TAG  :
      s = TO_SEXP(obj);
#ifdef DEBUG_PRINT
      objj = s;
      len1 = LEN(s->contents.tag);
      len2 = LEN(s->tag);
      len3 = LEN(d->tag);
      printf ("len1 = %li, len2=%li, len3 = %li\n", len1, len2, len3);
      fflush (stdout);
#endif
      current += (LEN(s->contents.tag) + 2) * sizeof (int);
      *copy = s->tag;
      copy++;
      *copy   = s->contents.tag;
      copy++;
      i = LEN(s->contents.tag);
      d->tag = (int) copy;
      copy_elements (copy, obj, i);
      break;

  default:
#ifdef DEBUG_PRINT
    printf ("ERROR: gc_copy: weird tag: %p", TAG(d->tag)); fflush (stdout);
#endif
    perror ("ERROR: gc_copy: weird tag");
    exit (1);
  }
#ifdef DEBUG_PRINT
  printf ("gc_copy: %p(%p) -> %p (%p); new-current = %p\n", obj, objj, copy, newobjj, current);
  fflush (stdout);
#endif
  return copy;
}

extern void gc_test_and_copy_root (size_t ** root) {
  if (IS_VALID_HEAP_POINTER(*root)) {
#ifdef DEBUG_PRINT
    printf ("gc_test_and_copy_root: root %p %p\n", root, *root); fflush (stdout);
#endif
    *root = gc_copy (*root);
  }
}

extern void gc_root_scan_data (void) {
  size_t * p = &__start_custom_data;
  while  (p != &__stop_custom_data) {
    gc_test_and_copy_root (p);
    p++;
  }
}

extern void init_pool (void) {
  size_t space_size = SPACE_SIZE * sizeof(size_t);
  from_space.begin = mmap (NULL, space_size, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
  to_space.begin   = mmap (NULL, space_size, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
  if (to_space.begin == MAP_FAILED || from_space.begin == MAP_FAILED) {
    perror ("EROOR: init_pool: mmap failed\n");
    exit   (1);
  }
  from_space.current = from_space.begin;
  from_space.end     = from_space.begin + SPACE_SIZE;
  from_space.size    = SPACE_SIZE;
  to_space.current   = to_space.begin;
  to_space.end       = to_space.begin + SPACE_SIZE;
  to_space.size      = SPACE_SIZE;
}

static int free_pool (pool * p) {
  return munmap((void *)p->begin, p->size);
}

static void * gc (size_t size) {
  current = to_space.begin;
#ifdef DEBUG_PRINT
  printf ("\ngc: current:%p; to_space.b =%p; to_space.e =%p; f_space.b = %p; f_space.e = %p\n",
	  current, to_space.begin, to_space.end, from_space.begin, from_space.end);
  fflush (stdout);
#endif
  gc_root_scan_data    ();
#ifdef DEBUG_PRINT
  printf ("gc: data is scanned\n"); fflush (stdout);
#endif
  __gc_root_scan_stack ();
  if (!IN_PASSIVE_SPACE(current)) {
    perror ("ASSERT: !IN_PASSIVE_SPACE(current)\n");
    exit   (1);
  }

  while (current + size >= to_space.end) {
#ifdef DEBUG_PRINT
    printf ("gc pre-extend_spaces : %p %zu %p \n", current, size, to_space.end);
    fflush (stdout);
#endif
    extend_spaces ();
#ifdef DEBUG_PRINT
    printf ("gc post-extend_spaces: %p %zu %p \n", current, size, to_space.end);
    fflush (stdout);
#endif
  }
  assert (IN_PASSIVE_SPACE(current));
  assert (current + size < to_space.end);

  gc_swap_spaces ();
  from_space.current = current + size;
#ifdef DEBUG_PRINT
  printf ("gc: end: (allocate!) return %p; from_space.current %p; from_space.end %p \n\n",
	  current, from_space.current, from_space.end);
  fflush (stdout);
#endif
  return (void *) current;
}

#ifdef DEBUG_PRINT
static void printFromSpace (void) {
  size_t * cur = from_space.begin, *tmp = NULL;
  data   * d   = NULL;
  sexp   * s   = NULL;
  size_t   len = 0;

  printf ("\nHEAP SNAPSHOT\n===================\n");
  printf ("f_begin = %p, f_end = %p,\n", from_space.begin, from_space.end);
  while (cur < from_space.current) {
    printf ("data at %p", cur);
    d  = (data *) cur;

    switch (TAG(d->tag)) {

    case STRING_TAG:
      printf ("(=>%p): STRING\n\t%s\n", d->contents, d->contents);
      len = LEN(d->tag) + 1;
      fflush (stdout);
      break;

    case CLOSURE_TAG:
      printf ("(=>%p): CLOSURE\n\t", d->contents);
      len = LEN(d->tag);
      for (int i = 0; i < len; i++) {
	int elem = ((int*)d->contents)[i];
	if (UNBOXED(elem)) printf ("%d ", elem);
	else printf ("%p ", elem);
      }
      len += 1;
      printf ("\n");
      fflush (stdout);
      break;

    case ARRAY_TAG:
      printf ("(=>%p): ARRAY\n\t", d->contents);
      len = LEN(d->tag);
      for (int i = 0; i < len; i++) {
	int elem = ((int*)d->contents)[i];
	if (UNBOXED(elem)) printf ("%d ", elem);
	else printf ("%p ", elem);
      }
      len += 1;
      printf ("\n");
      fflush (stdout);
      break;

    case SEXP_TAG:
      s = (sexp *) d;
      d = (data *) &(s->contents);
      char * tag = de_hash (GET_SEXP_TAG(s->tag));
      printf ("(=>%p): SEXP\n\ttag(%s) ", s->contents.contents, tag);
      len = LEN(d->tag);
      tmp = (s->contents.contents);
      for (int i = 0; i < len; i++) {
	int elem = ((int*)tmp)[i];
	if (UNBOXED(elem)) printf ("%d ", UNBOX(elem));
	else printf ("%p ", elem);
      }
      len += 2;
      printf ("\n");
      fflush (stdout);
      break;

    case 0:
      printf ("\nprintFromSpace: end!\n===================\n\n");
      return;

    default:
      printf ("\nprintFromSpace: ERROR: bad tag %d", TAG(d->tag));
      fflush (stdout);
      exit   (1);
    }
    cur += len * sizeof(int);
    printf ("len = %zu, new cur = %p\n", len, cur);
  }
}
#endif

#ifdef __ENABLE_GC__
extern void * alloc (size_t size) {
  void * p = (void*)BOX(NULL);
  if (from_space.current + size < from_space.end) {
#ifdef DEBUG_PRINT
    printf ("alloc: current: %p %zu", from_space.current, size); fflush (stdout);
#endif
    p = (void*) from_space.current;
    from_space.current += size;
#ifdef DEBUG_PRINT
    printf (";new current: %p \n", from_space.current); fflush (stdout);
#endif
    return p;
  }
#ifdef DEBUG_PRINT
  printf ("alloc: call gc: %zu\n", size); fflush (stdout);
  printFromSpace(); fflush (stdout);
#endif
  p = gc (size);
#ifdef DEBUG_PRINT
  printf("gc END\n\n"); fflush (stdout);
  printFromSpace(); fflush (stdout);
#endif
  return gc (size);
}
# endif
