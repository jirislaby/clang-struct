#ifndef MESSAGE_H
#define MESSAGE_H

#include <cassert>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <sstream>
#include <tuple>
#include <vector>

template<typename T>
class Message {
public:
	enum TYPE {
		INT = 'i',
		NUL = 'n',
		TEXT = 't',
	};
	enum KIND {
		INVALID = -1,
		SOURCE = 'S',
		STRUCT = 'T',
		MEMBER = 'M',
		USE = 'U',
	};
	using entry = std::tuple<TYPE, const T, const T>;
	using storage = std::vector<entry>;

	Message() : Message(KIND::INVALID) { }
	Message(const KIND &kind) : kind(kind) { entries.reserve(10); }

	void add(TYPE type, T key, T val) {
		entries.emplace_back(std::move(type), std::move(key), std::move(val));
	}

	void add(T key, T val) {
		add(TEXT, std::move(key), std::move(val));
	}

	void add(T key) {
		add(NUL, std::move(key), "");
	}

	template<typename U>
	void add(T key, U val) {
		add(INT, std::move(key), std::to_string(val));
	}

	void renew(const KIND &kind) {
		entries.clear();
		setKind(kind);
	}

	KIND getKind() const { return kind; }
	size_t size() const { return entries.size(); }
	typename storage::const_reference operator[](typename storage::size_type idx) const {
		return entries[idx];
	}
	typename storage::const_iterator begin() const { return entries.begin(); }
	typename storage::const_iterator end() const { return entries.end(); }

	std::string serialize() const;
	void deserialize(const std::string_view &str);
private:

	void setKind(const KIND &kind) { this->kind = kind; }

	static void serializeString(std::stringstream &ss, const std::string &str);
	static std::string_view deserializeString(std::string_view &str);

	KIND kind;
	storage entries;
};

template<typename T> inline void Message<T>::serializeString(std::stringstream &ss, const std::string &str)
{
	uint16_t sz = str.length();
	ss.write((char *)&sz, sizeof(sz));
	ss << str;
}

template<typename T> inline std::string_view Message<T>::deserializeString(std::string_view &str)
{
	using slen = uint16_t;
	assert(str.length() > sizeof(slen));

	auto len = *(slen *)str.data();
	auto ret = str.substr(sizeof(slen), len);
	str = str.substr(sizeof(slen) + len);
	return ret;
}

template<typename T> inline std::string Message<T>::serialize() const
{
	std::stringstream ss;

	ss.put(kind);

	for (auto &e : entries) {
		const auto [type, key, val] = e;

		ss.put(type);
		serializeString(ss, key);
		serializeString(ss, val);
	}

	return ss.str();
}

template<typename T> inline void Message<T>::deserialize(const std::string_view &str)
{
	renew((KIND)str[0]);
	auto cur = str.substr(1);

	while (cur.length()) {
		auto type = cur[0];
		if (type == EOF)
			break;

		cur = cur.substr(1);
		auto key = deserializeString(cur);
		auto val = deserializeString(cur);

		add((TYPE)type, key, val);
	}
}

template<typename T> inline std::ostream& operator<<(std::ostream &os, const Message<T> &msg)
{
	os.put(msg.getKind());

	for (auto &e : msg) {
		const auto [type, key, val] = e;
		os << " --- " << key << "(";
		os.put(type);
		os << ")" << '=' << val;
	}

	return os;
}

#endif
