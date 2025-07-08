#include "TypeCheck.h"
#include <ranges>

static bool isNumeric(Value::Type type) {
    return type == Value::I8 || type == Value::I16 ||
           type == Value::I32 || type == Value::I64;
}

static Value::Type promoteNumeric(Value::Type a, Value::Type b) {
    if (a == Value::I64 || b == Value::I64) return Value::I64;
    if (a == Value::I16 || b == Value::I16) return Value::I16;
    if (a == Value::I8 || b == Value::I8) return Value::I8;
    return Value::I32;
}


Value TypeCheck::lookup(const std::string& id) const {
    return *table->lookup(id);
}

void TypeCheck::declare(const std::string& id, const Value& val) const {
        table->declare(id, val);
}

int TypeCheck::getScopeDepth() const {
    return table->getScopeDepth();
}

void TypeCheck::assertMut(const Value& val, int line, int col) {
    if (!val.mut) {
        throw std::runtime_error("cannot assign to immutable variable at "
                                + std::to_string(line) + ':' + std::to_string(col));
    }
}

Value TypeCheck::assertType(Value from, Value to, int line, int col) {
    if (from.type == to.type) return to;
    if (isNumeric(from.type) && isNumeric(to.type)
        && (from.literal ^ to.literal)) {
        return promoteNumeric(from.type, to.type);
    }
    throw std::runtime_error("type mismatch at " + std::to_string(line) + ':' +
                             std::to_string(col));
}

void TypeCheck::assertStringRef(const Value& val, int line, int col) {
    if (val.type == Value::STR && !val.ref) {
        throw std::runtime_error("string type must be referenced at " +
                                 std::to_string(line) + ':' +
                                 std::to_string(col));
    }
}

std::string TypeCheck::typeToFormat(Value::Type type) {
    switch (type) {
        case Value::CHAR: return "%c";
        case Value::BOOL: return "%s";
        case Value::I32:  return "%d";
        case Value::I64:  return "%ld";
        case Value::STR:  return "%s";
        default:          return "%d";
    }
}


TypeCheck::~TypeCheck() = default;

// Visit methods for expressions
Value TypeCheck::visit(Block* block) {
    ++blockDepth;
    table->pushScope();
    Value::Type last = Value::UNIT;
    for (auto stmt : block->stmts) {
        last = stmt->accept(this).type;
    }
    table->popScope();
    --blockDepth;
    block->type = last;
    return {last};
}

Value TypeCheck::visit(BinaryExp* exp) {
    Value lhs = exp->lhs->accept(this);
    Value rhs = exp->rhs->accept(this);
    switch (exp->op) {
        case BinaryExp::LAND:
        case BinaryExp::LOR:
            assertType(lhs, Value::BOOL, exp->line, exp->col);
            assertType(rhs, Value::BOOL, exp->line, exp->col);
            exp->type = Value::BOOL;
            break;
        case BinaryExp::GT: case BinaryExp::LT:
        case BinaryExp::GE: case BinaryExp::LE:
            if (!lhs.isNumber() || !rhs.isNumber())
                throw std::runtime_error("Invalid binary operation");
            exp->type = Value::BOOL;
            break;
        case BinaryExp::EQ: case BinaryExp::NEQ:
            if (lhs.type != rhs.type) {
                if (!(lhs.isNumber() && rhs.isNumber()))
                    assertType(lhs, rhs, exp->line, exp->col);
            }
            if (!(lhs.isNumber() && rhs.isNumber()) &&
                lhs.type != Value::I32 && lhs.type != Value::BOOL)
                throw std::runtime_error("Invalid binary operation");
            exp->type = Value::BOOL;
            break;
        case BinaryExp::PLUS: case BinaryExp::MINUS:
        case BinaryExp::TIMES: case BinaryExp::DIV:
            if (!lhs.isNumber() || !rhs.isNumber())
                throw std::runtime_error("Invalid binary operation");
            exp->type = assertType(lhs, rhs, exp->line, exp->col).type;
            break;
        default:
            throw std::runtime_error("Invalid binary operation");
    }
    Value val (exp->type);
    if (lhs.literal && rhs.literal) val.literal = true;
    return val;

}

Value TypeCheck::visit(UnaryExp* exp) {
    Value v = exp->exp->accept(this);
    switch (exp->op) {
        case UnaryExp::LNOT:
            assertType(v, Value::BOOL, exp->line, exp->col);
            exp->type = Value::BOOL;
            break;
        default:
            throw std::runtime_error("Invalid unary operation");
    }
    return v;
}

Value TypeCheck::visit(Literal* exp) {
    return exp->value;
}

Value TypeCheck::visit(Variable* exp) {
    Value* entry = table->lookup(exp->name);
    Value v = *entry;
    if (!lhsContext && !v.initialized)
        throw std::runtime_error("use of uninitialized variable at " +
                                 std::to_string(exp->line) + ':' +
                                 std::to_string(exp->col));
    if (lhsContext) {
        lhsEntry = entry;
        lhsIsVariable = true;
    }
    exp->type = v.type;
    return v;
}

Value TypeCheck::visit(FunCall* exp) {
    Value fn = lookup(exp->id);
    if (fn.types.size() < exp->args.size())
        throw std::runtime_error("too many arguments for " + exp->id + " at " +
                                 std::to_string(exp->line) + ':' +
                                 std::to_string(exp->col));
    if (fn.types.size() > exp->args.size())
        throw std::runtime_error("not enough arguments for " + exp->id + " at " +
                                 std::to_string(exp->line) + ':' +
                                 std::to_string(exp->col));
    auto it = fn.types.begin();
    for (auto arg : exp->args) {
        Value a = arg->accept(this);
        assertType(a, *it, arg->line, arg->col);
        ++it;
    }
    exp->type = fn.type;
    return {fn.type};
}

Value TypeCheck::visit(IfExp* exp) {
    Value cond = exp->ifBranch->getCondition()->accept(this);
    Exp* condex = exp->ifBranch->getCondition();
    assertType(cond, Value::BOOL, condex->line, condex->col);
    Value t = exp->ifBranch->getBlock()->accept(this);
    Value e = {Value::UNIT};
    for (auto br : exp->elseIfBranches) {
        Value c = br->getCondition()->accept(this);
        Exp* cex = br->getCondition();
        assertType(c, Value::BOOL, cex->line, cex->col);
        e = br->getBlock()->accept(this);
    }
    if (exp->elseBranch)
        e = exp->elseBranch->getBlock()->accept(this);
    assertType(t, e, exp->line, exp->col);
    exp->type = t.type;
    return {exp->type};
}

Value TypeCheck::visit(LoopExp* exp) {
    Value v = exp->block->accept(this);
    exp->type = v.type;
    return v;
}

Value TypeCheck::visit(SubscriptExp* exp) {
    Value* entry = table->lookup(exp->id);
    Value coll = *entry;
    Value idx = exp->exp->accept(this);
    assertType(idx, Value::I32, exp->line, exp->col);
    if (!(coll.type == Value::STR || coll.size > 0))
        throw std::runtime_error("subscript on non-indexable at " +
                                 std::to_string(exp->line) + ':' +
                                 std::to_string(exp->col));
    if (lhsContext) {
        lhsEntry = entry;
        lhsIsVariable = false;
    }
    exp->type = coll.type;
    return coll;
}

Value TypeCheck::visit(SliceExp* exp) {
    lookup(exp->id);
    if (exp->start) {
        Value s = exp->start->accept(this);
        Exp* ex = exp->start;
        assertType(s, Value::I32, ex->line, ex->col);
    }
    if (exp->end) {
        Value e = exp->end->accept(this);
        Exp* ex = exp->end;
        assertType(e, Value::I32, ex->line, ex->col);
    }
    exp->type = Value::STR;
    return {exp->type};
}

Value TypeCheck::visit(ReferenceExp* exp) {
    Value v = exp->exp->accept(this);
    v.ref = true;
    exp->type = v.type;
    return v;
}

Value TypeCheck::visit(ArrayExp* exp) {
    Value::Type elType = Value::UNDEFINED;
    bool ref = true;
    for (auto el : exp->elements) {
        Value v = el->accept(this);
        if (elType == Value::UNDEFINED)
            elType = v.type;
        else
            assertType(v, elType, el->line, el->col);
        ref = ref && v.ref;
    }
    exp->type = elType;
    Value val{elType};
    val.size = exp->elements.size();
    val.ref = ref;
    return val;
}

Value TypeCheck::visit(UniformArrayExp* exp) {
    Value v = exp->value->accept(this);
    assertStringRef(v, exp->line, exp->col);
    Value s = exp->size->accept(this);
    Exp* size = exp->size;
    assertType(s, Value::I32, size->line, size->col);
    if (s.numericValues.empty()) {
        throw std::runtime_error("array size must be constant at " +
                                 std::to_string(size->line) + ':' +
                                 std::to_string(size->col));
    }
    if (s.numericValues.front() <= 0) {
        throw std::runtime_error("array size must be positive at " +
                                 std::to_string(size->line) + ':' +
                                 std::to_string(size->col));
    }
    exp->type = v.type;
    Value val{v.type};
    val.size = s.numericValues.front();
    val.ref = v.ref;
    return val;
}

// Visit methods for statements
Value TypeCheck::visit(DecStmt* stmt) {
    if (stmt->var.type == Value::STR)
        assertStringRef(stmt->var, stmt->line, stmt->col);
    if (stmt->var.size < 0)
        throw std::runtime_error("array size must be positive at " +
                                 std::to_string(stmt->line) + ':' +
                                 std::to_string(stmt->col));
    Value rhs{stmt->var.type};
    if (stmt->rhs) {
        rhs = stmt->rhs->accept(this);
        if (stmt->var.type == Value::UNDEFINED)
            stmt->var.type = rhs.type;
        stmt->var.type = assertType(rhs, stmt->var, stmt->line, stmt->col).type;
        if (stmt->var.type == Value::STR)
            assertStringRef(rhs, stmt->line, stmt->col);
        stmt->var.initialized = true;
    } else {
        stmt->var.initialized = false;
    }
    declare(stmt->id, stmt->var);
    dec[stmt->id] = stmt;
    return stmt->var;
}

Value TypeCheck::visit(AssignStmt* stmt) {
    lhsContext = true;
    lhsIsVariable = false;
    lhsEntry = nullptr;
    Value lhs = stmt->lhs->accept(this);
    lhsContext = false;
    Value rhs = stmt->rhs->accept(this);
    assertStringRef(rhs, stmt->line, stmt->col);
    if (lhsIsVariable && lhsEntry) {
        if (!lhsEntry->initialized) {
            if (lhsEntry->type == Value::UNDEFINED)
                lhsEntry->type = rhs.type;
            lhsEntry->ref = rhs.ref;
            if (rhs.size > 0)
                lhsEntry->size = rhs.size;
            lhsEntry->initialized = true;
            std::string id;
            auto var = dynamic_cast<Variable*>(stmt->lhs);
            if (var) {
                id = var->name;
            }
            // or SubscriptExp
            auto var2 = dynamic_cast<SubscriptExp*>(stmt->lhs);
            if (var2) {
                id = var2->id;
            }
            table->update(id, *lhsEntry);
            dec[id]->var = *lhsEntry;
        } else {
            assertMut(*lhsEntry, stmt->line, stmt->col);
            assertType(rhs, *lhsEntry, stmt->line, stmt->col);
            if (lhsEntry->type == Value::STR)
                assertStringRef(*lhsEntry, stmt->line, stmt->col);
        }
    } else {
        assertMut(lhs, stmt->line, stmt->col);
        assertType(rhs, lhs, stmt->line, stmt->col);
    }
    return {Value::UNIT};
}

Value TypeCheck::visit(CompoundAssignStmt* stmt) {
    Value lhs = stmt->lhs->accept(this);
    Value rhs = stmt->rhs->accept(this);
    assertMut(lhs, stmt->line, stmt->col);
    if (!lhs.isNumber() || !rhs.isNumber())
        throw std::runtime_error("Invalid compound assignment");
    return {Value::UNIT};
}

Value TypeCheck::visit(ForStmt* stmt) {
    Value val = stmt->start->accept(this);
    stmt->start->type = val.type;
    val = stmt->end->accept(this);
    stmt->end->type = val.type;
    table->pushScope();
    Value value (Value::I32);
    value.initialized = true;
    declare(stmt->id, value);
    stmt->block->accept(this);
    table->popScope();
    return {Value::UNIT};
}


Value TypeCheck::visit(WhileStmt* stmt) {
    Value c = stmt->cond->accept(this);
    Exp* cond = stmt->cond;
    assertType(c, Value::BOOL, cond->line, cond->col);
    table->pushScope();
    stmt->block->accept(this);
    table->popScope();
    return {Value::UNIT};
}

Value TypeCheck::visit(PrintStmt* stmt) {
    std::string parsed;
    size_t pos = 0;
    auto it = stmt->args.begin();

    const std::string& input = stmt->strLiteral;
    while (true) {
        size_t open = input.find('{', pos);
        if (open == std::string::npos) {
            parsed += input.substr(pos);
            break;
        }

        parsed += input.substr(pos, open - pos);

        if (open + 1 >= input.size() || input[open + 1] != '}') {
            throw std::runtime_error(
                "unsupported format string at " + std::to_string(stmt->line) + ":" + std::to_string(stmt->col));
        }

        if (it == stmt->args.end()) {
            throw std::runtime_error(
                "not enough arguments for print at " + std::to_string(stmt->line) + ":" + std::to_string(stmt->col));
        }

        Value v = (*it)->accept(this);
        parsed += typeToFormat(v.type);

        ++it;
        pos = open + 2;
    }

    if (it != stmt->args.end()) {
        throw std::runtime_error(
            "too many arguments for print at " + std::to_string(stmt->line) + ":" + std::to_string(stmt->col));
    }

    stmt->strLiteral = parsed + "\\n";
    return {Value::UNIT};
}

Value TypeCheck::visit(BreakStmt* stmt) {
    if (blockDepth <= 0)
        throw std::runtime_error("break outside loop at " +
                                 std::to_string(stmt->line) + ':' +
                                 std::to_string(stmt->col));
    Value r{Value::UNIT};
    if (stmt->exp) r = stmt->exp->accept(this);
    stmt->type = r.type;
    return r;
}

Value TypeCheck::visit(ReturnStmt* stmt) {
    if (getScopeDepth() == 0)
        throw std::runtime_error("return outside function at " +
                                 std::to_string(stmt->line) + ':' +
                                 std::to_string(stmt->col));
    Value r{Value::UNIT};
    if (stmt->exp) r = stmt->exp->accept(this);
    assertType(r, currentReturnType, stmt->line, stmt->col);
    stmt->type = r.type;
    return {Value::UNIT};
}

Value TypeCheck::visit(ExpStmt* stmt) {
    Value r = stmt->exp->accept(this);
    stmt->type = stmt->returnValue ? r.type : Value::UNIT;
    return {stmt->type};
}

// Visit methods for functions and programs
Value TypeCheck::visit(Fun* fun) {
    --blockDepth;
    table->pushScope();
    currentReturnType = fun->type != Value::UNDEFINED ? fun->type : Value::UNIT;
    for (const auto& p : fun->params) {
        Value paramVal{p.type};
        paramVal.initialized = true; // parameters are always initialized
        declare(p.id, paramVal);
    }
    Value r = fun->block->accept(this);
    fun->type = currentReturnType;
    table->popScope();
    ++blockDepth;
    return {Value::UNIT};
}

void TypeCheck::visit(Program* program) {
    table->pushScope();
    for (const auto& [id, fun]: program->funs) {
        Value val{fun->type, true};
        for (const auto& param : fun->params)
            val.addType(param.type);
        declare(id, val);
    }
    for (const auto &[id, fun]: program->funs) {
        fun->accept(this);
        Value* entry = table->lookup(id);
        if (entry) entry->type = fun->type != Value::UNDEFINED ? fun->type : Value::UNIT;
    }
    table->popScope();
}
