#include <yakpp/Dictionary.hh>
#include <algorithm>

namespace yak
{

IO_OBJ_DEFINE(Dictionary, Object);
#define super Object

void Dictionary::resize(size_t new_size)
{
	if (new_size == capacity)
		return;

	if (new_size < count) {
		for (size_t i = new_size; i < count; i++) {
			if (entries[i].key)
				entries[i].key->release();
			if (entries[i].value)
				entries[i].value->release();
		}

		count = new_size;
	}

	Entry *new_entries = nullptr;

	if (new_size > 0) {
		new_entries = new Entry[new_size];

		size_t copy_count = std::min(count, new_size);
		if (entries && copy_count > 0)
			memcpy(new_entries, entries,
			       sizeof(Entry) * copy_count);

		for (size_t i = copy_count; i < new_size; ++i) {
			new_entries[i].key = nullptr;
			new_entries[i].value = nullptr;
		}
	}

	delete[] entries;
	entries = new_entries;

	capacity = new_size;
}

bool Dictionary::insert(const char *key, Object *value)
{
	return insert(String::fromCStr(key), value);
}

bool Dictionary::insert(String *key, Object *value)
{
	if (!entries || count >= capacity) {
		resize(capacity == 0 ? 4 : capacity * 2);
	}

	for (size_t i = 0; i < count; i++) {
		if (key->isEqual(entries[i].key))
			return false;
	}

	value->retain();
	entries[count++] = { key, value };
	return true;
}

bool Dictionary::isEqual(Object *object) const
{
	if (auto dict = object->safe_cast<Dictionary>()) {
		if (this->count != dict->count)
			return false;

		for (auto v : *dict) {
			auto el = lookup(v.key);
			if (!el)
				return false;

			if (!el->isEqual(v.value)) {
				el->release();
				return false;
			}

			el->release();
		}
	}

	return true;
}

Object *Dictionary::lookup(const char *key) const
{
	auto str = String::fromCStr(key);
	auto obj = lookup(str);
	str->release();
	return obj;
}

Object *Dictionary::lookup(String *key) const
{
	for (size_t i = 0; i < count; i++) {
		if (key->isEqual(entries[i].key)) {
			auto res = entries[i].value;
			res->retain();
			return res;
		}
	}
	return nullptr;
}

void Dictionary::init()
{
	super::init();
	entries = nullptr;
	count = capacity = 0;
}

void Dictionary::initWithSize(size_t cap)
{
	resize(cap);
}

void Dictionary::deinit()
{
	resize(0);
	super::deinit();
}

}
