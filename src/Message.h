#ifndef MESSAGE_H
#define MESSAGE_H

#include <cstdint>
#include <ostream>
#include <string>
#include <sstream>
#include <tuple>
#include <vector>

class Message {
public:
	enum TYPE {
		INT = 'i',
		TEXT = 't',
	};
	enum KIND {
		SOURCE = 'S',
		STRUCT = 'T',
		MEMBER = 'M',
		USE = 'U',
	};
	using entry = std::tuple<TYPE, std::string, std::string>;
	using storage = std::vector<entry>;

	Message(const KIND &kind) : kind(kind) {}

	void add(const TYPE &type, const std::string &key, const std::string &val) {
		entries.push_back(std::make_tuple(type, key, val));
	}

	void add(const std::string &key, const std::string &val) {
		add(TEXT, key, val);
	}

	template<typename T>
	void add(const std::string &key, T val) {
		add(INT, key, std::to_string(val));
	}

	void renew(const KIND &kind) {
		entries.clear();
		setKind(kind);
	}

	KIND getKind() const { return kind; }
	size_t size() const { return entries.size(); }
	entry operator[](int idx) const { return entries[idx]; }
	storage::const_iterator begin() const { return entries.begin(); }
	storage::const_iterator end() const { return entries.end(); }

	std::string serialize() const;
	static Message deserialize(const std::string &str);
private:
	Message() {}

	void setKind(const KIND &kind) { this->kind = kind; }

	static void serializeString(std::stringstream &ss, const std::string &str);
	static std::string deserializeString(std::istringstream &ss);

	KIND kind;
	storage entries;
};

inline void Message::serializeString(std::stringstream &ss, const std::string &str)
{
	uint16_t sz = str.length();
	ss.write((char *)&sz, sizeof(sz));
	ss << str;
}

inline std::string Message::deserializeString(std::istringstream &ss)
{
	uint16_t sz;
	ss.read((char *)&sz, sizeof(sz));

	char buf[sz];
	ss.read(buf, sz);

	return std::string(buf, sz);
}

inline std::string Message::serialize() const
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

inline Message Message::deserialize(const std::string &str)
{
	std::istringstream iss(str);
	std::string item;
	Message msg((KIND)iss.get());

	while (true) {
		auto type = iss.get();
		if (type == EOF)
			break;

		auto key = deserializeString(iss);
		auto val = deserializeString(iss);

		msg.add((TYPE)type, key, val);
	}

	return msg;
}

inline std::ostream& operator<<(std::ostream &os, const Message &msg)
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
