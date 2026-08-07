#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <stack>
#include <map>
#include <stdexcept>
#include "mctx.h"

namespace expr_utils
{

dixelu::mctx operator+(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	if (lhs.is<std::string>() && rhs.is<std::string>())
		return lhs.get_as<std::string>() + rhs.get_as<std::string>();

	return lhs.get_as<uint64_t>() + rhs.get_as<uint64_t>();
}

dixelu::mctx operator-(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	return lhs.get_as<uint64_t>() - rhs.get_as<uint64_t>();
}

dixelu::mctx operator*(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	return lhs.get_as<uint64_t>() * rhs.get_as<uint64_t>();
}

dixelu::mctx operator/(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	return lhs.get_as<uint64_t>() / rhs.get_as<uint64_t>();
}

dixelu::mctx operator%(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	return lhs.get_as<uint64_t>() % rhs.get_as<uint64_t>();
}

dixelu::mctx operator|(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	return lhs.get_as<uint64_t>() | rhs.get_as<uint64_t>();
}

dixelu::mctx operator^(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	return lhs.get_as<uint64_t>() ^ rhs.get_as<uint64_t>();
}

dixelu::mctx operator&(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	return lhs.get_as<uint64_t>() & rhs.get_as<uint64_t>();
}

dixelu::mctx operator&&(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	return lhs.get_as<uint64_t>() && rhs.get_as<uint64_t>();
}

dixelu::mctx operator||(const dixelu::mctx& lhs, const dixelu::mctx& rhs)
{
	if (!lhs.is_scalar() || !rhs.is_scalar())
		return nullptr;

	return lhs.get_as<uint64_t>() || rhs.get_as<uint64_t>();
}

enum class token_type
{
	val_t,	  // Numbers, strings, variables
	op_t,	   // +, -, *, /, etc.
	func_t,	 // Built-in functions
	lp_t,	   // (
	rp_t,	   // )
	comma_t,	// ,
	assign_t	// =
};

struct token_info
{
	std::string value;
	token_type type;

	token_info(std::string v = "", token_type t = token_type::val_t)
		: value(std::move(v)), type(t)
	{
	}
};

struct op_info
{
	int precedence;
	std::function<dixelu::mctx(const std::vector<dixelu::mctx>&)> operation;
	size_t args;

	op_info(
		int prec = 0,
		std::function<dixelu::mctx(const std::vector<dixelu::mctx>&)> op = nullptr,
		size_t args = 2) :
		precedence(prec),
		operation(std::move(op)),
		args(args)
	{
	}
};

class expr_ever
{
public:
	expr_ever()
	{
		// Initialize operators
		operators["+"] = op_info(1, [](const std::vector<dixelu::mctx>& args) { return args[0] + args[1]; });
		operators["-"] = op_info(1, [](const std::vector<dixelu::mctx>& args) { return args[0] - args[1]; });
		operators["*"] = op_info(2, [](const std::vector<dixelu::mctx>& args) { return args[0] * args[1]; });
		operators["/"] = op_info(2, [](const std::vector<dixelu::mctx>& args) { return args[0] / args[1]; });
		operators["="] = op_info(0, [this](const std::vector<dixelu::mctx>& args) -> dixelu::mctx
		{
			if (!args[0].is<std::string>())
				return args[0];

			variables[args[0].get_as<std::string>()] = args[1];
			return args[0];
		});

		operators["#"] = op_info(1, [](const std::vector<dixelu::mctx>& args) -> dixelu::mctx
		{
			if (!args[0].is<std::string>() || !args[1].is<std::string>())
				return nullptr;
			{
				return dixelu::mctx(args[0].get_as<std::string>() +
					args[1].get_as<std::string>());
			}

			throw std::runtime_error("String concatenation requires string operands");
		});

		register_function("max", 2, [this](const std::vector<dixelu::mctx>& args) -> dixelu::mctx
		{
			return (std::max)(args[0].get_as<uint64_t>(), args[1].get_as<uint64_t>());
		});
	}

	// Регистрация пользовательской функции
	void register_function(
		const std::string& name,
		size_t args,
		std::function<dixelu::mctx(const std::vector<dixelu::mctx>&)> func)
	{
		functions[name] = { args, func };
	}

	dixelu::mctx evaluate(const std::string& expression)
	{
		auto tokens = tokenize(expression);
		auto postfix = to_postfix(tokens);
		return evaluate_postfix(postfix);
	}

private:
	struct func_info
	{
		size_t args;
		std::function<dixelu::mctx(const std::vector<dixelu::mctx>&)> func;
	};

	std::map<std::string, dixelu::mctx> variables;
	std::map<std::string, op_info> operators;
	std::map<std::string, func_info> functions;

	std::vector<token_info> tokenize(const std::string& expr)
	{
		std::vector<token_info> tokens;
		std::string current;

		for (size_t i = 0; i < expr.length(); i++)
		{
			char c = expr[i];
			if (std::isspace(c))
			{
				if (!current.empty())
				{
					tokens.emplace_back(current, token_type::val_t);
					current.clear();
				}
				continue;
			}
			if (std::isalnum(c) || c == '_')
			{
				current += c;
				continue;
			}
			if (!current.empty())
			{
				if (c == '(')
				{
					tokens.emplace_back(current, token_type::func_t);
				}
				else
				{
					tokens.emplace_back(current, token_type::val_t);
				}

				current.clear();
			}
			if (c == '(')
				tokens.emplace_back("(", token_type::lp_t);
			else if (c == ')')
				tokens.emplace_back(")", token_type::rp_t);
			else if (c == ',')
				tokens.emplace_back(",", token_type::comma_t);
			else
			{
				std::string op(1, c);
				if (operators.count(op))
				{
					tokens.emplace_back(op, token_type::op_t);
				}
			}
		}

		if (!current.empty())
		{
			tokens.emplace_back(current, token_type::val_t);
		}

		return tokens;
	}

	std::vector<token_info> to_postfix(const std::vector<token_info>& tokens)
	{
		std::vector<token_info> output;
		std::stack<token_info> operator_stack;

		for (const auto& token : tokens)
		{
			switch (token.type)
			{
				case token_type::val_t:
				{
					output.push_back(token);
					break;
				}
				case token_type::func_t:
				{
					operator_stack.push(token);
					break;
				}
				case token_type::op_t:
				{
					while (!operator_stack.empty() &&
						(operator_stack.top().type == token_type::op_t && operator_stack.top().type == token_type::func_t) &&
						operators[operator_stack.top().value].precedence >=
						operators[token.value].precedence)
					{
						output.push_back(operator_stack.top());
						operator_stack.pop();
					}

					operator_stack.push(token);
					break;
				}

				case token_type::lp_t:
				{
					operator_stack.push(token);
					break;
				}
				case token_type::rp_t:
				{
					while (!operator_stack.empty() && operator_stack.top().type != token_type::lp_t)
					{
						output.push_back(operator_stack.top());
						operator_stack.pop();
					}

					if (!operator_stack.empty()) 
						operator_stack.pop();

					if (!operator_stack.empty() && operator_stack.top().type == token_type::func_t)
					{
						output.push_back(operator_stack.top());
						operator_stack.pop();
					}

					break;
				}
				case token_type::comma_t:
				{
					while (!operator_stack.empty() && operator_stack.top().type != token_type::lp_t)
					{
						output.push_back(operator_stack.top());
						operator_stack.pop();
					}

					break;
				}
				default:
					break;
			}
		}

		while (!operator_stack.empty())
		{
			output.push_back(operator_stack.top());
			operator_stack.pop();
		}

		return output;
	}

	dixelu::mctx evaluate_postfix(const std::vector<token_info>& postfix)
	{
		std::stack<dixelu::mctx> value_stack;

		for (const auto& token : postfix) {
			if (token.type == token_type::val_t)
			{
				try
				{
					value_stack.push(std::stoull(token.value));
				}
				catch (...)
				{
					// If not a number, treat as variable or string
					auto it = variables.find(token.value);

					if (it != variables.end())
						value_stack.push(it->second);
					else
						value_stack.push(dixelu::mctx(token.value));
				}
			}
			else if (token.type == token_type::op_t)
			{
				auto& op = operators[token.value];
				std::vector<dixelu::mctx> args;

				for (size_t i = 0; i < op.args; i++)
				{
					if (value_stack.empty())
						throw std::runtime_error("Invalid expression: not enough operands");
					args.insert(args.begin(), value_stack.top());
					value_stack.pop();
				}

				value_stack.push(op.operation(args));
			}
			else if (token.type == token_type::func_t)
			{
				auto it = functions.find(token.value);
				if (it == functions.end())
					throw std::runtime_error("Unknown function: " + token.value);
				std::vector<dixelu::mctx> args(it->second.args);
				for (size_t i = 0; i < it->second.args; ++i)
				{
					if (value_stack.empty())
						throw std::runtime_error("Not enough arguments for function: " + token.value);

					args[it->second.args - i - 1] = value_stack.top();
					value_stack.pop();
				}
				value_stack.push(it->second.func(args));
			}
		}

		if (value_stack.empty())
			return {};

		return value_stack.top();
	}
};

} // namespace expr_utils

// Global evaluator function
inline dixelu::mctx evaluate_expression(std::string expr)
{
	static expr_utils::expr_ever evaluator;
	return evaluator.evaluate(expr);
}

int static_test()
{
	dixelu::mctx res = evaluate_expression("a = 1");
	res = evaluate_expression("b = 4");
	res = evaluate_expression("a + b");
	std::cout << "Result: " << res.get_as<uint64_t>() << std::endl;

	res = evaluate_expression("da # fb + sadjf");
	std::cout << "Result: " << res.get_as<std::string>() << std::endl;

	res = evaluate_expression("max(1, b)");
	std::cout << "Result: " << res.get_as<uint64_t>() << std::endl;
	std::cout << "Result: " << res.get_as<std::string>() << std::endl;

	return 0;
}

// int __s = static_test();