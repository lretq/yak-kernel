#include <yakpp/String.hh>

namespace yak
{

IO_OBJ_DEFINE(String, Object);
#define super Object

void String::init()
{
	super::init();
	data_ = nullptr;
	length_ = 0;
}

void String::deinit()
{
	delete[] data_;
	data_ = nullptr;
	length_ = 0;
	super::deinit();
}

void String::init(const char *str, size_t length)
{
	super::init();

	if (!str) {
		data_ = nullptr;
		length_ = 0;
		return;
	}

	data_ = new char[length + 1];
	memcpy(data_, str, length);
	data_[length] = '\0';
	length_ = length;
}

void String::init(const char *str)
{
	super::init();
	if (!str) {
		data_ = nullptr;
		length_ = 0;
		return;
	}
	length_ = strlen(str);
	data_ = new char[length_ + 1];
	memcpy(data_, str, length_ + 1);
}

String *String::fromCStr(const char *c_str)
{
	auto obj = new String();
	obj->init(c_str);
	return obj;
}

const char *String::getCStr() const
{
	return data_ ? data_ : "";
}

bool String::isEqual(Object *other) const
{
	auto str = other->safe_cast<String>();
	if (!str)
		return false;

	const char *a = getCStr();
	const char *b = str->getCStr();

	return strcmp(a, b) == 0;
}

size_t String::length() const
{
	return length_;
}

}
