//
// Copyright (c) 2011 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/preprocessor/MacroExpander.h"

#include <algorithm>

#include "common/debug.h"
#include "compiler/preprocessor/DiagnosticsBase.h"
#include "compiler/preprocessor/Token.h"

namespace pp
{

namespace
{

const size_t kMaxContextTokens = 10000;

class TokenLexer : public Lexer
{
  public:
    typedef std::vector<Token> TokenVector;

    TokenLexer(TokenVector *tokens)
    {
        tokens->swap(mTokens);
        mIter = mTokens.begin();
    }

    void lex(Token *token) override
    {
        if (mIter == mTokens.end())
        {
            token->reset();
            token->type = Token::LAST;
        }
        else
        {
            *token = *mIter++;
        }
    }

  private:
    TokenVector mTokens;
    TokenVector::const_iterator mIter;
};

}  // anonymous namespace

class MacroExpander::ScopedMacroReenabler final : angle::NonCopyable
{
  public:
    ScopedMacroReenabler(MacroExpander *expander);
    ~ScopedMacroReenabler();

  private:
    MacroExpander *mExpander;
};

MacroExpander::ScopedMacroReenabler::ScopedMacroReenabler(MacroExpander *expander)
    : mExpander(expander)
{
    mExpander->mDeferReenablingMacros = true;
}

MacroExpander::ScopedMacroReenabler::~ScopedMacroReenabler()
{
    mExpander->mDeferReenablingMacros = false;
    for (const auto& macro : mExpander->mMacrosToReenable)
    {
        // Copying the string here by using substr is a check for use-after-free. It detects
        // use-after-free more reliably than just toggling the disabled flag.
        ASSERT(macro->name.substr() != "");
        macro->disabled = false;
    }
    mExpander->mMacrosToReenable.clear();
}

MacroExpander::MacroExpander(Lexer *lexer,
                             MacroSet *macroSet,
                             Diagnostics *diagnostics,
                             int allowedMacroExpansionDepth)
    : mLexer(lexer),
      mMacroSet(macroSet),
      mDiagnostics(diagnostics),
      mTotalTokensInContexts(0),
      mAllowedMacroExpansionDepth(allowedMacroExpansionDepth),
      mDeferReenablingMacros(false)
{
}

MacroExpander::~MacroExpander()
{
    ASSERT(mMacrosToReenable.empty());
    for (MacroContext *context : mContextStack)
    {
        delete context;
    }
}

void MacroExpander::lex(Token *token)
{
    while (true)
    {
        getToken(token);

        if (token->type != Token::IDENTIFIER)
            break;

        if (token->expansionDisabled())
            break;

        MacroSet::const_iterator iter = mMacroSet->find(token->text);
        if (iter == mMacroSet->end())
            break;

        std::shared_ptr<Macro> macro = iter->second;
        if (macro->disabled)
        {
            // If a particular token is not expanded, it is never expanded.
            token->setExpansionDisabled(true);
            break;
        }

        // Bump the expansion count before peeking if the next token is a '('
        // otherwise there could be a #undef of the macro before the next token.
        macro->expansionCount++;
        if ((macro->type == Macro::kTypeFunc) && !isNextTokenLeftParen())
        {
            // If the token immediately after the macro name is not a '(',
            // this macro should not be expanded.
            macro->expansionCount--;
            break;
        }

        pushMacro(macro, *token);
    }
}

void MacroExpander::getToken(Token *token)
{
    if (mReserveToken.get())
    {
        *token = *mReserveToken;
        mReserveToken.reset();
        return;
    }

    // First pop all empty macro contexts.
    while (!mContextStack.empty() && mContextStack.back()->empty())
    {
        popMacro();
    }

    if (!mContextStack.empty())
    {
        *token = mContextStack.back()->get();
    }
    else
    {
        ASSERT(mTotalTokensInContexts == 0);
        mLexer->lex(token);
    }
}

void MacroExpander::ungetToken(const Token &token)
{
    if (!mContextStack.empty())
    {
        MacroContext *context = mContextStack.back();
        context->unget();
        ASSERT(context->replacements[context->index] == token);
    }
    else
    {
        ASSERT(!mReserveToken.get());
        mReserveToken.reset(new Token(token));
    }
}

bool MacroExpander::isNextTokenLeftParen()
{
    Token token;
    getToken(&token);

    bool lparen = token.type == '(';
    ungetToken(token);

    return lparen;
}

bool MacroExpander::pushMacro(std::shared_ptr<Macro> macro, const Token &identifier)
{
    ASSERT(!macro->disabled);
    ASSERT(!identifier.expansionDisabled());
    ASSERT(identifier.type == Token::IDENTIFIER);
    ASSERT(identifier.text == macro->name);

    std::vector<Token> replacements;
    if (!expandMacro(*macro, identifier, &replacements))
        return false;

    // Macro is disabled for expansion until it is popped off the stack.
    macro->disabled = true;

    MacroContext *context = new MacroContext;
    context->macro        = macro;
    context->replacements.swap(replacements);
    mContextStack.push_back(context);
    mTotalTokensInContexts += context->replacements.size();
    return true;
}

void MacroExpander::popMacro()
{
    ASSERT(!mContextStack.empty());

    MacroContext *context = mContextStack.back();
    mContextStack.pop_back();

    ASSERT(context->empty());
    ASSERT(context->macro->disabled);
    ASSERT(context->macro->expansionCount > 0);
    if (mDeferReenablingMacros)
    {
        mMacrosToReenable.push_back(context->macro);
    }
    else
    {
        context->macro->disabled = false;
    }
    context->macro->expansionCount--;
    mTotalTokensInContexts -= context->replacements.size();
    delete context;
}

bool MacroExpander::expandMacro(const Macro &macro,
                                const Token &identifier,
                                std::vector<Token> *replacements)
{
    replacements->clear();

    // In the case of an object-like macro, the replacement list gets its location
    // from the identifier, but in the case of a function-like macro, the replacement
    // list gets its location from the closing parenthesis of the macro invocation.
    // This is tested by dEQP-GLES3.functional.shaders.preprocessor.predefined_macros.*
    SourceLocation replacementLocation = identifier.location;
    if (macro.type == Macro::kTypeObj)
    {
        replacements->assign(macro.replacements.begin(), macro.replacements.end());

        if (macro.predefined)
        {
            const char kLine[] = "__LINE__";
            const char kFile[] = "__FILE__";

            ASSERT(replacements->size() == 1);
            Token &repl = replacements->front();
            if (macro.name == kLine)
            {
                repl.text = ToString(identifier.location.line);
            }
            else if (macro.name == kFile)
            {
                repl.text = ToString(identifier.location.file);
            }
        }
    }
    else
    {
        ASSERT(macro.type == Macro::kTypeFunc);
        std::vector<MacroArg> args;
        args.reserve(macro.parameters.size());
        if (!collectMacroArgs(macro, identifier, &args, &replacementLocation))
            return false;

        replaceMacroParams(macro, args, replacements);
    }

    for (std::size_t i = 0; i < replacements->size(); ++i)
    {
        Token &repl = replacements->at(i);
        if (i == 0)
        {
            // The first token in the replacement list inherits the padding
            // properties of the identifier token.
            repl.setAtStartOfLine(identifier.atStartOfLine());
            repl.setHasLeadingSpace(identifier.hasLeadingSpace());
        }
        repl.location = replacementLocation;
    }
    return true;
}

bool MacroExpander::collectMacroArgs(const Macro &macro,
                                     const Token &identifier,
                                     std::vector<MacroArg> *args,
                                     SourceLocation *closingParenthesisLocation)
{
    Token token;
    getToken(&token);
    ASSERT(token.type == '(');

    args->push_back(MacroArg());

    // Defer reenabling macros until args collection is finished to avoid the possibility of
    // infinite recursion. Otherwise infinite recursion might happen when expanding the args after
    // macros have been popped from the context stack when parsing the args.
    ScopedMacroReenabler deferReenablingMacros(this);

    int openParens = 1;
    while (openParens != 0)
    {
        getToken(&token);

        if (token.type == Token::LAST)
        {
            mDiagnostics->report(Diagnostics::PP_MACRO_UNTERMINATED_INVOCATION, identifier.location,
                                 identifier.text);
            // Do not lose EOF token.
            ungetToken(token);
            return false;
        }

        bool isArg = false;  // True if token is part of the current argument.
        switch (token.type)
        {
            case '(':
                ++openParens;
                isArg = true;
                break;
            case ')':
                --openParens;
                isArg                       = openParens != 0;
                *closingParenthesisLocation = token.location;
                break;
            case ',':
                // The individual arguments are separated by comma tokens, but
                // the comma tokens between matching inner parentheses do not
                // seperate arguments.
                if (openParens == 1)
                    args->push_back(MacroArg());
                isArg = openParens != 1;
                break;
            default:
                isArg = true;
                break;
        }
        if (isArg)
        {
            MacroArg &arg = args->back();
            // Initial whitespace is not part of the argument.
            if (arg.empty())
                token.setHasLeadingSpace(false);
            arg.push_back(token);
        }
    }

    const Macro::Parameters &params = macro.parameters;
    // If there is only one empty argument, it is equivalent to no argument.
    if (params.empty() && (args->size() == 1) && args->front().empty())
    {
        args->clear();
    }
    // Validate the number of arguments.
    if (args->size() != params.size())
    {
        Diagnostics::ID id = args->size() < macro.parameters.size()
                                 ? Diagnostics::PP_MACRO_TOO_FEW_ARGS
                                 : Diagnostics::PP_MACRO_TOO_MANY_ARGS;
        mDiagnostics->report(id, identifier.location, identifier.text);
        return false;
    }

    // Pre-expand each argument before substitution.
    // This step expands each argument individually before they are
    // inserted into the macro body.
    size_t numTokens = 0;
    for (auto &arg : *args)
    {
        TokenLexer lexer(&arg);
        if (mAllowedMacroExpansionDepth < 1)
        {
            mDiagnostics->report(Diagnostics::PP_MACRO_INVOCATION_CHAIN_TOO_DEEP, token.location,
                                 token.text);
            return false;
        }
        MacroExpander expander(&lexer, mMacroSet, mDiagnostics, mAllowedMacroExpansionDepth - 1);

        arg.clear();
        expander.lex(&token);
        while (token.type != Token::LAST)
        {
            arg.push_back(token);
            expander.lex(&token);
            numTokens++;
            if (numTokens + mTotalTokensInContexts > kMaxContextTokens)
            {
                mDiagnostics->report(Diagnostics::PP_OUT_OF_MEMORY, token.location, token.text);
                return false;
            }
        }
    }
    return true;
}

void MacroExpander::replaceMacroParams(const Macro &macro,
                                       const std::vector<MacroArg> &args,
                                       std::vector<Token> *replacements)
{
    for (std::size_t i = 0; i < macro.replacements.size(); ++i)
    {
        if (!replacements->empty() &&
            replacements->size() + mTotalTokensInContexts > kMaxContextTokens)
        {
            const Token &token = replacements->back();
            mDiagnostics->report(Diagnostics::PP_OUT_OF_MEMORY, token.location, token.text);
            return;
        }

        const Token &repl = macro.replacements[i];
        if (repl.type != Token::IDENTIFIER)
        {
            replacements->push_back(repl);
            continue;
        }

        // TODO(alokp): Optimize this.
        // There is no need to search for macro params every time.
        // The param index can be cached with the replacement token.
        Macro::Parameters::const_iterator iter =
            std::find(macro.parameters.begin(), macro.parameters.end(), repl.text);
        if (iter == macro.parameters.end())
        {
            replacements->push_back(repl);
            continue;
        }

        std::size_t iArg    = std::distance(macro.parameters.begin(), iter);
        const MacroArg &arg = args[iArg];
        if (arg.empty())
        {
            continue;
        }
        std::size_t iRepl = replacements->size();
        replacements->insert(replacements->end(), arg.begin(), arg.end());
        // The replacement token inherits padding properties from
        // macro replacement token.
        replacements->at(iRepl).setHasLeadingSpace(repl.hasLeadingSpace());
    }
}

MacroExpander::MacroContext::MacroContext() : macro(0), index(0)
{
}

MacroExpander::MacroContext::~MacroContext()
{
}

bool MacroExpander::MacroContext::empty() const
{
    return index == replacements.size();
}

const Token &MacroExpander::MacroContext::get()
{
    return replacements[index++];
}

void MacroExpander::MacroContext::unget()
{
    ASSERT(index > 0);
    --index;
}

}  // namespace pp
