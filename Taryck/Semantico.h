#ifndef SEMANTICO_H
#define SEMANTICO_H
#include "Token.h"
#include "SemanticError.h"
#include <vector>
#include <string>
#include <ostream>
#include <algorithm>
#include <functional>   // +++ ADICIONE

class Simbolo {
public:
    std::string tipo;
    std::string nome;
    bool usado = false;
    bool inicializado = false;
    std::string modalidade;   // "variavel", "vetor", "parametro", "funcao"
    std::string escopo;       // "global" ou nome_da_funcao

    friend std::ostream& operator<<(std::ostream& os, const Simbolo& s);
};

class Semantico {
private:
    bool        modoDeclaracao = false;
    std::string tipoAtual;
    int         lastDeclaredPos = -1;

    std::string ultimoDeclaradoNome;

    std::vector<std::vector<Simbolo>> pilhaEscopos;
    std::vector<std::string> pilhaFuncoes;
    std::vector<bool>        pilhaEscopoEhFuncao;

    bool inInitList      = false;
    int  initListDepth   = 0;
    bool pendingInitList = false;

    bool existeNoEscopoAtual(const std::string& nome) const;
    bool existe(const std::string& nome) const;

    void declarar(const Token* tok);
    void usar(const Token* tok);

    mutable std::function<void(const std::string&)> logger_;   // << mutable
    mutable std::vector<std::string> mensagens_;               // << mutable

    // helper de aviso agora é const
    void warn(const std::string& msg) const;

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
        inInitList = false; initListDepth = 0; pendingInitList = false;
    }

public:
    std::vector<Simbolo> tabelaSimbolo;

    void executeAction(int action, const Token* token);
    void abrirEscopo() { pilhaEscopos.push_back({}); }
    void fecharEscopo();
    void verificarNaoUsados() const;

    // +++ NOVO: API do logger
    void setLogger(std::function<void(const std::string&)> fn) { logger_ = std::move(fn); }
    void clearMensagens() { mensagens_.clear(); }
    const std::vector<std::string>& mensagens() const { return mensagens_; }
};

#endif
