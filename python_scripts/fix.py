import re

# Fix lexer.c
with open('src/compiler/lexer/lexer.c', 'r') as f:
    code = f.read()

# Remove static from cm_lexer calls
code = code.replace('static void cm_lexer_init', 'void cm_lexer_init')
code = code.replace('static cm_token_t cm_lexer_next_token', 'cm_token_t cm_lexer_next_token')

# Add missing breaks in the switch statement for =, !, <, >, &, |
code = re.sub(
    r'(if \(cm_lexer_peek\(lx\) == [^\)]+\) \{ cm_lexer_next\(lx\); return cm_make_token\([^;]+;\s*\})\n',
    r'\1\n            break;\n',
    code
)
# For the `=>` logic
code = re.sub(
    r'(if \(cm_lexer_peek\(lx\) == \'>\'\) \{ cm_lexer_next\(lx\); return cm_make_token\(CM_TOK_IDENTIFIER, "=>"[^;]+;\s*\})\n',
    r'\1\n            break;\n',
    code
)

with open('src/compiler/lexer/lexer.c', 'w') as f:
    f.write(code)

# Fix ast.c
with open('src/compiler/ast/ast.c', 'r') as f:
    code = f.read()

code = code.replace('static cm_ast_node_t* cm_ast_new', 'cm_ast_node_t* cm_ast_new')
code = code.replace('static void cm_ast_list_append', 'void cm_ast_list_append')
code = code.replace('static void cm_ast_free_list', 'void cm_ast_free_list')

with open('src/compiler/ast/ast.c', 'w') as f:
    f.write(code)

print("Fixed statics and breaks.")

