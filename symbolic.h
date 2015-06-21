#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <boost/multiprecision/cpp_int.hpp>

using namespace boost::multiprecision;

class Symbol;
enum Op { ADD, SUB, MUL, DIV, AND, OR, XOR, NOT, UNDEFINED };
std::vector<std::string> op_strings = { "+", "-", "*", "/", "&", "|", "^", "!", "NOP" };

class Expression
{
public:
	Expression();
	Expression(const Symbol&);
	Expression eval();

	bool is_numeric() const;

	std::string print();

	Expression operator+(const Expression&);
	Expression operator-(const Expression&);
	Expression operator*(const Expression&);
	Expression operator/(const Expression&);
	Expression operator&(const Expression&);
	Expression operator|(const Expression&);
	Expression operator^(const Expression&);

	Expression operator=(const Symbol&);

	std::vector<Symbol> symbols;
	Op op;
};

class Symbol
{
public:
	Symbol();
	Symbol(const Expression&);
	Symbol(const std::string&);
	Symbol(int128_t);
	std::string print();
	bool is_numeric() const;
	// a symbol can be a numeric, a string or an expression
	enum Type { NUMERIC, STRING, EXPRESSION, UNDEFINED } type;
	int128_t 		value;
	std::string 	name;
	Expression 		expr;
};

class SymbolicEnv
{
public:
	void add(const std::string&, const Symbol&);
	void remove(const std::string&);
	Symbol& operator[](const std::string& name);
	void print();
private:
	std::unordered_map<std::string, Symbol> env;
};
