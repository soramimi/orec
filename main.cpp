
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_os_ostream.h>

#include <assert.h>
#include <stdio.h>
#include <string>
#include <map>

#include "json.h"

#pragma GCC diagnostic ignored "-Wswitch"

using namespace llvm;

class OrecError {
protected:
	std::string msg;
public:
	virtual std::string message() const
	{
		return msg;
	}
};

class SyntaxError : public OrecError {
public:
	SyntaxError()
	{
		msg = "Syntax error.";
	}
};

class UnknownOperator : public OrecError {
public:
	UnknownOperator(std::string const &name)
	{
		msg = "Unknown operator '";
		msg += name;
		msg += "'.";
	}
};

class ArgumentCountIncorrect : public OrecError {
public:
	ArgumentCountIncorrect()
	{
		msg = "Argument count incorrect.";
	}
};

class VariableNotFound : public OrecError {
public:
	VariableNotFound(std::string const &name)
	{
		msg = "Variable not found '";
		msg += name;
		msg += "'.";
	}
};

class InternalError : public OrecError {
public:
	InternalError()
	{
		msg = "Internal error.";
	}
};

class OreLangCompiler {
private:
	LLVMContext llvmcx;
	Module *module;
	Function *current_function;
	BasicBlock *current_block;
	DataLayout *data_layout;
	Function *func_print_number;

	std::map<std::string, Value *> vars; // 変数

	// グローバルバイト列を作成
	static Constant *createGlobalByteArrayPtr(Module *module, void const *p, size_t n)
	{
		LLVMContext &cx = module->getContext();
		Constant *data = ConstantDataArray::get(cx, ArrayRef<uint8_t>((uint8_t const *)p, n)); // 配列定数
		GlobalVariable *gv = new GlobalVariable(*module, data->getType(), true, Function::ExternalLinkage, data); // グローバル変数
		Value *i32zero = ConstantInt::get(Type::getInt32Ty(cx), 0); // 整数のゼロ
		return ConstantExpr::getInBoundsGetElementPtr(gv->getValueType(), gv, { i32zero, i32zero });
	}

	// グローバル文字列を作成
	static Constant *createGlobalStringPtr(Module *module, StringRef str)
	{
		return createGlobalByteArrayPtr(module, str.data(), str.size() + 1); // ヌル文字を含めたバイト列を作成する
	}

	// print_number関数を作成
	static Function *create_print_number_func(Module *module)
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

	// 変数を得る
	Value *getvar(std::string const &name)
	{
		auto it = vars.find(name);
		if (it != vars.end()) {
			return it->second;
		}
		throw VariableNotFound(name);
	}

	// 構文木を評価して値を得る
	Value *eval(JSON::Node const &node)
	{
		switch (node.type) {
		case JSON::Type::Array:
			{
				Value *v = nullptr;
				generate(node.children, 0, &v); // 子を評価
				return v;
			}
		case JSON::Type::String:
			return getvar(node.value); // 変数を取得
		case JSON::Type::Number:
		case JSON::Type::Boolean:
			{
				unsigned long v = strtoul(node.value.c_str(), nullptr, 10); // 文字列から数値へ変換
				return ConstantInt::get(Type::getInt32Ty(llvmcx), (uint32_t)v); // 整数オブジェクトを作成
			}
		}
		return nullptr;
	}

	// コード生成
	size_t generate(std::vector<JSON::Node> const &program, size_t position, Value **result = nullptr)
	{
		size_t pos = position;
		while (pos < program.size()) {
			JSON::Node const &node = program[pos];
			if (node.type == JSON::Type::Array) { // [...]
				generate(node.children, 0); // 子を処理
				pos++;
			} else if (node.type == JSON::Type::String) {
				if (pos != 0) {
					// すべての命令が配列 [...] で独立している文法なのでposはゼロであるはず
					throw SyntaxError();
				}
				std::string op = program[0].value;
				if (op == "step") {
					// 特に何もしないで先へ進める
					pos++;
					pos += generate(program, pos);
				} else if (op == "set") {
					if (program.size() != 3) throw ArgumentCountIncorrect();

					std::string name = program[1].value; // 変数名

					Value *into; // 代入先変数
					auto it = vars.find(name);
					if (it == vars.end()) { // 見つからなければ
						into = new AllocaInst(Type::getInt32Ty(llvmcx), "", current_block); // 確保する
					} else {
						into = it->second; // 見つかった
						if (!isa<AllocaInst>(into)) throw InternalError();
					}

					Value *v = eval(program[2]); // 代入する値
					if (!v) throw InternalError();

					Type *ty = Type::getInt32Ty(llvmcx);
					unsigned int align = data_layout->getABITypeAlignment(ty);
					new StoreInst(v, into, false, align, current_block); // 代入する

					vars[name] = into; // 新しい値で更新する
					pos += 3;
				} else if (op == "get") {
					assert(result);
					if (program.size() != 2) throw ArgumentCountIncorrect();

					*result = eval(program[1]); // 変数を取得
					if (!isa<AllocaInst>(*result)) throw InternalError();

					*result = new LoadInst(*result, "", current_block); // 変数の値を読み取る
					pos += 2;
				} else if (op == "while") {
					if (program.size() != 3) throw ArgumentCountIncorrect();
					BasicBlock *cond_block = BasicBlock::Create(llvmcx,"while.if", current_function); // 条件判定ブロック
					BasicBlock *body_block = BasicBlock::Create(llvmcx,"while.body", current_function); // ループ本体ブロック
					BranchInst::Create(cond_block, current_block);

					current_block = cond_block; // 条件判定ブロックを現在のブロックにする
					Value *cond = nullptr;
					generate(program[1].children, 0, &cond); // ループ条件を評価する
					assert(cond);

					current_block = body_block; // ループ本体ブロックを現在のブロックにする

					generate(program[2].children, 0); // ループ内のコード生成
					BranchInst::Create(cond_block, current_block); // 条件判定ブロックへ

					current_block = BasicBlock::Create(llvmcx,"while.exit", current_function); // ループ終了ブロック

					BranchInst::Create(body_block, current_block, cond, cond_block); // 条件判定ブロックに分岐命令を追加

					pos += 3;
				} else if (op == "<=") {
					assert(result);
					if (program.size() != 3) throw ArgumentCountIncorrect();
					Value *lv = eval(program[1]); // 左辺を評価
					Value *rv = eval(program[2]); // 右辺を評価
					*result = new ICmpInst(*current_block, ICmpInst::ICMP_SLE, lv, rv, "cond");
					pos += 3;
				} else if (op == "+") {
					assert(result);
					if (program.size() != 3) throw ArgumentCountIncorrect();
					Value *lv = eval(program[1]); // 左辺を評価
					Value *rv = eval(program[2]); // 右辺を評価
					*result = BinaryOperator::Create(BinaryOperator::Add, lv, rv, "add", current_block);
					pos += 3;
				} else if (op == "print") {
					if (program.size() != 2) throw ArgumentCountIncorrect();
					Value *v = eval(program[1]); // 結果を取得
					std::vector<Value *> args = { v };
					CallInst::Create(func_print_number, args, "", current_block); // print_number関数を実行
					pos += 2;
				} else {
					throw UnknownOperator(op);
				}
			}
		}
		return pos - position;
	}
public:
	std::string compile(JSON const &json)
	{
		module = new Module("ore", llvmcx);
		DataLayout dl(module);
		data_layout = &dl;

		func_print_number = create_print_number_func(module);

		// main関数を作成（戻り値int、引数なし）
		current_function = Function::Create(FunctionType::get(Type::getInt32Ty(llvmcx), false), GlobalVariable::ExternalLinkage, "main", module);
		current_block = BasicBlock::Create(llvmcx, "entry", current_function);

		// 関数の内容を構築
		generate(json.node.children, 0);

		// return 0
		ReturnInst::Create(llvmcx, ConstantInt::get(Type::getInt32Ty(llvmcx), 0), current_block);

		// LLVM IR を出力
		std::string ll;
		raw_string_ostream o(ll);
		module->print(o, nullptr);
		o.flush();

		return ll;
	}
};

int main()
{
	static char const source[] = R"---(
["step",
  ["set", "sum", 0 ],
  ["set", "i", 1 ],
  ["while", ["<=", ["get", "i"], 10],
	["step",
	  ["set", "sum", ["+", ["get", "sum"], ["get", "i"]]],
	  ["set", "i", ["+", ["get", "i"], 1]]]],
  ["print", ["get", "sum"]]]
)---";
	try {
		JSON json;
		bool f = json.parse(source);
		if (!f) throw SyntaxError();
		OreLangCompiler orec;
		std::string llvm_ir = orec.compile(json);
		fwrite(llvm_ir.c_str(), 1, llvm_ir.size(), stdout);
	} catch (OrecError &e) {
		fprintf(stderr, "error: %s\n", e.message().c_str());
	}

	return 0;
}
