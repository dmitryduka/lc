#include <iostream>
#include "symbolic.h"

using std::cout;
using std::endl;

Expression::Expression() {}

Expression::Expression(const Symbol& x) 
{
	op = UNDEFINED;
	symbols.clear();
	symbols.push_back(x);
}

Expression Expression::eval()
{
}

template<Op op>
Expression op_func(const Expression& e, const Expression& x)
{
	Expression expr;
	expr.symbols.push_back(e);
	expr.symbols.push_back(x);
	expr.op = op;
	return expr;
}

Expression Expression::operator+(const Expression& x) { return op_func<ADD>(*this, x); }
Expression Expression::operator-(const Expression& x) { return op_func<SUB>(*this, x); }
Expression Expression::operator*(const Expression& x) { return op_func<MUL>(*this, x); }
Expression Expression::operator/(const Expression& x) { return op_func<DIV>(*this, x); }
Expression Expression::operator&(const Expression& x) { return op_func<AND>(*this, x); }
Expression Expression::operator|(const Expression& x) { return op_func<OR>(*this, x); }
Expression Expression::operator^(const Expression& x) { return op_func<XOR>(*this, x); }

Expression Expression::operator=(const Symbol& x) 
{ 
	Expression expr(*this);
	expr.symbols.push_back(x); 
	return expr;
}

std::string Expression::print()
{
	if (symbols.size() == 1) return symbols[0].print();
	if (symbols.size() == 2) return std::string("(") + symbols[0].print() + op_strings[op] + symbols[1].print() + ")";
	return "";
}

Symbol::Symbol() : type(UNDEFINED) {}
Symbol::Symbol(const std::string& x) : type(STRING), name(x) {}
Symbol::Symbol(uint64_t x) : type(NUMERIC), value(x) {}
Symbol::Symbol(const Expression& x) : type(EXPRESSION), expr(x) {}
std::string Symbol::print()
{
	if (type == NUMERIC) return std::to_string(value);
	else if (type == STRING) return name;
	else if (type == EXPRESSION) return expr.print();
}

void SymbolicEnv::add(const Symbol&) {}
void SymbolicEnv::remove(const std::string&) {}
Symbol SymbolicEnv::operator[](const std::string& name) {}

int main()
{
// i144 = load_relative_int(134886744, 0)
// i145 = i144 - 1	
// i146 = i145 * 8
// i147 = i146
// i148 = 168230920 + i147


	Symbol i144("SP"), i145, i146, i147, i148, stack("168230920");
	i145 = Expression(i144) - Symbol(1);
	i146 = Expression(i145) * Symbol(8);
	i147 = i146;
	i148 = Expression(stack) + i147;

	cout << i148.print() << endl;
	return 0;
}