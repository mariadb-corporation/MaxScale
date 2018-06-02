/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains SQLite's grammar for SQL.  Process this file
** using the lemon parser generator to generate C code that runs
** the parser.  Lemon will also generate a header file containing
** numeric codes for all of the tokens.
*/

// All token codes are small integers with #defines that begin with "TK_"
%token_prefix TK_

// The type of the data attached to each token is Token.  This is also the
// default type for non-terminals.
//
%token_type {Token}
%default_type {Token}

// The generated parser function takes a 4th argument as follows:
%extra_argument {Parse *pParse}

// This code runs whenever there is a syntax error
//
%syntax_error {
  UNUSED_PARAMETER(yymajor);  /* Silence some compiler warnings */
  assert( TOKEN.z[0] );  /* The tokenizer always gives us a token */
  sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &TOKEN);
}
%stack_overflow {
  UNUSED_PARAMETER(yypMinor); /* Silence some compiler warnings */
  sqlite3ErrorMsg(pParse, "parser stack overflow");
}

// The name of the generated procedure that implements the parser
// is as follows:
%name sqlite3Parser

%ifdef MAXSCALE
%include {
#ifdef MAXSCALE
#ifndef SQLITE_ENABLE_UPDATE_DELETE_LIMIT
#error sqlite3 for MaxScale should be built with SQLITE_ENABLED_DELETE_LIMIT defined.
#endif
#endif
}
%endif

// The following text is included near the beginning of the C source
// code file that implements the parser.
//
%include {
#include "sqliteInt.h"

// Copied from query_classifier.h
enum
{
  QUERY_TYPE_READ               = 0x000002, /*< Read database data:any */
  QUERY_TYPE_WRITE              = 0x000004, /*< Master data will be  modified:master */
  QUERY_TYPE_USERVAR_READ       = 0x000040, /*< Read a user variable:master or any */
};

typedef enum qc_field_usage
{
    QC_USED_IN_SELECT    = 0x01, /*< SELECT fld FROM... */
    QC_USED_IN_SUBSELECT = 0x02, /*< SELECT 1 FROM ... SELECT fld ... */
    QC_USED_IN_WHERE     = 0x04, /*< SELECT ... FROM ... WHERE fld = ... */
    QC_USED_IN_SET       = 0x08, /*< UPDATE ... SET fld = ... */
    QC_USED_IN_GROUP_BY  = 0x10, /*< ... GROUP BY fld */
} qc_field_usage_t;

// MaxScale naming convention:
//
// - A function that "overloads" a sqlite3 function has the same name
//   as the function it overloads, prefixed with "mxs_".
// - A function that is new for MaxScale has the name "maxscaleXYZ"
//   where "XYZ" reflects the statement the function handles.
//
extern void mxs_sqlite3AlterFinishAddColumn(Parse *, Token *);
extern void mxs_sqlite3AlterBeginAddColumn(Parse *, SrcList *);
extern void mxs_sqlite3Analyze(Parse *, SrcList *);
extern void mxs_sqlite3BeginTransaction(Parse*, int token, int type);
extern void mxs_sqlite3CommitTransaction(Parse*);
extern void mxs_sqlite3CreateIndex(Parse*,Token*,Token*,SrcList*,ExprList*,int,Token*,
                                   Expr*, int, int);
extern void mxs_sqlite3BeginTrigger(Parse*, Token*,Token*,int,int,IdList*,SrcList*,
                                    Expr*,int, int);
extern void mxs_sqlite3FinishTrigger(Parse*, TriggerStep*, Token*);
extern void mxs_sqlite3CreateView(Parse*,Token*,Token*,Token*,ExprList*,Select*,int,int);
extern void mxs_sqlite3DeleteFrom(Parse* pParse, SrcList* pTabList, Expr* pWhere, SrcList* pUsing);
extern void mxs_sqlite3DropIndex(Parse*, SrcList*, SrcList*,int);
extern void mxs_sqlite3DropTable(Parse*, SrcList*, int, int, int);
extern void mxs_sqlite3EndTable(Parse*, Token*, Token*, u8, Select*, SrcList*);
extern void mxs_sqlite3Insert(Parse*, SrcList*, Select*, IdList*, int,ExprList*);
extern void mxs_sqlite3RollbackTransaction(Parse*);
extern void mxs_sqlite3Savepoint(Parse *pParse, int op, Token *pName);
extern int  mxs_sqlite3Select(Parse*, Select*, SelectDest*);
extern void mxs_sqlite3StartTable(Parse*,Token*,Token*,int,int,int,int);
extern void mxs_sqlite3Update(Parse*, SrcList*, ExprList*, Expr*, int);

extern void maxscaleCollectInfoFromSelect(Parse*, Select*, int);

extern void maxscaleAlterTable(Parse*, mxs_alter_t command, SrcList*, Token*);
extern void maxscaleCall(Parse*, SrcList* pName, ExprList* pExprList);
extern void maxscaleCheckTable(Parse*, SrcList* pTables);
extern void maxscaleCreateSequence(Parse*, Token* pDatabase, Token* pTable);
extern void maxscaleDeclare(Parse* pParse);
extern void maxscaleDeallocate(Parse*, Token* pName);
extern void maxscaleDo(Parse*, ExprList* pEList);
extern void maxscaleDrop(Parse*, int what, Token* pDatabase, Token* pName);
extern void maxscaleExecute(Parse*, Token* pName, int type_mask);
extern void maxscaleExecuteImmediate(Parse*, Token* pName, ExprSpan* pExprSpan, int type_mask);
extern void maxscaleExplain(Parse*, Token* pNext);
extern void maxscaleFlush(Parse*, Token* pWhat);
extern void maxscaleHandler(Parse*, mxs_handler_t, SrcList* pFullName, Token* pName);
extern void maxscaleLoadData(Parse*, SrcList* pFullName, int local);
extern void maxscaleLock(Parse*, mxs_lock_t, SrcList*);
extern void maxscalePrepare(Parse*, Token* pName, Expr* pStmt);
extern void maxscalePrivileges(Parse*, int kind);
extern void maxscaleRenameTable(Parse*, SrcList* pTables);
extern void maxscaleSet(Parse*, int scope, mxs_set_t kind, ExprList*);
extern void maxscaleShow(Parse*, MxsShow* pShow);
extern void maxscaleTruncate(Parse*, Token* pDatabase, Token* pName);
extern void maxscaleUse(Parse*, Token*);

extern void maxscale_update_function_info(const char* name, const Expr* pExpr);

// Exposed utility functions
void exposed_sqlite3ExprDelete(sqlite3 *db, Expr *pExpr)
{
  sqlite3ExprDelete(db, pExpr);
}

void exposed_sqlite3ExprListDelete(sqlite3 *db, ExprList *pList)
{
  sqlite3ExprListDelete(db, pList);
}

void exposed_sqlite3IdListDelete(sqlite3 *db, IdList *pList)
{
  sqlite3IdListDelete(db, pList);
}

void exposed_sqlite3SelectDelete(sqlite3 *db, Select *p)
{
  sqlite3SelectDelete(db, p);
}

void exposed_sqlite3SrcListDelete(sqlite3 *db, SrcList *pList)
{
  sqlite3SrcListDelete(db, pList);
}


// Exposed SQL functions.
void exposed_sqlite3BeginTrigger(Parse *pParse,      /* The parse context of the CREATE TRIGGER statement */
                                 Token *pName1,      /* The name of the trigger */
                                 Token *pName2,      /* The name of the trigger */
                                 int tr_tm,          /* One of TK_BEFORE, TK_AFTER, TK_INSTEAD */
                                 int op,             /* One of TK_INSERT, TK_UPDATE, TK_DELETE */
                                 IdList *pColumns,   /* column list if this is an UPDATE OF trigger */
                                 SrcList *pTableName,/* The name of the table/view the trigger applies to */
                                 Expr *pWhen,        /* WHEN clause */
                                 int isTemp,         /* True if the TEMPORARY keyword is present */
                                 int noErr)          /* Suppress errors if the trigger already exists */
{
  sqlite3BeginTrigger(pParse, pName1, pName2, tr_tm, op, pColumns, pTableName, pWhen, isTemp, noErr);
}

void exposed_sqlite3FinishTrigger(Parse *pParse,
                                  TriggerStep *pStepList,
                                  Token *pAll)
{
  sqlite3FinishTrigger(pParse, pStepList, pAll);
}

int exposed_sqlite3Dequote(char *z)
{
  return sqlite3Dequote(z);
}

void exposed_sqlite3Insert(Parse* pParse,
                           SrcList* pTabList,
                           Select* pSelect,
                           IdList* pColumns,
                           int onError)
{
  sqlite3Insert(pParse, pTabList, pSelect, pColumns, onError);
}

void exposed_sqlite3EndTable(Parse* pParse, Token* pCons, Token* pEnd, u8 tabOpts, Select* pSelect)
{
  sqlite3EndTable(pParse, pCons, pEnd, tabOpts, pSelect);
}

int exposed_sqlite3Select(Parse* pParse, Select* p, SelectDest* pDest)
{
  return sqlite3Select(pParse, p, pDest);
}

void exposed_sqlite3StartTable(Parse *pParse,   /* Parser context */
                               Token *pName1,   /* First part of the name of the table or view */
                               Token *pName2,   /* Second part of the name of the table or view */
                               int isTemp,      /* True if this is a TEMP table */
                               int isView,      /* True if this is a VIEW */
                               int isVirtual,   /* True if this is a VIRTUAL table */
                               int noErr)       /* Do nothing if table already exists */
{
  sqlite3StartTable(pParse, pName1, pName2, isTemp, isView, isVirtual, noErr);
}

void exposed_sqlite3Update(Parse* pParse, SrcList* pTabList, ExprList* pChanges, Expr* pWhere, int onError)
{
    sqlite3Update(pParse, pTabList, pChanges, pWhere, onError);
}

/*
** Disable all error recovery processing in the parser push-down
** automaton.
*/
#define YYNOERRORRECOVERY 1

/*
** Make yytestcase() the same as testcase()
*/
#define yytestcase(X) testcase(X)

/*
** Indicate that sqlite3ParserFree() will never be called with a null
** pointer.
*/
#define YYPARSEFREENEVERNULL 1

/*
** Alternative datatype for the argument to the malloc() routine passed
** into sqlite3ParserAlloc().  The default is size_t.
*/
#define YYMALLOCARGTYPE  u64

/*
** An instance of this structure holds information about the
** LIMIT clause of a SELECT statement.
*/
struct LimitVal {
  Expr *pLimit;    /* The LIMIT expression.  NULL if there is no limit */
  Expr *pOffset;   /* The OFFSET expression.  NULL if there is none */
};

/*
** An instance of this structure is used to store the LIKE,
** GLOB, NOT LIKE, and NOT GLOB operators.
*/
struct LikeOp {
  Token eOperator;  /* "like" or "glob" or "regexp" */
  int bNot;         /* True if the NOT keyword is present */
};

/*
** An instance of the following structure describes the event of a
** TRIGGER.  "a" is the event type, one of TK_UPDATE, TK_INSERT,
** TK_DELETE, or TK_INSTEAD.  If the event is of the form
**
**      UPDATE ON (a,b,c)
**
** Then the "b" IdList records the list "a,b,c".
*/
struct TrigEvent { int a; IdList * b; };

/*
** An instance of this structure holds the ATTACH key and the key type.
*/
struct AttachKey { int type;  Token key; };

/*
** Disable lookaside memory allocation for objects that might be
** shared across database connections.
*/
static void disableLookaside(Parse *pParse){
  pParse->disableLookaside++;
  pParse->db->lookaside.bDisable++;
}

} // end %include

// Input is a single SQL command
input ::= cmdlist.
cmdlist ::= cmdlist ecmd.
cmdlist ::= ecmd.
ecmd ::= SEMI.
ecmd ::= explain SEMI.
ecmd ::= cmdx SEMI.
ecmd ::= oracle_assignment SEMI.
%ifdef MAXSCALE
explain_kw ::= EXPLAIN.  // Also covers DESCRIBE
explain_kw ::= DESC.

explain ::= explain_kw.             { pParse->explain = 1; }
// deferred_id is defined later, after the id token_class has been defined.
explain ::= explain_kw deferred_id(A). { maxscaleExplain(pParse, &A); }
explain ::= explain_kw deferred_id(A) DOT deferred_id. { maxscaleExplain(pParse, &A); }
ecmd ::= explain FOR(A) deferred_id INTEGER SEMI. { // FOR CONNECTION connection_id
  pParse->explain = 1;
  maxscaleExplain(pParse, &A);
}
%endif
%ifndef SQLITE_OMIT_EXPLAIN
%ifndef MAXSCALE
explain ::= EXPLAIN QUERY PLAN.   { pParse->explain = 2; }
%endif
%endif  SQLITE_OMIT_EXPLAIN
cmdx ::= cmd.           { sqlite3FinishCoding(pParse); }

///////////////////// Begin and end transactions. ////////////////////////////
//

%ifdef MAXSCALE
work_opt ::= WORK.
work_opt ::= .
cmd ::= BEGIN work_opt. {mxs_sqlite3BeginTransaction(pParse, TK_BEGIN, 0);} // BEGIN [WORK]
%endif
%ifndef MAXSCALE
cmd ::= BEGIN transtype(Y) trans_opt.  {sqlite3BeginTransaction(pParse, Y);}
trans_opt ::= .
trans_opt ::= TRANSACTION.
trans_opt ::= TRANSACTION nm.
%type transtype {int}
transtype(A) ::= .             {A = TK_DEFERRED;}
transtype(A) ::= DEFERRED(X).  {A = @X;}
transtype(A) ::= IMMEDIATE(X). {A = @X;}
transtype(A) ::= EXCLUSIVE(X). {A = @X;}
%endif
%ifdef MAXSCALE
cmd ::= COMMIT work_opt.       {mxs_sqlite3CommitTransaction(pParse);}
cmd ::= ROLLBACK work_opt.     {mxs_sqlite3RollbackTransaction(pParse);}
%endif
%ifndef MAXSCALE
cmd ::= COMMIT trans_opt.      {sqlite3CommitTransaction(pParse);}
cmd ::= END trans_opt.         {sqlite3CommitTransaction(pParse);}
cmd ::= ROLLBACK trans_opt.    {sqlite3RollbackTransaction(pParse);}
%endif

%ifdef MAXSCALE
savepoint_opt ::= SAVEPOINT.
savepoint_opt ::= .
cmd ::= SAVEPOINT nm(X). {
  mxs_sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &X);
}
cmd ::= RELEASE savepoint_opt nm(X). {
  mxs_sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &X);
}
cmd ::= ROLLBACK work_opt TO savepoint_opt nm(X). {
  mxs_sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &X);
}
%endif

%ifndef MAXSCALE
savepoint_opt ::= SAVEPOINT.
savepoint_opt ::= .
cmd ::= SAVEPOINT nm(X). {
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &X);
}
cmd ::= RELEASE savepoint_opt nm(X). {
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &X);
}
cmd ::= ROLLBACK trans_opt TO savepoint_opt nm(X). {
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &X);
}
%endif

///////////////////// The CREATE TABLE statement ////////////////////////////
//
cmd ::= create_table create_table_args.
create_table ::= createkw temp(T) TABLE ifnotexists(E) nm(Y) dbnm(Z). {
#ifdef MAXSCALE
   mxs_sqlite3StartTable(pParse,&Y,&Z,T,0,0,E);
#else
   sqlite3StartTable(pParse,&Y,&Z,T,0,0,E);
#endif
}
%ifdef MAXSCALE
or_replace_opt ::= .
or_replace_opt ::= OR REPLACE.

createkw(A) ::= CREATE(X) or_replace_opt.  {
  disableLookaside(pParse);
  A = X;
}
%endif
%ifndef MAXSCALE
createkw(A) ::= CREATE(X).  {
  disableLookaside(pParse);
  A = X;
}
%endif
%type ifnotexists {int}
ifnotexists(A) ::= .              {A = 0;}
ifnotexists(A) ::= IF NOT EXISTS. {A = 1;}
%type temp {int}
%ifndef SQLITE_OMIT_TEMPDB
temp(A) ::= TEMP.  {A = 1;}
%endif  SQLITE_OMIT_TEMPDB
temp(A) ::= .      {A = 0;}
create_table_args ::= LP columnlist conslist_opt(X) RP(E) table_options(F). {
#ifdef MAXSCALE
  mxs_sqlite3EndTable(pParse,&X,&E,F,0,0);
#else
  sqlite3EndTable(pParse,&X,&E,F,0);
#endif
}
%ifdef MAXSCALE
create_table_args ::= table_options(F) as_opt select(S). {
  mxs_sqlite3EndTable(pParse,0,0,F,S,0);
  sqlite3SelectDelete(pParse->db, S);
}

ignore_or_replace_opt ::= .
ignore_or_replace_opt ::= IGNORE.
ignore_or_replace_opt ::= REPLACE.

create_table_args ::= LP columnlist conslist_opt(X) RP(E) table_options(F)
                      ignore_or_replace_opt as_opt select(S). {
  mxs_sqlite3EndTable(pParse,&X,&E,F,S,0);
  sqlite3SelectDelete(pParse->db, S);
}

create_table_args ::= LIKE_KW fullname(X). {
  mxs_sqlite3EndTable(pParse,0,0,0,0,X);
}

%endif
%ifndef MAXSCALE
create_table_args ::= AS select(S). {
  sqlite3EndTable(pParse,0,0,0,S);
  sqlite3SelectDelete(pParse->db, S);
}
%endif
%type table_option {int}
%type table_options {int}
table_options(A) ::= .    {A = 0;}
%ifdef MAXSCALE
table_options(A) ::= table_options(B) table_option(C). {A = B|C;}
table_option(A) ::= ENGINE eq_opt nm. {A = 0;}
table_option(A) ::= AUTOINCR eq_opt INTEGER. {A = 0;}
table_option(A) ::= default_opt CHARSET eq_opt nm. {A = 0;}
table_option(A) ::= default_opt CHARACTER SET eq_opt nm. {A = 0;}
table_option(A) ::= COMMENT eq_opt STRING. {A = 0;}
table_option(A) ::= ID eq_opt DEFAULT. {A = 0;}
table_option(A) ::= ID eq_opt ID. {A = 0;}
table_option(A) ::= ID eq_opt INTEGER. {A = 0;}
table_option(A) ::= ID eq_opt STRING. {A = 0;}
table_option(A) ::= UNION eq LP fullnames(X) RP. {
  sqlite3SrcListDelete(pParse->db, X);
  A = 0;
}
%endif
%ifndef MAXSCALE
table_options(A) ::= WITHOUT nm(X). {
  if( X.n==5 && sqlite3_strnicmp(X.z,"rowid",5)==0 ){
    A = TF_WithoutRowid | TF_NoVisibleRowid;
  }else{
    A = 0;
    sqlite3ErrorMsg(pParse, "unknown table option: %.*s", X.n, X.z);
  }
}
%endif
columnlist ::= columnlist COMMA column.
columnlist ::= column.

// A "column" is a complete description of a single column in a
// CREATE TABLE statement.  This includes the column name, its
// datatype, and other keywords such as PRIMARY KEY, UNIQUE, REFERENCES,
// NOT NULL and so forth.
//
%ifdef MAXSCALE
column(A) ::= columnid(X) type type_options carglist. {
%endif
%ifndef MAXSCALE
column(A) ::= columnid(X) type carglist. {
%endif
  A.z = X.z;
  A.n = (int)(pParse->sLastToken.z-X.z) + pParse->sLastToken.n;
}

%ifdef MAXSCALE
fulltext_or_spatial ::= FULLTEXT.
fulltext_or_spatial ::= SPATIAL.

index_or_key ::= INDEX.
index_or_key ::= KEY.

index_or_key_opt ::= .
index_or_key_opt ::= index_or_key.

index_name_opt ::= .
index_name_opt ::= index_name.

index_type ::= USING deferred_id. // USING {BTREE|HASH}

index_type_opt ::= .
index_type_opt ::= index_type.

index_option ::= deferred_id INTEGER.          // KEY_BLOCK_SIZE valye
index_option ::= deferred_id eq INTEGER.       // KEY_BLOCK_SIZE = value
index_option ::= index_type.                   // USING {BTREE|HASH}
index_option ::= WITH deferred_id deferred_id. // WITH PARSER parser_name.
index_option ::= COMMENT STRING.               // COMMENT 'string'

index_option_opt ::= .
index_option_opt ::= index_option.

// PRIMARY KEY [index_option] (index_col_name, ...)
column(A) ::= PRIMARY KEY index_option_opt LP index_type_opt sortlist(X) RP index_option_opt. {
  sqlite3ExprListDelete(pParse->db, X);
  A.z = 0;
  A.n = 0;
}

// {INDEX|KEY} [index_name] [index_type] (index_col_name, ...) [index_option]
column(A) ::= index_or_key index_name_opt index_type_opt LP sortlist(X) RP  index_option_opt. {
  sqlite3ExprListDelete(pParse->db, X);
  A.z = 0;
  A.n = 0;
}

// UNIQUE [INDEX|KEY] [index_name] (index_col_name, ...) [index_option]
column(A) ::= UNIQUE index_or_key_opt index_name_opt LP sortlist(X) RP index_option_opt. {
  sqlite3ExprListDelete(pParse->db, X);
  A.z = 0;
  A.n = 0;
}

// {FULLTEXT|SPATIAL} [INDEX|KEY] [index_name] (index_col_name, ...)  [index_option]
column(A) ::= fulltext_or_spatial index_or_key_opt index_name_opt LP sortlist(X) RP index_option_opt. {
  sqlite3ExprListDelete(pParse->db, X);
  A.z = 0;
  A.n = 0;
}

// FOREIGN KEY [index_name] LP index_col_name, ... RP reference_definition
column(A) ::= FOREIGN KEY LP eidlist(FA) RP
          REFERENCES nm(T) eidlist_opt(TA) refargs(R) defer_subclause_opt(D). {
    sqlite3CreateForeignKey(pParse, FA, &T, TA, R);
    sqlite3DeferForeignKey(pParse, D);
    A.z = 0;
    A.n = 0;
}

// CHECK (expr)
column(A) ::= CHECK LP expr(X) RP. {
  sqlite3AddCheckConstraint(pParse,X.pExpr);
  A.z = 0;
  A.n = 0;
}

%endif

%ifdef MAXSCALE
columnid(A) ::= nm(X). {
  sqlite3AddColumn(pParse,&X);
  A = X;
  pParse->constraintName.n = 0;
}
columnid(A) ::= nm DOT nm(X). {
  sqlite3AddColumn(pParse,&X);
  A = X;
  pParse->constraintName.n = 0;
}
columnid(A) ::= nm DOT nm DOT nm(X). {
  sqlite3AddColumn(pParse,&X);
  A = X;
  pParse->constraintName.n = 0;
}
%endif
%ifndef MAXSCALE
columnid(A) ::= nm(X). {
  sqlite3AddColumn(pParse,&X);
  A = X;
  pParse->constraintName.n = 0;
}
%endif

// An IDENTIFIER can be a generic identifier, or one of several
// keywords.  Any non-standard keyword can also be an identifier.
//
%token_class id  ID|INDEXED.

// The following directive causes tokens ABORT, AFTER, ASC, etc. to
// fallback to ID if they will not parse as their original value.
// This obviates the need for the "id" nonterminal.
//
%fallback ID
%ifndef MAXSCALE
  ABORT ACTION AFTER ANALYZE ASC ATTACH BEFORE BEGIN BY CASCADE CAST COLUMNKW
  CONFLICT DATABASE DEFERRED DESC DETACH EACH END EXCLUSIVE EXPLAIN FAIL FOR
  IGNORE IMMEDIATE INITIALLY INSTEAD LIKE_KW MATCH NO PLAN
  QUERY KEY OF OFFSET PRAGMA RAISE RECURSIVE RELEASE REPLACE RESTRICT ROW
  ROLLBACK SAVEPOINT TEMP TRIGGER VACUUM VIEW VIRTUAL WITH WITHOUT
%ifdef SQLITE_OMIT_COMPOUND_SELECT
  EXCEPT INTERSECT UNION
%endif SQLITE_OMIT_COMPOUND_SELECT
  REINDEX RENAME CTIME_KW IF
%endif
%ifdef MAXSCALE
  /*ABORT*/ ACTION AFTER ALGORITHM /*ANALYZE*/ /*ASC*/ /*ATTACH*/
  /*BEFORE*/ /*BEGIN*/ BY
  // TODO: BINARY is a reserved word and should not automatically convert into an identifer.
  // TODO: However, if not here then rules such as CAST need to be modified.
  BINARY
  /*CASCADE*/ CAST CLOSE COLUMNKW COLUMNS COMMENT CONCURRENT /*CONFLICT*/
  DATA /*DATABASE*/ DEALLOCATE DEFERRED /*DESC*/ /*DETACH*/ DUMPFILE
  /*EACH*/ END ENGINE ENUM EXCLUSIVE /*EXPLAIN*/
  FIRST FLUSH /*FOR*/ FORMAT
  GLOBAL
  // TODO: IF is a reserved word and should not automatically convert into an identifer.
  IF IMMEDIATE INITIALLY INSTEAD
  /*KEY*/
  /*LIKE_KW*/
  MASTER /*MATCH*/ MERGE
  NAMES NEXT
  NO
  OF OFFSET OPEN
  PREVIOUS
  QUICK
  RAISE RECURSIVE /*REINDEX*/ RELEASE /*RENAME*/ /*REPLACE*/ RESTRICT ROLLBACK ROLLUP ROW
  SAVEPOINT SELECT_OPTIONS_KW /*SEQUENCE*/ SLAVE /*START*/ STATEMENT STATUS
  TABLES TEMP TEMPTABLE /*TRIGGER*/
  /*TRUNCATE*/
  // TODO: UNSIGNED is a reserved word and should not automatically convert into an identifer.
  // TODO: However, if not here then rules such as CAST need to be modified.
  UNSIGNED
  VALUE VIEW /*VIRTUAL*/
  /*WITH*/
  WORK
%endif
  .
%wildcard ANY.

// Define operator precedence early so that this is the first occurrence
// of the operator tokens in the grammer.  Keeping the operators together
// causes them to be assigned integer values that are close together,
// which keeps parser tables smaller.
//
// The token values assigned to these symbols is determined by the order
// in which lemon first sees them.  It must be the case that ISNULL/NOTNULL,
// NE/EQ, GT/LE, and GE/LT are separated by only a single value.  See
// the sqlite3ExprIfFalse() routine for additional information on this
// constraint.
//
%left OR.
%left AND.
%right NOT.
%left IS MATCH LIKE_KW BETWEEN IN ISNULL NOTNULL NE EQ.
%left GT LE LT GE.
%right ESCAPE.
%left BITAND BITOR LSHIFT RSHIFT.
%left PLUS MINUS.
%left STAR SLASH REM.
%left CONCAT.
%left COLLATE.
%right BITNOT.

%ifdef MAXSCALE
// We need EQ in engine_opt up there, where CREATE is defined. However, as the
// value of a token is defined by the point the token is seen, EQ must not be
// used before the precedence is declared above where EQ and friends are
// declared in one go.
eq ::= EQ.
%endif

// And "ids" is an identifer-or-string.
//
%token_class ids  ID|STRING.

// The name of a column or table can be any of the following:
//
%type nm {Token}
nm(A) ::= id(X).         {A = X;}
nm(A) ::= STRING(X).     {A = X;}
nm(A) ::= JOIN_KW(X).    {A = X;}
nm(A) ::= START(X).      {A = X;}
nm(A) ::= TRUNCATE(X).   {A = X;}
nm(A) ::= BEGIN(X).      {A = X;}
nm(A) ::= REPLACE(X).    {A = X;}

// A typetoken is really one or more tokens that form a type name such
// as can be found after the column name in a CREATE TABLE statement.
// Multiple tokens are concatenated to form the value of the typetoken.
//
%type typetoken {Token}
type ::= .
type ::= typetoken(X).                   {sqlite3AddColumnType(pParse,&X);}
%ifdef MAXSCALE
enumnames ::= STRING.
enumnames ::= enumnames COMMA STRING.

type ::= ENUM LP enumnames RP.
type ::= SET LP enumnames RP.
%endif
typetoken(A) ::= typename(X).   {A = X;}
typetoken(A) ::= typename(X) LP signed RP(Y). {
  A.z = X.z;
  A.n = (int)(&Y.z[Y.n] - X.z);
}
typetoken(A) ::= typename(X) LP signed COMMA signed RP(Y). {
  A.z = X.z;
  A.n = (int)(&Y.z[Y.n] - X.z);
}
%type typename {Token}
typename(A) ::= ids(X).             {A = X;}
typename(A) ::= typename(X) ids(Y). {A.z=X.z; A.n=Y.n+(int)(Y.z-X.z);}
signed ::= plus_num.
signed ::= minus_num.

// "carglist" is a list of additional constraints that come after the
// column name and column type in a CREATE TABLE statement.
//
%ifdef MAXSCALE
carglist ::= .
carglist ::= cconslist.

virtual_or_persistent ::= VIRTUAL.
virtual_or_persistent ::= PERSISTENT.

unique_key ::= .
unique_key ::= UNIQUE.
unique_key ::= UNIQUE KEY.

comment_string_opt ::= .
comment_string_opt ::= COMMENT STRING.

carglist ::= AS LP expr(X) RP virtual_or_persistent unique_key comment_string_opt. {
  sqlite3ExprDelete(pParse->db, X.pExpr);
}

cconslist ::= ccons.
cconslist ::= cconslist ccons.
%endif
%ifndef MAXSCALE
carglist ::= carglist ccons.
carglist ::= .
%endif
%ifndef MAXSCALE
ccons ::= CONSTRAINT nm(X).           {pParse->constraintName = X;}
%endif
ccons ::= DEFAULT term(X).            {sqlite3AddDefaultValue(pParse,&X);}
ccons ::= DEFAULT LP expr(X) RP.      {sqlite3AddDefaultValue(pParse,&X);}
ccons ::= DEFAULT PLUS term(X).       {sqlite3AddDefaultValue(pParse,&X);}
ccons ::= DEFAULT MINUS(A) term(X).      {
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_UMINUS, X.pExpr, 0, 0);
  v.zStart = A.z;
  v.zEnd = X.zEnd;
  sqlite3AddDefaultValue(pParse,&v);
}
ccons ::= DEFAULT id(X).              {
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, &X);
  sqlite3AddDefaultValue(pParse,&v);
}
%ifdef MAXSCALE
ccons ::= DEFAULT id(X) LP RP. {
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, &X);
  sqlite3AddDefaultValue(pParse,&v);
}
%endif

// In addition to the type name, we also care about the primary key and
// UNIQUE constraints.
//
%ifdef MAXSCALE
ccons ::= AUTOINCR.
%endif
ccons ::= NULL onconf.
ccons ::= NOT NULL onconf(R).    {sqlite3AddNotNull(pParse, R);}
%ifndef MAXSCALE
ccons ::= PRIMARY KEY sortorder(Z) onconf(R) autoinc(I).
                                 {sqlite3AddPrimaryKey(pParse,0,R,I,Z);}
%endif
%ifdef MAXSCALE
// The addition of AUTOINCR above leads to conflicts if autoinc(I)
// follows onconf(R).
primary_opt ::= .
primary_opt ::= PRIMARY.

ccons ::= primary_opt KEY onconf(R).
                                 {sqlite3AddPrimaryKey(pParse,0,R,0,0);}
%endif
ccons ::= UNIQUE onconf(R).      {sqlite3CreateIndex(pParse,0,0,0,0,R,0,0,0,0);}
%ifndef MAXSCALE
ccons ::= CHECK LP expr(X) RP.   {sqlite3AddCheckConstraint(pParse,X.pExpr);}
// TODO: Following rule conflicts with "ccons ::= ON UPDATE ..." below.
ccons ::= REFERENCES nm(T) eidlist_opt(TA) refargs(R).
                                 {sqlite3CreateForeignKey(pParse,0,&T,TA,R);}
%endif
ccons ::= defer_subclause(D).    {sqlite3DeferForeignKey(pParse,D);}
ccons ::= COLLATE ids(C).        {sqlite3AddCollateType(pParse, &C);}
%ifdef MAXSCALE
reference_option ::= RESTRICT.
reference_option ::= CASCADE.
reference_option ::= SET NULL.
reference_option ::= NO ACTION.
reference_option ::= CTIME_KW.

// TODO: ccons ::= [MATCH FULL | MATCH PARTIAL | MATCH SIMPLE]
ccons ::= ON DELETE reference_option.
ccons ::= ON UPDATE reference_option.
ccons ::= ON UPDATE id LP RP.
%endif

// The optional AUTOINCREMENT keyword
%ifndef MAXSCALE
%type autoinc {int}
autoinc(X) ::= .          {X = 0;}
autoinc(X) ::= AUTOINCR.  {X = 1;}
%endif

// The next group of rules parses the arguments to a REFERENCES clause
// that determine if the referential integrity checking is deferred or
// or immediate and which determine what action to take if a ref-integ
// check fails.
//
%type refargs {int}
refargs(A) ::= .                  { A = OE_None*0x0101; /* EV: R-19803-45884 */}
refargs(A) ::= refargs(X) refarg(Y). { A = (X & ~Y.mask) | Y.value; }
%type refarg {struct {int value; int mask;}}
refarg(A) ::= MATCH nm.              { A.value = 0;     A.mask = 0x000000; }
refarg(A) ::= ON INSERT refact.      { A.value = 0;     A.mask = 0x000000; }
refarg(A) ::= ON DELETE refact(X).   { A.value = X;     A.mask = 0x0000ff; }
refarg(A) ::= ON UPDATE refact(X).   { A.value = X<<8;  A.mask = 0x00ff00; }
%type refact {int}
refact(A) ::= SET NULL.              { A = OE_SetNull;  /* EV: R-33326-45252 */}
refact(A) ::= SET DEFAULT.           { A = OE_SetDflt;  /* EV: R-33326-45252 */}
refact(A) ::= CASCADE.               { A = OE_Cascade;  /* EV: R-33326-45252 */}
refact(A) ::= RESTRICT.              { A = OE_Restrict; /* EV: R-33326-45252 */}
refact(A) ::= NO ACTION.             { A = OE_None;     /* EV: R-33326-45252 */}
%type defer_subclause {int}
defer_subclause(A) ::= NOT DEFERRABLE init_deferred_pred_opt.     {A = 0;}
defer_subclause(A) ::= DEFERRABLE init_deferred_pred_opt(X).      {A = X;}
%type init_deferred_pred_opt {int}
init_deferred_pred_opt(A) ::= .                       {A = 0;}
init_deferred_pred_opt(A) ::= INITIALLY DEFERRED.     {A = 1;}
init_deferred_pred_opt(A) ::= INITIALLY IMMEDIATE.    {A = 0;}

conslist_opt(A) ::= .                         {A.n = 0; A.z = 0;}
conslist_opt(A) ::= COMMA(X) conslist.        {A = X;}
conslist ::= conslist tconscomma tcons.
conslist ::= tcons.
tconscomma ::= COMMA.            {pParse->constraintName.n = 0;}
tconscomma ::= .
tcons ::= CONSTRAINT nm(X).      {pParse->constraintName = X;}
%ifndef MAXSCALE
tcons ::= PRIMARY KEY LP sortlist(X) autoinc(I) RP onconf(R).
                                 {sqlite3AddPrimaryKey(pParse,X,R,I,0);}
tcons ::= UNIQUE LP sortlist(X) RP onconf(R).
                                 {sqlite3CreateIndex(pParse,0,0,0,X,R,0,0,0,0);}
tcons ::= CHECK LP expr(E) RP onconf.
                                 {sqlite3AddCheckConstraint(pParse,E.pExpr);}
tcons ::= FOREIGN KEY LP eidlist(FA) RP
          REFERENCES nm(T) eidlist_opt(TA) refargs(R) defer_subclause_opt(D). {
    sqlite3CreateForeignKey(pParse, FA, &T, TA, R);
    sqlite3DeferForeignKey(pParse, D);
}
%endif
%type defer_subclause_opt {int}
defer_subclause_opt(A) ::= .                    {A = 0;}
defer_subclause_opt(A) ::= defer_subclause(X).  {A = X;}

// The following is a non-standard extension that allows us to declare the
// default behavior when there is a constraint conflict.
//
%type onconf {int}
%type orconf {int}
%ifdef MAXSCALE
onconf ::= .
orconf ::= .
%endif
%ifndef MAXSCALE
%type resolvetype {int}
onconf(A) ::= .                              {A = OE_Default;}
onconf(A) ::= ON CONFLICT resolvetype(X).    {A = X;}
orconf(A) ::= .                              {A = OE_Default;}
orconf(A) ::= OR resolvetype(X).             {A = X;}
resolvetype(A) ::= raisetype(X).             {A = X;}
resolvetype(A) ::= IGNORE.                   {A = OE_Ignore;}
resolvetype(A) ::= REPLACE.                  {A = OE_Replace;}
%endif

////////////////////////// The DROP TABLE /////////////////////////////////////
//
%ifdef MAXSCALE
table_or_tables ::= TABLE.
table_or_tables ::= TABLES.

cmd ::= DROP temp(T) table_or_tables ifexists(E) fullnames(X). {
  mxs_sqlite3DropTable(pParse, X, 0, E, T);
}
%endif
%ifndef MAXSCALE
cmd ::= DROP TABLE ifexists(E) fullname(X). {
  sqlite3DropTable(pParse, X, 0, E);
}
%endif
%type ifexists {int}
ifexists(A) ::= IF EXISTS.   {A = 1;}
ifexists(A) ::= .            {A = 0;}

///////////////////// The CREATE VIEW statement /////////////////////////////
//
%ifndef SQLITE_OMIT_VIEW
%ifdef MAXSCALE
%type algorithm {int}
algorithm(A) ::= UNDEFINED. {A=0;}
algorithm(A) ::= MERGE. {A=0;}
algorithm(A) ::= TEMPTABLE. {A=1;}

%type algorithm_opt {int}
algorithm_opt(A) ::= . {A=0;}
algorithm_opt(A) ::= ALGORITHM EQ algorithm(X). {A=X;}

cmd ::= createkw(X) algorithm_opt(T) VIEW ifnotexists(E) nm(Y) dbnm(Z) eidlist_opt(C)
          AS select(S). {
  mxs_sqlite3CreateView(pParse, &X, &Y, &Z, C, S, T, E);
  sqlite3SelectDelete(pParse->db, S);
}
%endif
%ifndef MAXSCALE
cmd ::= createkw(X) temp(T) VIEW ifnotexists(E) nm(Y) dbnm(Z) eidlist_opt(C)
          AS select(S). {
  sqlite3CreateView(pParse, &X, &Y, &Z, C, S, T, E);
}
%endif

%ifdef MAXSCALE
cmd ::= DROP VIEW ifexists(E) fullnames(X). {
  mxs_sqlite3DropTable(pParse, X, 1, E, 0);
}
%endif
%ifndef MAXSCALE
cmd ::= DROP VIEW ifexists(E) fullname(X). {
  sqlite3DropTable(pParse, X, 1, E);
}
%endif
%endif  SQLITE_OMIT_VIEW

//////////////////////// The SELECT statement /////////////////////////////////
//
cmd ::= select(X).  {
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0};
#ifdef MAXSCALE
  mxs_sqlite3Select(pParse, X, &dest);
#else
  sqlite3Select(pParse, X, &dest);
#endif
  sqlite3SelectDelete(pParse->db, X);
}

%type select {Select*}
%destructor select {sqlite3SelectDelete(pParse->db, $$);}
%type selectnowith {Select*}
%destructor selectnowith {sqlite3SelectDelete(pParse->db, $$);}
%type oneselect {Select*}
%destructor oneselect {sqlite3SelectDelete(pParse->db, $$);}

%include {
  /*
  ** For a compound SELECT statement, make sure p->pPrior->pNext==p for
  ** all elements in the list.  And make sure list length does not exceed
  ** SQLITE_LIMIT_COMPOUND_SELECT.
  */
  static void parserDoubleLinkSelect(Parse *pParse, Select *p){
    if( p->pPrior ){
      Select *pNext = 0, *pLoop;
      int mxSelect, cnt = 0;
      for(pLoop=p; pLoop; pNext=pLoop, pLoop=pLoop->pPrior, cnt++){
        pLoop->pNext = pNext;
        pLoop->selFlags |= SF_Compound;
      }
      if( (p->selFlags & SF_MultiValue)==0 && 
        (mxSelect = pParse->db->aLimit[SQLITE_LIMIT_COMPOUND_SELECT])>0 &&
        cnt>mxSelect
      ){
        sqlite3ErrorMsg(pParse, "too many terms in compound SELECT");
      }
    }
  }
}

select(A) ::= with(W) selectnowith(X). {
  Select *p = X;
  if( p ){
    p->pWith = W;
    parserDoubleLinkSelect(pParse, p);
  }else{
    sqlite3WithDelete(pParse->db, W);
  }
  A = p;
}

selectnowith(A) ::= oneselect(X).                      {A = X;}
%ifndef SQLITE_OMIT_COMPOUND_SELECT
selectnowith(A) ::= selectnowith(X) multiselect_op(Y) oneselect(Z).  {
  Select *pRhs = Z;
  Select *pLhs = X;
  if( pRhs && pRhs->pPrior ){
    SrcList *pFrom;
    Token x;
    x.n = 0;
    parserDoubleLinkSelect(pParse, pRhs);
    pFrom = sqlite3SrcListAppendFromTerm(pParse,0,0,0,&x,pRhs,0,0);
#ifdef MAXSCALE
    pRhs = sqlite3SelectNew(pParse,0,pFrom,0,0,0,0,0,0,0,0);
#else
    pRhs = sqlite3SelectNew(pParse,0,pFrom,0,0,0,0,0,0,0);
#endif
  }
  if( pRhs ){
    pRhs->op = (u8)Y;
    pRhs->pPrior = pLhs;
    if( ALWAYS(pLhs) ) pLhs->selFlags &= ~SF_MultiValue;
    pRhs->selFlags &= ~SF_MultiValue;
    if( Y!=TK_ALL ) pParse->hasCompound = 1;
  }else{
    sqlite3SelectDelete(pParse->db, pLhs);
  }
  A = pRhs;
}
%type multiselect_op {int}
multiselect_op(A) ::= UNION(OP).             {A = @OP;}
multiselect_op(A) ::= UNION ALL.             {A = TK_ALL;}
multiselect_op(A) ::= EXCEPT|INTERSECT(OP).  {A = @OP;}
%endif SQLITE_OMIT_COMPOUND_SELECT
%ifdef MAXSCALE

wf_window_name ::= id.

wf_window_ref_opt ::= .
wf_window_ref_opt ::= id.

wf_window_spec ::= LP wf_window_ref_opt wf_partition_by_opt wf_order_by_opt wf_frame_opt RP.

wf_window_def ::= wf_window_name AS wf_window_spec.

wf_window_def_list ::= wf_window_def_list COMMA wf_window_def.
wf_window_def_list ::= wf_window_def.

wf_window ::= WINDOW wf_window_def_list.
wf_window ::= WINDOW LP RP.

wf_window_opt ::= .
wf_window_opt ::= wf_window.

oneselect(A) ::= SELECT select_options(D) selcollist(W) select_into_opt(I1) from(X) where_opt(Y)
                 groupby_opt(P) having_opt(Q) wf_window_opt orderby_opt(Z) limit_opt(L)
                 select_into_opt(I2). {
  if (!I1) {
    I1=I2;
  }
  A = sqlite3SelectNew(pParse,W,X,Y,P,Q,Z,D,L.pLimit,L.pOffset,I1);
}
oneselect(A) ::= SELECT select_options(D) selcollist(W) select_into(I). {
  A = sqlite3SelectNew(pParse,W,0,0,0,0,0,D,0,0,I);
}
oneselect(A) ::= SELECT select_options(D) selcollist(W) orderby_opt(Z) limit_opt(L). {
  A = sqlite3SelectNew(pParse,W,0,0,0,0,Z,D,L.pLimit,L.pOffset,0);
}
%endif
%ifndef MAXSCALE
oneselect(A) ::= SELECT(S) distinct(D) selcollist(W) from(X) where_opt(Y)
                 groupby_opt(P) having_opt(Q) orderby_opt(Z) limit_opt(L). {
  A = sqlite3SelectNew(pParse,W,X,Y,P,Q,Z,D,L.pLimit,L.pOffset);
#if SELECTTRACE_ENABLED
  /* Populate the Select.zSelName[] string that is used to help with
  ** query planner debugging, to differentiate between multiple Select
  ** objects in a complex query.
  **
  ** If the SELECT keyword is immediately followed by a C-style comment
  ** then extract the first few alphanumeric characters from within that
  ** comment to be the zSelName value.  Otherwise, the label is #N where
  ** is an integer that is incremented with each SELECT statement seen.
  */
  if( A!=0 ){
    const char *z = S.z+6;
    int i;
    sqlite3_snprintf(sizeof(A->zSelName), A->zSelName, "#%d",
                     ++pParse->nSelect);
    while( z[0]==' ' ) z++;
    if( z[0]=='/' && z[1]=='*' ){
      z += 2;
      while( z[0]==' ' ) z++;
      for(i=0; sqlite3Isalnum(z[i]); i++){}
      sqlite3_snprintf(sizeof(A->zSelName), A->zSelName, "%.*s", i, z);
    }
  }
#endif /* SELECTRACE_ENABLED */
}
%endif
oneselect(A) ::= values(X).    {A = X;}

%type values {Select*}
%destructor values {sqlite3SelectDelete(pParse->db, $$);}
%ifdef MAXSCALE
value_or_values ::= VALUE.
value_or_values ::= VALUES.

values(A) ::= value_or_values LP exprlist(X) RP. {
  A = sqlite3SelectNew(pParse,X,0,0,0,0,0,SF_Values,0,0,0);
}
%endif
%ifndef MAXSCALE
values(A) ::= VALUES LP nexprlist(X) RP. {
  A = sqlite3SelectNew(pParse,X,0,0,0,0,0,SF_Values,0,0);
}
%endif
values(A) ::= values(X) COMMA LP exprlist(Y) RP. {
  Select *pRight, *pLeft = X;
#ifdef MAXSCALE
  pRight = sqlite3SelectNew(pParse,Y,0,0,0,0,0,SF_Values|SF_MultiValue,0,0,0);
#else
  pRight = sqlite3SelectNew(pParse,Y,0,0,0,0,0,SF_Values|SF_MultiValue,0,0);
#endif
  if( ALWAYS(pLeft) ) pLeft->selFlags &= ~SF_MultiValue;
  if( pRight ){
    pRight->op = TK_ALL;
    pLeft = X;
    pRight->pPrior = pLeft;
    A = pRight;
  }else{
    A = pLeft;
  }
}

%ifdef MAXSCALE
%type variables {ExprList*}
%destructor variables {sqlite3ExprListDelete(pParse->db, $$);}

variables(A) ::= VARIABLE(X). {
      A = sqlite3ExprListAppend(pParse, NULL, 0);
      sqlite3ExprListSetName(pParse, A, &X, 1);
}
variables(A) ::= variables(X) COMMA VARIABLE(Y). {
      A = sqlite3ExprListAppend(pParse, X, 0);
      sqlite3ExprListSetName(pParse, A, &Y, 1);
}

%type select_into_opt {ExprList*}
%destructor select_into_opt {sqlite3ExprListDelete(pParse->db, $$);}
select_into_opt(A) ::= . {A = 0;}
select_into_opt(A) ::= select_into(X). {A = X;}

%type select_into {ExprList*}
%destructor select_into {sqlite3ExprListDelete(pParse->db, $$);}
select_into(A) ::= INTO variables(X). {A = X;}
select_into(A) ::= INTO DUMPFILE STRING. {A = sqlite3ExprListAppend(pParse, 0, 0);}
select_into(A) ::= INTO OUTFILE STRING. {A = sqlite3ExprListAppend(pParse, 0, 0);}

%type select_options {int}
select_options(A) ::= . {A = 0;}
select_options(A) ::= select_options DISTINCT. {A = SF_Distinct;}
select_options(A) ::= select_options UNIQUE. {A = SF_Distinct;}
select_options(A) ::= select_options ALL. {A = SF_All;}
select_options(A) ::= select_options(X) HIGH_PRIORITY. {A = X;}
select_options(A) ::= select_options(X) SELECT_OPTIONS_KW. {A = X;}
select_options(A) ::= select_options(X) STRAIGHT_JOIN. {A = X;}
%endif
// The "distinct" nonterminal is true (1) if the DISTINCT keyword is
// present and false (0) if it is not.
//
%type distinct {int}
%ifdef MAXSCALE
distinct(A) ::= UNIQUE.     {A = SF_Distinct;}
%endif
distinct(A) ::= DISTINCT.   {A = SF_Distinct;}
distinct(A) ::= ALL.        {A = SF_All;}
distinct(A) ::= .           {A = 0;}

// selcollist is a list of expressions that are to become the return
// values of the SELECT statement.  The "*" in statements like
// "SELECT * FROM ..." is encoded as a special expression with an
// opcode of TK_ASTERISK.
//
%type selcollist {ExprList*}
%destructor selcollist {sqlite3ExprListDelete(pParse->db, $$);}
%type sclp {ExprList*}
%destructor sclp {sqlite3ExprListDelete(pParse->db, $$);}
sclp(A) ::= selcollist(X) COMMA.             {A = X;}
sclp(A) ::= .                                {A = 0;}
selcollist(A) ::= sclp(P) expr(X) as(Y).     {
   A = sqlite3ExprListAppend(pParse, P, X.pExpr);
   if( Y.n>0 ) sqlite3ExprListSetName(pParse, A, &Y, 1);
   sqlite3ExprListSetSpan(pParse,A,&X);
}
selcollist(A) ::= sclp(P) STAR. {
  Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
  A = sqlite3ExprListAppend(pParse, P, p);
}
selcollist(A) ::= sclp(P) nm(X) DOT STAR(Y). {
  Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0, &Y);
  Expr *pLeft = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight, 0);
  A = sqlite3ExprListAppend(pParse,P, pDot);
}
%ifdef MAXSCALE
next_or_previous(A) ::= NEXT(X). {A = X;}
next_or_previous(A) ::= PREVIOUS(X). {A = X;}

selcollist(A) ::= sclp(P) next_or_previous VALUE FOR nm(X) as(Y).     {
  Expr* pSeq = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  ExprList* pArgs = sqlite3ExprListAppend(pParse, NULL, pSeq);
  Token nextval = { "nextval", 7 };
  Expr* pFunc = sqlite3ExprFunction(pParse, pArgs, &nextval);
  if( Y.n>0 ) sqlite3ExprListSetName(pParse, A, &Y, 1);
  A = sqlite3ExprListAppend(pParse, P, pFunc);
}
selcollist(A) ::= sclp(P) DEFAULT LP nm RP as. {
  A = P;
}
selcollist(A) ::= sclp(P) MATCH LP id(X) RP AGAINST LP expr(Y) RP. {
  // Could be a subselect as well, but we just don't know it at this point.
  sqlite3ExprDelete(pParse->db, Y.pExpr);
  Expr *p = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  maxscale_update_function_info("match", p);
  A = sqlite3ExprListAppend(pParse, P, p);
}
%endif


// An option "AS <id>" phrase that can follow one of the expressions that
// define the result set, or one of the tables in the FROM clause.
//
%type as {Token}
as(X) ::= AS nm(Y).    {X = Y;}
as(X) ::= ids(Y).      {X = Y;}
as(X) ::= .            {X.n = 0;}


%type seltablist {SrcList*}
%destructor seltablist {sqlite3SrcListDelete(pParse->db, $$);}
%type stl_prefix {SrcList*}
%destructor stl_prefix {sqlite3SrcListDelete(pParse->db, $$);}
%type from {SrcList*}
%destructor from {sqlite3SrcListDelete(pParse->db, $$);}

// A complete FROM clause.
//
%ifndef MAXSCALE
from(A) ::= .                {A = sqlite3DbMallocZero(pParse->db, sizeof(*A));}
%endif
from(A) ::= FROM seltablist(X). {
  A = X;
  sqlite3SrcListShiftJoinType(A);
}
%endif

// "seltablist" is a "Select Table List" - the content of the FROM clause
// in a SELECT statement.  "stl_prefix" is a prefix of this list.
//
stl_prefix(A) ::= seltablist(X) joinop(Y).    {
   A = X;
   if( ALWAYS(A && A->nSrc>0) ) A->a[A->nSrc-1].fg.jointype = (u8)Y;
}
stl_prefix(A) ::= .                           {A = 0;}
seltablist(A) ::= stl_prefix(X) nm(Y) dbnm(D) as(Z) indexed_opt(I)
                  on_opt(N) using_opt(U). {
  A = sqlite3SrcListAppendFromTerm(pParse,X,&Y,&D,&Z,0,N,U);
  sqlite3SrcListIndexedBy(pParse, A, &I);
}
%ifdef MAXSCALE
// as(X) above cannot be used, since it is used in other contexts as well.
seltablist(A) ::= stl_prefix(X) nm(Y) dbnm(D) EQ nm(Z) indexed_opt(I)
                  on_opt(N) using_opt(U). {
  A = sqlite3SrcListAppendFromTerm(pParse,X,&Y,&D,&Z,0,N,U);
  sqlite3SrcListIndexedBy(pParse, A, &I);
}
%endif
seltablist(A) ::= stl_prefix(X) nm(Y) dbnm(D) LP exprlist(E) RP as(Z)
                  on_opt(N) using_opt(U). {
  A = sqlite3SrcListAppendFromTerm(pParse,X,&Y,&D,&Z,0,N,U);
  sqlite3SrcListFuncArgs(pParse, A, E);
}
%ifndef SQLITE_OMIT_SUBQUERY
  seltablist(A) ::= stl_prefix(X) LP select(S) RP
                    as(Z) on_opt(N) using_opt(U). {
    A = sqlite3SrcListAppendFromTerm(pParse,X,0,0,&Z,S,N,U);
  }
  seltablist(A) ::= stl_prefix(X) LP seltablist(F) RP
                    as(Z) on_opt(N) using_opt(U). {
    if( X==0 && Z.n==0 && N==0 && U==0 ){
      A = F;
    }else if( F->nSrc==1 ){
      A = sqlite3SrcListAppendFromTerm(pParse,X,0,0,&Z,0,N,U);
      if( A ){
        struct SrcList_item *pNew = &A->a[A->nSrc-1];
        struct SrcList_item *pOld = F->a;
        pNew->zName = pOld->zName;
        pNew->zDatabase = pOld->zDatabase;
        pNew->pSelect = pOld->pSelect;
        pOld->zName = pOld->zDatabase = 0;
        pOld->pSelect = 0;
      }
      sqlite3SrcListDelete(pParse->db, F);
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(F);
#ifdef MAXSCALE
      pSubquery = sqlite3SelectNew(pParse,0,F,0,0,0,0,SF_NestedFrom,0,0,0);
#else
      pSubquery = sqlite3SelectNew(pParse,0,F,0,0,0,0,SF_NestedFrom,0,0);
#endif
      A = sqlite3SrcListAppendFromTerm(pParse,X,0,0,&Z,pSubquery,N,U);
    }
  }
%endif  SQLITE_OMIT_SUBQUERY

%type dbnm {Token}
dbnm(A) ::= .          {A.z=0; A.n=0;}
dbnm(A) ::= DOT nm(X). {A = X;}

%type fullname {SrcList*}
%destructor fullname {sqlite3SrcListDelete(pParse->db, $$);}
fullname(A) ::= nm(X) dbnm(Y).  {A = sqlite3SrcListAppend(pParse->db,0,&X,&Y);}

%ifdef MAXSCALE
%type fullnames {SrcList*}
%destructor fullnames {sqlite3SrcListDelete(pParse->db, $$);}
fullnames(A) ::= fullname(X). {A = X;}
fullnames(A) ::= fullnames(W) COMMA nm(X) dbnm(Y). {
    A = sqlite3SrcListAppend(pParse->db, W, &X, &Y);
}
%endif

%type joinop {int}
joinop(X) ::= COMMA|JOIN.              { X = JT_INNER; }
joinop(X) ::= JOIN_KW(A) JOIN.         { X = sqlite3JoinType(pParse,&A,0,0); }
joinop(X) ::= JOIN_KW(A) nm(B) JOIN.   { X = sqlite3JoinType(pParse,&A,&B,0); }
joinop(X) ::= JOIN_KW(A) nm(B) nm(C) JOIN.
                                       { X = sqlite3JoinType(pParse,&A,&B,&C); }
%ifdef MAXSCALE
joinop(X) ::= join_opt STRAIGHT_JOIN.  { X = JT_INNER; }
%endif

%type on_opt {Expr*}
%destructor on_opt {sqlite3ExprDelete(pParse->db, $$);}
on_opt(N) ::= ON expr(E).   {N = E.pExpr;}
on_opt(N) ::= .             {N = 0;}

// Note that this block abuses the Token type just a little. If there is
// no "INDEXED BY" clause, the returned token is empty (z==0 && n==0). If
// there is an INDEXED BY clause, then the token is populated as per normal,
// with z pointing to the token data and n containing the number of bytes
// in the token.
//
// If there is a "NOT INDEXED" clause, then (z==0 && n==1), which is 
// normally illegal. The sqlite3SrcListIndexedBy() function 
// recognizes and interprets this as a special case.
//
%type indexed_opt {Token}
indexed_opt(A) ::= .                 {A.z=0; A.n=0;}
%ifndef MAXSCALE
indexed_opt(A) ::= INDEXED BY nm(X). {A = X;}
indexed_opt(A) ::= NOT INDEXED.      {A.z=0; A.n=1;}
%endif
%ifdef MAXSCALE
uif ::= USE|IGNORE|FORCE.

jog ::= JOIN|ORDER BY|GROUP BY.

for_jog ::= .
for_jog ::= FOR jog.

index_name ::= id.
index_name ::= PRIMARY.

index_list ::= index_name.
index_list ::= index_list COMMA index_name.

index_hint ::= uif index_or_key for_jog LP index_list RP.

index_hint_list ::= index_hint.
// TODO: index_hint_list ::= index_hint_list COMMA index_hint.
// TODO: Causes parsing conflict.

indexed_opt(A) ::= index_hint_list. {A.z=0; A.n=0;}
%endif

%type using_opt {IdList*}
%destructor using_opt {sqlite3IdListDelete(pParse->db, $$);}
using_opt(U) ::= USING LP idlist(L) RP.  {U = L;}
using_opt(U) ::= .                        {U = 0;}


%type orderby_opt {ExprList*}
%destructor orderby_opt {sqlite3ExprListDelete(pParse->db, $$);}

// the sortlist non-terminal stores a list of expression where each
// expression is optionally followed by ASC or DESC to indicate the
// sort order.
//
%type sortlist {ExprList*}
%destructor sortlist {sqlite3ExprListDelete(pParse->db, $$);}

orderby_opt(A) ::= .                          {A = 0;}
orderby_opt(A) ::= ORDER BY sortlist(X).      {A = X;}
sortlist(A) ::= sortlist(X) COMMA expr(Y) sortorder(Z). {
  A = sqlite3ExprListAppend(pParse,X,Y.pExpr);
  sqlite3ExprListSetSortOrder(A,Z);
}
sortlist(A) ::= expr(Y) sortorder(Z). {
  A = sqlite3ExprListAppend(pParse,0,Y.pExpr);
  sqlite3ExprListSetSortOrder(A,Z);
}

%type sortorder {int}

sortorder(A) ::= ASC.           {A = SQLITE_SO_ASC;}
sortorder(A) ::= DESC.          {A = SQLITE_SO_DESC;}
sortorder(A) ::= .              {A = SQLITE_SO_UNDEFINED;}

%type groupby_opt {ExprList*}
%destructor groupby_opt {sqlite3ExprListDelete(pParse->db, $$);}
groupby_opt(A) ::= .                      {A = 0;}
%ifndef MAXSCALE
groupby_opt(A) ::= GROUP BY nexprlist(X). {A = X;}
%endif
%ifdef MAXSCALE
with_rollup ::= WITH ROLLUP.
with_rollup ::= .

groupby_opt(A) ::= GROUP BY nexprlist(X) sortorder with_rollup. {A = X;}
%endif

%type having_opt {Expr*}
%destructor having_opt {sqlite3ExprDelete(pParse->db, $$);}
having_opt(A) ::= .                {A = 0;}
having_opt(A) ::= HAVING expr(X).  {A = X.pExpr;}

%type limit_opt {struct LimitVal}

// The destructor for limit_opt will never fire in the current grammar.
// The limit_opt non-terminal only occurs at the end of a single production
// rule for SELECT statements.  As soon as the rule that create the 
// limit_opt non-terminal reduces, the SELECT statement rule will also
// reduce.  So there is never a limit_opt non-terminal on the stack 
// except as a transient.  So there is never anything to destroy.
//
//%destructor limit_opt {
//  sqlite3ExprDelete(pParse->db, $$.pLimit);
//  sqlite3ExprDelete(pParse->db, $$.pOffset);
//}
limit_opt(A) ::= .                    {A.pLimit = 0; A.pOffset = 0;}
limit_opt(A) ::= LIMIT expr(X).       {A.pLimit = X.pExpr; A.pOffset = 0;}
limit_opt(A) ::= LIMIT expr(X) OFFSET expr(Y). 
                                      {A.pLimit = X.pExpr; A.pOffset = Y.pExpr;}
limit_opt(A) ::= LIMIT expr(X) COMMA expr(Y). 
                                      {A.pOffset = X.pExpr; A.pLimit = Y.pExpr;}

/////////////////////////// The DELETE statement /////////////////////////////
//
%ifdef SQLITE_ENABLE_UPDATE_DELETE_LIMIT
%ifdef MAXSCALE
low_priority_quick_ignore_opt ::= .
low_priority_quick_ignore_opt ::= LOW_PRIORITY.
low_priority_quick_ignore_opt ::= QUICK.
low_priority_quick_ignore_opt ::= IGNORE.

cmd ::= with(C) DELETE low_priority_quick_ignore_opt FROM fullname(X) indexed_opt(I) where_opt(W) 
        orderby_opt(O) limit_opt(L). {
%endif
%ifndef MAXSCALE
cmd ::= with(C) DELETE FROM fullname(X) indexed_opt(I) where_opt(W) 
        orderby_opt(O) limit_opt(L). {
%endif
  sqlite3WithPush(pParse, C, 1);
  sqlite3SrcListIndexedBy(pParse, X, &I);
#ifdef MAXSCALE
  // We are not interested in the order by or limit information.
  // Thus we simply delete it. sqlite also has some limitations, which
  // will not bother us if we hide the information from it.
  sqlite3ExprListDelete(pParse->db, O);
  sqlite3ExprDelete(pParse->db, L.pLimit);
  sqlite3ExprDelete(pParse->db, L.pOffset);
  mxs_sqlite3DeleteFrom(pParse,X,W,0);
#else
  W = sqlite3LimitWhere(pParse, X, W, O, L.pLimit, L.pOffset, "DELETE");
  sqlite3DeleteFrom(pParse,X,W);
#endif
}
%endif
%ifndef SQLITE_ENABLE_UPDATE_DELETE_LIMIT
cmd ::= with(C) DELETE FROM fullname(X) indexed_opt(I) where_opt(W). {
  sqlite3WithPush(pParse, C, 1);
  sqlite3SrcListIndexedBy(pParse, X, &I);
  sqlite3DeleteFrom(pParse,X,W);
}
%endif

%type where_opt {Expr*}
%destructor where_opt {sqlite3ExprDelete(pParse->db, $$);}

where_opt(A) ::= .                    {A = 0;}
where_opt(A) ::= WHERE expr(X).       {A = X.pExpr;}

%ifdef MAXSCALE
%type table_factor {SrcList*}
%destructor table_factor {sqlite3SrcListDelete(pParse->db, $$);}

table_factor(A) ::= nm(X). {
  A = sqlite3SrcListAppend(pParse->db, 0, &X, 0);
}

table_factor(A) ::= nm(X) as_opt id(Y). {
  A = sqlite3SrcListAppendFromTerm(pParse, 0, &X, 0, &Y, 0, 0, 0);
}

table_factor(A) ::= nm(X) DOT nm(Y). {
  A = sqlite3SrcListAppend(pParse->db, 0, &X, &Y);
}

table_factor(A) ::= nm(X) DOT nm(Y) as_opt id(Z). {
  A = sqlite3SrcListAppendFromTerm(pParse, 0, &X, &Y, &Z, 0, 0, 0);
}

table_factor(A) ::= LP oneselect(S) RP as_opt id. {
    maxscaleCollectInfoFromSelect(pParse, S, 1);
  sqlite3SelectDelete(pParse->db, S);
  A = 0;
}

%type table_reference {SrcList*}
%destructor table_reference {sqlite3SrcListDelete(pParse->db, $$);}

table_reference(A) ::= table_factor(X). {
  A = X;
}

table_reference(A) ::= join_table(X). {
  A = X;
}

%type join_table {SrcList*}
%destructor join_table {sqlite3SrcListDelete(pParse->db, $$);}

join_opt ::= .
join_opt ::= JOIN_KW.

join_table(A) ::= table_reference(X) join_opt JOIN table_reference(Y) join_condition. {
  A = sqlite3SrcListCat(pParse->db, X, Y);
}

join_condition ::= ON expr(X). {
    sqlite3ExprDelete(pParse->db, X.pExpr);
}

%type escaped_table_reference {SrcList*}
%destructor escaped_table_reference {sqlite3SrcListDelete(pParse->db, $$);}

escaped_table_reference(A) ::= table_reference(X). {
  A = X;
}

%type table_references {SrcList*}
%destructor table_references {sqlite3SrcListDelete(pParse->db, $$);}

table_references(A) ::= escaped_table_reference(X). {
  A = X;
}

table_references(A) ::= table_references(X) COMMA escaped_table_reference(Y). {
  A = sqlite3SrcListCat(pParse->db, X, Y);
}

%type tbl_name {SrcList*}
%destructor tbl_name {sqlite3SrcListDelete(pParse->db, $$);}

tbl_name(A) ::= nm(X). { A = sqlite3SrcListAppend(pParse->db,0,&X,0); }
tbl_name(A) ::= nm(X) DOT STAR. { A = sqlite3SrcListAppend(pParse->db,0,&X,0); }
tbl_name(A) ::= nm(X) DOT nm(Y). { A = sqlite3SrcListAppend(pParse->db,0,&X,&Y); }
tbl_name(A) ::= nm(X) DOT nm(Y) DOT STAR. { A = sqlite3SrcListAppend(pParse->db,0,&X,&Y); }

%type tbl_names {SrcList*}
%destructor tbl_names {sqlite3SrcListDelete(pParse->db, $$);}

tbl_names(A) ::= tbl_name(X). { A = X; }
tbl_names(A) ::= tbl_names(X) COMMA tbl_name(Y). {
  A = sqlite3SrcListCat(pParse->db, X, Y);
}

cmd ::= with(C) DELETE low_priority_quick_ignore_opt
            tbl_names(X)
            FROM table_references(U)
            where_opt(W). {
  sqlite3WithPush(pParse, C, 1);
  mxs_sqlite3DeleteFrom(pParse, X, W, U);
}

cmd ::= with(C) DELETE low_priority_quick_ignore_opt
            FROM tbl_names(X)
            USING table_references(U)
            where_opt(W). {
  sqlite3WithPush(pParse, C, 1);
  mxs_sqlite3DeleteFrom(pParse, X, W, U);
}

%endif

////////////////////////// The UPDATE command ////////////////////////////////
//
%ifdef SQLITE_ENABLE_UPDATE_DELETE_LIMIT
%ifdef MAXSCALE
low_priority_or_ignore_opt ::= .
low_priority_or_ignore_opt ::= low_priority_or_ignore_opt IGNORE.
low_priority_or_ignore_opt ::= low_priority_or_ignore_opt LOW_PRIORITY.

%type mxs_setlist {ExprList*}
%destructor mxs_setlist {sqlite3ExprListDelete(pParse->db, $$);}

mxs_setlist(A) ::= col_name(X) EQ expr(Y). {
  Expr* pEq = sqlite3PExpr(pParse, TK_EQ, X.pExpr, Y.pExpr, 0);
  A = sqlite3ExprListAppend(pParse, 0, pEq);
}

mxs_setlist(A) ::= mxs_setlist(Z) COMMA col_name(X) EQ expr(Y). {
  Expr* pEq = sqlite3PExpr(pParse, TK_EQ, X.pExpr, Y.pExpr, 0);
  A = sqlite3ExprListAppend(pParse, Z, pEq);
}

cmd ::= with(C) UPDATE low_priority_or_ignore_opt orconf(R) table_references(X) indexed_opt(I) SET mxs_setlist(Y)
        where_opt(W) orderby_opt(O) limit_opt(L).  {
%endif
%ifndef MAXSCALE
cmd ::= with(C) UPDATE orconf(R) fullname(X) indexed_opt(I) SET setlist(Y)
        where_opt(W) orderby_opt(O) limit_opt(L).  {
%endif
  sqlite3WithPush(pParse, C, 1);
  sqlite3SrcListIndexedBy(pParse, X, &I);
  sqlite3ExprListCheckLength(pParse,Y,"set list"); 
#ifdef MAXSCALE
  // We are not interested in the order by or limit information.
  // Thus we simply delete it. sqlite also has some limitations, which
  // will not bother us if we hide the information from it.
  sqlite3ExprListDelete(pParse->db, O);
  sqlite3ExprDelete(pParse->db, L.pLimit);
  sqlite3ExprDelete(pParse->db, L.pOffset);
  mxs_sqlite3Update(pParse,X,Y,W,R);
#else
  W = sqlite3LimitWhere(pParse, X, W, O, L.pLimit, L.pOffset, "UPDATE");
  sqlite3Update(pParse,X,Y,W,R);
#endif
}
%endif
%ifndef SQLITE_ENABLE_UPDATE_DELETE_LIMIT
cmd ::= with(C) UPDATE orconf(R) fullname(X) indexed_opt(I) SET setlist(Y)
        where_opt(W).  {
  sqlite3WithPush(pParse, C, 1);
  sqlite3SrcListIndexedBy(pParse, X, &I);
  sqlite3ExprListCheckLength(pParse,Y,"set list"); 
  sqlite3Update(pParse,X,Y,W,R);
}
%endif

%type setlist {ExprList*}
%destructor setlist {sqlite3ExprListDelete(pParse->db, $$);}

setlist(A) ::= setlist(Z) COMMA nm(X) EQ expr(Y). {
  A = sqlite3ExprListAppend(pParse, Z, Y.pExpr);
  sqlite3ExprListSetName(pParse, A, &X, 1);
}
setlist(A) ::= nm(X) EQ expr(Y). {
  A = sqlite3ExprListAppend(pParse, 0, Y.pExpr);
  sqlite3ExprListSetName(pParse, A, &X, 1);
}

////////////////////////// The INSERT command /////////////////////////////////
//
cmd ::= with(W) insert_cmd(R) INTO fullname(X) idlist_opt(F) select(S). {
  sqlite3WithPush(pParse, W, 1);
#ifdef MAXSCALE
  mxs_sqlite3Insert(pParse, X, S, F, R, 0);
#else
  sqlite3Insert(pParse, X, S, F, R);
#endif
}
cmd ::= with(W) insert_cmd(R) INTO fullname(X) idlist_opt(F) DEFAULT VALUES.
{
  sqlite3WithPush(pParse, W, 1);
#ifdef MAXSCALE
  mxs_sqlite3Insert(pParse, X, 0, F, R, 0);
#else
  sqlite3Insert(pParse, X, 0, F, R);
#endif
}

%ifdef MAXSCALE
cmd ::= with(W) insert_cmd(R) fullname(X) idlist_opt(F) DEFAULT VALUES.
{
  sqlite3WithPush(pParse, W, 1);
  mxs_sqlite3Insert(pParse, X, 0, F, R, 0);
}

cmd ::= with(W) insert_cmd(R) fullname(X) idlist_opt(F) select(S). {
  sqlite3WithPush(pParse, W, 1);
  mxs_sqlite3Insert(pParse, X, S, F, R, 0);
}

%type col_name {ExprSpan}
%destructor col_name {sqlite3ExprDelete(pParse->db, $$.pExpr);}

col_name(A) ::= nm(X). {
    spanExpr(&A, pParse, TK_ID, &X);
}
col_name(A) ::= nm(X) DOT nm(Y). {
  Expr *temp1 = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  Expr *temp2 = sqlite3PExpr(pParse, TK_ID, 0, 0, &Y);
  A.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2, 0);
  spanSet(&A,&X,&Y);
}
col_name(A) ::= nm(X) DOT nm(Y) DOT nm(Z). {
  Expr *temp1 = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  Expr *temp2 = sqlite3PExpr(pParse, TK_ID, 0, 0, &Y);
  Expr *temp3 = sqlite3PExpr(pParse, TK_ID, 0, 0, &Z);
  Expr *temp4 = sqlite3PExpr(pParse, TK_DOT, temp2, temp3, 0);
  A.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp4, 0);
  spanSet(&A,&X,&Z);
}

%type col_name_value {ExprSpan}
%destructor col_name_value {sqlite3ExprDelete(pParse->db, $$.pExpr);}

col_name_value(A) ::= col_name(X) EQ expr(Y). {
  spanBinaryExpr(&A,pParse,TK_EQ,&X,&Y);
}

%type col_name_values {ExprList*}
%destructor col_name_values {sqlite3ExprListDelete(pParse->db, $$);}

col_name_values(A) ::= col_name_value(X). {
  A = sqlite3ExprListAppend(pParse, 0, X.pExpr);
}
col_name_values(A) ::= col_name_values(X) COMMA col_name_value(Y). {
  A = sqlite3ExprListAppend(pParse, X, Y.pExpr);
}

cmd ::= with(W) insert_cmd(R) fullname(X) SET col_name_values(Z).
{
  sqlite3WithPush(pParse, W, 1);
  mxs_sqlite3Insert(pParse, X, 0, 0, R, Z);
}

cmd ::= with(W) insert_cmd(R) INTO fullname(X) SET col_name_values(Z).
{
  sqlite3WithPush(pParse, W, 1);
  mxs_sqlite3Insert(pParse, X, 0, 0, R, Z);
}
%endif

%type insert_cmd {int}
%ifdef MAXSCALE
priority_opt ::= .
priority_opt ::= LOW_PRIORITY.
priority_opt ::= DELAYED.
priority_opt ::= HIGH_PRIORITY.

ignore_opt ::= .
ignore_opt ::= IGNORE.

insert_cmd(A) ::= INSERT priority_opt ignore_opt orconf(R).   {A = R;}
%endif
%ifndef MAXSCALE
insert_cmd(A) ::= INSERT orconf(R).   {A = R;}
%endif
insert_cmd(A) ::= REPLACE.            {A = OE_Replace;}
%type idlist_opt {IdList*}
%destructor idlist_opt {sqlite3IdListDelete(pParse->db, $$);}

%type idlist {IdList*}
%destructor idlist {sqlite3IdListDelete(pParse->db, $$);}

idlist_opt(A) ::= .                       {A = 0;}
idlist_opt(A) ::= LP idlist(X) RP.    {A = X;}
idlist(A) ::= idlist(X) COMMA nm(Y).
    {A = sqlite3IdListAppend(pParse->db,X,&Y);}
idlist(A) ::= nm(Y).
    {A = sqlite3IdListAppend(pParse->db,0,&Y);}

/////////////////////////// Expression Processing /////////////////////////////
//

%type expr {ExprSpan}
%destructor expr {sqlite3ExprDelete(pParse->db, $$.pExpr);}
%type term {ExprSpan}
%destructor term {sqlite3ExprDelete(pParse->db, $$.pExpr);}

%include {
  /* This is a utility routine used to set the ExprSpan.zStart and
  ** ExprSpan.zEnd values of pOut so that the span covers the complete
  ** range of text beginning with pStart and going to the end of pEnd.
  */
  static void spanSet(ExprSpan *pOut, Token *pStart, Token *pEnd){
    pOut->zStart = pStart->z;
    pOut->zEnd = &pEnd->z[pEnd->n];
  }

  /* Construct a new Expr object from a single identifier.  Use the
  ** new Expr to populate pOut.  Set the span of pOut to be the identifier
  ** that created the expression.
  */
  static void spanExpr(ExprSpan *pOut, Parse *pParse, int op, Token *pValue){
    pOut->pExpr = sqlite3PExpr(pParse, op, 0, 0, pValue);
    pOut->zStart = pValue->z;
    pOut->zEnd = &pValue->z[pValue->n];
  }
}

expr(A) ::= term(X).             {A = X;}
expr(A) ::= LP(B) expr(X) RP(E). {A.pExpr = X.pExpr; spanSet(&A,&B,&E);}
%ifdef MAXSCALE
expr(A) ::= LP expr(X) COMMA(OP) expr(Y) RP. {spanBinaryExpr(&A,pParse,@OP,&X,&Y);}
term(A) ::= DEFAULT(X).          {spanExpr(&A, pParse, @X, &X);}
%endif
term(A) ::= NULL(X).             {spanExpr(&A, pParse, @X, &X);}
expr(A) ::= id(X).               {spanExpr(&A, pParse, TK_ID, &X);}
expr(A) ::= nm(X) DOT nm(Y). {
  Expr *temp1 = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  Expr *temp2 = sqlite3PExpr(pParse, TK_ID, 0, 0, &Y);
  A.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2, 0);
  spanSet(&A,&X,&Y);
}
expr(A) ::= DOT nm(X) DOT nm(Y). {
  Expr *temp1 = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  Expr *temp2 = sqlite3PExpr(pParse, TK_ID, 0, 0, &Y);
  A.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2, 0);
  spanSet(&A,&X,&Y);
}
expr(A) ::= nm(X) DOT nm(Y) DOT nm(Z). {
  Expr *temp1 = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  Expr *temp2 = sqlite3PExpr(pParse, TK_ID, 0, 0, &Y);
  Expr *temp3 = sqlite3PExpr(pParse, TK_ID, 0, 0, &Z);
  Expr *temp4 = sqlite3PExpr(pParse, TK_DOT, temp2, temp3, 0);
  A.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp4, 0);
  spanSet(&A,&X,&Z);
}
term(A) ::= INTEGER|FLOAT|BLOB(X).  {spanExpr(&A, pParse, @X, &X);}
term(A) ::= STRING(X).              {spanExpr(&A, pParse, @X, &X);}
expr(A) ::= VARIABLE(X).     {
  if( X.n>=2 && X.z[0]=='#' && sqlite3Isdigit(X.z[1]) ){
    /* When doing a nested parse, one can include terms in an expression
    ** that look like this:   #1 #2 ...  These terms refer to registers
    ** in the virtual machine.  #N is the N-th register. */
    if( pParse->nested==0 ){
      sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &X);
      A.pExpr = 0;
    }else{
      A.pExpr = sqlite3PExpr(pParse, TK_REGISTER, 0, 0, &X);
      if( A.pExpr ) sqlite3GetInt32(&X.z[1], &A.pExpr->iTable);
    }
  }else{
    spanExpr(&A, pParse, TK_VARIABLE, &X);
    sqlite3ExprAssignVarNumber(pParse, A.pExpr);
  }
  spanSet(&A, &X, &X);
}
%ifdef MAXSCALE
expr(A) ::= VARIABLE(X) variable_tail(Y).     {
  // As we won't be executing any queries, we do not need to do
  // the things that are done above.
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_VARIABLE, 0, 0, &X);
  spanSet(&v, &X, &X);
  A.pExpr = sqlite3PExpr(pParse, TK_DOT, v.pExpr, Y.pExpr, 0);
  A.zStart = v.zStart;
  A.zEnd = Y.zEnd;
}
%endif
expr(A) ::= expr(E) COLLATE ids(C). {
  A.pExpr = sqlite3ExprAddCollateToken(pParse, E.pExpr, &C, 1);
  A.zStart = E.zStart;
  A.zEnd = &C.z[C.n];
}
%ifndef SQLITE_OMIT_CAST
expr(A) ::= CAST(X) LP expr(E) AS typetoken(T) RP(Y). {
  A.pExpr = sqlite3PExpr(pParse, TK_CAST, E.pExpr, 0, &T);
  spanSet(&A,&X,&Y);
}
%endif  SQLITE_OMIT_CAST
%ifdef MAXSCALE

%type group_concat_colname {ExprSpan}
%destructor group_concat_colname {sqlite3ExprDelete(pParse->db, $$.pExpr);}

group_concat_colname(A) ::= nm(X). {
  spanExpr(&A, pParse, TK_ID, &X);
}

group_concat_colname(A) ::= nm(X) DOT nm(Y). {
  Expr *temp1 = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  Expr *temp2 = sqlite3PExpr(pParse, TK_ID, 0, 0, &Y);
  A.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2, 0);
  spanSet(&A,&X,&Y);
}

%type group_concat_colnames {ExprList*}
%destructor group_concat_colnames {sqlite3ExprListDelete(pParse->db, $$);}

group_concat_colnames(A) ::= group_concat_colname(Y). {
  A = sqlite3ExprListAppend(pParse, 0, Y.pExpr);
}

group_concat_colnames(A) ::= group_concat_colnames(X) COMMA group_concat_colname(Y). {
  A = sqlite3ExprListAppend(pParse, X, Y.pExpr);
}

%type group_concat_colnames_opt {ExprList*}
%destructor group_concat_colnames_opt {sqlite3ExprListDelete(pParse->db, $$);}

group_concat_colnames_opt(A) ::= . { A = NULL; }
group_concat_colnames_opt(A) ::= COMMA group_concat_colnames(X). { A = X; }

%type group_concat_order_by {ExprList*}
%destructor group_concat_order_by {sqlite3ExprListDelete(pParse->db, $$);}

group_concat_order_by(A) ::= ORDER BY INTEGER sortorder group_concat_colnames_opt(X). {
  A = X;
}
group_concat_order_by(A) ::= ORDER BY col_name(X) sortorder group_concat_colnames_opt(Y). {
  A = sqlite3ExprListAppend(pParse, Y, X.pExpr);
}
// TODO: The following causes conflicts.
//group_concat_order_by ::= ORDER BY expr(X) sortorder group_concat_colnames_opt. {
//  sqlite3ExprDelete(pParse->db, X.pExpr);
//}

group_concat_separator ::= SEPARATOR STRING.

%type group_concat_tail {ExprList*}
%destructor group_concat_tail {sqlite3ExprListDelete(pParse->db, $$);}

group_concat_tail(A) ::= group_concat_order_by(X). { A = X; }
group_concat_tail(A) ::= group_concat_separator. { A = NULL; }
group_concat_tail(A) ::= group_concat_order_by(X) group_concat_separator. { A = X; }

convert_tail ::= USING id.

// Since we don't use the arguments for anything, any function can have these
// as trailing arguments. It's ok because if used incorrectly, the server will
// reject the statement

%type func_arg_tail_opt {ExprList*}
%destructor func_arg_tail_opt {sqlite3ExprListDelete(pParse->db, $$);}

func_arg_tail_opt(A) ::= . { A = NULL; }
func_arg_tail_opt(A) ::= group_concat_tail(X). { A = X; }
func_arg_tail_opt(A) ::= convert_tail. { A = NULL; }

wf_partition_by ::= PARTITION BY nexprlist(X). {
  sqlite3ExprListDelete(pParse->db, X);
}

wf_partition_by_opt ::= .
wf_partition_by_opt ::= wf_partition_by.

wf_order_by ::= ORDER BY nexprlist(X) sortorder. {
  sqlite3ExprListDelete(pParse->db, X);
}

wf_order_by_opt ::= .
wf_order_by_opt ::= wf_order_by.

wf_frame_units ::= RANGE.
wf_frame_units ::= ROWS.

wf_frame_start ::= UNBOUNDED PRECEDING.
wf_frame_start ::= CURRENT ROW.
wf_frame_start ::= term PRECEDING.

wf_frame_bound ::= wf_frame_start.
wf_frame_bound ::= UNBOUNDED FOLLOWING.
wf_frame_bound ::= term FOLLOWING.

wf_frame_extent ::= wf_frame_start.
wf_frame_extent ::= BETWEEN wf_frame_bound AND wf_frame_bound.

wf_frame_exclusion_opt ::= .
wf_frame_exclusion_opt ::= EXCLUDE CURRENT ROW.
wf_frame_exclusion_opt ::= EXCLUDE GROUP.
wf_frame_exclusion_opt ::= EXCLUDE TIES.
wf_frame_exclusion_opt ::= EXCLUDE NO OTHERS.

wf_frame_opt ::= .
wf_frame_opt ::= wf_frame_units wf_frame_extent wf_frame_exclusion_opt .

wf ::= OVER LP wf_window_ref_opt wf_partition_by_opt wf_order_by_opt wf_frame_opt RP.
wf ::= OVER id.

wf_opt ::= .
wf_opt ::= wf.

expr(A) ::= id(X) LP distinct(D) exprlist(Y) func_arg_tail_opt(Z) RP(E) wf_opt. {
  // We just append Z on Y as we are only interested in what columns
  // the function used.
  Y = sqlite3ExprListAppendList(pParse, Y, Z);
%endif
%ifndef MAXSCALE
expr(A) ::= id(X) LP distinct(D) exprlist(Y) RP(E). {
%endif
  if( Y && Y->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &X);
  }
  A.pExpr = sqlite3ExprFunction(pParse, Y, &X);
  spanSet(&A,&X,&E);
  if( D==SF_Distinct && A.pExpr ){
    A.pExpr->flags |= EP_Distinct;
  }
}
%ifdef MAXSCALE
expr(A) ::= nm DOT nm(X) LP distinct(D) exprlist(Y) func_arg_tail_opt(Z) RP(E) wf_opt. {
  // We just append Z on Y as we are only interested in what columns
  // the function used.
  Y = sqlite3ExprListAppendList(pParse, Y, Z);
  if( Y && Y->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &X);
  }
  A.pExpr = sqlite3ExprFunction(pParse, Y, &X);
  spanSet(&A,&X,&E);
  if( D==SF_Distinct && A.pExpr ){
    A.pExpr->flags |= EP_Distinct;
  }
}

keyword_as_function(A) ::= JOIN_KW(X). {A = X;}
keyword_as_function(A) ::= INSERT(X). {A = X;}
keyword_as_function(A) ::= REPLACE(X). {A = X;}

expr(A) ::= keyword_as_function(X) LP distinct(D) exprlist(Y) RP(E). {
  if( Y && Y->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &X);
  }
  A.pExpr = sqlite3ExprFunction(pParse, Y, &X);
  spanSet(&A,&X,&E);
  if( D==SF_Distinct && A.pExpr ){
    A.pExpr->flags |= EP_Distinct;
  }
}
%endif
expr(A) ::= id(X) LP STAR RP(E) wf_opt. {
  A.pExpr = sqlite3ExprFunction(pParse, 0, &X);
  spanSet(&A,&X,&E);
}
term(A) ::= CTIME_KW(OP). {
  A.pExpr = sqlite3ExprFunction(pParse, 0, &OP);
  spanSet(&A, &OP, &OP);
}

%include {
  /* This routine constructs a binary expression node out of two ExprSpan
  ** objects and uses the result to populate a new ExprSpan object.
  */
  static void spanBinaryExpr(
    ExprSpan *pOut,     /* Write the result here */
    Parse *pParse,      /* The parsing context.  Errors accumulate here */
    int op,             /* The binary operation */
    ExprSpan *pLeft,    /* The left operand */
    ExprSpan *pRight    /* The right operand */
  ){
    pOut->pExpr = sqlite3PExpr(pParse, op, pLeft->pExpr, pRight->pExpr, 0);
    pOut->zStart = pLeft->zStart;
    pOut->zEnd = pRight->zEnd;
  }

  /* If doNot is true, then add a TK_NOT Expr-node wrapper around the
  ** outside of *ppExpr.
  */
  static void exprNot(Parse *pParse, int doNot, Expr **ppExpr){
    if( doNot ) *ppExpr = sqlite3PExpr(pParse, TK_NOT, *ppExpr, 0, 0);
  }
}

expr(A) ::= expr(X) AND(OP) expr(Y).    {spanBinaryExpr(&A,pParse,@OP,&X,&Y);}
expr(A) ::= expr(X) OR(OP) expr(Y).     {spanBinaryExpr(&A,pParse,@OP,&X,&Y);}
expr(A) ::= expr(X) LT|GT|GE|LE(OP) expr(Y).
                                        {spanBinaryExpr(&A,pParse,@OP,&X,&Y);}
expr(A) ::= expr(X) EQ|NE(OP) expr(Y).  {spanBinaryExpr(&A,pParse,@OP,&X,&Y);}
expr(A) ::= expr(X) BITAND|BITOR|LSHIFT|RSHIFT(OP) expr(Y).
                                        {spanBinaryExpr(&A,pParse,@OP,&X,&Y);}
expr(A) ::= expr(X) PLUS|MINUS(OP) expr(Y).
                                        {spanBinaryExpr(&A,pParse,@OP,&X,&Y);}
%ifdef MAXSCALE
expr(A) ::= INTERVAL INTEGER(X) id. {
  // Here we could check that id is one of MICROSECOND, SECOND, MINUTE
  // HOUR, DAY, WEEK, etc.
  spanExpr(&A, pParse, @X, &X);
}
%endif
expr(A) ::= expr(X) STAR|SLASH|REM(OP) expr(Y).
                                        {spanBinaryExpr(&A,pParse,@OP,&X,&Y);}
expr(A) ::= expr(X) CONCAT(OP) expr(Y). {spanBinaryExpr(&A,pParse,@OP,&X,&Y);}
%type likeop {struct LikeOp}
likeop(A) ::= LIKE_KW|MATCH(X).     {A.eOperator = X; A.bNot = 0;}
likeop(A) ::= NOT LIKE_KW|MATCH(X). {A.eOperator = X; A.bNot = 1;}
expr(A) ::= expr(X) likeop(OP) expr(Y).  [LIKE_KW]  {
  ExprList *pList;
  pList = sqlite3ExprListAppend(pParse,0, Y.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, X.pExpr);
  A.pExpr = sqlite3ExprFunction(pParse, pList, &OP.eOperator);
  exprNot(pParse, OP.bNot, &A.pExpr);
  A.zStart = X.zStart;
  A.zEnd = Y.zEnd;
  if( A.pExpr ) A.pExpr->flags |= EP_InfixFunc;
}
expr(A) ::= expr(X) likeop(OP) expr(Y) ESCAPE expr(E).  [LIKE_KW]  {
  ExprList *pList;
  pList = sqlite3ExprListAppend(pParse,0, Y.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, X.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, E.pExpr);
  A.pExpr = sqlite3ExprFunction(pParse, pList, &OP.eOperator);
  exprNot(pParse, OP.bNot, &A.pExpr);
  A.zStart = X.zStart;
  A.zEnd = E.zEnd;
  if( A.pExpr ) A.pExpr->flags |= EP_InfixFunc;
}

%include {
  /* Construct an expression node for a unary postfix operator
  */
  static void spanUnaryPostfix(
    ExprSpan *pOut,        /* Write the new expression node here */
    Parse *pParse,         /* Parsing context to record errors */
    int op,                /* The operator */
    ExprSpan *pOperand,    /* The operand */
    Token *pPostOp         /* The operand token for setting the span */
  ){
    pOut->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0, 0);
    pOut->zStart = pOperand->zStart;
    pOut->zEnd = &pPostOp->z[pPostOp->n];
  }                           
}

expr(A) ::= expr(X) ISNULL|NOTNULL(E).   {spanUnaryPostfix(&A,pParse,@E,&X,&E);}
expr(A) ::= expr(X) NOT NULL(E). {spanUnaryPostfix(&A,pParse,TK_NOTNULL,&X,&E);}

%include {
  /* A routine to convert a binary TK_IS or TK_ISNOT expression into a
  ** unary TK_ISNULL or TK_NOTNULL expression. */
  static void binaryToUnaryIfNull(Parse *pParse, Expr *pY, Expr *pA, int op){
    sqlite3 *db = pParse->db;
    if( pA && pY && pY->op==TK_NULL ){
      pA->op = (u8)op;
      sqlite3ExprDelete(db, pA->pRight);
      pA->pRight = 0;
    }
  }
}

//    expr1 IS expr2
//    expr1 IS NOT expr2
//
// If expr2 is NULL then code as TK_ISNULL or TK_NOTNULL.  If expr2
// is any other expression, code as TK_IS or TK_ISNOT.
// 
expr(A) ::= expr(X) IS expr(Y).     {
  spanBinaryExpr(&A,pParse,TK_IS,&X,&Y);
  binaryToUnaryIfNull(pParse, Y.pExpr, A.pExpr, TK_ISNULL);
}
expr(A) ::= expr(X) IS NOT expr(Y). {
  spanBinaryExpr(&A,pParse,TK_ISNOT,&X,&Y);
  binaryToUnaryIfNull(pParse, Y.pExpr, A.pExpr, TK_NOTNULL);
}

%include {
  /* Construct an expression node for a unary prefix operator
  */
  static void spanUnaryPrefix(
    ExprSpan *pOut,        /* Write the new expression node here */
    Parse *pParse,         /* Parsing context to record errors */
    int op,                /* The operator */
    ExprSpan *pOperand,    /* The operand */
    Token *pPreOp         /* The operand token for setting the span */
  ){
    pOut->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0, 0);
    pOut->zStart = pPreOp->z;
    pOut->zEnd = pOperand->zEnd;
  }
}



expr(A) ::= NOT(B) expr(X).    {spanUnaryPrefix(&A,pParse,@B,&X,&B);}
expr(A) ::= BITNOT(B) expr(X). {spanUnaryPrefix(&A,pParse,@B,&X,&B);}
expr(A) ::= MINUS(B) expr(X). [BITNOT]
                               {spanUnaryPrefix(&A,pParse,TK_UMINUS,&X,&B);}
expr(A) ::= PLUS(B) expr(X). [BITNOT]
                               {spanUnaryPrefix(&A,pParse,TK_UPLUS,&X,&B);}

%type between_op {int}
between_op(A) ::= BETWEEN.     {A = 0;}
between_op(A) ::= NOT BETWEEN. {A = 1;}
expr(A) ::= expr(W) between_op(N) expr(X) AND expr(Y). [BETWEEN] {
  ExprList *pList = sqlite3ExprListAppend(pParse,0, X.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, Y.pExpr);
  A.pExpr = sqlite3PExpr(pParse, TK_BETWEEN, W.pExpr, 0, 0);
  if( A.pExpr ){
    A.pExpr->x.pList = pList;
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  } 
  exprNot(pParse, N, &A.pExpr);
  A.zStart = W.zStart;
  A.zEnd = Y.zEnd;
}
%ifndef SQLITE_OMIT_SUBQUERY
  %type in_op {int}
  in_op(A) ::= IN.      {A = 0;}
  in_op(A) ::= NOT IN.  {A = 1;}
  expr(A) ::= expr(X) in_op(N) LP exprlist(Y) RP(E). [IN] {
    if( Y==0 ){
      /* Expressions of the form
      **
      **      expr1 IN ()
      **      expr1 NOT IN ()
      **
      ** simplify to constants 0 (false) and 1 (true), respectively,
      ** regardless of the value of expr1.
      */
      A.pExpr = sqlite3PExpr(pParse, TK_INTEGER, 0, 0, &sqlite3IntTokens[N]);
      sqlite3ExprDelete(pParse->db, X.pExpr);
    }else if( Y->nExpr==1 ){
      /* Expressions of the form:
      **
      **      expr1 IN (?1)
      **      expr1 NOT IN (?2)
      **
      ** with exactly one value on the RHS can be simplified to something
      ** like this:
      **
      **      expr1 == ?1
      **      expr1 <> ?2
      **
      ** But, the RHS of the == or <> is marked with the EP_Generic flag
      ** so that it may not contribute to the computation of comparison
      ** affinity or the collating sequence to use for comparison.  Otherwise,
      ** the semantics would be subtly different from IN or NOT IN.
      */
      Expr *pRHS = Y->a[0].pExpr;
      Y->a[0].pExpr = 0;
      sqlite3ExprListDelete(pParse->db, Y);
      /* pRHS cannot be NULL because a malloc error would have been detected
      ** before now and control would have never reached this point */
      if( ALWAYS(pRHS) ){
        pRHS->flags &= ~EP_Collate;
        pRHS->flags |= EP_Generic;
      }
      A.pExpr = sqlite3PExpr(pParse, N ? TK_NE : TK_EQ, X.pExpr, pRHS, 0);
    }else{
      A.pExpr = sqlite3PExpr(pParse, TK_IN, X.pExpr, 0, 0);
      if( A.pExpr ){
        A.pExpr->x.pList = Y;
        sqlite3ExprSetHeightAndFlags(pParse, A.pExpr);
      }else{
        sqlite3ExprListDelete(pParse->db, Y);
      }
      exprNot(pParse, N, &A.pExpr);
    }
    A.zStart = X.zStart;
    A.zEnd = &E.z[E.n];
  }
  expr(A) ::= LP(B) select(X) RP(E). {
    A.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0, 0);
    if( A.pExpr ){
      A.pExpr->x.pSelect = X;
      ExprSetProperty(A.pExpr, EP_xIsSelect|EP_Subquery);
      sqlite3ExprSetHeightAndFlags(pParse, A.pExpr);
    }else{
      sqlite3SelectDelete(pParse->db, X);
    }
    A.zStart = B.z;
    A.zEnd = &E.z[E.n];
  }
  expr(A) ::= expr(X) in_op(N) LP select(Y) RP(E).  [IN] {
    A.pExpr = sqlite3PExpr(pParse, TK_IN, X.pExpr, 0, 0);
    if( A.pExpr ){
      A.pExpr->x.pSelect = Y;
      ExprSetProperty(A.pExpr, EP_xIsSelect|EP_Subquery);
      sqlite3ExprSetHeightAndFlags(pParse, A.pExpr);
    }else{
      sqlite3SelectDelete(pParse->db, Y);
    }
    exprNot(pParse, N, &A.pExpr);
    A.zStart = X.zStart;
    A.zEnd = &E.z[E.n];
  }
  expr(A) ::= expr(X) in_op(N) nm(Y) dbnm(Z). [IN] {
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&Y,&Z);
    A.pExpr = sqlite3PExpr(pParse, TK_IN, X.pExpr, 0, 0);
    if( A.pExpr ){
#ifdef MAXSCALE
      A.pExpr->x.pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0,0);
#else
      A.pExpr->x.pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
#endif
      ExprSetProperty(A.pExpr, EP_xIsSelect|EP_Subquery);
      sqlite3ExprSetHeightAndFlags(pParse, A.pExpr);
    }else{
      sqlite3SrcListDelete(pParse->db, pSrc);
    }
    exprNot(pParse, N, &A.pExpr);
    A.zStart = X.zStart;
    A.zEnd = Z.z ? &Z.z[Z.n] : &Y.z[Y.n];
  }
  expr(A) ::= EXISTS(B) LP select(Y) RP(E). {
    Expr *p = A.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0, 0);
    if( p ){
      p->x.pSelect = Y;
      ExprSetProperty(p, EP_xIsSelect|EP_Subquery);
      sqlite3ExprSetHeightAndFlags(pParse, p);
    }else{
      sqlite3SelectDelete(pParse->db, Y);
    }
    A.zStart = B.z;
    A.zEnd = &E.z[E.n];
  }
%endif SQLITE_OMIT_SUBQUERY

/* CASE expressions */
expr(A) ::= CASE(C) case_operand(X) case_exprlist(Y) case_else(Z) END(E). {
  A.pExpr = sqlite3PExpr(pParse, TK_CASE, X, 0, 0);
  if( A.pExpr ){
    A.pExpr->x.pList = Z ? sqlite3ExprListAppend(pParse,Y,Z) : Y;
    sqlite3ExprSetHeightAndFlags(pParse, A.pExpr);
  }else{
    sqlite3ExprListDelete(pParse->db, Y);
    sqlite3ExprDelete(pParse->db, Z);
  }
  A.zStart = C.z;
  A.zEnd = &E.z[E.n];
}
%type case_exprlist {ExprList*}
%destructor case_exprlist {sqlite3ExprListDelete(pParse->db, $$);}
case_exprlist(A) ::= case_exprlist(X) WHEN expr(Y) THEN expr(Z). {
  A = sqlite3ExprListAppend(pParse,X, Y.pExpr);
  A = sqlite3ExprListAppend(pParse,A, Z.pExpr);
}
case_exprlist(A) ::= WHEN expr(Y) THEN expr(Z). {
  A = sqlite3ExprListAppend(pParse,0, Y.pExpr);
  A = sqlite3ExprListAppend(pParse,A, Z.pExpr);
}
%type case_else {Expr*}
%destructor case_else {sqlite3ExprDelete(pParse->db, $$);}
case_else(A) ::=  ELSE expr(X).         {A = X.pExpr;}
case_else(A) ::=  .                     {A = 0;} 
%type case_operand {Expr*}
%destructor case_operand {sqlite3ExprDelete(pParse->db, $$);}
case_operand(A) ::= expr(X).            {A = X.pExpr;} 
case_operand(A) ::= .                   {A = 0;} 

%type exprlist {ExprList*}
%destructor exprlist {sqlite3ExprListDelete(pParse->db, $$);}
%type nexprlist {ExprList*}
%destructor nexprlist {sqlite3ExprListDelete(pParse->db, $$);}

exprlist(A) ::= nexprlist(X).                {A = X;}
exprlist(A) ::= .                            {A = 0;}
nexprlist(A) ::= nexprlist(X) COMMA expr(Y).
    {A = sqlite3ExprListAppend(pParse,X,Y.pExpr);}
nexprlist(A) ::= expr(Y).
    {A = sqlite3ExprListAppend(pParse,0,Y.pExpr);}
%ifdef MAXSCALE
nexprlist(A) ::= expr(Y) AS typename.
    {A = sqlite3ExprListAppend(pParse,0,Y.pExpr);}
nexprlist(A) ::= nexprlist(X) COMMA expr(Y) AS typename.
    {A = sqlite3ExprListAppend(pParse,X,Y.pExpr);}
%endif

///////////////////////////// The CREATE INDEX command ///////////////////////
//
cmd ::= createkw(S) uniqueflag(U) INDEX ifnotexists(NE) nm(X) dbnm(D)
        ON nm(Y) LP sortlist(Z) RP where_opt(W). {
#ifdef MAXSCALE
  mxs_sqlite3CreateIndex(pParse, &X, &D,
                         sqlite3SrcListAppend(pParse->db,0,&Y,0), Z, U,
                         &S, W, SQLITE_SO_ASC, NE);
#else
  sqlite3CreateIndex(pParse, &X, &D, 
                     sqlite3SrcListAppend(pParse->db,0,&Y,0), Z, U,
                      &S, W, SQLITE_SO_ASC, NE);
#endif
}

%type uniqueflag {int}
uniqueflag(A) ::= UNIQUE.  {A = OE_Abort;}
uniqueflag(A) ::= .        {A = OE_None;}


// The eidlist non-terminal (Expression Id List) generates an ExprList
// from a list of identifiers.  The identifier names are in ExprList.a[].zName.
// This list is stored in an ExprList rather than an IdList so that it
// can be easily sent to sqlite3ColumnsExprList().
//
// eidlist is grouped with CREATE INDEX because it used to be the non-terminal
// used for the arguments to an index.  That is just an historical accident.
//
// IMPORTANT COMPATIBILITY NOTE:  Some prior versions of SQLite accepted
// COLLATE clauses and ASC or DESC keywords on ID lists in inappropriate
// places - places that might have been stored in the sqlite_master schema.
// Those extra features were ignored.  But because they might be in some
// (busted) old databases, we need to continue parsing them when loading
// historical schemas.
//
%type eidlist {ExprList*}
%destructor eidlist {sqlite3ExprListDelete(pParse->db, $$);}
%type eidlist_opt {ExprList*}
%destructor eidlist_opt {sqlite3ExprListDelete(pParse->db, $$);}

%include {
  /* Add a single new term to an ExprList that is used to store a
  ** list of identifiers.  Report an error if the ID list contains
  ** a COLLATE clause or an ASC or DESC keyword, except ignore the
  ** error while parsing a legacy schema.
  */
  static ExprList *parserAddExprIdListTerm(
    Parse *pParse,
    ExprList *pPrior,
    Token *pIdToken,
    int hasCollate,
    int sortOrder
  ){
    ExprList *p = sqlite3ExprListAppend(pParse, pPrior, 0);
    if( (hasCollate || sortOrder!=SQLITE_SO_UNDEFINED)
        && pParse->db->init.busy==0
    ){
      sqlite3ErrorMsg(pParse, "syntax error after column name \"%.*s\"",
                         pIdToken->n, pIdToken->z);
    }
    sqlite3ExprListSetName(pParse, p, pIdToken, 1);
    return p;
  }
} // end %include

eidlist_opt(A) ::= .                         {A = 0;}
eidlist_opt(A) ::= LP eidlist(X) RP.         {A = X;}
eidlist(A) ::= eidlist(X) COMMA nm(Y) collate(C) sortorder(Z).  {
  A = parserAddExprIdListTerm(pParse, X, &Y, C, Z);
}
eidlist(A) ::= nm(Y) collate(C) sortorder(Z). {
  A = parserAddExprIdListTerm(pParse, 0, &Y, C, Z);
}

%type collate {int}
collate(C) ::= .              {C = 0;}
collate(C) ::= COLLATE ids.   {C = 1;}


///////////////////////////// The DROP INDEX command /////////////////////////
//
%ifdef MAXSCALE

%type drop_index_algorithm_option {int}
drop_index_algorithm_option(A) ::= DEFAULT. {A=0;}
drop_index_algorithm_option(A) ::= id. {
  // Here we could verify that the id is "COPY" or "INPLACE"
  A=1;
}

%type drop_index_algorithm {int}
drop_index_algorithm(A) ::= ALGORITHM drop_index_algorithm_option(X). {A=X;}
drop_index_algorithm(A) ::= ALGORITHM EQ drop_index_algorithm_option(X). {A=X;}

%type drop_index_lock_option {int}
drop_index_lock_option(A) ::= DEFAULT. {A=0;}
drop_index_lock_option(A) ::= id. {
  // Here we could verify that the id is "EXCLUSIVE", "NONE" or "SHARED".
  A=4;
}

%type drop_index_lock {int}
drop_index_lock(A) ::= LOCK drop_index_lock_option(X). {A=X;}
drop_index_lock(A) ::= LOCK EQ drop_index_lock_option(X). {A=X;}

%type drop_index_options {int}
drop_index_options(A) ::= . {A=0;}
drop_index_options(A) ::= drop_index_options(X) drop_index_algorithm(Y). {A=X|Y;}
drop_index_options(A) ::= drop_index_options(X) drop_index_lock(Y). {A=X|Y;}

cmd ::= DROP INDEX fullname(X) ON fullname(Y) drop_index_options(Z). {
  mxs_sqlite3DropIndex(pParse, X, Y, Z);
}

%endif
%ifndef MAXSCALE
cmd ::= DROP INDEX ifexists(E) fullname(X).   {sqlite3DropIndex(pParse, X, E);}
%endif

///////////////////////////// The VACUUM command /////////////////////////////
//
%ifndef SQLITE_OMIT_VACUUM
%ifndef SQLITE_OMIT_ATTACH
cmd ::= VACUUM.                {sqlite3Vacuum(pParse);}
cmd ::= VACUUM nm.             {sqlite3Vacuum(pParse);}
%endif  SQLITE_OMIT_ATTACH
%endif  SQLITE_OMIT_VACUUM

///////////////////////////// The PRAGMA command /////////////////////////////
//
%ifndef SQLITE_OMIT_PRAGMA
cmd ::= PRAGMA nm(X) dbnm(Z).                {sqlite3Pragma(pParse,&X,&Z,0,0);}
cmd ::= PRAGMA nm(X) dbnm(Z) EQ nmnum(Y).    {sqlite3Pragma(pParse,&X,&Z,&Y,0);}
cmd ::= PRAGMA nm(X) dbnm(Z) LP nmnum(Y) RP. {sqlite3Pragma(pParse,&X,&Z,&Y,0);}
cmd ::= PRAGMA nm(X) dbnm(Z) EQ minus_num(Y). 
                                             {sqlite3Pragma(pParse,&X,&Z,&Y,1);}
cmd ::= PRAGMA nm(X) dbnm(Z) LP minus_num(Y) RP.
                                             {sqlite3Pragma(pParse,&X,&Z,&Y,1);}

nmnum(A) ::= plus_num(X).             {A = X;}
nmnum(A) ::= nm(X).                   {A = X;}
nmnum(A) ::= ON(X).                   {A = X;}
nmnum(A) ::= DELETE(X).               {A = X;}
nmnum(A) ::= DEFAULT(X).              {A = X;}
%endif SQLITE_OMIT_PRAGMA
%token_class number INTEGER|FLOAT.
plus_num(A) ::= PLUS number(X).       {A = X;}
plus_num(A) ::= number(X).            {A = X;}
minus_num(A) ::= MINUS number(X).     {A = X;}
//////////////////////////// The CREATE TRIGGER command /////////////////////

%ifndef SQLITE_OMIT_TRIGGER

cmd ::= createkw trigger_decl(A) BEGIN trigger_cmd_list(S) END(Z). {
  Token all;
  all.z = A.z;
  all.n = (int)(Z.z - A.z) + Z.n;
#ifdef MAXSCALE
  mxs_sqlite3FinishTrigger(pParse, S, &all);
#else
  sqlite3FinishTrigger(pParse, S, &all);
#endif
}

trigger_decl(A) ::= temp(T) TRIGGER ifnotexists(NOERR) nm(B) dbnm(Z) 
                    trigger_time(C) trigger_event(D)
                    ON fullname(E) foreach_clause when_clause(G). {
#ifdef MAXSCALE
  mxs_sqlite3BeginTrigger(pParse, &B, &Z, C, D.a, D.b, E, G, T, NOERR);
#else
  sqlite3BeginTrigger(pParse, &B, &Z, C, D.a, D.b, E, G, T, NOERR);
#endif
  A = (Z.n==0?B:Z);
}

%type trigger_time {int}
trigger_time(A) ::= BEFORE.      { A = TK_BEFORE; }
trigger_time(A) ::= AFTER.       { A = TK_AFTER;  }
trigger_time(A) ::= INSTEAD OF.  { A = TK_INSTEAD;}
trigger_time(A) ::= .            { A = TK_BEFORE; }

%type trigger_event {struct TrigEvent}
%destructor trigger_event {sqlite3IdListDelete(pParse->db, $$.b);}
trigger_event(A) ::= DELETE|INSERT(OP).       {A.a = @OP; A.b = 0;}
trigger_event(A) ::= UPDATE(OP).              {A.a = @OP; A.b = 0;}
trigger_event(A) ::= UPDATE OF idlist(X). {A.a = TK_UPDATE; A.b = X;}

foreach_clause ::= .
foreach_clause ::= FOR EACH ROW.

%type when_clause {Expr*}
%destructor when_clause {sqlite3ExprDelete(pParse->db, $$);}
when_clause(A) ::= .             { A = 0; }
when_clause(A) ::= WHEN expr(X). { A = X.pExpr; }

%type trigger_cmd_list {TriggerStep*}
%destructor trigger_cmd_list {sqlite3DeleteTriggerStep(pParse->db, $$);}
trigger_cmd_list(A) ::= trigger_cmd_list(Y) trigger_cmd(X) SEMI. {
  assert( Y!=0 );
  Y->pLast->pNext = X;
  Y->pLast = X;
  A = Y;
}
trigger_cmd_list(A) ::= trigger_cmd(X) SEMI. { 
  assert( X!=0 );
  X->pLast = X;
  A = X;
}

// Disallow qualified table names on INSERT, UPDATE, and DELETE statements
// within a trigger.  The table to INSERT, UPDATE, or DELETE is always in 
// the same database as the table that the trigger fires on.
//
%type trnm {Token}
trnm(A) ::= nm(X).   {A = X;}
trnm(A) ::= nm DOT nm(X). {
  A = X;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}

// Disallow the INDEX BY and NOT INDEXED clauses on UPDATE and DELETE
// statements within triggers.  We make a specific error message for this
// since it is an exception to the default grammar rules.
//
tridxby ::= .
tridxby ::= INDEXED BY nm. {
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
tridxby ::= NOT INDEXED. {
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}



%type trigger_cmd {TriggerStep*}
%destructor trigger_cmd {sqlite3DeleteTriggerStep(pParse->db, $$);}
// UPDATE 
trigger_cmd(A) ::=
   UPDATE orconf(R) trnm(X) tridxby SET setlist(Y) where_opt(Z).  
   { A = sqlite3TriggerUpdateStep(pParse->db, &X, Y, Z, R); }

// INSERT
trigger_cmd(A) ::= insert_cmd(R) INTO trnm(X) idlist_opt(F) select(S).
               {A = sqlite3TriggerInsertStep(pParse->db, &X, F, S, R);}

// DELETE
trigger_cmd(A) ::= DELETE FROM trnm(X) tridxby where_opt(Y).
               {A = sqlite3TriggerDeleteStep(pParse->db, &X, Y);}

// SELECT
trigger_cmd(A) ::= select(X).  {A = sqlite3TriggerSelectStep(pParse->db, X); }

// The special RAISE expression that may occur in trigger programs
expr(A) ::= RAISE(X) LP IGNORE RP(Y).  {
  A.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0, 0); 
  if( A.pExpr ){
    A.pExpr->affinity = OE_Ignore;
  }
  A.zStart = X.z;
  A.zEnd = &Y.z[Y.n];
}
%ifndef MAXSCALE
expr(A) ::= RAISE(X) LP raisetype(T) COMMA nm(Z) RP(Y).  {
  A.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0, &Z); 
  if( A.pExpr ) {
    A.pExpr->affinity = (char)T;
  }
  A.zStart = X.z;
  A.zEnd = &Y.z[Y.n];
}
%endif
%endif  !SQLITE_OMIT_TRIGGER

%ifndef MAXSCALE
%type raisetype {int}
raisetype(A) ::= ROLLBACK.  {A = OE_Rollback;}
raisetype(A) ::= ABORT.     {A = OE_Abort;}
raisetype(A) ::= FAIL.      {A = OE_Fail;}
%endif


////////////////////////  DROP TRIGGER statement //////////////////////////////
%ifndef SQLITE_OMIT_TRIGGER
cmd ::= DROP TRIGGER ifexists(NOERR) fullname(X). {
  sqlite3DropTrigger(pParse,X,NOERR);
}
%endif  !SQLITE_OMIT_TRIGGER

//////////////////////// ATTACH DATABASE file AS name /////////////////////////
%ifndef SQLITE_OMIT_ATTACH
cmd ::= ATTACH database_kw_opt expr(F) AS expr(D) key_opt(K). {
  sqlite3Attach(pParse, F.pExpr, D.pExpr, K);
}
cmd ::= DETACH database_kw_opt expr(D). {
  sqlite3Detach(pParse, D.pExpr);
}

%type key_opt {Expr*}
%destructor key_opt {sqlite3ExprDelete(pParse->db, $$);}
key_opt(A) ::= .                     { A = 0; }
key_opt(A) ::= KEY expr(X).          { A = X.pExpr; }

database_kw_opt ::= DATABASE.
database_kw_opt ::= .
%endif SQLITE_OMIT_ATTACH

////////////////////////// REINDEX collation //////////////////////////////////
%ifndef SQLITE_OMIT_REINDEX
cmd ::= REINDEX.                {sqlite3Reindex(pParse, 0, 0);}
cmd ::= REINDEX nm(X) dbnm(Y).  {sqlite3Reindex(pParse, &X, &Y);}
%endif  SQLITE_OMIT_REINDEX

/////////////////////////////////// ANALYZE ///////////////////////////////////
%ifndef SQLITE_OMIT_ANALYZE
%ifdef MAXSCALE
analyze_options ::= .
analyze_options ::= NO_WRITE_TO_BINLOG.
analyze_options ::= LOCAL.

cmd ::= ANALYZE analyze_options TABLE fullnames(X).  {mxs_sqlite3Analyze(pParse, X);}
%endif
%ifndef MAXSCALE
cmd ::= ANALYZE.                {sqlite3Analyze(pParse, 0, 0);}
cmd ::= ANALYZE nm(X) dbnm(Y).  {sqlite3Analyze(pParse, &X, &Y);}
%endif
%endif

//////////////////////// ALTER TABLE table ... ////////////////////////////////
%ifndef SQLITE_OMIT_ALTERTABLE
%ifndef MAXSCALE
cmd ::= ALTER TABLE fullname(X) RENAME TO nm(Z). {
  sqlite3AlterRenameTable(pParse,X,&Z);
}

cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt column(Y). {
  sqlite3AlterFinishAddColumn(pParse, &Y);
}
%endif
%ifdef MAXSCALE
first_opt ::= .
first_opt ::= FIRST.

as_or_to_opt ::= .
as_or_to_opt ::= AS.
as_or_to_opt ::= TO.

cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt column(Y) first_opt. {
  mxs_sqlite3AlterFinishAddColumn(pParse, &Y);
}

cmd ::= ALTER TABLE fullname(X) ENABLE KEYS. {
  maxscaleAlterTable(pParse,MXS_ALTER_ENABLE_KEYS,X,0);
}

cmd ::= ALTER TABLE fullname(X) DISABLE KEYS. {
  maxscaleAlterTable(pParse,MXS_ALTER_DISABLE_KEYS,X,0);
}

cmd ::= ALTER TABLE fullname(X) RENAME as_or_to_opt nm(Z). {
  maxscaleAlterTable(pParse,MXS_ALTER_RENAME,X,&Z);
}
%endif
add_column_fullname ::= fullname(X). {
  disableLookaside(pParse);
#ifdef MAXSCALE
  mxs_sqlite3AlterBeginAddColumn(pParse, X);
#else
  sqlite3AlterBeginAddColumn(pParse, X);
#endif
}
kwcolumn_opt ::= .
kwcolumn_opt ::= COLUMNKW.
%endif  SQLITE_OMIT_ALTERTABLE

//////////////////////// CREATE VIRTUAL TABLE ... /////////////////////////////
%ifndef SQLITE_OMIT_VIRTUALTABLE
cmd ::= create_vtab.                       {sqlite3VtabFinishParse(pParse,0);}
cmd ::= create_vtab LP vtabarglist RP(X).  {sqlite3VtabFinishParse(pParse,&X);}
create_vtab ::= createkw VIRTUAL TABLE ifnotexists(E)
                nm(X) dbnm(Y) USING nm(Z). {
    sqlite3VtabBeginParse(pParse, &X, &Y, &Z, E);
}
vtabarglist ::= vtabarg.
vtabarglist ::= vtabarglist COMMA vtabarg.
vtabarg ::= .                       {sqlite3VtabArgInit(pParse);}
vtabarg ::= vtabarg vtabargtoken.
vtabargtoken ::= ANY(X).            {sqlite3VtabArgExtend(pParse,&X);}
vtabargtoken ::= lp anylist RP(X).  {sqlite3VtabArgExtend(pParse,&X);}
lp ::= LP(X).                       {sqlite3VtabArgExtend(pParse,&X);}
anylist ::= .
anylist ::= anylist LP anylist RP.
anylist ::= anylist ANY.
%endif  SQLITE_OMIT_VIRTUALTABLE


//////////////////////// COMMON TABLE EXPRESSIONS ////////////////////////////
%type with {With*}
%type wqlist {With*}
%destructor with {sqlite3WithDelete(pParse->db, $$);}
%destructor wqlist {sqlite3WithDelete(pParse->db, $$);}

with(A) ::= . {A = 0;}
%ifndef SQLITE_OMIT_CTE
with(A) ::= WITH wqlist(W).              { A = W; }
with(A) ::= WITH RECURSIVE wqlist(W).    { A = W; }

wqlist(A) ::= nm(X) eidlist_opt(Y) AS LP select(Z) RP. {
  A = sqlite3WithAdd(pParse, 0, &X, Y, Z);
}
wqlist(A) ::= wqlist(W) COMMA nm(X) eidlist_opt(Y) AS LP select(Z) RP. {
  A = sqlite3WithAdd(pParse, W, &X, Y, Z);
}
%endif  SQLITE_OMIT_CTE

%ifdef MAXSCALE
/*
** MaxScale additions.
**
** New grammar rules made for MaxScale follow here.
**
*/

cmd ::= do.

do ::= DO nexprlist(X). {
  maxscaleDo(pParse, X);
}

%type type_options {int}
type_options(A) ::= . {A=0;}
type_options(A) ::= type_options UNSIGNED. {A|=1;}
type_options(A) ::= type_options ZEROFILL. {A|=2;}
type_options(A) ::= type_options BINARY. {A|=4;}
type_options(A) ::= type_options CHARACTER SET ids. {A|=8;}
type_options(A) ::= type_options CHARSET ids. {A|=8;}

// deferred_id is used instead of id before the token_class id has been defined.
deferred_id(A) ::= id(X). {A=X;}

as_opt ::= .
as_opt ::= AS.

eq_opt ::= .
eq_opt ::= EQ.

default_opt ::= .
default_opt ::= DEFAULT.

//////////////////////// CALL statement ////////////////////////////////////
//
cmd ::= call.

%type call_args_opt {ExprList*}
call_args_opt(A) ::= . {A=0;}
call_args_opt(A) ::= LP exprlist(X) RP. {A=X;}

call ::= CALL fullname(X) call_args_opt(Y). {
    maxscaleCall(pParse, X, Y);
}

//////////////////////// DROP FUNCTION statement ////////////////////////////////////
//
cmd ::= DROP FUNCTION_KW ifexists nm(X). {
  maxscaleDrop(pParse, MXS_DROP_FUNCTION, NULL, &X);
}

//////////////////////// The CHECK TABLE statement ////////////////////////////////////
//
cmd ::= check.

// FOR UPGRADE | QUICK | FAST | MEDIUM | EXTENDED | CHANGED
check_option ::= FOR id.
check_option ::= QUICK.
check_option ::= id.

check_options ::= check_option.
check_options ::= check_options check_option.

check_options_opt ::= .
check_options_opt ::= check_options.


check ::= CHECK TABLE fullnames(X) check_options_opt. {
  maxscaleCheckTable(pParse, X);
}

//////////////////////// The FLUSH statement ////////////////////////////////////
//
cmd ::= flush.

flush ::= FLUSH STATUS(X).
{
  maxscaleFlush(pParse, &X);
}

flush ::= FLUSH nm(X).
{
  maxscaleFlush(pParse, &X);
}

//////////////////////// The GRANT & REVOKE statements ////////////////////////////////////
//
cmd ::= grant.

grant ::= GRANT. {
    maxscalePrivileges(pParse, TK_GRANT);
}

cmd ::= revoke.

revoke ::= REVOKE. {
    maxscalePrivileges(pParse, TK_REVOKE);
}

//////////////////////// The HANDLER statement ////////////////////////////////////
//
cmd ::= handler.

handler ::= HANDLER fullname(X) OPEN as_opt nm(Y). {
  maxscaleHandler(pParse, MXS_HANDLER_OPEN, X, &Y);
}

handler ::= HANDLER nm(X) CLOSE. {
  maxscaleHandler(pParse, MXS_HANDLER_CLOSE, 0, &X);
}

// TODO: Rest of HANDLER commands.

//////////////////////// The LOAD DATA INFILE statement ////////////////////////////////////
//

%type ld_local_opt {int}

cmd ::= load_data.

ld_priority_opt ::= .
ld_priority_opt ::= LOW_PRIORITY.
ld_priority_opt ::= CONCURRENT.

ld_local_opt(A) ::= .      {A = 0;}
ld_local_opt(A) ::= LOCAL. {A = 1;}

ld_charset_opt ::= .
ld_charset_opt ::= CHARACTER SET ids.

load_data ::= LOAD DATA ld_priority_opt ld_local_opt(Y)
              INFILE STRING ignore_or_replace_opt
              INTO TABLE fullname(X)
              /* ld_partition_opt */
              ld_charset_opt
              /* ld_fields_opt */
              /* ld_ignore_opt */
              /* ld_col_name_or_user_var_opt */
              /* ld_set */.
{
    maxscaleLoadData(pParse, X, Y);
}

//////////////////////// The LOCK/UNLOCK statement ////////////////////////////////////
//
cmd ::= lock.

lock ::= LOCK table_or_tables lock_target(X).{
  maxscaleLock(pParse, MXS_LOCK_LOCK, X);
}

%type lock_target {SrcList*}
%destructor lock_target {sqlite3SrcListDelete(pParse->db, $$);}

lock_target(A) ::= table_factor(X) lock_type. {
  A = X;
}
lock_target(A) ::= lock_target(X) COMMA table_factor(Y). {
  A = sqlite3SrcListCat(pParse->db, X, Y);
}

%type lock_type {int}

lock_type(A) ::= READ. { A = 1; }
lock_type(A) ::= READ LOCAL. { A = 3; }
lock_type(A) ::= WRITE. { A = 4; }
lock_type(A) ::= LOW_PRIORITY WRITE. { A = 12; }


cmd ::= unlock.

unlock ::= UNLOCK TABLES. {
  maxscaleLock(pParse, MXS_LOCK_UNLOCK, 0);
}

//////////////////////// PREPARE and EXECUTE statements ////////////////////////////////////
//
cmd ::= prepare.
cmd ::= execute.
cmd ::= deallocate.

prepare ::= PREPARE nm(X) FROM expr(Y).
{
  maxscalePrepare(pParse, &X, Y.pExpr);
}

%type execute_variable {int}
execute_variable(A) ::= INTEGER.  {A=0;} // For Oracle
execute_variable(A) ::= VARIABLE. {A=QUERY_TYPE_USERVAR_READ;}

%type execute_variables {int}
execute_variables(A) ::= execute_variable(X). {A=X;}
execute_variables(A) ::= execute_variables(X) COMMA execute_variable(Y). {
  A = X|Y;
}

%type execute_variables_opt {int}

execute_variables_opt(A) ::= .                           {A=0;}
execute_variables_opt(A) ::= USING execute_variables(X). {A=X;}

execute ::= EXECUTE nm(X) execute_variables_opt(Y). {
    maxscaleExecute(pParse, &X, Y);
}

execute ::= EXECUTE id(X) expr(Y) execute_variables_opt(Z). {
    maxscaleExecuteImmediate(pParse, &X, &Y, Z);
}

dod ::= DEALLOCATE.
dod ::= DROP.

deallocate ::= dod PREPARE nm(X). {
  maxscaleDeallocate(pParse, &X);
}

//////////////////////// RENAME statement ////////////////////////////////////
//
cmd ::= rename.

%type table_to_rename {SrcList*}
%destructor table_to_rename {sqlite3SrcListDelete(pParse->db, $$);}

table_to_rename(A) ::= fullname(X) TO nm(Y). {
  // The new name is passed in the alias field.
  X->a[0].zAlias = sqlite3NameFromToken(pParse->db, &Y);
  A = X;
}

%type tables_to_rename {SrcList*}
%destructor tables_to_rename {sqlite3SrcListDelete(pParse->db, $$);}

tables_to_rename(A) ::= table_to_rename(X). {
  A = X;
}
tables_to_rename(A) ::= tables_to_rename(X) COMMA table_to_rename(Y).
{
  A = sqlite3SrcListCat(pParse->db, X, Y);
}

rename ::= RENAME TABLE tables_to_rename(X). {
  maxscaleRenameTable(pParse, X);
}

//////////////////////// The SET statement ////////////////////////////////////
//
%type set_scope {int}

set_scope(A) ::= .           { A = 0; }
set_scope(A) ::= GLOBAL(X).  { A = @X; }
set_scope(A) ::= SESSION(X). { A = @X; }
set_scope(A) ::= LOCAL.      { A = TK_SESSION; }

%type variable {ExprSpan}
%destructor variable { sqlite3ExprDelete(pParse->db, $$.pExpr); }

%type variable_head {ExprSpan}
%destructor variable_head { sqlite3ExprDelete(pParse->db, $$.pExpr); }

%type variable_tail {ExprSpan}
%destructor variable_tail { sqlite3ExprDelete(pParse->db, $$.pExpr); }

variable_head(A) ::= VARIABLE(X). {
  A.pExpr = sqlite3PExpr(pParse, TK_VARIABLE, 0, 0, &X);
  spanSet(&A, &X, &X);
}

variable_head(A) ::= id(X). {
  A.pExpr = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  spanSet(&A, &X, &X);
}

variable_tail(A) ::= DOT(D) id(X). {
  Expr* pName = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
  A.pExpr = sqlite3PExpr(pParse, TK_DOT, 0, pName, 0);
  spanSet(&A, &D, &X);
}

variable_tail(A) ::= variable_tail(X) DOT id(Y). {
  assert(!X.pExpr->pLeft);
  X.pExpr->pLeft = X.pExpr->pRight;
  Expr* pName = sqlite3PExpr(pParse, TK_ID, 0, 0, &Y);
  X.pExpr->pRight = pName;
  A = X;
  A.zStart = X.zStart;
  A.zEnd = X.zEnd;
}

variable(A) ::= variable_head(X). {
  A = X;
}

variable(A) ::= variable_head(X) variable_tail(Y). {
  A.pExpr = sqlite3PExpr(pParse, TK_DOT, X.pExpr, Y.pExpr, 0);
  A.zStart = X.zStart;
  A.zEnd = Y.zEnd;
}

%type variable_assignment {Expr*}
%destructor variable_assignment {sqlite3ExprDelete(pParse->db, $$);}

variable_assignment(A) ::= set_scope variable(X) EQ expr(Y). {
  A = sqlite3PExpr(pParse, TK_EQ, X.pExpr, Y.pExpr, 0);
}

variable_assignment(A) ::= set_scope variable(X) EQ ON. {
  Expr* pOn = sqlite3PExpr(pParse, TK_INTEGER, 0, 0, 0);
  pOn->u.iValue = 1;
  A = sqlite3PExpr(pParse, TK_EQ, X.pExpr, pOn, 0);
}

variable_assignment(A) ::= set_scope variable(X) EQ ALL. {
  Expr* pOn = sqlite3PExpr(pParse, TK_INTEGER, 0, 0, 0);
  pOn->u.iValue = 1;
  A = sqlite3PExpr(pParse, TK_EQ, X.pExpr, pOn, 0);
}

set_names_arg ::= ids.             // 'charset_name'
set_names_arg ::= ids COLLATE ids. // 'charset_name' COLLATE 'collation_name'
set_names_arg ::= DEFAULT.

variable_assignment(A) ::= NAMES set_names_arg. {
  A = sqlite3PExpr(pParse, TK_NAMES, 0, 0, 0);
}

set_character_set_arg ::= ids.
set_character_set_arg ::= DEFAULT.

variable_assignment(A) ::= CHARACTER SET set_character_set_arg. {
  A = sqlite3PExpr(pParse, TK_CHARACTER, 0, 0, 0);
}

%type variable_assignments {ExprList*}
%destructor variable_assignments {sqlite3ExprListDelete(pParse->db, $$);}

variable_assignments(A) ::= variable_assignment(X). {
  A = sqlite3ExprListAppend(pParse, 0, X);
}

variable_assignments(A) ::= variable_assignments(X) COMMA variable_assignment(Y). {
  A = sqlite3ExprListAppend(pParse, X, Y);
}

cmd ::= SET variable_assignments(Y). {
  maxscaleSet(pParse, 0, MXS_SET_VARIABLES, Y);
}

transaction_characteristic ::= READ WRITE.
transaction_characteristic ::= READ id.                 // READ ONLY
transaction_characteristic ::= id id transaction_level. // ISOLATION LEVEL transaction_level

transaction_level ::= id READ. // REPEATABLE READ
transaction_level ::= READ id. // {READ COMMITTED|READ UNCOMMITTED}
transaction_level ::= id.      // SERIALIZABLE

transaction_characteristics ::= transaction_characteristic.
transaction_characteristics ::= transaction_characteristics COMMA transaction_characteristic.

cmd ::= SET set_scope(X) TRANSACTION transaction_characteristics. {
  maxscaleSet(pParse, X, MXS_SET_TRANSACTION, 0);
}

cmd ::= SET STATEMENT variable_assignments(X) FOR cmd. {
  // The parsing of cmd will cause the relevant maxscale-callback to
  // be called, so we neither need to call it here, nor free cmd (as
  // it will be freed by that callback). The variable definitions we
  // just throw away, as they are of no interest.
  sqlite3ExprListDelete(pParse->db, X);
}

//////////////////////// The USE statement ////////////////////////////////////
//
cmd ::= use(X). {
  maxscaleUse(pParse, &X);
}

%type use {Token}

use(A) ::= USE id(X). {A = X;}

//////////////////////// The SHOW statement ////////////////////////////////////
//

cmd ::= show(X). {
  maxscaleShow(pParse, &X);
}

from_or_in ::= FROM.
from_or_in ::= IN.

from_or_in_db_opt(A) ::= . { A.z = 0; A.n = 0; }
from_or_in_db_opt(A) ::= FROM nm(X). { A = X; }
from_or_in_db_opt(A) ::= IN nm(X). { A = X; }

// sqlite returns FULL (as well as CROSS, INNER, LEFT,
// NATURAL, OUTER and RIGHT) as JOIN_KW.
%type full_opt {u32}
full_opt(A) ::= . { A = 0; }
full_opt(A) ::= JOIN_KW. { A = MXS_SHOW_COLUMNS_FULL; }

like_or_where_opt ::= .
like_or_where_opt ::= LIKE_KW ids.
like_or_where_opt ::= WHERE expr.

%type show {MxsShow}

show(A) ::= SHOW full_opt(X) COLUMNS from_or_in nm(Y) dbnm(Z) from_or_in_db_opt(W) like_or_where_opt . {
  A.what = MXS_SHOW_COLUMNS;
  A.data = X;
  if (Z.z) {
      A.pName = &Z;
      A.pDatabase = &Y;
  }
  else if (W.z) {
      A.pName = &Y;
      A.pDatabase = &W;
  }
  else {
      A.pName = &Y;
      A.pDatabase = NULL;
  }
}

show(A) ::= SHOW CREATE TABLE nm(X) dbnm(Y). {
  A.what = MXS_SHOW_CREATE_TABLE;
  A.data = 0;
  if (Y.z) {
      A.pName = &Y;
      A.pDatabase = &X;
  }
  else {
      A.pName = &X;
      A.pDatabase = NULL;
  }
}

show(A) ::= SHOW CREATE VIEW nm(X) dbnm(Y). {
  A.what = MXS_SHOW_CREATE_VIEW;
  A.data = 0;
  if (Y.z) {
      A.pName = &Y;
      A.pDatabase = &X;
  }
  else {
      A.pName = &X;
      A.pDatabase = NULL;
  }
}

show(A) ::= SHOW CREATE SEQUENCE nm(X) dbnm(Y). {
  A.what = MXS_SHOW_CREATE_SEQUENCE;
  A.data = 0;
  if (Y.z) {
      A.pName = &Y;
      A.pDatabase = &X;
  }
  else {
      A.pName = &X;
      A.pDatabase = NULL;
  }
}

show(A) ::= SHOW DATABASES_KW like_or_where_opt. {
  A.what = MXS_SHOW_DATABASES;
  A.data = 0;
  A.pName = NULL;
  A.pDatabase = NULL;
}

show(A) ::= SHOW ALL id STATUS. { // SHOW ALL SLAVES STATUS
  A.what = MXS_SHOW_STATUS;
  A.data = MXS_SHOW_STATUS_ALL_SLAVES;
  A.pName = NULL;
  A.pDatabase = NULL;
}

show(A) ::= SHOW MASTER STATUS. {
  A.what = MXS_SHOW_STATUS;
  A.data = MXS_SHOW_STATUS_MASTER;
  A.pName = NULL;
  A.pDatabase = NULL;
}

show(A) ::= SHOW SLAVE STATUS. {
  A.what = MXS_SHOW_STATUS;
  A.data = MXS_SHOW_STATUS_SLAVE;
  A.pName = NULL;
  A.pDatabase = NULL;
}

%type index_indexes_keys {int}

index_indexes_keys(A) ::= INDEX. {A = MXS_SHOW_INDEX;}
index_indexes_keys(A) ::= INDEXES. {A = MXS_SHOW_INDEXES;}
index_indexes_keys(A) ::= KEYS. {A = MXS_SHOW_KEYS;}

show(A) ::= SHOW index_indexes_keys(X) from_or_in nm(Y) dbnm(Z) from_or_in_db_opt where_opt . {
  A.what = X;
  A.data = 0;
  if (Z.z) {
      A.pName = &Z;
      A.pDatabase = &Y;
  }
  else {
      A.pName = &Y;
      A.pDatabase = NULL;
  }
}

%type global_session_local_opt {int}
global_session_local_opt(A) ::= .        {A=MXS_SHOW_VARIABLES_UNSPECIFIED;}
global_session_local_opt(A) ::= GLOBAL.  {A=MXS_SHOW_VARIABLES_GLOBAL;}
global_session_local_opt(A) ::= LOCAL.   {A=MXS_SHOW_VARIABLES_SESSION;}
global_session_local_opt(A) ::= SESSION. {A=MXS_SHOW_VARIABLES_SESSION;}

show(A) ::= SHOW global_session_local_opt(X) STATUS like_or_where_opt. {
  A.what = MXS_SHOW_STATUS;
  A.data = X;
  A.pName = NULL;
  A.pDatabase = NULL;
}

show(A) ::= SHOW full_opt(X) TABLES from_or_in_db_opt(Y) like_or_where_opt. {
  A.what = MXS_SHOW_TABLES;
  A.data = X;
  A.pDatabase = &Y;
  A.pName = NULL;
}

show(A) ::= SHOW TABLE STATUS from_or_in_db_opt(X) like_or_where_opt. {
  A.what = MXS_SHOW_TABLE_STATUS;
  A.data = 0;
  A.pDatabase = &X;
  A.pName = NULL;
}

show(A) ::= SHOW global_session_local_opt(X) VARIABLES like_or_where_opt. {
  A.what = MXS_SHOW_VARIABLES;
  A.data = X;
  A.pName = NULL;
  A.pDatabase = NULL;
}

show_warnings_options ::= .
show_warnings_options ::= LIMIT INTEGER.
show_warnings_options ::= LIMIT INTEGER COMMA INTEGER.

show(A) ::= SHOW WARNINGS show_warnings_options. {
   A.what = MXS_SHOW_WARNINGS;
   A.data = 0;
   A.pName = NULL;
   A.pDatabase = NULL;
}

//////////////////////// The START TRANSACTION statement ////////////////////////////////////
//

%type start_transaction_characteristic {int}

start_transaction_characteristic(A) ::= READ WRITE. {
  A = QUERY_TYPE_WRITE;
}

start_transaction_characteristic(A) ::= READ id. { // READ ONLY
  A = QUERY_TYPE_READ;
}

start_transaction_characteristic(A) ::= WITH id id. { // WITH CONSISTENT SNAPSHOT
  A = 0;
}

%type start_transaction_characteristics {int}

start_transaction_characteristics(A) ::= .
{
  A = 0;
}

start_transaction_characteristics(A) ::= start_transaction_characteristic(X).
{
  A = X;
}

start_transaction_characteristics(A) ::=
    start_transaction_characteristics(X) COMMA start_transaction_characteristic(Y). {
  A = X | Y;
}

cmd ::= START TRANSACTION start_transaction_characteristics(X). {
  mxs_sqlite3BeginTransaction(pParse, TK_START, X);
}

//////////////////////// The TRUNCATE statement ////////////////////////////////////
//

table_opt ::= .
table_opt ::= TABLE.

cmd ::= TRUNCATE table_opt nm(X) dbnm(Y). {
  Token* pName;
  Token* pDatabase;
  if (Y.z) {
    pDatabase = &X;
    pName = &Y;
  }
  else {
    pDatabase = NULL;
    pName = &X;
  }

  maxscaleTruncate(pParse, pDatabase, pName);
}

//////////////////////// ORACLE Assignment ////////////////////////////////////
//
oracle_assignment ::= id(X) EQ expr(Y). {
    Expr* pX = sqlite3PExpr(pParse, TK_ID, 0, 0, &X);
    Expr* pExpr = sqlite3PExpr(pParse, TK_EQ, pX, Y.pExpr, 0);
    ExprList* pExprList = sqlite3ExprListAppend(pParse, 0, pExpr);
    maxscaleSet(pParse, 0, MXS_SET_VARIABLES, pExprList);
}

//////////////////////// ORACLE CREATE SEQUENCE ////////////////////////////////////
//
cmd ::= CREATE SEQUENCE nm(X) dbnm(Y).{ // CREATE SEQUENCE db
    Token* pDatabase;
    Token* pTable;
    if (Y.z)
    {
        pDatabase = &X;
        pTable = &Y;
    }
    else
    {
        pDatabase = NULL;
        pTable = &X;
    }
    maxscaleCreateSequence(pParse, pDatabase, pTable);
}

//////////////////////// ORACLE CREATE SEQUENCE ////////////////////////////////////
//
cmd ::= DROP SEQUENCE nm(X) dbnm(Y).{ // CREATE SEQUENCE db
    Token* pDatabase;
    Token* pTable;
    if (Y.z)
    {
        pDatabase = &X;
        pTable = &Y;
    }
    else
    {
        pDatabase = NULL;
        pTable = &X;
    }
    maxscaleDrop(pParse, MXS_DROP_SEQUENCE, pDatabase, pTable);
}

//////////////////////// ORACLE DECLARE ////////////////////////////////////
//
cmd ::= DECLARE. {
    maxscaleDeclare(pParse);
}

%endif
