#pragma once

#include <yakpp/String.hh>
#include <yakpp/Object.hh>
#include <stddef.h>

namespace yak
{

class Dictionary : public Object {
	IO_OBJ_DECLARE(Dictionary);

    private:
	struct Entry {
		String *key;
		Object *value;
	};

	class Iterator {
	    public:
		Iterator(const Dictionary *dict, size_t pos)
			: dict(dict)
			, current(pos)
		{
		}

		Entry operator*() const
		{
			return dict->entries[current];
		}

		Iterator &operator++()
		{
			++current;
			return *this;
		}

		bool operator!=(const Iterator &other) const
		{
			return current != other.current;
		}

	    private:
		const Dictionary *dict;
		size_t current;
	};

    public:
	void init() override;
	void deinit() override;

	void initWithSize(size_t cap = 4);

	Dictionary(Dictionary &&) = delete;
	Dictionary(const Dictionary &) = delete;
	Dictionary &operator=(Dictionary &&) = delete;
	Dictionary &operator=(const Dictionary &) = delete;

	void resize(size_t new_size);
	// returns true if inserted
	// returns false if key exists already
	bool insert(const char *key, Object *value);
	bool insert(String *key, Object *value);

	Object *lookup(const char *key) const;
	Object *lookup(String *key) const;

	bool isEqual(Object *object) const override;

	Iterator begin() const
	{
		return Iterator(this, 0);
	}

	Iterator end() const
	{
		return Iterator(this, count);
	}

    private:
	Entry *entries = nullptr;
	size_t count = 0;
	size_t capacity = 0;
};

}
