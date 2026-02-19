#pragma once

#include <cstddef>
#include <string.h>
#include <yakpp/Object.hh>

namespace yak
{

class String : public Object {
	IO_OBJ_DECLARE(String);

    public:
	void init() override;
	void init(const char *str);
	void init(const char *str, size_t length);

	void deinit() override;

	// Move constructor
	String(String &&other) noexcept
		: data_(other.data_)
		, length_(other.length_)
	{
		other.data_ = nullptr;
		other.length_ = 0;
	}

	// Move assignment
	String &operator=(String &&other) noexcept
	{
		if (this != &other) {
			if (data_)
				delete[] data_;
			data_ = other.data_;
			length_ = other.length_;
			other.data_ = nullptr;
			other.length_ = 0;
		}
		return *this;
	}

	// Copy constructor
	String(const String &other)
	{
		length_ = other.length_;
		if (other.data_) {
			data_ = new char[length_ + 1];
			memcpy(data_, other.data_, length_ + 1);
		}
	}

	// Copy assignment
	String &operator=(const String &other)
	{
		if (this != &other) {
			delete[] data_;
			length_ = other.length_;
			if (other.data_) {
				data_ = new char[length_ + 1];
				memcpy(data_, other.data_, length_ + 1);
			} else {
				data_ = nullptr;
			}
		}
		return *this;
	}

	bool isEqual(Object *other) const override;

	static String *fromCStr(const char *c_str);
	const char *getCStr() const;

	size_t length() const;

    private:
	char *data_ = nullptr;
	size_t length_ = 0;
};

}
