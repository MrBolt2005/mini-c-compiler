#define _POSIX_C_SOURCE 200809L
#include "symbol_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

// Platform-specific alignment and size constants
#define CHAR_SIZE 1
#define CHAR_ALIGN 1
#define SHORT_SIZE 2
#define SHORT_ALIGN 2
#define INT_SIZE 4
#define INT_ALIGN 4
#define LONG_SIZE 8
#define LONG_ALIGN 8
#define FLOAT_SIZE 4
#define FLOAT_ALIGN 4
#define DOUBLE_SIZE 8
#define DOUBLE_ALIGN 8
#define POINTER_SIZE 8
#define POINTER_ALIGN 8
#define BOOL_SIZE 1
#define BOOL_ALIGN 1

// Utility function for string duplication
static char *string_duplicate(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char *copy = malloc(len);
    if (!copy) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }
    strcpy(copy, str);
    return copy;
}

// Deep copy function for type_info_t with proper memory management
static type_info_t deep_copy_type_info(const type_info_t *src) {
    type_info_t result;
    result.base_type = src->base_type ? string_duplicate(src->base_type) : NULL;
    result.pointer_level = src->pointer_level;
    result.is_array = src->is_array;
    result.is_vla = src->is_vla;
    result.is_function = src->is_function;
    result.is_struct = src->is_struct;
    result.is_union = src->is_union;
    result.is_enum = src->is_enum;
    result.is_incomplete = src->is_incomplete;
    result.storage_class = src->storage_class;
    result.qualifiers = src->qualifiers;
    result.array_size = NULL;
    result.param_types = NULL;
    result.param_count = src->param_count;
    result.is_variadic = src->is_variadic;
    return result;
}

// Alignment calculation helper
static size_t align_to(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

// Create a new symbol table
symbol_table_t *create_symbol_table(void) {
    symbol_table_t *table = malloc(sizeof(symbol_table_t));
    if (!table) {
        fprintf(stderr, "Failed to allocate symbol table\n");
        exit(1);
    }
    
    // Create global scope
    table->global_scope = malloc(sizeof(scope_t));
    table->global_scope->symbols = NULL;
    table->global_scope->level = 0;
    table->global_scope->parent = NULL;
    
    table->current_scope = table->global_scope;
    table->scope_counter = 0;
    table->temp_counter = 0;
    table->current_function = NULL;
    
    return table;
}

// Free a single symbol and all its memory
void free_symbol(symbol_t *sym) {
    if (!sym) return;
    
    free(sym->name);
    free(sym->llvm_name);
    free_type_info(&sym->type_info);
    
    if (sym->array_sizes) {
        free(sym->array_sizes);
    }
    
    if (sym->members) {
        // Note: Don't free the members themselves here as they might be shared
        // The actual member symbols are freed when their scope is destroyed
        free(sym->members);
    }
    
    free(sym->label_name);
    free(sym);
}

// Clean up symbols in a scope
static void free_scope_symbols(symbol_t *symbols) {
    symbol_t *sym = symbols;
    while (sym) {
        symbol_t *next = sym->next;
        free_symbol(sym);
        sym = next;
    }
}

// Destroy symbol table and free all memory
void destroy_symbol_table(symbol_table_t *table) {
    if (!table) return;
    
    // Exit all scopes to clean up
    while (table->current_scope != table->global_scope) {
        exit_scope(table);
    }
    
    // Clean up global scope
    free_scope_symbols(table->global_scope->symbols);
    free(table->global_scope);
    
    free(table->current_function);
    free(table);
}

// Enter a new scope
void enter_scope(symbol_table_t *table) {
    scope_t *new_scope = malloc(sizeof(scope_t));
    new_scope->symbols = NULL;
    new_scope->level = ++table->scope_counter;
    new_scope->parent = table->current_scope;
    table->current_scope = new_scope;
}

// Exit current scope
void exit_scope(symbol_table_t *table) {
    if (table->current_scope == table->global_scope) {
        return; // Can't exit global scope
    }
    
    scope_t *old_scope = table->current_scope;
    table->current_scope = old_scope->parent;
    table->scope_counter--;
    
    // Free all symbols in the exiting scope
    free_scope_symbols(old_scope->symbols);
    free(old_scope);
}

// Calculate size of a basic type
static size_t get_basic_type_size(const char *type_name) {
    if (strcmp(type_name, "char") == 0) return CHAR_SIZE;
    if (strcmp(type_name, "short") == 0) return SHORT_SIZE;
    if (strcmp(type_name, "int") == 0) return INT_SIZE;
    if (strcmp(type_name, "long") == 0) return LONG_SIZE;
    if (strcmp(type_name, "float") == 0) return FLOAT_SIZE;
    if (strcmp(type_name, "double") == 0) return DOUBLE_SIZE;
    if (strcmp(type_name, "_Bool") == 0) return BOOL_SIZE;
    if (strcmp(type_name, "void") == 0) return 0; // void has no size
    
    // For signed/unsigned variants
    if (strstr(type_name, "char")) return CHAR_SIZE;
    if (strstr(type_name, "short")) return SHORT_SIZE;
    if (strstr(type_name, "int")) return INT_SIZE;
    if (strstr(type_name, "long")) return LONG_SIZE;
    
    return INT_SIZE; // Default
}

// Calculate alignment of a basic type
static size_t get_basic_type_alignment(const char *type_name) {
    if (strcmp(type_name, "char") == 0) return CHAR_ALIGN;
    if (strcmp(type_name, "short") == 0) return SHORT_ALIGN;
    if (strcmp(type_name, "int") == 0) return INT_ALIGN;
    if (strcmp(type_name, "long") == 0) return LONG_ALIGN;
    if (strcmp(type_name, "float") == 0) return FLOAT_ALIGN;
    if (strcmp(type_name, "double") == 0) return DOUBLE_ALIGN;
    if (strcmp(type_name, "_Bool") == 0) return BOOL_ALIGN;
    if (strcmp(type_name, "void") == 0) return 1; // void* alignment
    
    // For signed/unsigned variants
    if (strstr(type_name, "char")) return CHAR_ALIGN;
    if (strstr(type_name, "short")) return SHORT_ALIGN;
    if (strstr(type_name, "int")) return INT_ALIGN;
    if (strstr(type_name, "long")) return LONG_ALIGN;
    
    return INT_ALIGN; // Default
}

// Calculate type size including arrays, pointers, structs, etc.
size_t calculate_type_size(type_info_t *type_info, symbol_table_t *table) {
    if (!type_info || !type_info->base_type) {
        return 0;
    }
    
    // Pointers have fixed size regardless of what they point to
    if (type_info->pointer_level > 0) {
        return POINTER_SIZE;
    }
    
    size_t base_size;
    
    // Handle different type categories
    if (type_info->is_struct) {
        symbol_t *struct_sym = find_symbol(table, type_info->base_type);
        if (struct_sym && struct_sym->sym_type == SYM_STRUCT) {
            return calculate_struct_size(struct_sym);
        }
        return 0; // Unknown struct
    }
    
    if (type_info->is_union) {
        symbol_t *union_sym = find_symbol(table, type_info->base_type);
        if (union_sym && union_sym->sym_type == SYM_UNION) {
            return calculate_union_size(union_sym);
        }
        return 0; // Unknown union
    }
    
    if (type_info->is_enum) {
        return INT_SIZE; // Enums are typically int-sized
    }
    
    // Basic types
    base_size = get_basic_type_size(type_info->base_type);
    
    // Handle arrays
    if (type_info->is_array) {
        // For now, we'll assume single-dimensional arrays
        // Multi-dimensional arrays would need more complex calculation
        if (type_info->array_size && type_info->array_size->type == AST_NUMBER) {
            int array_length = type_info->array_size->data.number.value;
            return base_size * array_length;
        }
        // VLA or incomplete array - return base size for pointer decay
        return POINTER_SIZE;
    }
    
    return base_size;
}

// Calculate type alignment
size_t calculate_type_alignment(type_info_t *type_info, symbol_table_t *table) {
    if (!type_info || !type_info->base_type) {
        return 1;
    }
    
    // Pointers have pointer alignment
    if (type_info->pointer_level > 0) {
        return POINTER_ALIGN;
    }
    
    // Handle different type categories
    if (type_info->is_struct) {
        symbol_t *struct_sym = find_symbol(table, type_info->base_type);
        if (struct_sym && struct_sym->sym_type == SYM_STRUCT) {
            return struct_sym->max_alignment;
        }
        return 1;
    }
    
    if (type_info->is_union) {
        symbol_t *union_sym = find_symbol(table, type_info->base_type);
        if (union_sym && union_sym->sym_type == SYM_UNION) {
            return union_sym->max_alignment;
        }
        return 1;
    }
    
    if (type_info->is_enum) {
        return INT_ALIGN;
    }
    
    // Arrays have same alignment as their element type
    return get_basic_type_alignment(type_info->base_type);
}

// Calculate struct size with proper padding and alignment
size_t calculate_struct_size(symbol_t *struct_sym) {
    if (!struct_sym || struct_sym->sym_type != SYM_STRUCT) {
        return 0;
    }
    
    size_t total_size = 0;
    size_t max_alignment = 1;
    
    // Calculate size and alignment for each member
    for (int i = 0; i < struct_sym->member_count; i++) {
        symbol_t *member = struct_sym->members[i];
        
        size_t member_size = member->size;
        size_t member_alignment = member->alignment;
        
        // Track maximum alignment for the struct
        if (member_alignment > max_alignment) {
            max_alignment = member_alignment;
        }
        
        // Align current offset to member's alignment requirement
        total_size = align_to(total_size, member_alignment);
        
        // Set member offset
        member->offset = total_size;
        
        // Add member size
        total_size += member_size;
    }
    
    // Align final size to struct's alignment
    total_size = align_to(total_size, max_alignment);
    
    struct_sym->total_size = total_size;
    struct_sym->max_alignment = max_alignment;
    
    return total_size;
}

// Calculate union size (largest member)
size_t calculate_union_size(symbol_t *union_sym) {
    if (!union_sym || union_sym->sym_type != SYM_UNION) {
        return 0;
    }
    
    size_t max_size = 0;
    size_t max_alignment = 1;
    
    // Find largest member and maximum alignment
    for (int i = 0; i < union_sym->member_count; i++) {
        symbol_t *member = union_sym->members[i];
        
        if (member->size > max_size) {
            max_size = member->size;
        }
        
        if (member->alignment > max_alignment) {
            max_alignment = member->alignment;
        }
        
        // All union members start at offset 0
        member->offset = 0;
    }
    
    // Align union size to maximum alignment
    max_size = align_to(max_size, max_alignment);
    
    union_sym->total_size = max_size;
    union_sym->max_alignment = max_alignment;
    
    return max_size;
}

// Create a new symbol
symbol_t *create_symbol(const char *name, symbol_type_t sym_type, type_info_t type_info) {
    symbol_t *sym = malloc(sizeof(symbol_t));
    if (!sym) {
        fprintf(stderr, "Failed to allocate symbol\n");
        exit(1);
    }
    
    sym->name = string_duplicate(name);
    sym->llvm_name = NULL; // Will be set when added to table
    sym->sym_type = sym_type;
    sym->type_info = deep_copy_type_info(&type_info); // Make a deep copy
    
    // Initialize all fields
    sym->scope_level = 0;
    sym->is_global = 0;
    sym->is_parameter = 0;
    sym->is_static = 0;
    sym->is_extern = 0;
    
    sym->size = 0;
    sym->alignment = 1;
    sym->offset = 0;
    
    sym->array_dimensions = 0;
    sym->array_sizes = NULL;
    sym->is_vla = 0;
    sym->vla_size_expr = NULL;
    
    sym->param_symbols = NULL;
    sym->param_count = 0;
    sym->is_function_defined = 0;
    sym->is_variadic = 0;
    
    sym->members = NULL;
    sym->member_count = 0;
    sym->total_size = 0;
    sym->max_alignment = 1;
    
    sym->enum_value = 0;
    sym->label_name = NULL;
    sym->label_defined = 0;
    
    sym->next = NULL;
    
    return sym;
}

// Generate unique LLVM name
char *generate_unique_name(symbol_table_t *table, const char *base_name) {
    char *unique_name = malloc(256);
    if (!unique_name) {
        fprintf(stderr, "Failed to allocate unique name\n");
        exit(1);
    }
    
    if (table->current_function) {
        snprintf(unique_name, 256, "%s.%s.%d.%d", 
                 table->current_function, base_name, 
                 table->current_scope->level, ++table->temp_counter);
    } else {
        snprintf(unique_name, 256, "global.%s.%d", base_name, ++table->temp_counter);
    }
    
    return unique_name;
}

// Add symbol to current scope
symbol_t *add_symbol(symbol_table_t *table, const char *name, symbol_type_t sym_type, type_info_t type_info) {
    // Check if symbol already exists in current scope
    symbol_t *existing = find_symbol_in_scope(table->current_scope, name);
    if (existing) {
        fprintf(stderr, "Symbol '%s' already defined in current scope\n", name);
        return NULL;
    }
    
    symbol_t *sym = create_symbol(name, sym_type, type_info);
    sym->scope_level = table->current_scope->level;
    sym->is_global = (table->current_scope == table->global_scope);
    
    // Generate unique LLVM name
    if (sym_type == SYM_VARIABLE || sym_type == SYM_FUNCTION) {
        sym->llvm_name = generate_unique_name(table, name);
    } else {
        sym->llvm_name = string_duplicate(name);
    }
    
    // Calculate size and alignment for variables
    if (sym_type == SYM_VARIABLE) {
        sym->size = calculate_type_size(&sym->type_info, table);
        sym->alignment = calculate_type_alignment(&sym->type_info, table);
    }
    
    // Add to current scope
    sym->next = table->current_scope->symbols;
    table->current_scope->symbols = sym;
    
    return sym;
}

// Find symbol by name (search all scopes from current to global)
symbol_t *find_symbol(symbol_table_t *table, const char *name) {
    scope_t *scope = table->current_scope;
    
    while (scope) {
        symbol_t *sym = find_symbol_in_scope(scope, name);
        if (sym) {
            return sym;
        }
        scope = scope->parent;
    }
    
    return NULL;
}

// Find symbol in specific scope
symbol_t *find_symbol_in_scope(scope_t *scope, const char *name) {
    symbol_t *sym = scope->symbols;
    
    while (sym) {
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
        sym = sym->next;
    }
    
    return NULL;
}

// Add member to struct/union
void add_struct_member(symbol_t *struct_sym, symbol_t *member) {
    if (!struct_sym || (!(struct_sym->sym_type == SYM_STRUCT) && !(struct_sym->sym_type == SYM_UNION))) {
        return;
    }
    
    // Resize members array
    struct_sym->member_count++;
    struct_sym->members = realloc(struct_sym->members, 
                                  struct_sym->member_count * sizeof(symbol_t*));
    
    struct_sym->members[struct_sym->member_count - 1] = member;
    
    // Recalculate struct/union size
    if (struct_sym->sym_type == SYM_STRUCT) {
        calculate_struct_size(struct_sym);
    } else {
        calculate_union_size(struct_sym);
    }
}

// Find member in struct/union
symbol_t *find_struct_member(symbol_t *struct_sym, const char *member_name) {
    if (!struct_sym || (!(struct_sym->sym_type == SYM_STRUCT) && !(struct_sym->sym_type == SYM_UNION))) {
        return NULL;
    }
    
    for (int i = 0; i < struct_sym->member_count; i++) {
        if (strcmp(struct_sym->members[i]->name, member_name) == 0) {
            return struct_sym->members[i];
        }
    }
    
    return NULL;
}

// Calculate struct member offsets
void calculate_struct_offsets(symbol_t *struct_sym) {
    if (struct_sym->sym_type == SYM_STRUCT) {
        calculate_struct_size(struct_sym);
    } else if (struct_sym->sym_type == SYM_UNION) {
        calculate_union_size(struct_sym);
    }
}

// Add enum constant
symbol_t *add_enum_constant(symbol_table_t *table, const char *name, int value) {
    type_info_t enum_type = create_type_info(string_duplicate("int"), 0, 0, NULL);
    symbol_t *sym = add_symbol(table, name, SYM_ENUM_CONSTANT, enum_type);
    if (sym) {
        sym->enum_value = value;
        sym->size = INT_SIZE;
        sym->alignment = INT_ALIGN;
    }
    free_type_info(&enum_type); // Clean up the temporary type_info
    return sym;
}

// Set current function context
void set_current_function(symbol_table_t *table, const char *func_name) {
    free(table->current_function);
    table->current_function = string_duplicate(func_name);
}

// Get current function symbol
symbol_t *get_current_function(symbol_table_t *table) {
    if (!table->current_function) {
        return NULL;
    }
    return find_symbol(table, table->current_function);
}

// Add label
symbol_t *add_label(symbol_table_t *table, const char *label_name) {
    type_info_t void_type = create_type_info(string_duplicate("void"), 0, 0, NULL);
    symbol_t *sym = add_symbol(table, label_name, SYM_LABEL, void_type);
    if (sym) {
        sym->label_name = string_duplicate(label_name);
        sym->label_defined = 1;
    }
    free_type_info(&void_type); // Clean up the temporary type_info
    return sym;
}

// Find label (search function scope only)
symbol_t *find_label(symbol_table_t *table, const char *label_name) {
    // Labels have function scope, so search all scopes in current function
    scope_t *scope = table->current_scope;
    
    while (scope) {
        symbol_t *sym = scope->symbols;
        while (sym) {
            if (sym->sym_type == SYM_LABEL && strcmp(sym->name, label_name) == 0) {
                return sym;
            }
            sym = sym->next;
        }
        scope = scope->parent;
    }
    
    return NULL;
}

// Check type compatibility
int is_compatible_type(type_info_t *type1, type_info_t *type2) {
    if (!type1 || !type2) return 0;
    
    // Check pointer levels
    if (type1->pointer_level != type2->pointer_level) return 0;
    
    // Check array status
    if (type1->is_array != type2->is_array) return 0;
    
    // Check base type
    if (!type1->base_type || !type2->base_type) return 0;
    if (strcmp(type1->base_type, type2->base_type) != 0) return 0;
    
    // Check struct/union/enum flags
    if (type1->is_struct != type2->is_struct) return 0;
    if (type1->is_union != type2->is_union) return 0;
    if (type1->is_enum != type2->is_enum) return 0;
    
    return 1;
}

// Get expression type (returns a deep copy that must be freed)
type_info_t get_expression_type(ast_node_t *expr, symbol_table_t *table) {
    type_info_t default_type = create_type_info(string_duplicate("int"), 0, 0, NULL);
    
    if (!expr) return default_type;
    
    switch (expr->type) {
        case AST_NUMBER:
            free_type_info(&default_type);
            return create_type_info(string_duplicate("int"), 0, 0, NULL);
            
        case AST_CHARACTER:
            free_type_info(&default_type);
            return create_type_info(string_duplicate("char"), 0, 0, NULL);
            
        case AST_STRING_LITERAL:
            free_type_info(&default_type);
            return create_type_info(string_duplicate("char"), 1, 0, NULL); // char*
            
        case AST_IDENTIFIER: {
            symbol_t *sym = find_symbol(table, expr->data.identifier.name);
            if (sym) {
                free_type_info(&default_type);
                return deep_copy_type_info(&sym->type_info);
            }
            break;
        }
        
        case AST_BINARY_OP:
            // Most binary ops return int, comparisons return bool
            if (expr->data.binary_op.op >= OP_EQ && expr->data.binary_op.op <= OP_GE) {
                free_type_info(&default_type);
                return create_type_info(string_duplicate("_Bool"), 0, 0, NULL);
            }
            return default_type;
            
        case AST_UNARY_OP:
            if (expr->data.unary_op.op == OP_NOT) {
                free_type_info(&default_type);
                return create_type_info(string_duplicate("_Bool"), 0, 0, NULL);
            } else {
                type_info_t operand_type = get_expression_type(expr->data.unary_op.operand, table);
                free_type_info(&default_type);
                return operand_type;
            }
            
        case AST_ADDRESS_OF: {
            type_info_t operand_type = get_expression_type(expr->data.address_of.operand, table);
            operand_type.pointer_level++;
            free_type_info(&default_type);
            return operand_type;
        }
        
        case AST_DEREFERENCE: {
            type_info_t operand_type = get_expression_type(expr->data.dereference.operand, table);
            if (operand_type.pointer_level > 0) {
                operand_type.pointer_level--;
            }
            free_type_info(&default_type);
            return operand_type;
        }
        
        default:
            break;
    }
    
    return default_type;
}

// Clean up type_info in a symbol
void cleanup_symbol_type_info(symbol_t *sym) {
    if (sym) {
        free_type_info(&sym->type_info);
    }
}

// Debug functions
void print_symbol_table(symbol_table_t *table) {
    printf("=== Symbol Table ===\n");
    scope_t *scope = table->current_scope;
    
    while (scope) {
        printf("Scope level %d:\n", scope->level);
        symbol_t *sym = scope->symbols;
        while (sym) {
            print_symbol(sym, 2);
            sym = sym->next;
        }
        scope = scope->parent;
    }
}

void print_symbol(symbol_t *sym, int indent) {
    printf("%*s%s: ", indent, "", sym->name);
    
    switch (sym->sym_type) {
        case SYM_VARIABLE:
            printf("variable, type=%s", sym->type_info.base_type ? sym->type_info.base_type : "null");
            if (sym->type_info.pointer_level > 0) {
                for (int i = 0; i < sym->type_info.pointer_level; i++) {
                    printf("*");
                }
            }
            printf(", size=%zu, align=%zu", sym->size, sym->alignment);
            break;
            
        case SYM_FUNCTION:
            printf("function, returns %s", sym->type_info.base_type ? sym->type_info.base_type : "null");
            break;
            
        case SYM_STRUCT:
            printf("struct, size=%zu, align=%zu, members=%d", 
                   sym->total_size, sym->max_alignment, sym->member_count);
            break;
            
        case SYM_UNION:
            printf("union, size=%zu, align=%zu, members=%d", 
                   sym->total_size, sym->max_alignment, sym->member_count);
            break;
            
        case SYM_ENUM:
            printf("enum");
            break;
            
        case SYM_ENUM_CONSTANT:
            printf("enum constant, value=%d", sym->enum_value);
            break;
            
        case SYM_TYPEDEF:
            printf("typedef");
            break;
            
        case SYM_LABEL:
            printf("label");
            break;
    }
    
    printf("\n");
}
