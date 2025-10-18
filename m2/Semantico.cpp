#include "Semantico.h"
#include "Token.h"
#include "SemanticError.h"

#include <iostream>
#include <algorithm>
#include <string>

// Guarda o último identificador visto (serve para vetor [ ] e para LHS de =)
static std::string g_ultimoIdVisto;         // ex.: ao ler t_ID "v", guarda "v"
static std::string g_ultimoIdAntesDaAtrib;  // candidato a LHS de '='

static bool g_inParamList = false;
static bool g_nextBraceIsFuncBody = false;
static std::vector<Simbolo> g_paramBuffer;

std::ostream& operator<<(std::ostream& os, const Simbolo& s) {
    os << "Tipo: " << s.tipo << " - Nome: " << s.nome
       << " - Usado: " << (s.usado ? "Sim" : "Não")
       << " - Inicializado: " << (s.inicializado ? "Sim" : "Não");
    return os;
}

// ----------------- helpers -----------------

static void marcarUsadoPorNome(
    const std::string& nome,
    std::vector<std::vector<Simbolo>>& pilhaEscopos,
    std::vector<Simbolo>& tabelaSimbolo
    ) {
    if (nome.empty()) return;
    for (auto& escopo : pilhaEscopos) {
        for (auto& simbolo : escopo) {
            if (simbolo.nome == nome) {
                simbolo.usado = true;
                for (auto& s : tabelaSimbolo)
                    if (s.nome == nome && s.tipo == simbolo.tipo)
                        s.usado = true;
                return;
            }
        }
    }
}

static void marcarInicializadoPorNome(
    const std::string& nome,
    std::vector<std::vector<Simbolo>>& pilhaEscopos,
    std::vector<Simbolo>& tabelaSimbolo
    ) {
    if (nome.empty()) return;
    for (auto it = pilhaEscopos.rbegin(); it != pilhaEscopos.rend(); ++it) {
        for (auto& simbolo : *it) {
            if (simbolo.nome == nome) {
                simbolo.inicializado = true;
                for (auto& s : tabelaSimbolo)
                    if (s.nome == nome && s.tipo == simbolo.tipo)
                        s.inicializado = true;
                return;
            }
        }
    }
}

// ----------------- Semantico -----------------

void Semantico::declarar(const Token* tok) {
    const std::string nome = tok->getLexeme();
    if (nome.empty()) return;

    if (pilhaEscopos.empty())
        abrirEscopo();

    auto& escopoAtual = pilhaEscopos.back();

    if (std::any_of(escopoAtual.begin(), escopoAtual.end(),
                    [&](const Simbolo& s){ return s.nome == nome; })) {
        throw SemanticError(std::string("Símbolo '") + nome + "' já existe neste escopo",
                            tok->getPosition());
    }

    if (tipoAtual.empty()) {
        throw SemanticError(std::string("Declaração de '") + nome + "' sem tipo corrente",
                            tok->getPosition());
    }

    // Começa não inicializado; será marcado quando chegar '='
    escopoAtual.push_back(Simbolo{ tipoAtual, nome, false, false });
    tabelaSimbolo.push_back(Simbolo{ tipoAtual, nome, false, false });

    // Pode ser "int x = 5;" — guardamos o nome
    g_ultimoIdVisto        = nome;
    g_ultimoIdAntesDaAtrib = nome;
}

void Semantico::usar(const Token* tok) {
    const std::string nome = tok->getLexeme();
    if (nome.empty()) return;

    bool encontrado = false;
    for (auto& escopo : pilhaEscopos) {
        for (auto& simbolo : escopo) {
            if (simbolo.nome == nome) {
                if (!simbolo.inicializado) {
                    std::cerr << "Aviso: Símbolo '" << nome
                              << "' (tipo: " << simbolo.tipo
                              << ") usado sem inicialização na posição "
                              << tok->getPosition() << std::endl;
                }
                simbolo.usado = true;
                for (auto& s : tabelaSimbolo)
                    if (s.nome == nome && s.tipo == simbolo.tipo)
                        s.usado = true;
                encontrado = true;
                break;
            }
        }
        if (encontrado) break;
    }

    if (!encontrado) {
        throw SemanticError(std::string("Símbolo '") + nome + "' não declarado neste escopo",
                            tok->getPosition());
    }
}

void Semantico::fecharEscopo() {
    if (pilhaEscopos.empty()) return;

    const auto& escopoAtual = pilhaEscopos.back();
    for (const auto& simbolo : escopoAtual) {
        if (!simbolo.usado) {
            std::cerr << "Aviso: Símbolo '" << simbolo.nome
                      << "' (tipo: " << simbolo.tipo
                      << ") declarado mas não usado." << std::endl;
        }
    }

    pilhaEscopos.pop_back();
}

void Semantico::verificarNaoUsados() const {
    for (const auto& simbolo : tabelaSimbolo) {
        if (!simbolo.usado) {
            std::cerr << "Aviso: Símbolo '" << simbolo.nome
                      << "' (tipo: " << simbolo.tipo
                      << ") declarado mas não usado." << std::endl;
        }
    }
}

void Semantico::executeAction(int /*action*/, const Token* token) {
    if (!token) return;

    const int id = token->getId();

    switch (id) {
    // TIPOS: começar modo de declaração
    case t_KEY_INT:
    case t_KEY_FLOAT:
    case t_KEY_CHAR:
    case t_KEY_STRING:
    case t_KEY_BOOL:
    case t_KEY_DOUBLE:
    case t_KEY_LONG:
        beginDeclaracao(token->getLexeme());
        break;

    case t_KEY_VOID:
        break;

    // "(" e ")" (lista de parâmetros)
    case t_DELIM_PARENTESESE:
        if (modoDeclaracao) {
            g_inParamList = true;
            g_nextBraceIsFuncBody = false;
            g_paramBuffer.clear();
            lastDeclaredPos = -1;
        }
        break;

    case t_DELIM_PARENTESESD:
        if (g_inParamList) {
            g_inParamList = false;
            g_nextBraceIsFuncBody = true;
            endDeclaracao();
        } else {
            endDeclaracao();
        }
        break;

    // IDENTIFICADOR
    case t_ID:
        if (g_inParamList) {
            if (tipoAtual.empty())
                throw SemanticError("Parâmetro sem tipo declarado", token->getPosition());

            if (lastDeclaredPos != token->getPosition()) {
                const std::string nomeParam = token->getLexeme();

                bool dup = std::any_of(g_paramBuffer.begin(), g_paramBuffer.end(),
                                       [&](const Simbolo& s){ return s.nome == nomeParam; });
                if (dup)
                    throw SemanticError(std::string("Parâmetro '") + nomeParam + "' duplicado",
                                        token->getPosition());

                g_paramBuffer.push_back(Simbolo{ tipoAtual, nomeParam, false, false });
                tabelaSimbolo.push_back(Simbolo{ tipoAtual, nomeParam, false, false });
                lastDeclaredPos = token->getPosition();
            }
        }
        else if (modoDeclaracao) {
            if (lastDeclaredPos != token->getPosition()) {
                declarar(token);
                lastDeclaredPos = token->getPosition();
            }
        } else {
            usar(token);                               // uso do ID
            g_ultimoIdVisto        = token->getLexeme(); // para '[' e '='
            g_ultimoIdAntesDaAtrib = g_ultimoIdVisto;    // candidato a LHS
        }
        break;

    // VÍRGULA: permite novo ID em declarações/params
    case t_DELIM_VIRGULA:
        if (modoDeclaracao || g_inParamList)
            lastDeclaredPos = -1;
        break;

    // FIM DE DECLARAÇÃO
    case t_DELIM_PONTOVIRGULA:
        endDeclaracao();
        g_ultimoIdVisto.clear();
        g_ultimoIdAntesDaAtrib.clear();
        break;

    // ABRE ESCOPO
    case t_DELIM_CHAVEE:
        abrirEscopo();
        if (g_nextBraceIsFuncBody) {
            auto& escopoAtual = pilhaEscopos.back();
            for (const auto& p : g_paramBuffer) {
                bool dup = std::any_of(escopoAtual.begin(), escopoAtual.end(),
                                       [&](const Simbolo& s){ return s.nome == p.nome; });
                if (!dup)
                    escopoAtual.push_back(Simbolo{ p.tipo, p.nome, false, false });
            }
            g_paramBuffer.clear();
            g_nextBraceIsFuncBody = false;
        }
        break;

    // FECHA ESCOPO
    case t_DELIM_CHAVED:
        fecharEscopo();
        g_ultimoIdVisto.clear();
        g_ultimoIdAntesDaAtrib.clear();
        break;

    // '='  -> marca LHS como inicializado
    case t_OPR_ATRIB:
        marcarInicializadoPorNome(g_ultimoIdAntesDaAtrib, pilhaEscopos, tabelaSimbolo);
        break;

    // '['  -> uso de vetor: marca o último ID visto como "usado"
    case t_DELIM_COLCHETESE:   // definido no seu Constants.h
        marcarUsadoPorNome(g_ultimoIdVisto, pilhaEscopos, tabelaSimbolo);
        break;

    default:
        break;
    }
}
