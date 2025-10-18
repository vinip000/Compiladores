// Semantico.h
#ifndef SEMANTICO_H
#define SEMANTICO_H
#include "Token.h"
#include "SemanticError.h"
#include <vector>
#include <string>
#include <ostream>
#include <algorithm>
#include <optional>

class Simbolo {
public:
    std::string tipo;
    std::string nome;
    bool usado = false;
    bool inicializado = false;

    // existente no seu projeto
    std::string modalidade;   // "variavel", "vetor", "parametro", "funcao"
    std::string escopo;       // "global" ou nome_da_funcao

    friend std::ostream& operator<<(std::ostream& os, const Simbolo& s);
};

// === NOVO: sistema de tipos interno ===
enum class TipoPrim {
    TP_INT, TP_FLOAT, TP_DOUBLE, TP_LONG,
    TP_CHAR, TP_BOOL, TP_STRING,
    TP_INVALID
};

class Semantico {
private:
    bool        modoDeclaracao = false;
    std::string tipoAtual;
    int         lastDeclaredPos = -1;
    std::string ultimoDeclaradoNome;

    // Escopos aninhados (primeiro é o global)
    std::vector<std::vector<Simbolo>> pilhaEscopos;

    // Funções/escopos (já existia na sua base)
    std::vector<std::string> pilhaFuncoes;
    std::vector<bool> pilhaEscopoEhFuncao;

    // === NOVO: pilha de tipos de expressão e controle de atribuição ===
    std::vector<TipoPrim> pilhaTiposExp;
    bool atribPendente = false;
    TipoPrim tipoLHS   = TipoPrim::TP_INVALID;
    std::string nomeLHS;

    // auxiliares que já existiam
    bool existeNoEscopoAtual(const std::string& nome) const;
    bool existe(const std::string& nome) const;

    void declarar(const Token* tok);
    void usar(const Token* tok);

    std::string escopoAtual() const {
        return pilhaFuncoes.empty() ? "global" : pilhaFuncoes.back();
    }

    void marcarUltimoDeclaradoComoVetor(const std::string& nome);

    void beginDeclaracao(const std::string& tipo) {
        modoDeclaracao = true; tipoAtual = tipo; lastDeclaredPos = -1;
    }
    void endDeclaracao() {
        modoDeclaracao = false;
        tipoAtual.clear();
        lastDeclaredPos = -1;
        ultimoDeclaradoNome.clear();
    }

    // === NOVO: mapeamento e regras de tipo ===
    static TipoPrim mapTipoString(const std::string& tlex);
    static std::string toString(TipoPrim t);
    static TipoPrim promoverBinario(TipoPrim a, TipoPrim b, const std::string& op, int pos); // resulta no tipo do expr
    static bool atribuivel(TipoPrim dest, TipoPrim src);

    // busca tipo de um identificador no escopo (para uso em expressões)
    std::optional<TipoPrim> tipoDeIdent(const std::string& nome) const;

    // valida a atribuição pendente ao fim do RHS
    void checarAtribuicaoFinal(int pos);

public:
    std::vector<Simbolo> tabelaSimbolo; // para exibição

    void executeAction(int action, const Token* token);
    void abrirEscopo() { pilhaEscopos.push_back({}); }
    void fecharEscopo();
    void verificarNaoUsados() const;

    // === NOVO: utilitários expostos pra gramática (# ações) ===
    void pushTipoLiteral(TipoPrim t) { pilhaTiposExp.push_back(t); }
    void pushTipoIdent(const std::string& nome, int pos);
    void binop(const std::string& op, int pos);   // consome 2, empilha 1
    void unop(const std::string& op, int pos);    // consome 1, empilha 1 (ex.: !, -)
    void indexacao(int pos);                      // v[expr] => expr deve ser int
    void chamadaFuncTermina(int /*pos*/);         // se quiser propagar tipo de retorno no futuro
};

#endif
