#include <stdio.h>
#include <string>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_os_ostream.h>

#include "json.h"
#include <assert.h>
#include <map>

using namespace llvm;

// グローバルバイト列を作成
Constant *createGlobalByteArrayPtr(Module *module, void const *p, size_t n)
{
	LLVMContext &cx = module->getContext();
	Constant *str = ConstantDataArray::get(cx, ArrayRef<uint8_t>((uint8_t const *)p, n)); // 配列定数
	GlobalVariable *gv = new GlobalVariable(*module, str->getType(), true, Function::ExternalLinkage, str); // グローバル変数
	Value *i32zero = ConstantInt::get(Type::getInt32Ty(cx), 0); // 整数のゼロ
	std::vector<Value *> zero2 = { i32zero, i32zero }; // ゼロふたつ
	return ConstantExpr::getInBoundsGetElementPtr(gv, zero2); // 実体を指すポインタの定数オブジェクトにする
	//return ConstantExpr::getInBoundsGetElementPtr(gv->getValueType(), gv, { i32zero, i32zero }); // LLVMのバージョンによってはこちら
}

// グローバル文字列を作成
Constant *createGlobalStringPtr(Module *m, StringRef s)
{
	return createGlobalByteArrayPtr(m, s.data(), s.size() + 1); // ヌル文字を含めたバイト列を作成する
}

// print_number関数を作成
Function *create_print_number_func(Module *module)
{
	LLVMContext &cx = module->getContext();

	// printf関数を宣言
	Function *fn_printf = Function::Create(FunctionType::get(Type::getInt32Ty(cx), { Type::getInt8PtrTy(cx) }, true), Function::ExternalLinkage, "printf", module);

	// print_number関数を作成（戻り値なし、引数int）
	Function *func = Function::Create(FunctionType::get(Type::getVoidTy(cx), { Type::getInt32Ty(cx) }, false), Function::ExternalLinkage, "print_number", module);
	BasicBlock *block = BasicBlock::Create(cx, "entry", func);

	Value *arg1 = &*func->arg_begin(); // 最初の引数

	// printf関数を呼ぶ
	std::string str = "%d\n";
	Constant *arg0 = createGlobalStringPtr(module, str); // グローバル文字列を作成
	std::vector<Value *>args = { arg0, arg1 };
	CallInst::Create(fn_printf, args, "", block); // printf(arg0, arg1) 呼び出し

	// return void
	ReturnInst::Create(cx, block);

	return func;
}


class Error {
protected:
	std::string msg;
public:
	virtual std::string message() const
	{
		return msg;
	}
};

class SyntaxError : public Error {
public:
	SyntaxError()
	{
		msg = "Syntax error.";
	}
};

class UnknownOperator : public Error {
public:
	UnknownOperator(std::string const &name)
	{
		msg = "Unknown operator '";
		msg += name;
		msg += "'.";
	}
};

class ArgumentCountIncorrect : public Error {
public:
	ArgumentCountIncorrect()
	{
		msg = "Argument count incorrect.";
	}
};

class VariableNotFound : public Error {
public:
	VariableNotFound(std::string const &name)
	{
		msg = "Variable not found '";
		msg += name;
		msg += "'.";
	}
};

class OreLang {
private:
	LLVMContext *llvmcx;
	Module *module;
	Function *current_function;
	BasicBlock *current_block;
	DataLayout *data_layout;
	Function *func_print_number;

	class OreValue {
	public:
		Value *value = nullptr;
	};

	std::map<std::string, OreValue> vars;

	bool getvar(std::string const &name, OreValue *result)
	{
		auto it = vars.find(name);
		if (it != vars.end()) {
			*result = it->second;
			return true;
		}
		throw VariableNotFound(name);
	}

	void eval(JSON::Node const &node, OreValue *result)
	{
		assert(result);
		switch (node.type) {
		case JSON::Type::Array:
			generate(node.children, 0, result);
			break;
		case JSON::Type::String:
			getvar(node.value, result);
			break;
		case JSON::Type::Number:
		case JSON::Type::Boolean:
			{
				*result = OreValue();
				double v =strtod(node.value.c_str(), nullptr);
				result->value = ConstantInt::get(Type::getInt32Ty(*llvmcx), (uint32_t)v);
			}
			break;
		}
	}

	size_t generate(std::vector<JSON::Node> const &program, size_t position, OreValue *result = nullptr)
	{
		size_t pos = position;
		while (pos < program.size()) {
			JSON::Node const &node = program[pos];
			if (node.type == JSON::Type::String) {
				std::string op = node.value.c_str();
				if (op == "step") {
					pos++;
					pos += generate(program, pos);
				} else if (op == "set") {
					if (program.size() != 3) throw ArgumentCountIncorrect();
					std::string name = program[1].value;
					Value *into = nullptr;
					{
						auto it = vars.find(name);
						if (it == vars.end()) {
							into = new AllocaInst(Type::getInt32Ty(*llvmcx), "", current_block);
						} else {
							into = it->second.value;
						}
					}
					OreValue v;
					eval(program[2], &v);
					if (into && v.value) {
						Type *ty = Type::getInt32Ty(*llvmcx);
						unsigned int align = data_layout->getABITypeAlignment(ty);
						new StoreInst(v.value, into, false, align, current_block);
						v.value = into;
					}
					vars[name] = v;
					pos += 3;
				} else if (op == "get") {
					assert(result);
					if (program.size() != 2) throw ArgumentCountIncorrect();
					eval(program[1], result);
					if (result->value) {
						if (isa<AllocaInst>(result->value)) {
							result->value = new LoadInst(result->value, "", current_block);
						}
					}
					pos += 2;
				} else if (op == "while") {
					if (program.size() != 3) throw ArgumentCountIncorrect();
					BasicBlock *cond_block = BasicBlock::Create(*llvmcx,"", current_function); // 条件判定ブロック
					BasicBlock *body_block = BasicBlock::Create(*llvmcx,"", current_function); // ループ本体ブロック
					BasicBlock *exit_block = BasicBlock::Create(*llvmcx,"", current_function); // ループ終了ブロック
					BranchInst *br1 = BranchInst::Create(cond_block, current_block);

					current_block = cond_block; // 条件判定ブロックを現在のブロックにする

					OreValue cond;
					generate(program[1].children, 0, &cond); // ループ条件を評価する
					assert(cond.value);
					BranchInst::Create(body_block, exit_block, cond.value, current_block); // trueならbodyへ、falseならexitへ

					current_block = body_block; // ループ本体ブロックを現在のブロックにする

					generate(program[2].children, 0); // ループ内のコード生成
					BranchInst::Create(cond_block, current_block);

					current_block = exit_block; // ループ終了
					pos += 3;
				} else if (op == "<=") {
					assert(result);
					if (program.size() != 3) throw ArgumentCountIncorrect();
					OreValue lv, rv;
					eval(program[1], &lv); // 左辺を評価
					eval(program[2], &rv); // 右辺を評価
					result->value = new ICmpInst(*current_block, ICmpInst::ICMP_SLE, lv.value, rv.value, "cond");
					pos += 3;
				} else if (op == "+") {
					assert(result);
					if (program.size() != 3) throw ArgumentCountIncorrect();
					OreValue lv, rv;
					eval(program[1], &lv); // 左辺を評価
					eval(program[2], &rv); // 右辺を評価
					result->value = BinaryOperator::Create(BinaryOperator::Add, lv.value, rv.value, "", current_block);
					pos += 3;
				} else if (op == "print") {
					if (program.size() != 2) throw ArgumentCountIncorrect();
					OreValue v;
					eval(program[1], &v); // 結果を取得
					std::vector<Value *> args = { v.value };
					CallInst::Create(func_print_number, args, "", current_block); // print_number関数を実行
					pos += 2;
				} else {
					throw UnknownOperator(op);
				}
			} else if (node.type == JSON::Type::Array) { // [...]
				generate(node.children, 0);
				pos++;
			}
		}
		return pos - position;
	}
public:
	int run(JSON const &json)
	{
		llvmcx = &getGlobalContext();
		module = new Module("ore", *llvmcx);
		DataLayout dl(module);
		data_layout = &dl;

		func_print_number = create_print_number_func(module);

		// main関数を作成（戻り値int、引数なし）
		current_function = Function::Create(FunctionType::get(Type::getInt32Ty(*llvmcx), false), GlobalVariable::ExternalLinkage, "main", module);
		current_block = BasicBlock::Create(*llvmcx, "entry", current_function);

		// 関数の内容を構築
		generate(json.node.children, 0);

		// return 0
		ReturnInst::Create(*llvmcx, ConstantInt::get(Type::getInt32Ty(*llvmcx), 0), current_block);

		// LLVM IR を出力
		std::string ll;
		raw_string_ostream o(ll);
		module->print(o, nullptr);
		o.flush();

		fwrite(ll.c_str(), 1, ll.size(), stdout);

		return 0;
	}
};

int main(int argc, char **argv)
{
	static char const source[] =
		"[\"step\","
		"  [\"set\", \"sum\", 0 ],"
		"  [\"set\", \"i\", 1 ],"
		"  [\"while\", [\"<=\", [\"get\", \"i\"], 10],"
		"    [\"step\","
		"      [\"set\", \"sum\", [\"+\", [\"get\", \"sum\"], [\"get\", \"i\"]]],"
		"      [\"set\", \"i\", [\"+\", [\"get\", \"i\"], 1]]]],"
		"  [\"print\", [\"get\", \"sum\"]]]";

	try {
		JSON json;
		bool f = json.parse(source);
		if (!f) throw SyntaxError();
		OreLang orelang;
		orelang.run(json);
	} catch (Error &e) {
		fprintf(stderr, "error: %s\n", e.message().c_str());
	}

	return 0;
}
