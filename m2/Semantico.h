#ifndef SEMANTICO_H
#define SEMANTICO_H

#include "Token.h"
#include "SemanticError.h"

#include <vector>
#include <string>
#include <ostream>
#include <algorithm>

class Simbolo {
public:
    std::string tipo;
    std::string nome;
    bool usado = false;
    bool inicializado = false; // Novo campo para rastrear inicialização

    friend std::ostream& operator<<(std::ostream& os, const Simbolo& s);
};

class Semantico {
private:
    bool        modoDeclaracao = false;
    std::string tipoAtual;
    int         lastDeclaredPos = -1;
    bool        esperandoAtribuicao = false; // Novo: indica que estamos após '=' em uma declaração

    std::vector<std::vector<Simbolo>> pilhaEscopos;

    bool existe(const std::string& nome) const {
        for (auto it = pilhaEscopos.rbegin(); it != pilhaEscopos.rend(); ++it) {
            const auto& escopo = *it;
            auto found = std::find_if(escopo.begin(), escopo.end(),
                                      [&](const Simbolo& s){ return s.nome == nome; });
            if (found != escopo.end())
                return true;
        }
        return false;
    }

    void declarar(const Token* tok);
    void usar(const Token* tok);
    void beginDeclaracao(const std::string& tipo) {
        modoDeclaracao = true;
        tipoAtual = tipo;
        lastDeclaredPos = -1;
    }
    void endDeclaracao() {
        modoDeclaracao = false;
        tipoAtual.clear();
        lastDeclaredPos = -1;
        esperandoAtribuicao = false; // Reseta após fim da declaração
    }

public:
    std::vector<Simbolo> tabelaSimbolo;

    void executeAction(int action, const Token* token);
    void abrirEscopo() { pilhaEscopos.push_back({}); }
    void fecharEscopo();
    void verificarNaoUsados() const;
};

#endif
