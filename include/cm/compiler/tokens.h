#ifndef CM_TOKENS_H
#define CM_TOKENS_H

#include "cm/core.h"
#include "cm/string.h"

typedef enum {
    CM_TOK_EOF = 0,
    CM_TOK_IDENTIFIER,
    CM_TOK_STRING_LITERAL,
    CM_TOK_NUMBER,
    CM_TOK_LPAREN,
    CM_TOK_RPAREN,
    CM_TOK_LBRACE,
    CM_TOK_RBRACE,
    CM_TOK_LBRACKET,
    CM_TOK_RBRACKET,
    CM_TOK_SEMI,
    CM_TOK_COMMA,
    CM_TOK_COLON,
    CM_TOK_EQUAL,

    /* Operators */
    CM_TOK_PLUS,
    CM_TOK_MINUS,
    CM_TOK_STAR,
    CM_TOK_SLASH,
    CM_TOK_DOT,
    CM_TOK_LT,
    CM_TOK_GT,
    CM_TOK_BANG,
    CM_TOK_AMPERSAND,
    CM_TOK_PIPE,
    /* New CM v2 operators */
    CM_TOK_COLON_EQUAL,  /* := inference assignment */
    CM_TOK_DEREF,        /* ^ postfix dereference */
    CM_TOK_ADDR_OF,      /* ^ prefix address-of */
    CM_TOK_QUESTION,     /* ? option type */
    CM_TOK_DOUBLE_QUESTION, /* ?? null coalescing */
    CM_TOK_AT,           /* @ attribute */
    CM_TOK_ARROW,        /* -> function return type */
    CM_TOK_FAT_ARROW,    /* => lambda/fat arrow */
    
    /* Comparison operators (multi-char) */
    CM_TOK_EQUAL_EQUAL,  /* == */
    CM_TOK_NOT_EQUAL,    /* != */
    CM_TOK_LT_EQUAL,     /* <= */
    CM_TOK_GT_EQUAL,     /* >= */
    CM_TOK_AND_AND,      /* && */
    CM_TOK_PIPE_PIPE,    /* || */

    /* New CM v2 keywords */
    CM_TOK_KW_LET,          /* let immutable declaration */
    CM_TOK_KW_MUT,          /* mut mutable declaration */
    CM_TOK_KW_FN,           /* fn function keyword */
    CM_TOK_KW_STRUCT,       /* struct type definition */
    CM_TOK_KW_UNION,        /* union sum type */
    CM_TOK_KW_IMPL,         /* impl implementation block */
    CM_TOK_KW_MATCH,        /* match expression */
    CM_TOK_KW_SOME,         /* Some variant of Option */
    CM_TOK_KW_NONE,         /* None variant of Option */
    CM_TOK_KW_OK,           /* Ok variant of Result */
    CM_TOK_KW_ERR,          /* Err variant of Result */
    CM_TOK_KW_RESULT,       /* Result type */
    CM_TOK_KW_OPTION,       /* Option type */
    CM_TOK_KW_ARRAY,        /* array<T> type */
    CM_TOK_KW_SLICE,        /* slice<T> type */
    CM_TOK_KW_MAP,          /* map<K,V> type */
    CM_TOK_KW_BOOL,         /* bool type */
    CM_TOK_KW_INT,          /* int type */
    CM_TOK_KW_FLOAT,        /* float type */
    CM_TOK_KW_STRING,       /* string type */
    CM_TOK_KW_VOID,         /* void type */
    CM_TOK_KW_TRUE,         /* true literal */
    CM_TOK_KW_FALSE,        /* false literal */
    CM_TOK_KW_IF,           /* if statement */
    CM_TOK_KW_ELSE,         /* else statement */
    CM_TOK_KW_WHILE,        /* while loop */
    CM_TOK_KW_FOR,          /* for loop */
    CM_TOK_KW_RETURN,       /* return statement */
    CM_TOK_KW_BREAK,        /* break statement */
    CM_TOK_KW_CONTINUE,     /* continue statement */
    CM_TOK_KW_MUTABLE,      /* mutable parameter marker */
    CM_TOK_KW_IMPORT,       /* import module */
    CM_TOK_KW_C,            /* c block keyword */
    CM_TOK_KW_GC,           /* gc namespace */
    CM_TOK_KW_PRINT,        /* print function */
    CM_TOK_KW_MALLOC,       /* malloc function */
    CM_TOK_KW_FREE,         /* free function */

    /* C#-like professional keywords (legacy) */
    CM_TOK_KW_NAMESPACE,
    CM_TOK_KW_USING,
    CM_TOK_KW_CLASS,
    CM_TOK_KW_PUBLIC,
    CM_TOK_KW_PRIVATE,
    CM_TOK_KW_STATIC,
    CM_TOK_KW_ASYNC,
    CM_TOK_KW_AWAIT,
    CM_TOK_KW_TASK,
    CM_TOK_KW_TRY,
    CM_TOK_KW_CATCH,
    CM_TOK_KW_FINALLY,
    CM_TOK_KW_THROW,
    CM_TOK_KW_GET,
    CM_TOK_KW_SET,
    CM_TOK_KW_DOUBLE,
    CM_TOK_KW_NULL,
    CM_TOK_KW_INTERFACE,
    CM_TOK_KW_IMPLEMENTS,
    CM_TOK_KW_EXTENDS,
    CM_TOK_KW_FROM,
    CM_TOK_KW_PACKAGE,

    /* Legacy keywords */
    CM_TOK_KW_INPUT,
    CM_TOK_KW_REQUIRE,

    /* Dynamic data structures (legacy) */
    CM_TOK_KW_LIST,
    CM_TOK_KW_NEW,
    CM_TOK_KW_PTR,

    /* Native API keywords (legacy) */
    CM_TOK_KW_STR,
    CM_TOK_KW_GC_COLLECT,

    /* Classified identifiers */
    CM_TOK_FUNCTION_NAME,
    CM_TOK_METHOD_NAME,

    /* Special */
    CM_TOK_COMMENT,
    CM_TOK_CPP_BLOCK,
    CM_TOK_C_BLOCK,
    
    /* New v2 specific */
    CM_TOK_INTERPOLATED_STRING,  /* "hello {name}" */
    CM_TOK_RAW_STRING           /* r"raw string" */
} cm_token_kind_t;

typedef struct {
    cm_token_kind_t kind;
    cm_string_t*    lexeme;
    size_t          line;
    size_t          column;
} cm_token_t;

/* Token functions */
void cm_token_free(cm_token_t* tok);

#endif
