#pragma once

#include <yakpp/Object.hh>

namespace yak
{

class Array : public Object {
	IO_OBJ_DECLARE(Array);

    public:
	class Iterator {
	    public:
		Iterator(const Array *arr, size_t pos);
		Object *operator*() const;
		Iterator &operator++();
		bool operator!=(const Iterator &other) const;

	    private:
		const Array *arr;
		size_t pos;
	};

	void init() override;
	void deinit() override;
	void initWithCapacity(size_t cap = 4);

	Array(Array &&) = delete;
	Array(const Array &) = delete;
	Array &operator=(Array &&) = delete;
	Array &operator=(const Array &) = delete;

	void resize(size_t new_size);
	size_t length() const;
	void push_back(Object *obj);
	Object **getCArray() const;

	Iterator begin() const;
	Iterator end() const;

    private:
	Object **entries_ = nullptr;
	size_t count_ = 0;
	size_t capacity_ = 0;
};

}
