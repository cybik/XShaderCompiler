/*
 * AST.h
 * 
 * This file is part of the XShaderCompiler project (Copyright (c) 2014-2017 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#ifndef XSC_AST_H
#define XSC_AST_H


#include <Xsc/Targets.h>
#include "Token.h"
#include "Visitor.h"
#include "Flags.h"
#include "ASTEnums.h"
#include "SourceCode.h"
#include "TypeDenoter.h"
#include "Identifier.h"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <functional>


namespace Xsc
{


/*
This file contains all node classes for the entire absract syntax tree (AST).
For simplicity only structs with public members are used here.
*/

class Visitor;

// Iteration callback for VarDecl AST nodes.
using VarDeclIteratorFunctor = std::function<void(VarDeclPtr& varDecl)>;

// Iteration callback for Expr AST nodes.
using ExprIteratorFunctor = std::function<void(ExprPtr& expr)>;

// Iteration callback for argument/parameter-type associations.
using ArgumentParameterTypeFunctor = std::function<void(ExprPtr& argument, const TypeDenoter& paramTypeDen)>;

/* --- Some helper macros --- */

#define AST_INTERFACE(CLASS_NAME)                               \
    static const Types classType = Types::CLASS_NAME;           \
    CLASS_NAME(const SourcePosition& astPos)                    \
    {                                                           \
        area = SourceArea(astPos, 1);                           \
    }                                                           \
    CLASS_NAME(const SourceArea& astArea)                       \
    {                                                           \
        area = astArea;                                         \
    }                                                           \
    Types Type() const override                                 \
    {                                                           \
        return Types::CLASS_NAME;                               \
    }                                                           \
    void Visit(Visitor* visitor, void* args = nullptr) override \
    {                                                           \
        visitor->Visit##CLASS_NAME(this, args);                 \
    }

#define DECL_AST_ALIAS(ALIAS, BASE) \
    using ALIAS         = BASE;     \
    using ALIAS##Ptr    = BASE##Ptr

#define FLAG_ENUM \
    enum : unsigned int

#define FLAG(IDENT, INDEX) \
    IDENT = (1u << (INDEX))

// Base class for all AST node classes.
struct AST
{
    enum class Types
    {
        Program,
        CodeBlock,
        FunctionCall,
        Attribute,
        SwitchCase,
        SamplerValue,
        Register,
        PackOffset,
        ArrayDimension,
        TypeSpecifier,
        VarIdent,

        VarDecl,
        BufferDecl,
        SamplerDecl,
        StructDecl,
        AliasDecl,

        FunctionDecl,       // Do not use "Stmnt" postfix here (There are no declaration sub-nodes)
        UniformBufferDecl,  // Do not use "Stmnt" postfix here (There are no declaration sub-nodes)
        VarDeclStmnt,
        BufferDeclStmnt,
        SamplerDeclStmnt,
        StructDeclStmnt,
        AliasDeclStmnt,     // Type alias (typedef)

        NullStmnt,
        CodeBlockStmnt,
        ForLoopStmnt,
        WhileLoopStmnt,
        DoWhileLoopStmnt,
        IfStmnt,
        ElseStmnt,
        SwitchStmnt,
        ExprStmnt,
        ReturnStmnt,
        CtrlTransferStmnt,

        NullExpr,
        ListExpr,
        LiteralExpr,
        TypeSpecifierExpr,
        TernaryExpr,
        BinaryExpr,
        UnaryExpr,
        PostUnaryExpr,
        FunctionCallExpr,
        BracketExpr,
        SuffixExpr,
        ArrayAccessExpr,
        CastExpr,
        VarAccessExpr,
        InitializerExpr,
    };

    virtual ~AST();
    
    // Returns the AST node type.
    virtual Types Type() const = 0;

    // Calls the respective visit-function of the specified visitor.
    virtual void Visit(Visitor* visitor, void* args = nullptr) = 0;

    #ifdef XSC_ENABLE_MEMORY_POOL

    void* operator new (std::size_t count);
    void operator delete (void* ptr);

    #endif

    FLAG_ENUM
    {
        FLAG( isReachable, 30 ), // This AST node is reachable from the main entry point.
        FLAG( isDeadCode,  29 ), // This AST node is dead code (after return path).
        FLAG( isBuildIn,   28 ), // This AST node is a build-in node (not part of the actual program source).
    };

    // Returns this AST node as the specified sub class if this AST node has the correct type. Otherwise, null is returned.
    template <typename T>
    T* As()
    {
        return (Type() == T::classType ? static_cast<T*>(this) : nullptr);
    }

    // Returns this AST node as the specified sub class if this AST node has the correct type. Otherwise, null is returned.
    template <typename T>
    const T* As() const
    {
        return (Type() == T::classType ? static_cast<const T*>(this) : nullptr);
    }

    SourceArea  area;
    Flags       flags;
};

/* --- Base AST nodes --- */

// Statement AST base class.
struct Stmnt : public AST
{
    // Collects all variable-, buffer-, and sampler AST nodes with their identifiers in the specified map.
    virtual void CollectDeclIdents(std::map<const AST*, std::string>& declASTIdents) const;

    std::string                 comment; // Optional commentary for this statement.
    std::vector<AttributePtr>   attribs; // Attribute list. May be empty.
};

// AST base class with type denoter.
struct TypedAST : public AST
{

    public:

        // Returns a type denoter for AST or throws an std::runtime_error if a type denoter can not be derived.
        const TypeDenoterPtr& GetTypeDenoter();

        // Resets the buffered type denoter.
        void ResetTypeDenoter();

    protected:

        virtual TypeDenoterPtr DeriveTypeDenoter() = 0;

    private:
    
        TypeDenoterPtr bufferedTypeDenoter_;

};

// Expression AST base class.
struct Expr : public TypedAST
{
    FLAG_ENUM
    {
        FLAG( wasConverted, 0 ), // This expression has already been converted.
    };

    // Returns the variable or null if this is not just a single variable expression.
    VarDecl* FetchVarDecl() const;

    // Returns the variable identifier or null if this is not just a single variable expression
    virtual VarIdent* FetchVarIdent() const;
};

// Declaration AST base class.
struct Decl : public TypedAST
{
    // Returns a descriptive string of the type signature.
    virtual std::string ToString() const;

    // Identifier of the declaration object (may be empty, e.g. for anonymous structures).
    Identifier ident;
};

// Program AST root.
struct Program : public AST
{   
    AST_INTERFACE(Program);

    // Layout meta data for tessellation-control shaders
    struct LayoutTessControlShader
    {
        unsigned int    outputControlPoints     = 0;
        float           maxTessFactor           = 0.0f;
        FunctionDecl*   patchConstFunctionRef   = nullptr;
    };

    // Layout meta data for tessellation-evaluation shaders
    struct LayoutTessEvaluationShader
    {
        AttributeValue  domainType      = AttributeValue::Undefined;
        AttributeValue  partitioning    = AttributeValue::Undefined;
        AttributeValue  outputTopology  = AttributeValue::Undefined;
    };

    // Layout meta data for fragment shaders
    struct LayoutGeometryShader
    {
        PrimitiveType   inputPrimitive  = PrimitiveType::Undefined;
        BufferType      outputPrimitive = BufferType::Undefined;        // Must be PointStream, LineStream, or TriangleStream
        unsigned int    maxVertices     = 0;
    };

    // Layout meta data for fragment shaders
    struct LayoutFragmentShader
    {
        // True, if fragment coordinate (SV_Position) is used inside a fragment shader.
        bool fragCoordUsed      = false;
        
        // True, if pixel center is assumed to be integral, otherwise pixel center is assumed to have an (0.5, 0.5) offset.
        bool pixelCenterInteger = false;

        // True, if the [earlydepthstencil] attribute is specified for the fragment shader entry point.
        bool earlyDepthStencil  = false;
    };

    // Layout meta data for compute shaders
    struct LayoutComputeShader
    {
        unsigned int numThreads[3] = { 0 };
    };

    // Registers a usage of an intrinsic with the specified argument data types (only base types).
    void RegisterIntrinsicUsage(const Intrinsic intrinsic, const std::vector<DataType>& argumentDataTypes);

    // Registers a usage of an intrinsic with the specified arguments (only base types).
    void RegisterIntrinsicUsage(const Intrinsic intrinsic, const std::vector<ExprPtr>& arguments);

    // Returns a usage-container of the specified intrinsic or null if the specified intrinsic was not registered to be used.
    const IntrinsicUsage* FetchIntrinsicUsage(const Intrinsic intrinsic) const;

    std::vector<StmntPtr>               globalStmnts;               // Global declaration statements

    std::vector<ASTPtr>                 disabledAST;                // AST nodes that have been disabled for code generation (not part of the default visitor).

    SourceCodePtr                       sourceCode;                 // Preprocessed source code
    FunctionDecl*                       entryPointRef   = nullptr;  // Reference to the entry point function declaration.
    std::map<Intrinsic, IntrinsicUsage> usedIntrinsics;             // Set of all used intrinsic (filled by the reference analyzer).

    LayoutTessControlShader             layoutTessControl;          // Global program layout attributes for a tessellation-control shader.
    LayoutTessEvaluationShader          layoutTessEvaluation;       // Global program layout attributes for a tessellation-evaluation shader.
    LayoutGeometryShader                layoutGeometry;             // Global program layout attributes for a geometry shader.
    LayoutFragmentShader                layoutFragment;             // Global program layout attributes for a fragment shader.
    LayoutComputeShader                 layoutCompute;              // Global program layout attributes for a compute shader.
};

// Code block.
struct CodeBlock : public AST
{
    AST_INTERFACE(CodeBlock);

    std::vector<StmntPtr> stmnts;
};

// Sampler state value assignment.
// see https://msdn.microsoft.com/de-de/library/windows/desktop/bb509644(v=vs.85).aspx
struct SamplerValue : public AST
{
    AST_INTERFACE(SamplerValue);

    std::string name;   // Sampler state name
    ExprPtr     value;  // Sampler state value expression
};

//TODO: maybe merge this AST class into "FunctionCallExpr".
// Function call.
struct FunctionCall : public TypedAST
{
    AST_INTERFACE(FunctionCall);

    FLAG_ENUM
    {
        // If this function call is an intrinsic, it's wrapper function can be inlined (i.e. no wrapper function must be generated)
        // e.g. "clip(a), clip(b);" can not be inlined, due to the list expression.
        FLAG( canInlineIntrinsicWrapper, 0 ),
    };

    TypeDenoterPtr DeriveTypeDenoter() override;

    // Returns a list of all argument expressions (including the default parameters).
    std::vector<Expr*> GetArguments() const;

    // Returns the function implementation of this function call, or null if not set.
    FunctionDecl* GetFunctionImpl() const;

    // Iterates over each argument expression that is assigned to an output parameter.
    void ForEachOutputArgument(const ExprIteratorFunctor& iterator);

    // Iterates over each argument expression together with its associated parameter.
    void ForEachArgumentWithParameterType(const ArgumentParameterTypeFunctor& iterator);

    VarIdentPtr             varIdent;                                   // Null, if the function call is a type constructor (e.g. "float2(0, 0)").
    TypeDenoterPtr          typeDenoter;                                // Null, if the function call is NOT a type constructor (e.g. "float2(0, 0)").
    std::vector<ExprPtr>    arguments;

    FunctionDecl*           funcDeclRef         = nullptr;              // Reference to the function declaration; may be null
    Intrinsic               intrinsic           = Intrinsic::Undefined; // Intrinsic ID (if this is an intrinsic).
    std::vector<Expr*>      defaultArgumentRefs;                        // Reference to default argument expressions of all remaining parameters
};

// Attribute (e.g. "[unroll]" or "[numthreads(x,y,z)]").
struct Attribute : public AST
{
    AST_INTERFACE(Attribute);

    AttributeType           attributeType   = AttributeType::Undefined;
    std::vector<ExprPtr>    arguments;
};

// Case block for a switch statement.
struct SwitchCase : public AST
{
    AST_INTERFACE(SwitchCase);

    // Returns true, if this is a default case (if 'expr' is null).
    bool IsDefaultCase() const;

    ExprPtr                 expr; // If null -> default case
    std::vector<StmntPtr>   stmnts;
};

// Register (e.g. ": register(t0)").
struct Register : public AST
{
    AST_INTERFACE(Register);

    std::string ToString() const;

    // Returns the first slot register for the specified shader target or null, if there is no register.
    static Register* GetForTarget(const std::vector<RegisterPtr>& registers, const ShaderTarget shaderTarget);

    ShaderTarget    shaderTarget    = ShaderTarget::Undefined;  // Shader target (or profile). Undefined means all targets are affected.
    RegisterType    registerType    = RegisterType::Undefined;
    int             slot            = 0;                        // Zero-based register slot index. By default 0.
};

// Pack offset.
struct PackOffset : public AST
{
    AST_INTERFACE(PackOffset);

    std::string ToString() const;

    std::string registerName;
    std::string vectorComponent; // May be empty
};

// Array dimension with bufferd expression evaluation.
struct ArrayDimension : public TypedAST
{
    AST_INTERFACE(ArrayDimension);

    std::string ToString() const;

    TypeDenoterPtr DeriveTypeDenoter() override;

    // Returns true if this array dimension has a dynamic size (i.e. size == 0).
    bool HasDynamicSize() const;

    #if 0 //UNUSED
    // Returns the array dimension sizes as integral vector.
    static std::vector<int> GetArrayDimensionSizes(const std::vector<ArrayDimensionPtr>& arrayDims);
    #endif

    ExprPtr expr;           // Array dimension expression. Must be a constant integer expression.

    int     size    = 0;    // Evaluated array dimension size. Zero for dynamic array dimension.
};

// Type specifier with optional structure declaration.
struct TypeSpecifier : public TypedAST
{
    AST_INTERFACE(TypeSpecifier);
    
    // Returns the name of this type: typeDenoter->ToString().
    std::string ToString() const;

    TypeDenoterPtr DeriveTypeDenoter() override;

    // Returns the StructDecl reference of this type denoter or null if there is no such reference.
    StructDecl* GetStructDeclRef();

    // Returns true if this is an input parameter.
    bool IsInput() const;

    // Returns true if this is an output parameter.
    bool IsOutput() const;

    // Returns true if the 'const' type modifier is set.
    bool IsConst() const;

    // Returns true if the 'const' type modifier or the 'uniform' input modifier is set.
    bool IsConstOrUniform() const;

    // Inserts the specified type modifier. Overlapping matrix packings will be removed.
    void SetTypeModifier(const TypeModifier modifier);

    // Returns true if any of the specified type modifiers is contained.
    bool HasAnyTypeModifierOf(const std::vector<TypeModifier>& modifiers) const;

    // Returns true if any of the specified storage classes is contained.
    bool HasAnyStorageClassesOf(const std::vector<StorageClass>& modifiers) const;

    bool                        isInput         = false;                    // Input modifier 'in'
    bool                        isOutput        = false;                    // Input modifier 'out'
    bool                        isUniform       = false;                    // Input modifier 'uniform'
    
    std::set<StorageClass>      storageClasses;                             // Storage classes, e.g. extern, precise, etc.
    std::set<InterpModifier>    interpModifiers;                            // Interpolation modifiers, e.g. nointerpolation, linear, centroid etc.
    std::set<TypeModifier>      typeModifiers;                              // Type modifiers, e.g. const, row_major, column_major (also 'snorm' and 'unorm' for floats)
    PrimitiveType               primitiveType   = PrimitiveType::Undefined; // Primitive type for geometry entry pointer parameters
    StructDeclPtr               structDecl;                                 // Optional structure declaration

    TypeDenoterPtr              typeDenoter;
};

// Variable (linked-list) identifier.
struct VarIdent : public TypedAST
{
    AST_INTERFACE(VarIdent);

    FLAG_ENUM
    {
        FLAG( isImmutable, 0 ), // This variable identifier must be written out as it is.
    };

    // Returns the full var-ident string (with '.' separation).
    std::string ToString() const;

    // Returns the last identifier AST node.
    VarIdent* Last();

    // Returns a type denoter for the symbol reference of the last variable identifier.
    TypeDenoterPtr DeriveTypeDenoter() override;

    // Returns the type denoter for this AST node or the last sub node.
    TypeDenoterPtr GetExplicitTypeDenoter(bool recursive);

    // Returns a type denoter for the vector subscript of this identifier or throws a runtime error on failure.
    BaseTypeDenoterPtr GetTypeDenoterFromSubscript(TypeDenoter& baseTypeDenoter) const;

    // Moves the next identifier into this one (i.e. removes the first identifier), and propagates the array indices.
    void PopFront(bool accumulateArrayIndices = true);

    // Returns a semantic if this is an identifier to a variable which has a semantic.
    IndexedSemantic FetchSemantic() const;

    // Returns the specified type of AST node from the symbol (if the symbol refers to one).
    template <typename T>
    T* FetchSymbol() const
    {
        if (symbolRef)
        {
            if (auto ast = symbolRef->As<T>())
                return ast;
        }
        return nullptr;
    }

    // Returns the declaration AST node (if the symbol refers to one).
    Decl* FetchDecl() const;

    // Returns the variable AST node (if the symbol refers to one).
    VarDecl* FetchVarDecl() const;

    // Returns the function declaration AST node (if the symbol refers to one).
    FunctionDecl* FetchFunctionDecl() const;

    std::string             ident;                      // Atomic identifier.
    std::vector<ExprPtr>    arrayIndices;               // Optional array indices
    bool                    nextIsStatic    = false;    // Specifies whether the next node is concatenated with the static double-colon token '::'.
    VarIdentPtr             next;                       // Next identifier; may be null.

    AST*                    symbolRef       = nullptr;  // Symbol reference for DAST to the variable object; may be null (e.g. for vector subscripts)
};

/* --- Declarations --- */

// Variable declaration.
struct VarDecl : public Decl
{
    AST_INTERFACE(VarDecl);

    FLAG_ENUM
    {
        FLAG( isShaderInput,        0 ), // This variable is used as shader input.
        FLAG( isShaderOutput,       1 ), // This variable is used as shader output.
        FLAG( isSystemValue,        2 ), // This variable is a system value (e.g. SV_Position/ gl_Position).
        FLAG( isDynamicArray,       3 ), // This variable is a dynamic array (for input/output semantics).
        FLAG( isWrittenTo,          4 ), // This variable is eventually written to.
        FLAG( isEntryPointOutput,   5 ), // This variable is used as entry point output (return value, output parameter, stream output).
        FLAG( isEntryPointLocal,    6 ), // This variable is a local variable of the entry point.

        isShaderInputSV     = (isShaderInput  | isSystemValue), // This variable is used as shader input, and it is a system value.
        isShaderOutputSV    = (isShaderOutput | isSystemValue), // This variable is used as shader output, and it is a system value.
    };

    // Returns the variable declaration as string.
    std::string ToString() const override;

    // Returns a type denoter for this variable declaration or throws an std::runtime_error if the type can not be derived.
    TypeDenoterPtr DeriveTypeDenoter() override;

    std::vector<ArrayDimensionPtr>  arrayDims;
    IndexedSemantic                 semantic;
    PackOffsetPtr                   packOffset;
    std::vector<VarDeclStmntPtr>    annotations;                // Annotations can be ignored by analyzers and generators.
    ExprPtr                         initializer;

    VarDeclStmnt*                   declStmntRef    = nullptr;  // Reference to its declaration statement (parent node); may be null
    UniformBufferDecl*              bufferDeclRef   = nullptr;  // Uniform buffer declaration reference for DAST (optional parent-parent-node); may be null
    StructDecl*                     structDeclRef   = nullptr;  // Structure declaration reference for DAST (optional parent-parent-node); may be null
};

// Buffer declaration.
struct BufferDecl : public Decl
{
    AST_INTERFACE(BufferDecl);

    TypeDenoterPtr DeriveTypeDenoter() override;

    // Returns the buffer type of the parent's node type denoter.
    BufferType GetBufferType() const;

    std::vector<ArrayDimensionPtr>  arrayDims;
    std::vector<RegisterPtr>        slotRegisters;

    BufferDeclStmnt*                declStmntRef    = nullptr;  // Reference to its declaration statement (parent node).
};

// Sampler state declaration.
struct SamplerDecl : public Decl
{
    AST_INTERFACE(SamplerDecl);

    TypeDenoterPtr DeriveTypeDenoter() override;

    // Returns the sampler type of the parent's node type denoter.
    SamplerType GetSamplerType() const;

    std::vector<ArrayDimensionPtr>  arrayDims;
    std::vector<RegisterPtr>        slotRegisters;
    std::string                     textureIdent;               // Optional variable identifier of the texture object (for DX9 effect files)
    std::vector<SamplerValuePtr>    samplerValues;              // State values for a sampler decl-ident.

    SamplerDeclStmnt*               declStmntRef    = nullptr;  // Reference to its declaration statmenet (parent node).
};

// StructDecl object.
struct StructDecl : public Decl
{
    AST_INTERFACE(StructDecl);

    FLAG_ENUM
    {
        FLAG( isShaderInput,        0 ), // This structure is used as shader input.
        FLAG( isShaderOutput,       1 ), // This structure is used as shader output.
        FLAG( isNestedStruct,       2 ), // This is a nested structure within another structure.
        FLAG( isNonEntryPointParam, 3 ), // This structure is eventually used as variable or parameter type of function other than the entry point.
    };

    // Returns a descriptive string of the structure signature (e.g. "struct s" or "struct <anonymous>").
    std::string ToString() const override;

    // Returns true if this is an anonymous structure.
    bool IsAnonymous() const;

    //TODO: rename to "FetchVarDecl"
    // Returns the VarDecl AST node inside this struct decl for the specified identifier, or null if there is no such VarDecl.
    VarDecl* Fetch(const std::string& ident, const StructDecl** owner = nullptr) const;

    // Returns the FunctionDecl AST node for the specified argument type denoter list (used to derive the overloaded function).
    FunctionDecl* FetchFunctionDecl(const std::string& ident, const std::vector<TypeDenoterPtr>& argTypeDenoters, const StructDecl** owner = nullptr) const;

    // Returns an identifier that is similar to the specified identifier (for suggestions of typos)
    std::string FetchSimilar(const std::string& ident);

    // Returns a type denoter for this structure.
    TypeDenoterPtr DeriveTypeDenoter() override;

    // Returns true if this structure has at least one member that is not a system value.
    bool HasNonSystemValueMembers() const;

    // Returns the total number of members (include all base structures).
    std::size_t NumVarMembers() const;

    // Returns a list with the type denoters of all members (including all base structures).
    void CollectMemberTypeDenoters(std::vector<TypeDenoterPtr>& memberTypeDens) const;

    // Iterates over each VarDecl AST node (included nested structures, and members in referenced structures).
    void ForEachVarDecl(const VarDeclIteratorFunctor& iterator);

    // Returns true if this structure is used more than once as entry point output (either through variable arrays or multiple variable declarations).
    bool HasMultipleShaderOutputInstances() const;

    // Returns true if this structure is a base of the specified sub structure.
    bool IsBaseOf(const StructDecl& subStructDecl) const;

    std::string                     baseStructName;                     // May be empty (if no inheritance is used).
    std::vector<StmntPtr>           localStmnts;                        // Local declaration statements

    std::vector<VarDeclStmntPtr>    varMembers;                         // List of all member variable declaration statements.
    std::vector<FunctionDeclPtr>    funcMembers;                        // List of all member function declarations.

    StructDeclStmnt*                declStmntRef            = nullptr;  // Reference to its declaration statement (parent node).
    StructDecl*                     baseStructRef           = nullptr;  // Optional reference to base struct
    std::string                     aliasName;                          // Alias name for input and output interface blocks of the DAST.
    std::map<std::string, VarDecl*> systemValuesRef;                    // List of members with system value semantic (SV_...).
    std::vector<StructDecl*>        nestedStructDeclRefs;               // References to all nested structures within this structure.
    std::set<VarDecl*>              shaderOutputVarDeclRefs;            // References to all variables from this structure that are used as entry point outputs.
};

// Type alias declaration.
struct AliasDecl : public Decl
{
    AST_INTERFACE(AliasDecl);

    TypeDenoterPtr DeriveTypeDenoter() override;

    TypeDenoterPtr  typeDenoter;            // Type denoter of the aliased type

    AliasDeclStmnt* declStmntRef = nullptr; // Reference to its declaration statement (parent node).
};

/* --- Declaration statements --- */

// Function declaration.
struct FunctionDecl : public Stmnt
{
    AST_INTERFACE(FunctionDecl);

    struct ParameterSemantics
    {
        using IteratorFunc = std::function<void(VarDecl* varDecl)>;

        void Add(VarDecl* varDecl);
        bool Contains(VarDecl* varDecl) const;
        void ForEach(const IteratorFunc& iterator);

        // Returns true if both lists are empty.
        bool Empty() const;

        // Updates the distribution of system-value and non-system-value semantics.
        void UpdateDistribution();

        std::vector<VarDecl*> varDeclRefs;      // References to all variable declarations of the user defined semantics
        std::vector<VarDecl*> varDeclRefsSV;    // References to all variable declarations of the system value semantics
    };

    struct ParameterStructure
    {
        VarIdent*   varIdent;   // Either this is used ...
        VarDecl*    varDecl;    // ... or this
        StructDecl* structDecl;
    };

    FLAG_ENUM
    {
        FLAG( isEntryPoint,            0 ), // This function is the main entry point.
        FLAG( isSecondaryEntryPoint,   1 ), // This function is a secondary entry point (e.g. patch constant function).
        FLAG( hasNonReturnControlPath, 2 ), // At least one control path does not return a value.
    };

    // Returns true if this function declaration is just a forward declaration (without function body).
    bool IsForwardDecl() const;

    // Returns true if this function has a void return type.
    bool HasVoidReturnType() const;

    // Returns true if this is a member function (member of a structure).
    bool IsMemberFunction() const;
    
    // Returns a descriptive string of the function signature (e.g. "void f(int x)").
    std::string ToString(bool useParamNames = true) const;

    // Returns true if the specified function declaration has the same signature as this function.
    bool EqualsSignature(const FunctionDecl& rhs) const;

    // Returns the minimal number of arguments for a call to this function.
    std::size_t NumMinArgs() const;

    // Returns the maximal number of arguments for a call to this function (this is merely: parameters.size()).
    std::size_t NumMaxArgs() const;

    // Sets the specified function AST node as the implementation of this forward declaration.
    void SetFuncImplRef(FunctionDecl* funcDecl);

    // Returns true if the specified type denoter matches the parameter.
    bool MatchParameterWithTypeDenoter(std::size_t paramIndex, const TypeDenoter& argType, bool implicitConversion) const;

    // Fetches the function declaration from the list that matches the specified argument types.
    static FunctionDecl* FetchFunctionDeclFromList(
        const std::vector<FunctionDecl*>& funcDeclList,
        const std::string& ident, const std::vector<TypeDenoterPtr>& argTypeDenoters,
        bool throwErrorIfNoMatch = true
    );

    TypeSpecifierPtr                returnType;
    Identifier                      ident;
    std::vector<VarDeclStmntPtr>    parameters;
    IndexedSemantic                 semantic            = Semantic::Undefined;  // May be undefined
    std::vector<VarDeclStmntPtr>    annotations;                                // Annotations can be ignored by analyzers and generators.
    CodeBlockPtr                    codeBlock;                                  // May be null (if this AST node is a forward declaration).

    ParameterSemantics              inputSemantics;                             // Entry point input semantics.
    ParameterSemantics              outputSemantics;                            // Entry point output semantics.

    FunctionDecl*                   funcImplRef         = nullptr;              // Reference to the function implementation (only for forward declarations).
    std::vector<FunctionDecl*>      funcForwardDeclRefs;                        // Reference to all forward declarations (only for implementations).
    StructDecl*                     structDeclRef       = nullptr;              // Structure declaration reference if this is a member function; may be null

    std::vector<ParameterStructure> paramStructs;                               // Parameter with structure type (only for entry point).
};

// Uniform buffer (cbuffer, tbuffer) declaration.
struct UniformBufferDecl : public Stmnt
{
    AST_INTERFACE(UniformBufferDecl);

    std::string ToString() const;

    UniformBufferType               bufferType      = UniformBufferType::Undefined;
    std::string                     ident;
    std::vector<RegisterPtr>        slotRegisters;
    std::vector<StmntPtr>           localStmnts;                                    // Local declaration statements

    std::vector<VarDeclStmntPtr>    varMembers;                                     // List of all member variable declaration statements.
};

// Buffer (and texture) declaration.
struct BufferDeclStmnt : public Stmnt
{
    AST_INTERFACE(BufferDeclStmnt);

    // Implements Stmnt::CollectDeclIdents
    void CollectDeclIdents(std::map<const AST*, std::string>& declASTIdents) const override;

    BufferTypeDenoterPtr        typeDenoter;
    std::vector<BufferDeclPtr>  bufferDecls;
};

// Sampler declaration.
struct SamplerDeclStmnt : public Stmnt
{
    AST_INTERFACE(SamplerDeclStmnt);

    // Implements Stmnt::CollectDeclIdents
    void CollectDeclIdents(std::map<const AST*, std::string>& declASTIdents) const override;

    SamplerTypeDenoterPtr       typeDenoter;
    std::vector<SamplerDeclPtr> samplerDecls;
};

// StructDecl declaration statement.
struct StructDeclStmnt : public Stmnt
{
    AST_INTERFACE(StructDeclStmnt);
    StructDeclPtr structDecl;
};

// Variable declaration statement.
struct VarDeclStmnt : public Stmnt
{
    AST_INTERFACE(VarDeclStmnt);

    FLAG_ENUM
    {
        FLAG( isShaderInput,    0 ), // This variable is used as shader input.
        FLAG( isShaderOutput,   1 ), // This variable is used as shader output.
        FLAG( isParameter,      2 ), // This variable is a function parameter (flag should be set during parsing).
        FLAG( isImplicitConst,  3 ), // This variable is implicitly declared as constant.
    };

    // Implements Stmnt::CollectDeclIdents
    void CollectDeclIdents(std::map<const AST*, std::string>& declASTIdents) const override;

    // Returns the var-decl statement as string.
    std::string ToString(bool useVarNames = true, bool isParam = false) const;
    
    // Returns the VarDecl AST node inside this var-decl statement for the specified identifier, or null if there is no such VarDecl.
    VarDecl* Fetch(const std::string& ident) const;

    // Returns true if this is an input parameter.
    bool IsInput() const;

    // Returns true if this is an output parameter.
    bool IsOutput() const;

    // Returns true if this is a uniform.
    bool IsUniform() const;

    // Returns true if the 'const' type modifier or the 'uniform' input modifier is set.
    bool IsConstOrUniform() const;

    // Inserts the specified type modifier. Overlapping matrix packings will be removed.
    void SetTypeModifier(const TypeModifier modifier);

    // Returns true if any of the specified type modifiers is contained.
    bool HasAnyTypeModifierOf(const std::vector<TypeModifier>& modifiers) const;

    // Iterates over each VarDecl AST node.
    void ForEachVarDecl(const VarDeclIteratorFunctor& iterator);

    // Makes this var-decl statement implicitly constant, iff not explicitly declared as constant (see 'isUniform' and 'isImplicitlyConst').
    void MakeImplicitConst();

    TypeSpecifierPtr        typeSpecifier;
    std::vector<VarDeclPtr> varDecls;
};

// Type alias declaration statement.
struct AliasDeclStmnt : public Stmnt
{
    AST_INTERFACE(AliasDeclStmnt);

    StructDeclPtr               structDecl; // Optional structure declaration
    std::vector<AliasDeclPtr>   aliasDecls; // Type aliases
};

/* --- Statements --- */

// Null statement.
struct NullStmnt : public Stmnt
{
    AST_INTERFACE(NullStmnt);
};

// Code block statement.
struct CodeBlockStmnt : public Stmnt
{
    AST_INTERFACE(CodeBlockStmnt);

    CodeBlockPtr codeBlock;
};

// 'for'-loop statemnet.
struct ForLoopStmnt : public Stmnt
{
    AST_INTERFACE(ForLoopStmnt);

    StmntPtr    initStmnt;
    ExprPtr     condition;
    ExprPtr     iteration;
    StmntPtr    bodyStmnt;
};

// 'while'-loop statement.
struct WhileLoopStmnt : public Stmnt
{
    AST_INTERFACE(WhileLoopStmnt);

    ExprPtr     condition;
    StmntPtr    bodyStmnt;
};

// 'do/while'-loop statement.
struct DoWhileLoopStmnt : public Stmnt
{
    AST_INTERFACE(DoWhileLoopStmnt);

    StmntPtr    bodyStmnt;
    ExprPtr     condition;
};

// 'if' statement.
struct IfStmnt : public Stmnt
{
    AST_INTERFACE(IfStmnt);

    ExprPtr         condition;
    StmntPtr        bodyStmnt;
    ElseStmntPtr    elseStmnt;  // May be null
};

// 'else' statement.
struct ElseStmnt : public Stmnt
{
    AST_INTERFACE(ElseStmnt);

    StmntPtr bodyStmnt;
};

// 'switch' statement.
struct SwitchStmnt : public Stmnt
{
    AST_INTERFACE(SwitchStmnt);

    ExprPtr                     selector;
    std::vector<SwitchCasePtr>  cases;
};

// Arbitrary expression statement.
struct ExprStmnt : public Stmnt
{
    AST_INTERFACE(ExprStmnt);

    ExprPtr expr;
};

// Returns statement.
struct ReturnStmnt : public Stmnt
{
    AST_INTERFACE(ReturnStmnt);

    FLAG_ENUM
    {
        FLAG( isEndOfFunction, 0 ), // This return statement is at the end of its function body.
    };

    ExprPtr expr; // May be null
};

// Control transfer statement.
struct CtrlTransferStmnt : public Stmnt
{
    AST_INTERFACE(CtrlTransferStmnt);

    CtrlTransfer transfer = CtrlTransfer::Undefined; // break, continue, discard
};

/* --- Expressions --- */

// Null expression (used for dynamic array dimensions).
struct NullExpr : public Expr
{
    AST_INTERFACE(NullExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;
};

// List expression ( expr ',' expr ).
struct ListExpr : public Expr
{
    AST_INTERFACE(ListExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    ExprPtr firstExpr;
    ExprPtr nextExpr;
};

// Literal expression.
struct LiteralExpr : public Expr
{
    AST_INTERFACE(LiteralExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    // Converts the data type of this literal expr, resets the buffered type denoter (see ResetTypeDenoter), and modifies the value string.
    void ConvertDataType(const DataType type);

    // Returns the value of this literal if it is a string literal (excluding the quotation marks). Otherwise an empty string is returned.
    std::string GetStringValue() const;

    // Returns true if this is a NULL literal.
    bool IsNull() const;

    DataType        dataType    = DataType::Undefined;  // Valid data types: String, Bool, Int, UInt, Half, Float, Double; (Undefined for 'NULL')
    std::string     value;
};

// Type name expression (used for simpler cast-expression parsing).
struct TypeSpecifierExpr : public Expr
{
    AST_INTERFACE(TypeSpecifierExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    TypeSpecifierPtr typeSpecifier;
};

// Ternary expression.
struct TernaryExpr : public Expr
{
    AST_INTERFACE(TernaryExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    // Returns true if the conditional expression is a vector type.
    bool IsVectorCondition() const;

    ExprPtr condExpr; // Condition expression
    ExprPtr thenExpr; // <then> case expression
    ExprPtr elseExpr; // <else> case expression
};

// Binary expressions.
struct BinaryExpr : public Expr
{
    AST_INTERFACE(BinaryExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    ExprPtr     lhsExpr;                        // Left-hand-side expression
    BinaryOp    op      = BinaryOp::Undefined;  // Binary operator
    ExprPtr     rhsExpr;                        // Right-hand-side expression
};

// (Pre-) Unary expressions.
struct UnaryExpr : public Expr
{
    AST_INTERFACE(UnaryExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    UnaryOp op      = UnaryOp::Undefined;
    ExprPtr expr;
};

// Post unary expressions (e.g. x++, x--)
struct PostUnaryExpr : public Expr
{
    AST_INTERFACE(PostUnaryExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    ExprPtr expr;
    UnaryOp op      = UnaryOp::Undefined;
};

// Function call expression (e.g. "foo()" or "foo().bar()" or "foo()[0].bar()").
struct FunctionCallExpr : public Expr
{
    AST_INTERFACE(FunctionCallExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    //TODO: add "prefixExpr"
    //ExprPtr         prefixExpr;   // Optional (left hand side) sub expression; may be null
    FunctionCallPtr call;
};

// Bracket expression.
struct BracketExpr : public Expr
{
    AST_INTERFACE(BracketExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    VarIdent* FetchVarIdent() const override;

    ExprPtr expr; // Inner expression
};

//TODO: maybe replace this by "VarAccessExpr"
// Suffix expression (e.g. "foo().suffix").
struct SuffixExpr : public Expr
{
    AST_INTERFACE(SuffixExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    ExprPtr     expr;       // Sub expression (left hand side)
    VarIdentPtr varIdent;   // Suffix var identifier (right hand side)
};

// Array-access expression (e.g. "foo()[arrayAccess]").
struct ArrayAccessExpr : public Expr
{
    AST_INTERFACE(ArrayAccessExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    //TODO: rename this "prefixExpr" to make the post-order traversal clear
    ExprPtr                 expr;           // Sub expression (left hand side)
    std::vector<ExprPtr>    arrayIndices;   // Array indices (right hand side)
};

// Cast expression.
struct CastExpr : public Expr
{
    AST_INTERFACE(CastExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    TypeSpecifierPtr    typeSpecifier;  // Cast type name expression
    ExprPtr             expr;           // Value expression
};

// Variable access expression.
struct VarAccessExpr : public Expr
{
    AST_INTERFACE(VarAccessExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    VarIdent* FetchVarIdent() const override;

    //TODO: add "prefixExpr" and make this a replacement to "SuffixExpr"
    //ExprPtr   prefixExpr;                         // Optional sub expression (left hand side); may be null
    VarIdentPtr varIdent;
    AssignOp    assignOp    = AssignOp::Undefined;  // May be undefined
    ExprPtr     assignExpr;                         // May be null
};

// Initializer list expression.
struct InitializerExpr : public Expr
{
    AST_INTERFACE(InitializerExpr);

    TypeDenoterPtr DeriveTypeDenoter() override;

    // Returns the number of scalar elements (with recursion).
    unsigned int NumElements() const;

    // Fetches the sub expression with the specified array indices and throws an ASTRuntimeError on failure.
    ExprPtr FetchSubExpr(const std::vector<int>& arrayIndices) const;

    // Returns the next array indices for a sub expression.
    bool NextArrayIndices(std::vector<int>& arrayIndices) const;

    std::vector<ExprPtr> exprs;
};

#undef AST_INTERFACE
#undef DECL_AST_ALIAS
#undef FLAG_ENUM
#undef FLAG


} // /namespace Xsc


#endif



// ================================================================================
