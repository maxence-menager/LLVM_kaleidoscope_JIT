//
// Created by maxence on 21/03/2021.
//

#ifndef LLVM_KALEIDOSCOPE_AST_H
#define LLVM_KALEIDOSCOPE_AST_H

#include <string>
#include <utility>
#include <memory>
#include <vector>

#include "visitor.h"


namespace ckalei {

    class Visitor;

    /// Base class for ast nodes
    class ASTNode{
    public:
        virtual void accept(Visitor& visitor) = 0;
        virtual ~ASTNode() = default;
    };

    /// Base class for all expression node
    class ExprAST: public ASTNode {
    public:
        void accept(Visitor& visitor) override = 0;
        ~ExprAST() override = default;
    };

    /// Node representing a number
    class NumberExprAST : public ExprAST {
    public:
        explicit NumberExprAST(double val) : val(val)
        {};
        void accept(Visitor& visitor) override;
        [[nodiscard]] double getVal() const {return val;}

    private:
        double val;
    };

    /// Node representing a variable name
    class VariableExprAST : public ExprAST {
    public:
        explicit VariableExprAST(std::string name) : name(std::move(name))
        {};
        void accept(Visitor& visitor) override;
        [[nodiscard]] const std::string &getName() const {return name;}

    private:
        std::string name;
    };

    /// Node representing an unary expression
    class UnaryExprAST: public ExprAST {

    public:
        UnaryExprAST(char opcode, std::unique_ptr<ExprAST> expr): opcode(opcode), expr(std::move(expr)) {};
        void accept(Visitor& visitor) override;

        [[nodiscard]] char getOpcode() const {return opcode;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getExpr() const {return expr;}

    private:
        char opcode;
        std::unique_ptr<ExprAST> expr;
    };

    /// Node representing a binary expressing
    class BinaryExprAST : public ExprAST {
    public:
        BinaryExprAST(std::unique_ptr<ExprAST> leftExpr,
                      std::unique_ptr<ExprAST> rightExpr,
                      char op) : rightExpr(std::move(rightExpr)), leftExpr(std::move(leftExpr)), op(op)
        {};
        void accept(Visitor& visitor) override;

        [[nodiscard]] char getOp() const {return op;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getRightExpr() const {return rightExpr;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getLeftExpr() const {return leftExpr;}

    private:
        char op; // operation of the expression
        std::unique_ptr<ExprAST>leftExpr, rightExpr;
    };

    /// Node representing variables creation: var a=1, b, c, d=2
    class DeclarationExprAST: public  ExprAST{
    public:
        explicit DeclarationExprAST(std::unique_ptr<ExprAST> body,
                                    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> vars)
                                    : vars(std::move(vars)), body(std::move(body))
                                    {}
        void accept(Visitor& visitor) override;

        [[nodiscard]] const std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> &getVars() const {return vars;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getBody() const{return body;}

    private:
        // List of allocation, pairs name: values
        std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> vars;
        // Body where variable are valid
        std::unique_ptr<ExprAST> body;
    };

    /// Node representing a function call
    class CallExprAST : public ExprAST {
    public:
        CallExprAST(std::string callee,
                    std::vector<std::unique_ptr<ExprAST>> args) : callee(std::move(callee)), args(std::move(args))
        {};
        void accept(Visitor& visitor) override;

        [[nodiscard]] const std::string &getCallee() const {return callee;}
        [[nodiscard]] const std::vector<std::unique_ptr<ExprAST>> &getArgs() const {return args;}

    private:
        std::string callee;
        std::vector<std::unique_ptr<ExprAST>> args;
    };

    /// IfExprAST: node for if then else expression
    class IfExprAST: public ExprAST{
    public:
        /// Create ifExprAst node elseExpr can be null if the node have no expression
        IfExprAST(
                std::unique_ptr<ExprAST> cond,
                std::unique_ptr<ExprAST> ifExpr,
                std::unique_ptr<ExprAST> elseExpr)
                : ifExpr(std::move(ifExpr)),
                elseExpr(std::move(elseExpr)),
                cond(std::move(cond)),
                haveElse(true)
        {}

        IfExprAST(
                std::unique_ptr<ExprAST> cond,
                std::unique_ptr<ExprAST> ifExpr)
                : ifExpr(std::move(ifExpr)),
                  elseExpr(nullptr),
                  cond(std::move(cond)),
                  haveElse(false)
        {}

        void accept(Visitor& visitor);

        [[nodiscard]] const std::unique_ptr<ExprAST> &getCond() const{return cond;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getIfExpr() const {return ifExpr;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getElseExpr() const {return elseExpr;}
        [[nodiscard]] bool haveElseMember() const{return haveElse;}

    private:
        bool haveElse;
        std::unique_ptr<ExprAST> cond;
        std::unique_ptr<ExprAST> ifExpr;
        std::unique_ptr<ExprAST> elseExpr;
    };

    /// Node representing a for loop
    class ForExprAST: public ExprAST{

    public:
        ForExprAST(
                std::unique_ptr<ExprAST> start,
                std::unique_ptr<ExprAST> step,
                std::unique_ptr<ExprAST> end,
                std::unique_ptr<ExprAST> body,
                std::string varName
                )
                : start(std::move(start)),
                step(std::move(step)),
                end(std::move(end)),
                body(std::move(body)),
                varName(std::move(varName)){};

        void accept(Visitor& visitor) override;

        [[nodiscard]] const std::string &getVarName() const{return varName;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getStart() const{return start;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getStep() const{return step;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getEnd() const{return end;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getBody() const{return body;}

    private:
        std::string varName;
        std::unique_ptr<ExprAST> start;
        std::unique_ptr<ExprAST> step;
        std::unique_ptr<ExprAST> end;
        std::unique_ptr<ExprAST> body;
    };

    /// Node representing a function prototype
    class PrototypeAST: public ASTNode{
    public:
        PrototypeAST(std::string name,
                     std::vector<std::string> args,
                     bool isOperator = false,
                     int precedence = 0):
                        name(std::move(name)),
                        args(std::move(args)),
                        isOperator(isOperator),
                        precedence(precedence){};
        PrototypeAST(const PrototypeAST& other):
                name(other.getName()),
                args(other.getArgs()),
                isOperator(other.isOperatorProto()),
                precedence(other.getPrecedence())  {};

        void accept(Visitor& visitor) override;
        [[nodiscard]] const std::string &getName() const {return name;}
        [[nodiscard]] const std::vector<std::string> &getArgs() const {return args;}
        [[nodiscard]] bool isOperatorProto() const{return isOperator;}
        [[nodiscard]] int getPrecedence() const{return precedence;}

    private:
        std::string name;
        std::vector<std::string> args;
        bool isOperator;
        int precedence; // precedence if a binary op.
    };

    /// Node representing a function declaration
    class FunctionAST: public ASTNode{
    public:
        FunctionAST(std::unique_ptr<PrototypeAST> proto,
                    std::unique_ptr<ExprAST> body): proto(std::move(proto)), body(std::move(body)) {};

        void accept(Visitor& visitor) override;
        [[nodiscard]] const std::unique_ptr<PrototypeAST> &getProto() const {return proto;}
        [[nodiscard]] const std::unique_ptr<ExprAST> &getBody() const {return body;}

    private:
        std::unique_ptr<PrototypeAST> proto;
        std::unique_ptr<ExprAST> body;
    };
}

#endif //LLVM_KALEIDOSCOPE_AST_H
