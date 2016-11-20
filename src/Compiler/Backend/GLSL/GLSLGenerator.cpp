/*
 * GLSLGenerator.cpp
 * 
 * This file is part of the XShaderCompiler project (Copyright (c) 2014-2016 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "GLSLGenerator.h"
#include "GLSLExtensionAgent.h"
#include "GLSLConverter.h"
#include "GLSLKeywords.h"
#include "GLSLIntrinsics.h"
#include "Exception.h"
#include "ReferenceAnalyzer.h"
#include "TypeDenoter.h"
#include "AST.h"
#include "Helper.h"
#include <initializer_list>
#include <algorithm>
#include <cctype>


namespace Xsc
{


/*
 * Internal members
 */

static const std::string interfaceBlockPrefix = "_I";


/*
 * GLSLGenerator class
 */

GLSLGenerator::GLSLGenerator(Log* log) :
    Generator{ log }
{
    EstablishMaps();
}

void GLSLGenerator::GenerateCodePrimary(
    Program& program, const ShaderInput& inputDesc, const ShaderOutput& outputDesc)
{
    /* Store parameters */
    shaderTarget_   = inputDesc.shaderTarget;
    versionOut_     = outputDesc.shaderVersion;
    localVarPrefix_ = outputDesc.formatting.prefix;
    allowLineMarks_ = outputDesc.formatting.lineMarks;
    stats_          = outputDesc.statistics;

    if (program.entryPointRef)
    {
        try
        {
            /* Mark all reachable AST nodes */
            {
                ReferenceAnalyzer refAnalyzer;
                refAnalyzer.MarkReferencesFromEntryPoint(program);
            }

            /* Convert AST for GLSL code generation */
            {
                GLSLConverter converter;
                converter.Convert(program, inputDesc.shaderTarget);
            }

            /* Write header */
            if (inputDesc.entryPoint.empty())
                Comment("GLSL " + TargetToString(shaderTarget_));
            else
                Comment("GLSL " + TargetToString(shaderTarget_) + " \"" + inputDesc.entryPoint + "\"");
        
            Comment("Generated by XShaderCompiler");

            Comment(TimePoint());
            Blank();

            /* Write GLSL version */
            Version(static_cast<int>(versionOut_));
            Blank();

            /* Visit program AST */
            Visit(&program);
        }
        catch (const Report& e)
        {
            throw e;
        }
        catch (const ASTRuntimeError& e)
        {
            Error(e.what(), e.GetAST());
        }
        catch (const std::exception& e)
        {
            Error(e.what());
        }
    }
    else
        Error("entry point \"" + inputDesc.entryPoint + "\" not found");
}


/*
 * ======= Private: =======
 */

void GLSLGenerator::EstablishMaps()
{
    #if 1 // TODO: remove all HLSL mappings from here !!!

    texFuncMap_ = std::map<std::string, std::string>
    {
        { "GetDimensions ",     "textureSize"   },
        { "Load",               "texelFetch"    },
        { "Sample",             "texture"       },
        { "SampleBias",         "textureOffset" },
        //{ "SampleCmp", "" },
        //{ "SampleCmpLevelZero", "" },
        { "SampleGrad",         "textureGrad"   },
        { "SampleLevel",        "textureLod"    },
    };

    #endif
}

void GLSLGenerator::Comment(const std::string& text)
{
    WriteLn("// " + text);
}

void GLSLGenerator::Version(int versionNumber)
{
    WriteLn("#version " + std::to_string(versionNumber));
}

void GLSLGenerator::Line(int lineNumber)
{
    if (allowLineMarks_)
        WriteLn("#line " + std::to_string(lineNumber));
}

void GLSLGenerator::Line(const TokenPtr& tkn)
{
    Line(tkn->Pos().Row());
}

void GLSLGenerator::Line(const AST* ast)
{
    Line(ast->area.Pos().Row());
}

void GLSLGenerator::AppendExtension(const std::string& extensionName)
{
    WriteLn("#extension " + extensionName + " : enable");// "require" or "enable"
}

void GLSLGenerator::AppendRequiredExtensions(Program& ast)
{
    /* Determine all required GLSL extensions with the GLSL extension agent */
    GLSLExtensionAgent extensionAgent;
    auto requiredExtensions = extensionAgent.DetermineRequiredExtensions(ast, versionOut_);

    if (!requiredExtensions.empty())
    {
        for (const auto& ext : requiredExtensions)
            AppendExtension(ext);
        Blank();
    }
}

void GLSLGenerator::AppendAllReferencedIntrinsics(Program& ast)
{
    auto Used = [&ast](Intrinsic intr)
    {
        return (ast.usedIntrinsics.find(intr) != ast.usedIntrinsics.end());
    };

    if (Used(Intrinsic::Clip))
        AppendClipIntrinsics();
}

void GLSLGenerator::AppendClipIntrinsics()
{
    WriteLn("void clip(float x) { if (x < 0.0) discard; }");

    for (const auto& typeName : std::vector<std::string>{ "vec2", "vec3", "vec4" })
        WriteLn("void clip(" + typeName + " x) { if (any(lessThan(x, " + typeName + "(0.0)))) discard; }");

    Blank();
}

void GLSLGenerator::OpenScope()
{
    WriteLn("{");
    IncIndent();
}

void GLSLGenerator::CloseScope(bool semicolon)
{
    DecIndent();
    WriteLn(semicolon ? "};" : "}");
}

void GLSLGenerator::ValidateRegisterPrefix(const std::string& registerName, char prefix, const AST* ast)
{
    if (registerName.empty() || registerName[0] != prefix)
    {
        Error(
            "invalid register prefix '" + std::string(1, registerName[0]) +
            "' (expected '" + std::string(1, prefix) + "')",
            ast
        );
    }
}

int GLSLGenerator::RegisterIndex(const std::string& registerName)
{
    return std::stoi(registerName.substr(1));
}

//TODO: move this to GLSLConverter
#if 1
std::string GLSLGenerator::BRegister(const std::string& registerName, const AST* ast)
{
    ValidateRegisterPrefix(registerName, 'b', ast);
    return registerName.substr(1);
}

std::string GLSLGenerator::TRegister(const std::string& registerName, const AST* ast)
{
    ValidateRegisterPrefix(registerName, 't', ast);
    return registerName.substr(1);
}

std::string GLSLGenerator::SRegister(const std::string& registerName, const AST* ast)
{
    ValidateRegisterPrefix(registerName, 's', ast);
    return registerName.substr(1);
}

std::string GLSLGenerator::URegister(const std::string& registerName, const AST* ast)
{
    ValidateRegisterPrefix(registerName, 'u', ast);
    return registerName.substr(1);
}
#endif

//TODO: move this to GLSLConverter
bool GLSLGenerator::MustResolveStruct(StructDecl* ast) const
{
    return
    (
        ( shaderTarget_ == ShaderTarget::VertexShader && ast->flags(StructDecl::isShaderInput) ) ||
        ( shaderTarget_ == ShaderTarget::FragmentShader && ast->flags(StructDecl::isShaderOutput) ) ||
        ( shaderTarget_ == ShaderTarget::ComputeShader && ( ast->flags(StructDecl::isShaderInput) || ast->flags(StructDecl::isShaderOutput) ) )
    );
}

bool GLSLGenerator::IsVersionOut(int version) const
{
    return static_cast<int>(versionOut_) >= version;
}

/* ------- Visit functions ------- */

#define IMPLEMENT_VISIT_PROC(AST_NAME) \
    void GLSLGenerator::Visit##AST_NAME(AST_NAME* ast, void* args)

IMPLEMENT_VISIT_PROC(Program)
{
    /* Append required extensions first */
    AppendRequiredExtensions(*ast);

    /* Write 'gl_FragCoord' layout */
    if (shaderTarget_ == ShaderTarget::FragmentShader)
    {
        BeginLn();
        {
            Write("layout(origin_upper_left");
            if (GetProgram()->flags(Program::hasSM3ScreenSpace))
                Write(", pixel_center_integer");
            Write(") in vec4 gl_FragCoord;");
        }
        EndLn();
        Blank();
    }

    /* Write entry point attributes */
    if (!ast->entryPointRef->attribs.empty())
    {
        for (auto& attrib : ast->entryPointRef->attribs)
            WriteAttribute(attrib.get());
        Blank();
    }

    /* Append default helper macros and functions */
    AppendAllReferencedIntrinsics(*ast);

    if (shaderTarget_ == ShaderTarget::FragmentShader)
        WriteFragmentShaderOutput();

    Visit(ast->globalStmnts);
}

IMPLEMENT_VISIT_PROC(CodeBlock)
{
    OpenScope();
    {
        Visit(ast->stmnts);
    }
    CloseScope();
}

IMPLEMENT_VISIT_PROC(FunctionCall)
{
    if (ast->intrinsic == Intrinsic::Mul)
        WriteFunctionCallIntrinsicMul(ast);
    else if (ast->intrinsic == Intrinsic::Rcp)
        WriteFunctionCallIntrinsicRcp(ast);
    else if (ast->flags(FunctionCall::isTexFunc))
        WriteFunctionCallIntrinsicTex(ast);
    else if (ast->intrinsic >= Intrinsic::InterlockedAdd && ast->intrinsic <= Intrinsic::InterlockedXor)
        WriteFunctionCallIntrinsicAtomic(ast);
    else
        WriteFunctionCallStandard(ast);
}

IMPLEMENT_VISIT_PROC(StructDecl)
{
    bool semicolon = (args != nullptr ? *reinterpret_cast<bool*>(&args) : false);

    /*
    Check if struct must be resolved:
    -> vertex shaders can not have input interface blocks and
       fragment shaders can not have output interface blocks
    -> see https://www.opengl.org/wiki/Interface_Block_%28GLSL%29#Input_and_output
    */
    auto resolveStruct = MustResolveStruct(ast);

    if ( resolveStruct || ( !ast->flags(StructDecl::isShaderInput) && !ast->flags(StructDecl::isShaderOutput) ) )
    {
        /* Write structure declaration */
        BeginLn();
        {
            Write("struct");
            if (!ast->ident.empty())
                Write(' ' + ast->ident);
        }
        EndLn();

        OpenScope();
        {
            VisitStructDeclMembers(ast);
        }
        CloseScope(semicolon);
    }

    /* Write structure members as global input/output variables (if structure must be resolved) */
    if (resolveStruct)
    {
        for (auto& member : ast->members)
        {
            /* Append struct input/output flag to member */
            if (ast->flags(StructDecl::isShaderInput))
                member->flags << VarDeclStmnt::isShaderInput;
            else if (ast->flags(StructDecl::isShaderOutput))
                member->flags << VarDeclStmnt::isShaderOutput;

            Visit(member);
        }
    }
    /* Write this structure as interface block (if structure doesn't need to be resolved) */
    else if (ast->flags(StructDecl::isShaderInput) || ast->flags(StructDecl::isShaderOutput))
    {
        BeginLn();
        {
            if (ast->flags(StructDecl::isShaderInput))
                Write("in");
            else
                Write("out");
            Write(" " + interfaceBlockPrefix + ast->ident);
        }
        EndLn();

        OpenScope();
        {
            isInsideInterfaceBlock_ = true;

            Visit(ast->members);

            isInsideInterfaceBlock_ = false;
        }
        CloseScope();

        WriteLn(ast->aliasName + ";");
    }
}

IMPLEMENT_VISIT_PROC(SwitchCase)
{
    /* Write case header */
    if (ast->expr)
    {
        BeginLn();
        {
            Write("case ");
            Visit(ast->expr);
            Write(":");
        }
        EndLn();
    }
    else
        WriteLn("default:");

    /* Write statement list */
    IncIndent();
    {
        Visit(ast->stmnts);
    }
    DecIndent();
}

/* --- Variables --- */

IMPLEMENT_VISIT_PROC(VarType)
{
    if (ast->structDecl)
        Visit(ast->structDecl);
    else
        WriteTypeDenoter(*ast->typeDenoter, ast);
}

IMPLEMENT_VISIT_PROC(VarIdent)
{
    /* Write single identifier */
    Write(ast->ident);

    /* Write array index expressions */
    WriteArrayDims(ast->arrayIndices);

    if (ast->next)
    {
        Write(".");
        Visit(ast->next);
    }
}

IMPLEMENT_VISIT_PROC(VarDecl)
{
    if (ast->flags(VarDecl::isLocalVar))
        Write(localVarPrefix_);
    
    Write(ast->ident);
    WriteArrayDims(ast->arrayDims);

    if (ast->initializer)
    {
        Write(" = ");
        Visit(ast->initializer);
    }
}

/* --- Declaration statements --- */

IMPLEMENT_VISIT_PROC(FunctionDecl)
{
    if (!ast->flags(AST::isReachable))
        return;

    Line(ast);

    /* Write function header */
    BeginLn();
    {
        if (ast->flags(FunctionDecl::isEntryPoint))
            Write("void main()");
        else
        {
            Visit(ast->returnType);
            Write(" " + ast->ident + "(");

            /* Write parameters */
            for (size_t i = 0; i < ast->parameters.size(); ++i)
            {
                VisitParameter(ast->parameters[i].get());
                if (i + 1 < ast->parameters.size())
                    Write(", ");
            }

            Write(")");

            if (!ast->codeBlock)
            {
                /*
                This is only a function forward declaration
                -> finish with line terminator
                */
                Write(";");
            }
        }
    }
    EndLn();

    if (ast->codeBlock)
    {
        /* Write function body */
        if (ast->flags(FunctionDecl::isEntryPoint))
        {
            OpenScope();
            {
                /* Write input parameters as local variables */
                WriteInputSemantics();

                /* Write code block (without additional scope) */
                isInsideEntryPoint_ = true;
                {
                    Visit(ast->codeBlock->stmnts);
                }
                isInsideEntryPoint_ = false;
            }
            CloseScope();
        }
        else
        {
            /* Write default code block */
            Visit(ast->codeBlock);
        }
    }

    Blank();
}

IMPLEMENT_VISIT_PROC(BufferDeclStmnt)
{
    if (!ast->flags(AST::isReachable))
        return;

    /* Write uniform buffer header */
    Line(ast);

    BeginLn();
    {
        Write("layout(std140");

        if (!ast->registerName.empty())
            Write(", binding = " + BRegister(ast->registerName, ast));

        Write(") uniform ");
        Write(ast->ident);
    }
    EndLn();

    OpenScope();
    {
        Visit(ast->members);
    }
    CloseScope(true);

    Blank();
}

IMPLEMENT_VISIT_PROC(TextureDeclStmnt)
{
    if (!ast->flags(AST::isReachable))
        return;

    /* Determine GLSL sampler type */
    auto samplerType = BufferTypeToGLSLKeyword(ast->textureType);
    if (!samplerType)
        Error("failed to map texture type to GLSL sampler type", ast);

    /* Write texture samplers */
    for (auto& texDecl : ast->textureDecls)
    {
        if (texDecl->flags(AST::isReachable))
        {
            BeginLn();
            {
                int binding = -1;

                /* Write uniform declaration */
                if (!texDecl->registerName.empty())
                {
                    binding = FromString<int>(TRegister(texDecl->registerName, texDecl.get()));
                    Write("layout(binding = " + TRegister(texDecl->registerName, texDecl.get()) + ") ");
                }

                Write("uniform " + *samplerType + " " + texDecl->ident + ";");

                /* Write output statistics */
                if (stats_)
                    stats_->textures.push_back({ texDecl->ident, binding });
            }
            EndLn();
        }
    }

    Blank();
}

IMPLEMENT_VISIT_PROC(StructDeclStmnt)
{
    if (!ast->structDecl->flags(AST::isReachable))
        return;

    Line(ast);

    bool semicolon = true;
    Visit(ast->structDecl, &semicolon);

    Blank();
}

IMPLEMENT_VISIT_PROC(VarDeclStmnt)
{
    auto varDecls = ast->varDecls;

    //TODO: refactor this!
    #if 1
    for (auto it = varDecls.begin(); it != varDecls.end();)
    {
        /*
        First check if code generation is disabled for variable declaration,
        then check if this is a system value semantic inside an interface block.
        */
        if ( (*it)->flags(VarDecl::disableCodeGen) ||
             ( isInsideInterfaceBlock_ && HasSystemValueSemantic((*it)->semantics) ) )
        {
            /*
            Code generation is disabled for this variable declaration
            -> Remove this from the list
            */
            it = varDecls.erase(it);
        }
        else
            ++it;
    }

    if (varDecls.empty())
    {
        /*
        All variable declarations within this statement are disabled
        -> Break code generation here
        */
        return;
    }
    #endif

    BeginLn();

    /* Write modifiers */
    if (ast->flags(VarDeclStmnt::isShaderInput))
        Write("in ");
    else if (ast->flags(VarDeclStmnt::isShaderOutput))
        Write("out ");

    for (auto storageClass : ast->storageModifiers)
    {
        auto keyword = StorageClassToGLSLKeyword(storageClass);
        if (keyword)
            Write(*keyword + " ");
        else
            Error("not all storage classes or interpolation modifiers can be mapped to GLSL keyword", ast);
    }

    for (const auto& modifier : ast->typeModifiers)
    {
        if (modifier == "const")
            Write(modifier + " ");
    }

    /* Write variable type */
    if (ast->varType->structDecl)
    {
        EndLn();
        Visit(ast->varType);
        BeginLn();
    }
    else
    {
        Visit(ast->varType);
        Write(" ");
    }

    /* Write variable declarations */
    for (size_t i = 0; i < varDecls.size(); ++i)
    {
        Visit(varDecls[i]);
        if (i + 1 < varDecls.size())
            Write(", ");
    }

    Write(";");
    EndLn();
}

IMPLEMENT_VISIT_PROC(AliasDeclStmnt)
{
    if (ast->structDecl && !ast->structDecl->IsAnonymous())
    {
        Line(ast);

        bool semicolon = true;
        Visit(ast->structDecl, &semicolon);

        Blank();
    }
}

/* --- Statements --- */

IMPLEMENT_VISIT_PROC(NullStmnt)
{
    WriteLn(";");
}

IMPLEMENT_VISIT_PROC(CodeBlockStmnt)
{
    Visit(ast->codeBlock);
}

IMPLEMENT_VISIT_PROC(ForLoopStmnt)
{
    /* Write loop header */
    BeginLn();
    {
        Write("for (");

        PushOptions({ false, false });
        {
            Visit(ast->initSmnt);
            Write(" "); // initStmnt already has the ';'!
            Visit(ast->condition);
            Write("; ");
            Visit(ast->iteration);
        }
        PopOptions();

        Write(")");
    }
    EndLn();

    VisitScopedStmnt(ast->bodyStmnt.get());
}

IMPLEMENT_VISIT_PROC(WhileLoopStmnt)
{
    /* Write loop condExpr */
    BeginLn();
    {
        Write("while (");
        Visit(ast->condition);
        Write(")");
    }
    EndLn();

    VisitScopedStmnt(ast->bodyStmnt.get());
}

IMPLEMENT_VISIT_PROC(DoWhileLoopStmnt)
{
    WriteLn("do");
    VisitScopedStmnt(ast->bodyStmnt.get());

    /* Write loop condExpr */
    BeginLn();
    {
        Write("while (");
        Visit(ast->condition);
        Write(");");
    }
    EndLn();
}

IMPLEMENT_VISIT_PROC(IfStmnt)
{
    bool hasElseParentNode = (args != nullptr ? *reinterpret_cast<bool*>(&args) : false);

    /* Write if condExpr */
    if (!hasElseParentNode)
        BeginLn();
    
    Write("if (");
    Visit(ast->condition);
    Write(")");
    
    EndLn();

    /* Write if body */
    VisitScopedStmnt(ast->bodyStmnt.get());

    Visit(ast->elseStmnt);
}

IMPLEMENT_VISIT_PROC(ElseStmnt)
{
    if (ast->bodyStmnt->Type() == AST::Types::IfStmnt)
    {
        /* Write else if statement */
        BeginLn();
        Write("else ");

        bool hasElseParentNode = true;
        Visit(ast->bodyStmnt, &hasElseParentNode);
    }
    else
    {
        /* Write else statement */
        WriteLn("else");
        VisitScopedStmnt(ast->bodyStmnt.get());
    }
}

IMPLEMENT_VISIT_PROC(SwitchStmnt)
{
    /* Write selector */
    BeginLn();
    {
        Write("switch (");
        Visit(ast->selector);
        Write(")");
    }
    EndLn();

    /* Write switch cases */
    OpenScope();
    {
        Visit(ast->cases);
    }
    CloseScope();
}

IMPLEMENT_VISIT_PROC(ExprStmnt)
{
    BeginLn();
    {
        Visit(ast->expr);
        Write(";");
    }
    EndLn();
}

IMPLEMENT_VISIT_PROC(ReturnStmnt)
{
    if (isInsideEntryPoint_)
    {
        if (ast->expr)
        {
            OpenScope();
            {
                WriteOutputSemantics(ast->expr.get());
                WriteLn("return;");
            }
            CloseScope();
        }
        else
            WriteLn("return;");
    }
    else
    {
        BeginLn();
        {
            Write("return");

            if (ast->expr)
            {
                Write(" ");
                Visit(ast->expr);
            }

            Write(";");
        }
        EndLn();
    }
}

IMPLEMENT_VISIT_PROC(CtrlTransferStmnt)
{
    WriteLn(CtrlTransformToString(ast->transfer) + ";");
}

/* --- Expressions --- */

IMPLEMENT_VISIT_PROC(ListExpr)
{
    Visit(ast->firstExpr);
    Write(", ");
    Visit(ast->nextExpr);
}

IMPLEMENT_VISIT_PROC(LiteralExpr)
{
    Write(ast->value);
}

IMPLEMENT_VISIT_PROC(TypeNameExpr)
{
    WriteTypeDenoter(*ast->typeDenoter, ast);
}

IMPLEMENT_VISIT_PROC(TernaryExpr)
{
    Visit(ast->condExpr);
    Write(" ? ");
    Visit(ast->thenExpr);
    Write(" : ");
    Visit(ast->elseExpr);
}

IMPLEMENT_VISIT_PROC(BinaryExpr)
{
    Visit(ast->lhsExpr);
    Write(" " + BinaryOpToString(ast->op) + " ");
    Visit(ast->rhsExpr);
}

IMPLEMENT_VISIT_PROC(UnaryExpr)
{
    Write(UnaryOpToString(ast->op));
    Visit(ast->expr);
}

IMPLEMENT_VISIT_PROC(PostUnaryExpr)
{
    Visit(ast->expr);
    Write(UnaryOpToString(ast->op));
}

IMPLEMENT_VISIT_PROC(FunctionCallExpr)
{
    Visit(ast->call);
}

IMPLEMENT_VISIT_PROC(BracketExpr)
{
    Write("(");
    Visit(ast->expr);
    Write(")");
}

IMPLEMENT_VISIT_PROC(SuffixExpr)
{
    Visit(ast->expr);
    Write(".");
    Visit(ast->varIdent);
}

IMPLEMENT_VISIT_PROC(ArrayAccessExpr)
{
    Visit(ast->expr);
    WriteArrayDims(ast->arrayIndices);
}

IMPLEMENT_VISIT_PROC(CastExpr)
{
    Visit(ast->typeExpr);
    Write("(");
    Visit(ast->expr);
    Write(")");
}

IMPLEMENT_VISIT_PROC(VarAccessExpr)
{
    //WriteVarIdentOrSystemValue(ast->varIdent.get());
    Visit(ast->varIdent);
    if (ast->assignExpr)
    {
        Write(" " + AssignOpToString(ast->assignOp) + " ");
        Visit(ast->assignExpr);
    }
}

IMPLEMENT_VISIT_PROC(InitializerExpr)
{
    Write("{ ");
        
    for (size_t i = 0; i < ast->exprs.size(); ++i)
    {
        Visit(ast->exprs[i]);
        if (i + 1 < ast->exprs.size())
            Write(", ");
    }

    Write(" }");
}

#undef IMPLEMENT_VISIT_PROC

/* --- Helper functions for code generation --- */

void GLSLGenerator::WriteAttribute(Attribute* ast)
{
    if (ast->ident == "numthreads")
        WriteAttributeNumThreads(ast);
    else if (ast->ident == "earlydepthstencil")
        WriteAttributeEarlyDepthStencil();
}

void GLSLGenerator::WriteAttributeNumThreads(Attribute* ast)
{
    if (ast->arguments.size() == 3)
    {
        BeginLn();
        {
            Write("layout(local_size_x = ");
            Visit(ast->arguments[0]);

            Write(", local_size_y = ");
            Visit(ast->arguments[1]);

            Write(", local_size_z = ");
            Visit(ast->arguments[2]);

            Write(") in;");
        }
        EndLn();
    }
    else
        ErrorInvalidNumArgs("\"numthreads\" attribute", ast);
}

void GLSLGenerator::WriteAttributeEarlyDepthStencil()
{
    WriteLn("layout(early_fragment_tests) in;");
}

void GLSLGenerator::WriteInputSemantics()
{
    auto& parameters = GetProgram()->inputSemantics.parameters;

    bool paramsWritten = false;

    for (auto& param : parameters)
    {
        if (WriteInputSemanticsParameter(param))
            paramsWritten = true;
    }

    if (paramsWritten)
        Blank();
}

//TODO: refactor this function:
bool GLSLGenerator::WriteInputSemanticsParameter(VarDeclStmnt* ast)
{
    /* Get variable declaration */
    if (ast->varDecls.size() != 1)
        Error("invalid number of variables inside parameter of entry point", ast);
    
    auto varDecl = ast->varDecls.front().get();
    auto typeDenoter = varDecl->GetTypeDenoter();

    /* Check if a structure input is used */
    auto typeRef = ast->varType->symbolRef;
    StructDecl* structDecl = nullptr;

    if (typeRef && typeRef->Type() == AST::Types::StructDecl)
        structDecl = dynamic_cast<StructDecl*>(typeRef);

    if (structDecl)
    {
        /* Write all members of input semantic structure (for system value semantics) */
        bool paramsWritten = false;

        for (auto& member : structDecl->members)
        {
            for (auto& memberVar : member->varDecls)
            {
                if (WriteInputSemanticsParameterVarDecl(memberVar.get()))
                    paramsWritten = true;
            }
        }

        return paramsWritten;
    }
    else if (varDecl->semantics.size() == 1)
    {
        /* Write single input semantic (for system value semantics) */
        return WriteInputSemanticsParameterVarDecl(varDecl);
    }

    return false;
}

bool GLSLGenerator::WriteInputSemanticsParameterVarDecl(VarDecl* varDecl)
{
    /* Map semantic to GL built-in constant */
    auto varSemantic = varDecl->semantics.front().get();
    auto semantic = varSemantic->semantic;
    if (IsSystemSemantic(semantic))
    {
        if (auto semanticKeyword = SemanticToGLSLKeyword(semantic))
        {
            /* Write local variable definition statement */
            BeginLn();
            {
                Visit(varDecl->declStmntRef->varType);
                Write(" " + varDecl->ident + " = " + *semanticKeyword + ";");
            }
            EndLn();
        }
        else
            Error("failed to map semantic name to GLSL keyword", varSemantic);

        return true;
    }
    return false;
}

void GLSLGenerator::WriteOutputSemantics(Expr* ast)
{
    auto& outp = GetProgram()->outputSemantics;

    if (!outp.singleOutputVariable.empty())
    {
        BeginLn();
        {
            Write(outp.singleOutputVariable + " = ");
            Visit(ast);
            Write(";");
        }
        EndLn();
    }
    else if (outp.returnType->symbolRef)
    {
        
        //TODO!
        
    }
}

void GLSLGenerator::WriteFragmentShaderOutput()
{
    auto& outp = GetProgram()->outputSemantics;

    if (outp.returnType->symbolRef || outp.returnType->structDecl)
    {
        /* Get structure AST node */
        StructDecl* structAST = nullptr;

        if (outp.returnType->symbolRef && outp.returnType->symbolRef->Type() == AST::Types::StructDecl)
            structAST = dynamic_cast<StructDecl*>(outp.returnType->symbolRef);
        else if (outp.returnType->structDecl)
            structAST = outp.returnType->structDecl.get();

        if (structAST)
        {
            for (const auto& member : structAST->members)
            {
                //TODO...
            }
        }
    }
    else
    {
        /* Write single output semantic declaration */
        if (auto semanticKeyword = SemanticToGLSLKeyword(outp.functionSemantic))
        {
            if (outp.functionSemantic == Semantic::Target)
            {
                //TODO: record semantic index
                int semanticIndex = 0;

                if (IsVersionOut(130))
                {
                    BeginLn();
                    {
                        Write("layout(location = " + std::to_string(semanticIndex) + ") out ");
                        Visit(outp.returnType);
                        Write(" " + *semanticKeyword + ";");
                    }
                    EndLn();
                    outp.singleOutputVariable = *semanticKeyword;
                }
                else
                    outp.singleOutputVariable = "gl_FragData[" + std::to_string(semanticIndex) + "]";
            }
            else if (outp.functionSemantic == Semantic::Depth)
                outp.singleOutputVariable = *semanticKeyword;
            else
                Error("invalid output semantic for fragment shader: \"" + *semanticKeyword + "\"");
        }
        else
            Error("failed to map fragment shader output semantic to GLSL keyword");
    }

    Blank();
}

void GLSLGenerator::VisitStructDeclMembers(StructDecl* ast)
{
    if (ast->baseStructRef)
        VisitStructDeclMembers(ast->baseStructRef);
    Visit(ast->members);
}

/*
Find the first VarIdent with a system value semantic,
and keep the remaining AST nodes (i.e. ast->next) which might be vector subscriptions (e.g. "gl_Position.xyz").
*/
VarIdent* GLSLGenerator::FindSystemValueVarIdent(VarIdent* ast)
{
    while (ast)
    {
        /* Check if current var-ident AST node has a system semantic */
        if (SemanticToGLSLKeyword(ast->systemSemantic) != nullptr)
            return ast;

        /* Search in next var-ident AST node */
        ast = ast->next.get();
    }
    return nullptr;
}

/*
Writes either the variable identifier as it is (e.g. "vertexOutput.position.xyz"),
or a system value if the identifier has a system value semantix (e.g. "gl_Position.xyz").
*/
void GLSLGenerator::WriteVarIdentOrSystemValue(VarIdent* ast)
{
    /* Find system value semantic in variable identifier */
    auto semanticVarIdent = FindSystemValueVarIdent(ast);
    const std::string* semanticKeyword = nullptr;

    if (semanticVarIdent)
        semanticKeyword = SemanticToGLSLKeyword(semanticVarIdent->systemSemantic);

    if (semanticVarIdent && semanticKeyword)
    {
        /* Write shader target respective system semantic */
        Write(*semanticKeyword);

        if (semanticVarIdent->next)
        {
            Write(".");
            Visit(semanticVarIdent->next);
        }
    }
    else
    {
        /* Write default variable identifier */
        Visit(ast);
    }
}

void GLSLGenerator::VisitParameter(VarDeclStmnt* ast)
{
    /* Write modifiers */
    if (!ast->inputModifier.empty())
        Write(ast->inputModifier + " ");

    for (const auto& modifier : ast->typeModifiers)
    {
        if (modifier == "const")
            Write(modifier + " ");
    }

    /* Write parameter type */
    Visit(ast->varType);
    Write(" ");

    /* Write parameter identifier */
    if (ast->varDecls.size() == 1)
        Visit(ast->varDecls.front());
    else
        Error("invalid number of variables in function parameter", ast);
}

void GLSLGenerator::VisitScopedStmnt(Stmnt* ast)
{
    if (ast)
    {
        if (ast->Type() != AST::Types::CodeBlockStmnt)
        {
            IncIndent();
            Visit(ast);
            DecIndent();
        }
        else
            Visit(ast);
    }
}

bool GLSLGenerator::HasSystemValueSemantic(const std::vector<VarSemanticPtr>& semantics) const
{
    for (const auto& varSem : semantics)
    {
        if (IsSystemSemantic(varSem->semantic))
            return true;
    }
    return false;
}

void GLSLGenerator::WriteArrayDims(const std::vector<ExprPtr>& arrayDims)
{
    for (auto& dim : arrayDims)
    {
        Write("[");
        Visit(dim);
        Write("]");
    }
}

void GLSLGenerator::WriteTypeDenoter(const TypeDenoter& typeDenoter, const AST* ast)
{
    if (typeDenoter.IsVoid())
    {
        /* Just write void type */
        Write("void");
    }
    else if (typeDenoter.IsBase())
    {
        /* Map GLSL base type */
        auto& baseTypeDen = static_cast<const BaseTypeDenoter&>(typeDenoter);
        if (auto keyword = DataTypeToGLSLKeyword(baseTypeDen.dataType))
            Write(*keyword);
        else
            Error("failed to map data type to GLSL keyword", ast);
    }
    else if (typeDenoter.IsTexture())
    {
        auto& textureTypeDen = static_cast<const TextureTypeDenoter&>(typeDenoter);
        if (auto texDecl = textureTypeDen.textureDeclRef)
        {
            if (auto keyword = BufferTypeToGLSLKeyword(texDecl->declStmntRef->textureType))
                Write(*keyword);
            else
                Error("failed to map texture type to GLSL keyword", ast);
        }
        else
            Error("missing reference to texture type denoter", ast);
    }
    else if (typeDenoter.IsStruct())
    {
        /* Write struct identifier */
        Write(typeDenoter.Ident());
    }
    else if (typeDenoter.IsAlias())
    {
        /* Resolve typename of aliased type */
        auto& aliasTypeDen = static_cast<const AliasTypeDenoter&>(typeDenoter);
        if (aliasTypeDen.aliasDeclRef)
            WriteTypeDenoter(*(aliasTypeDen.aliasDeclRef->typeDenoter), ast);
        else
            Error("missing reference to type alias '" + aliasTypeDen.ident + "'", ast);
    }
    else if (typeDenoter.IsArray())
    {
        /* Write array type denoter */
        auto& arrayTypeDen = static_cast<const ArrayTypeDenoter&>(typeDenoter);
        WriteTypeDenoter(*arrayTypeDen.baseTypeDenoter, ast);
        WriteArrayDims(arrayTypeDen.arrayDims);
    }
    else
        Error("failed to determine GLSL data type", ast);
}

void GLSLGenerator::WriteIdent(const std::string& ident)
{
    Write(ident);
}

void GLSLGenerator::AssertIntrinsicNumArgs(FunctionCall* ast, std::size_t numArgsMin, std::size_t numArgsMax)
{
    if (ast->arguments.size() < numArgsMin || ast->arguments.size() > numArgsMax)
        Error("invalid number of arguments in intrinsic", ast);
}

void GLSLGenerator::WriteFunctionCallStandard(FunctionCall* ast)
{
    /* Write function name */
    if (ast->varIdent)
    {
        if (ast->intrinsic != Intrinsic::Undefined)
        {
            /* Write GLSL intrinsic keyword */
            auto keyword = IntrinsicToGLSLKeyword(ast->intrinsic);
            if (keyword)
                Write(*keyword);
            else
                Error("failed to map intrinsic '" + ast->varIdent->ToString() + "' to GLSL keyword", ast);
        }
        else
        {
            /* Write function identifier */
            Visit(ast->varIdent);
        }
    }
    else if (ast->typeDenoter)
    {
        /* Write type denoter */
        WriteTypeDenoter(*ast->typeDenoter, ast);
    }
    else
        Error("missing function name", ast);

    /* Write arguments */
    Write("(");

    for (size_t i = 0; i < ast->arguments.size(); ++i)
    {
        Visit(ast->arguments[i]);
        if (i + 1 < ast->arguments.size())
            Write(", ");
    }

    Write(")");
}

void GLSLGenerator::WriteFunctionCallIntrinsicMul(FunctionCall* ast)
{
    AssertIntrinsicNumArgs(ast, 2, 2);

    auto WriteMulArgument = [&](const ExprPtr& expr)
    {
        /*
        Determine if the expression needs extra brackets when converted from a function call "mul(lhs, rhs)" to a binary expression "lhs * rhs",
        e.g. "mul(wMatrix, pos + float4(0, 1, 0, 0))" -> "wMatrix * (pos + float4(0, 1, 0, 0))" needs extra brackets
        */
        auto type = expr->Type();
        if (type == AST::Types::TernaryExpr || type == AST::Types::BinaryExpr || type == AST::Types::UnaryExpr || type == AST::Types::PostUnaryExpr)
        {
            Write("(");
            Visit(expr);
            Write(")");
        }
        else
            Visit(expr);
    };

    /* Convert this function call into a multiplication */
    Write("(");
    {
        WriteMulArgument(ast->arguments[0]);
        Write(" * ");
        WriteMulArgument(ast->arguments[1]);
    }
    Write(")");
}

void GLSLGenerator::WriteFunctionCallIntrinsicRcp(FunctionCall* ast)
{
    AssertIntrinsicNumArgs(ast, 1, 1);

    /* Get type denoter of argument expression */
    auto& expr = ast->arguments.front();
    auto typeDenoter = expr->GetTypeDenoter()->Get();

    if (typeDenoter->IsBase())
    {
        /* Convert this function call into a division */
        Write("(");
        {
            WriteTypeDenoter(*typeDenoter, ast);
            Write("(1) / (");
            Visit(expr);
        }
        Write("))");
    }
    else
        Error("invalid argument type for intrinsic 'rcp'", expr.get());
}

void GLSLGenerator::WriteFunctionCallIntrinsicAtomic(FunctionCall* ast)
{
    AssertIntrinsicNumArgs(ast, 2, 3);

    //TODO: move this to another visitor (e.g. "GLSLConverter" or the like) which does some transformation on the AST
    /* Find atomic intrinsic mapping */
    auto keyword = IntrinsicToGLSLKeyword(ast->intrinsic);
    if (keyword)
    {
        /* Write function call */
        if (ast->arguments.size() >= 3)
        {
            Visit(ast->arguments[2]);
            Write(" = ");
        }
        Write(*keyword + "(");
        Visit(ast->arguments[0]);
        Write(", ");
        Visit(ast->arguments[1]);
        Write(")");
    }
    else
        Error("failed to map intrinsic '" + ast->varIdent->ToString() + "' to GLSL keyword", ast);
}

void GLSLGenerator::WriteFunctionCallIntrinsicTex(FunctionCall* ast)
{
    if (!ast->varIdent->next)
        Error("missing member function in texture intrinsic", ast);

    /* Get function name */
    const auto& memberFuncName = ast->varIdent->next->ident;

    auto it = texFuncMap_.find(memberFuncName);
    if (it == texFuncMap_.end())
        Error("texture member function \"" + memberFuncName + "\" is not supported", ast);

    const auto& funcName = it->second;

    /* Write function call */
    Write(funcName + "(");
        
    for (size_t i = 0; i < ast->arguments.size(); ++i)
    {
        const auto& arg = ast->arguments[i];
            
        Visit(arg);
        if (i + 1 < ast->arguments.size())
            Write(", ");
    }

    Write(")");
}


} // /namespace Xsc



// ================================================================================
