#include <string.h>
#include <yak/log.h>
#include <yak/heap.h>
#include <yak/types.h>
#include <yak/tree.h>
#include <yak/symbol.h>

struct symbol_tree ksym = RB_INITIALIZER(ksym);

static int symbol_cmp(struct symbol *a, struct symbol *b)
{
	if (a->address < b->address)
		return -1;
	if (a->address > b->address)
		return 1;
	return 0;
}

RB_PROTOTYPE(symbol_tree, symbol, entry, symbol_cmp);
RB_GENERATE(symbol_tree, symbol, entry, symbol_cmp);

static struct symbol *symbol_create(uint64_t addr, char type, const char *name,
				    size_t name_len)
{
	struct symbol *sym = kzalloc(sizeof(*sym));
	if (!sym)
		return NULL;

	sym->address = addr;
	sym->end = 0;
	sym->type = type;
	sym->name = strndup(name, name_len);
	if (!sym->name) {
		kfree(sym, sizeof(*sym));
		return NULL;
	}

	return sym;
}

static void symbol_free(struct symbol *sym)
{
	if (!sym)
		return;
	kfree(sym->name, 0);
	kfree(sym, sizeof(*sym));
}

static int hex_to_u64(const char *s, size_t len, uint64_t *out)
{
	uint64_t v = 0;

	for (size_t i = 0; i < len; i++) {
		char c = s[i];
		uint8_t d;

		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'f')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			d = c - 'A' + 10;
		else
			return -1;

		v = (v << 4) | d;
	}

	*out = v;
	return 0;
}

static int is_space(char c)
{
	return (c == ' ' || c == '\t');
}

/* 
Format:
   <hexaddr> <type> <name>
*/
static struct symbol *parse_line(const char *line, size_t len)
{
	size_t i = 0;

	while (i < len && is_space(line[i]))
		i++;

	size_t addr_start = i;
	while (i < len && ((line[i] >= '0' && line[i] <= '9') ||
			   (line[i] >= 'a' && line[i] <= 'f') ||
			   (line[i] >= 'A' && line[i] <= 'F')))
		i++;

	if (i == addr_start)
		return NULL;

	vaddr_t address;
	if (hex_to_u64(line + addr_start, i - addr_start, &address) != 0)
		return NULL;

	while (i < len && is_space(line[i]))
		i++;

	if (i >= len)
		return NULL;

	char type = line[i++];

	while (i < len && is_space(line[i]))
		i++;

	if (i >= len)
		return NULL;

	return symbol_create(address, type, line + i, len - i);
}

static void compute_symbol_ranges(struct symbol_tree *tree)
{
	struct symbol *prev = NULL;
	struct symbol *sym;

	RB_FOREACH(sym, symbol_tree, tree)
	{
		if (prev) {
			prev->end =
				sym->address; // previous symbol ends where current starts
		}
		prev = sym;
	}

	if (prev)
		prev->end = prev->address + 1;
}

void load_symbols(struct symbol_tree *tree, const char *data, size_t size)
{
	size_t nsyms = 0;

	size_t pos = 0;

	while (pos < size) {
		size_t line_start = pos;

		while (pos < size && data[pos] != '\n' && data[pos] != '\r')
			pos++;

		size_t line_len = pos - line_start;

		if (line_len > 0) {
			struct symbol *sym =
				parse_line(data + line_start, line_len);
			if (sym) {
				if (RB_INSERT(symbol_tree, tree, sym) != NULL) {
					kfree(sym, sizeof(*sym));
				} else {
					nsyms++;
				}
			}
		}

		while (pos < size && (data[pos] == '\n' || data[pos] == '\r'))
			pos++;
	}

	compute_symbol_ranges(tree);

	pr_info("loaded %ld symbols\n", nsyms);
}

struct symbol *find_symbol_by_address(struct symbol_tree *tree, vaddr_t addr)
{
	struct symbol *node = RB_ROOT(tree);
	struct symbol *result = NULL;

	while (node) {
		if (addr < node->address) {
			node = RB_LEFT(node, entry);
		} else if (addr >= node->end) {
			node = RB_RIGHT(node, entry);
		} else {
			// addr >= node->address && addr < node->end
			result = node;
			break;
		}
	}

	return result;
}

void free_all_symbols(struct symbol_tree *tree)
{
	struct symbol *sym;
	while ((sym = RB_MIN(symbol_tree, tree)) != NULL) {
		RB_REMOVE(symbol_tree, tree, sym);
		symbol_free(sym);
	}
}
