#ifndef SEMANTICO_H
#define SEMANTICO_H
#include "Token.h"
#include "SemanticError.h"
#include <vector>
#include <string>
#include <ostream>
#include <algorithm>
#include <functional>

class Simbolo {
public:
    std::string tipo;
    std::string nome;
    bool usado = false;
    bool inicializado = false;
    std::string modalidade;   // "variavel", "parametro", "funcao", "vetor", etc.
    bool isVetor = false;
    int  vetorTam = 0;
    std::string escopo;       // "global" ou nome_da_funcao

    friend std::ostream& operator<<(std::ostream& os, const Simbolo& s);
};

class Semantico {
private:
    // ===== Helpers de busca/escopo =====
    bool existeNoEscopoAtual(const std::string& nome) const;
    bool existe(const std::string& nome) const;
    std::string escopoAtual() const;

    // >>> NOVO: impede sombreamento dentro da MESMA FUNÇÃO <<<
    bool existeNoEscopoDaFuncaoAtual(const std::string& nome) const;

    // ===== Estado do analisador =====
    bool        modoDeclaracao = false;
    std::string tipoAtual;
    int         lastDeclaredPos = -1;
    std::string ultimoDeclaradoNome;

    // pilhas de escopos/blocos e funções
    std::vector<std::vector<Simbolo>> pilhaEscopos;
    std::vector<std::string>          pilhaFuncoes;
    std::vector<bool>                 pilhaEscopoEhFuncao;

    // tabela linear opcional (histórico/relatório)
    std::vector<Simbolo>              tabelaLinear;

    // controle de listas de inicialização
    bool inInitList      = false;
    int  initListDepth   = 0;
    bool pendingInitList = false;

    // ===== logging/mensagens =====
    void warn(const std::string& msg) const;
    void info(const std::string& msg) const;
    void error(const std::string& msg) const;
    void addMsg(const std::string& msg) const;

    // ===== declar/acabamento de declaração =====
    void endDeclaracao();
    void beginDeclaracao(const std::string& tipo);

    // usado no case 10/colchetes: promove último declarado a "vetor"
    void marcarUltimoDeclaradoComoVetor(const std::string& nome);

public:
    // tabela “global” que você já usa
    std::vector<Simbolo> tabelaSimbolo;

    // API principal
    void executeAction(int action, const Token* token);
    void abrirEscopo() { pilhaEscopos.push_back({}); }
    void fecharEscopo();
    void verificarNaoUsados() const;

    // operações principais
    void declarar(const Token* tok);
    void usar(const Token* tok);

    // logging/mensagens
    void setLogger(std::function<void(const std::string&)> fn) { logger_ = std::move(fn); }
    void clearMensagens() { mensagens_.clear(); }
    const std::vector<std::string>& mensagens() const { return mensagens_; }

private:
    mutable std::function<void(const std::string&)> logger_;
    mutable std::vector<std::string> mensagens_;
};

#endif
