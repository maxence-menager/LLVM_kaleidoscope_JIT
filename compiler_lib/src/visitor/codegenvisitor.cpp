//
// Created by maxence on 28/03/2021.
//
#include "visitor.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"

namespace ckalei{

    CodeGenVisitor::CodeGenVisitor(): jitTopLevel(false), debug(false)
    {
        jit = std::make_unique<llvm::orc::KaleidoscopeJIT>();
        initModuleAndPassManager();
    }

    std::string CodeGenVisitor::ppformat() const
    {
        if (!lastFunction){
            return "Error during compilation\n";
        }
        std::string str;
        auto stream = llvm::raw_string_ostream(str);
        lastFunction->print(stream);
        return str;
    }

    void CodeGenVisitor::visit(NumberExprAST &node)
    {
        lastValue = llvm::ConstantFP::get(*context, llvm::APFloat(node.getVal()));
    }

    void CodeGenVisitor::visit(VariableExprAST &node)
    {
        llvm::Value *varAddress = namedValues[node.getName()];
        if (!varAddress){
            lastValue = logErrorV("Unknown variable name");
        }
        // load value from stack
        lastValue = builder->CreateLoad(varAddress, node.getName());
    }

    void CodeGenVisitor::visit(BinaryExprAST &node)
    {
        // special case for var assingment
        if (node.getOp() == '='){
            // ensure leftvalue is a var
            auto* lhse = dynamic_cast<VariableExprAST*>(node.getLeftExpr().get());
            if (!lhse){
                lastValue = logErrorV("destination of '=' must be a variable");
                return;
            }
            // codegen the rhs
            node.getRightExpr()->accept(*this); if (!lastValue){ return;}
            auto rv = lastValue;
            llvm::Value *variable = namedValues[lhse->getName()];
            if (!variable){lastValue = logErrorV("Unknown variable name"); return;}
            builder->CreateStore(rv, variable);
            lastValue = rv;
            return;
        }
        // Get left and right values
        node.getLeftExpr()->accept(*this);
        auto lv = lastValue;
        node.getRightExpr()->accept(*this);
        auto rv = lastValue;
        if (!lv || !rv){
            lastValue = nullptr;
            return;
        }

        switch (node.getOp()) {
            case '+':
                lastValue = builder->CreateFAdd(lv, rv, "addtmp");
                return;
            case '-':
                lastValue = builder->CreateFSub(lv, rv, "subtmp");
                return;
            case '*':
                lastValue = builder->CreateFMul(lv, rv, "multmp");
                return;
            case '/':
                lastValue = builder->CreateFDiv(lv, rv, "divtmp");
                return;
            case '<':
                lv = builder->CreateFCmpULT(lv, rv, "cmptmp");
                // convert bool 0/1 to double 0.0 or 1.0
                lastValue = builder->CreateUIToFP(lv, llvm::Type::getDoubleTy(*context), "booltmp");
                return;
            default:
                break;
        }
        llvm::Function *f = getFunction(std::string("binary")+node.getOp());
        assert(f && "binary operator not found");
        llvm::Value *ops[2] = {lv, rv};
        lastValue = builder->CreateCall(f, ops, "binop");
    }

    void CodeGenVisitor::visit(UnaryExprAST &node)
    {
        node.getExpr()->accept(*this);
        if (!lastValue){return;}
        auto expr = lastValue;
        llvm::Function *f = getFunction(std::string("unary")+node.getOpcode());
        assert(f && "binary operator not found");
        llvm::Value *ops[1] = {expr};
        lastValue = builder->CreateCall(f, ops, "binop");
    }

    void CodeGenVisitor::visit(DeclarationExprAST &node)
    {
        std::vector<llvm::AllocaInst *> oldBindings;
        auto *function = builder->GetInsertBlock()->getParent();
        for (const auto &val: node.getVars()){
            auto alloca = createEntryBlockAlloca(function, val.first);
            llvm::Value* varVal = llvm::ConstantFP::get(*context, llvm::APFloat(0.0));
            if (val.second){
                val.second->accept(*this);
                if (!lastValue){return;}
                varVal = lastValue;
            }
            builder->CreateStore(varVal, alloca);
            oldBindings.push_back(namedValues[val.first]);
            namedValues[val.first] = alloca;
        }
        node.getBody()->accept(*this);
        if (!lastValue){return;}
        auto bodyVal = lastValue;

        namedValues.clear();
        for (const auto &alloca: oldBindings){
            if (alloca){
                namedValues[alloca->getName()] = alloca;
            }
        }

        lastValue = bodyVal;
    }

    void CodeGenVisitor::visit(CallExprAST &node)
    {
        llvm::Function *calleeF = getFunction(node.getCallee());
        if (!calleeF){
            lastValue = logErrorV("Function not found");
            return;
        }

        if (calleeF->arg_size() != node.getArgs().size()){
            lastValue = logErrorV("Invalid number of arguments");
            return;
        }

        std::vector<llvm::Value *> argsVals;
        for (auto const& args: node.getArgs()){
            args->accept(*this);
            if (!lastValue){ // return in case of failure
                return;
            }
            argsVals.push_back(lastValue);
        }

        lastValue = builder->CreateCall(calleeF, argsVals, "calltmp");
    }

    void CodeGenVisitor::visit(IfExprAST &node)
    {
        node.getCond()->accept(*this);
        if (! lastValue){return;}
        auto condVal = lastValue;

        // Convert condition to a bool
        condVal = builder->CreateFCmpONE(condVal, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "ifcond");


        llvm::Function *function = builder->GetInsertBlock()->getParent();
        llvm::BasicBlock *thenBB = llvm::BasicBlock::Create(*context, "then", function);
        llvm::BasicBlock *elseBB = llvm::BasicBlock::Create(*context, "else");
        llvm::BasicBlock *mergeBB = llvm::BasicBlock::Create(*context, "ifcont");
        builder->CreateCondBr(condVal, thenBB, elseBB);

        // Create then value
        builder->SetInsertPoint(thenBB);
        node.getIfExpr()->accept(*this);
        if (! lastValue){return;}
        auto thenExpr = lastValue;
        builder->CreateBr(mergeBB);
        thenBB = builder->GetInsertBlock();

        // Create else value
        function->getBasicBlockList().push_back(elseBB);
        builder->SetInsertPoint(elseBB);
        llvm::Value *elseExpr = nullptr;
        if (node.haveElseMember()){
            node.getElseExpr()->accept(*this);
            if (! lastValue){return;}
            elseExpr = lastValue;
            builder->CreateBr(mergeBB);
        } else{
            logErrorV("Omitted Else are not supported yet");
            return;
        }
        elseBB = builder->GetInsertBlock();

        function->getBasicBlockList().push_back(mergeBB);
        builder->SetInsertPoint(mergeBB);
        llvm::PHINode *phiN = builder->CreatePHI(llvm::Type::getDoubleTy(*context), 2, "iftmp");

        phiN->addIncoming(thenExpr, thenBB);
        phiN->addIncoming(elseExpr, elseBB);
        lastValue = phiN;
    }

    void CodeGenVisitor::visit(ForExprAST &node)
    {
        llvm::Function *function = builder->GetInsertBlock()->getParent();
        llvm::AllocaInst *alloca = createEntryBlockAlloca(function, node.getVarName());

        node.getStart()->accept(*this);
        if (! lastValue){return;}
        auto startVal = lastValue;
        builder->CreateStore(startVal, alloca);

        llvm::BasicBlock *loopBB = llvm::BasicBlock::Create(*context, "loop", function);
        builder->CreateBr(loopBB);

        // Create body
        builder->SetInsertPoint(loopBB);
        llvm::AllocaInst* oldVar = namedValues[node.getVarName()]; // Save old var for restoration add set new var in context
        namedValues[node.getVarName()] = alloca;

        node.getBody()->accept(*this); // create body code
        if (! lastValue){return;}
        node.getStep()->accept(*this); // compute step value
        if (! lastValue){return;}
        auto stepVal = lastValue;

        /// Compute next value and store it
        llvm::Value *variable = builder->CreateLoad(alloca);
        llvm::Value *nextVar = builder->CreateFAdd(variable, stepVal, "nextvar");
        builder->CreateStore(nextVar, alloca);
        node.getEnd()->accept(*this); // compute end value
        if (! lastValue){return;}
        auto endCond = lastValue;
        endCond = builder->CreateFCmpONE(endCond, llvm::ConstantFP::get(*context, llvm::APFloat(0.0)), "loopcond");

        llvm::BasicBlock *afterBB = llvm::BasicBlock::Create(*context, "afterloop", function);

        builder->CreateCondBr(endCond, loopBB, afterBB);
        builder->SetInsertPoint(afterBB);

        // restore shadowed variable
        if (oldVar){
            namedValues[node.getVarName()] = oldVar;
        } else {
            namedValues.erase(node.getVarName());
        }
        lastValue = llvm::Constant::getNullValue(llvm::Type::getDoubleTy(*context));
    }

    void CodeGenVisitor::visit(PrototypeAST &node)
    {
        if (jitTopLevel){
            handleTopLevelExtern(node);
            return;
        }
        std::vector<llvm::Type *> argsTypes(node.getArgs().size(), llvm::Type::getDoubleTy(*context));
        auto signature = llvm::FunctionType::get(llvm::Type::getDoubleTy(*context), argsTypes, false);

        auto func = llvm::Function::Create(signature,
                                              llvm::Function::ExternalLinkage,
                                              node.getName(),
                                              module.get());

        // Set function args names
        for (int i=0; i<node.getArgs().size(); i++){
            func->getArg(i)->setName(node.getArgs()[i]);
        }
        lastFunction = func;
    }

    void CodeGenVisitor::visit(FunctionAST &node)
    {
        // Handle jit top level
        if (jitTopLevel && node.getProto()->getName() == "__anon_expr"){
            handleTopLevelExpression(node);
            return;
        }else if(jitTopLevel){
            handleTopLevelDefinition(node);
            return;
        }

        PrototypeAST& p = *(node.getProto());
        functionProtos[node.getProto()->getName()] = std::make_unique<PrototypeAST>(p);
        auto function = getFunction(p.getName());

        // Create the block for the function
        llvm::BasicBlock *bb = llvm::BasicBlock::Create(*context, "entry", function);
        builder->SetInsertPoint(bb);

        // Create a nue named value table containing functions args
        namedValues.clear();
        for (int i=0; i<function->arg_size(); i++){
            auto alloca = createEntryBlockAlloca(function, node.getProto()->getArgs()[i]);
            builder->CreateStore(function->getArg(i), alloca);
            namedValues[node.getProto()->getArgs()[i]] = alloca;
        }

        node.getBody()->accept(*this);
        auto retVal =  lastValue;
        if (retVal){
            builder->CreateRet(retVal);
            llvm::verifyFunction(*function);
            passManager->run(*function);
            lastFunction = function;
            return;
        }
        lastFunction = nullptr;
    }

    void CodeGenVisitor::handleTopLevelExpression(FunctionAST &node)
    {
        jitTopLevel = false;
        node.accept(*this);
        if (!lastFunction){
            return;
        }

        jit->addModule(std::move(module));
        initModuleAndPassManager();

        auto exprSymbol = jit->findSymbol("__anon_expr");
        assert(exprSymbol && "Function not found");

        auto adrr = exprSymbol.getAddress();
        if (!adrr){
            llvm::handleAllErrors(adrr.takeError());
            return;
        }
        double (*fp)() = (double (*)()) (intptr_t) exprSymbol.getAddress().get();
        double val = fp();
        evaluationRes->push_back(val);
    }

    void CodeGenVisitor::handleTopLevelDefinition(FunctionAST &node)
    {
        jitTopLevel = false;

        jit->addModule(std::move(module));
        initModuleAndPassManager();

        node.accept(*this);
    }

    void CodeGenVisitor::handleTopLevelExtern(PrototypeAST &node)
    {
        jitTopLevel = false;
        node.accept(*this);
        functionProtos[node.getName()] = std::make_unique<PrototypeAST>(node);
    }

    void CodeGenVisitor::initModuleAndPassManager()
    {
        context = std::make_unique<llvm::LLVMContext>();
        module = std::make_unique<llvm::Module>("jit", *context);
        module->setDataLayout(jit->getTargetMachine().createDataLayout());
        builder = std::make_unique<llvm::IRBuilder<>>(*context);
        passManager = std::make_unique<llvm::legacy::FunctionPassManager>(module.get());
        if (!debug){
            passManager->add(llvm::createPromoteMemoryToRegisterPass());
            passManager->add(llvm::createInstructionCombiningPass());
            passManager->add(llvm::createReassociatePass());
            passManager->add(llvm::createGVNPass());
            passManager->add(llvm::createCFGSimplificationPass());
        }
        passManager->doInitialization();
    }

    std::string CodeGenVisitor::getAssembly(const std::vector<std::unique_ptr<ASTNode>> &astData, bool debug)
    {
        if (debug){
            this->debug = true;
        }
        std::string res;
        for (auto const& node: astData){
            if (node != nullptr){
                node->accept(*this);
                res += ppformat();
            }
        }
        this->debug = false;
        return res;
    }

    std::unique_ptr<std::vector<double>> CodeGenVisitor::evaluate(const std::vector<std::unique_ptr<ASTNode>> &astData)
    {
        evaluationRes = std::make_unique<std::vector<double>>();
        for (auto const& node: astData){
            if (node != nullptr){
                jitTopLevel = true;
                node->accept(*this);
            }
        }
        return std::move(evaluationRes);
    }

    llvm::Function *CodeGenVisitor::getFunction(const std::string& name)
    {
        if (auto *f = module->getFunction(name)){
            return f;
        }

        auto fi = functionProtos.find(name);
        if (fi != functionProtos.end()){
           fi->second->accept(*this);
            return this->lastFunction;
        }

        return nullptr;
    }
}
