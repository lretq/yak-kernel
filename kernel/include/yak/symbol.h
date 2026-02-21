#pragma once

#include <stddef.h>
#include <yak/types.h>
#include <yak/tree.h>

struct symbol {
	vaddr_t address;
	vaddr_t end;

	char type;
	char *name;

	RB_ENTRY(symbol) entry;
};

RB_HEAD(symbol_tree, symbol);

extern struct symbol_tree ksym;

void load_symbols(struct symbol_tree *tree, const char *data, size_t size);
void free_all_symbols(struct symbol_tree *tree);
struct symbol *find_symbol_by_address(struct symbol_tree *tree, vaddr_t addr);
