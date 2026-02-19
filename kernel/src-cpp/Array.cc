#include <yakpp/Array.hh>
#include <algorithm>

namespace yak
{

IO_OBJ_DEFINE(Array, Object);

Array::Iterator::Iterator(const Array *arr, size_t pos)
	: arr(arr)
	, pos(pos)
{
}

Object *Array::Iterator::operator*() const
{
	return arr->entries_[pos];
}

Array::Iterator &Array::Iterator::operator++()
{
	++pos;
	return *this;
}

bool Array::Iterator::operator!=(const Iterator &other) const
{
	return pos != other.pos;
}

void Array::init()
{
	Object::init();
	entries_ = nullptr;
	count_ = capacity_ = 0;
}

void Array::deinit()
{
	resize(0);
	Object::deinit();
}

void Array::initWithCapacity(size_t cap)
{
	init();
	resize(cap);
}

void Array::resize(size_t new_size)
{
	if (new_size == capacity_)
		return;

	if (new_size < count_) {
		for (size_t i = new_size; i < count_; i++)
			entries_[i]->release();
		count_ = new_size;
	}

	Object **new_entries = nullptr;
	if (new_size > 0) {
		new_entries = new Object *[new_size];
		size_t copy_count = std::min(count_, new_size);
		if (entries_)
			memcpy(new_entries, entries_,
			       sizeof(Object *) * copy_count);
		for (size_t i = copy_count; i < new_size; i++)
			new_entries[i] = nullptr;
	}

	delete[] entries_;
	entries_ = new_entries;
	capacity_ = new_size;
}

size_t Array::length() const
{
	return count_;
}

void Array::push_back(Object *obj)
{
	if (count_ >= capacity_)
		resize(capacity_ == 0 ? 4 : capacity_ * 2);
	obj->retain();
	entries_[count_++] = obj;
}

Object **Array::getCArray() const
{
	return entries_;
}

Array::Iterator Array::begin() const
{
	return Iterator(this, 0);
}

Array::Iterator Array::end() const
{
	return Iterator(this, count_);
}

}
