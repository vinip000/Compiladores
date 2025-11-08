#include "Semantico.h"
#include "Token.h"
#include "SemanticError.h"

#include <iostream>
#include <algorithm>
#include <string>

static std::string g_ultimoIdVisto;
static std::string g_ultimoIdAntesDaAtrib;
static bool        g_inParamList = false;
static bool        g_nextBraceIsFuncBody = false;
static std::string g_funcEmConstrucao;
static std::vector<Simbolo> g_paramBuffer;

// --------- utilitários ---------
std::ostream& operator<<(std::ostream& os, const Simbolo& s) {
    os << "Tipo: " << s.tipo
       << " - Nome: " << s.nome
       << " - Modalidade: " << s.modalidade
       << " - Escopo: " << s.escopo
       << " - Usado: " << (s.usado ? "Sim" : "Não")
       << " - Inicializado: " << (s.inicializado ? "Sim" : "Não");
    return os;
}

// Promove o último ID declarado para FUNÇÃO (modalidade/escopo)
static void promoverParaFuncao(
    const std::string& nomeFunc,
    std::vector<std::vector<Simbolo>>& pilhaEscopos,
    std::vector<Simbolo>& tabelaSimbolo
    ){
    if (nomeFunc.empty() || pilhaEscopos.empty()) return;
    for (auto& s : pilhaEscopos.back()) {
        if (s.nome == nomeFunc) {
            s.modalidade = "funcao";
            s.escopo = "global";
            s.inicializado = true;
            for (auto& t : tabelaSimbolo)
                if (t.nome == s.nome && t.tipo == s.tipo && t.escopo == "global")
                { t.modalidade = "funcao"; t.inicializado = true; }
            return;
        }
    }
}

// *** CORREÇÃO: percorrer da pilha mais interna para a mais externa ***
static void marcarUsadoPorNome(
    const std::string& nome,
    std::vector<std::vector<Simbolo>>& pilhaEscopos,
    std::vector<Simbolo>& tabelaSimbolo
    ) {
    if (nome.empty()) return;
    for (auto it = pilhaEscopos.rbegin(); it != pilhaEscopos.rend(); ++it) {
        for (auto& simbolo : *it) {
            if (simbolo.nome == nome) {
                simbolo.usado = true;
                for (auto& s : tabelaSimbolo)
                    if (s.nome == simbolo.nome && s.tipo == simbolo.tipo && s.escopo == simbolo.escopo)
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
                std::cerr << "Marcando " << nome << " como inicializado no escopo " << simbolo.escopo << std::endl;
                for (auto& s : tabelaSimbolo)
                    if (s.nome == simbolo.nome && s.tipo == simbolo.tipo && s.escopo == simbolo.escopo)
                        s.inicializado = true;
                return;
            }
        }
    }
}

// Nova função para marcar inicialização de elementos de vetor
static void marcarElementoVetorInicializado(
    const std::string& nome,
    int /*indice*/,
    std::vector<std::vector<Simbolo>>& pilhaEscopos,
    std::vector<Simbolo>& tabelaSimbolo
    ) {
    if (nome.empty()) return;
    for (auto it = pilhaEscopos.rbegin(); it != pilhaEscopos.rend(); ++it) {
        for (auto& simbolo : *it) {
            if (simbolo.nome == nome && simbolo.modalidade == "vetor") {
                simbolo.inicializado = true;
                std::cerr << "Marcando elemento de " << nome << " como inicializado no escopo " << simbolo.escopo << std::endl;
                for (auto& s : tabelaSimbolo)
                    if (s.nome == simbolo.nome && s.tipo == simbolo.tipo && s.escopo == simbolo.escopo)
                        s.inicializado = true;
                return;
            }
        }
    }
}

// --------- membros privados auxiliares ---------
bool Semantico::existeNoEscopoAtual(const std::string& nome) const {
    if (pilhaEscopos.empty()) return false;
    const auto& esc = pilhaEscopos.back();
    return std::any_of(esc.begin(), esc.end(), [&](const Simbolo& s){ return s.nome == nome; });
}
bool Semantico::existe(const std::string& nome) const {
    for (auto it = pilhaEscopos.rbegin(); it != pilhaEscopos.rend(); ++it) {
        const auto& esc = *it;
        if (std::any_of(esc.begin(), esc.end(), [&](const Simbolo& s){ return s.nome == nome; }))
            return true;
    }
    return false;
}

// >>> NOVO: impede sombreamento na MESMA FUNÇÃO <<<
bool Semantico::existeNoEscopoDaFuncaoAtual(const std::string& nome) const {
    const std::string esc = escopoAtual();   // "global" ou nome_da_funcao
    if (esc == "global") return false;       // só aplicamos a regra dentro de função
    for (auto it = pilhaEscopos.rbegin(); it != pilhaEscopos.rend(); ++it) {
        for (const auto& s : *it) {
            if (s.nome == nome && s.escopo == esc) {
                return true;                 // já existe na mesma função
            }
        }
    }
    return false;
}

void Semantico::marcarUltimoDeclaradoComoVetor(const std::string& nome) {
    if (nome.empty() || pilhaEscopos.empty()) return;
    auto& escopoAtualRef = pilhaEscopos.back();
    for (auto& sim : escopoAtualRef) {
        if (sim.nome == nome) {
            sim.modalidade = "vetor";
            for (auto& s : tabelaSimbolo)
                if (s.nome == sim.nome && s.tipo == sim.tipo && s.escopo == sim.escopo)
                    s.modalidade = "vetor";
            return;
        }
    }
}

// Escopo atual: "global" ou nome da função do topo da pilha
std::string Semantico::escopoAtual() const {
    if (!pilhaFuncoes.empty()) return pilhaFuncoes.back();
    return "global";
}

// ====== IMPLEMENTAÇÕES QUE FALTAVAM (linker) ======
void Semantico::beginDeclaracao(const std::string& tipo) {
    modoDeclaracao   = true;
    tipoAtual        = tipo;
    lastDeclaredPos  = -1;
    ultimoDeclaradoNome.clear();
    // ao iniciar uma declaração, zera flags de lista de init
    inInitList = false;
    pendingInitList = false;
    initListDepth = 0;
}

void Semantico::endDeclaracao() {
    modoDeclaracao   = false;
    tipoAtual.clear();
    lastDeclaredPos  = -1;
    ultimoDeclaradoNome.clear();
    // garante estado consistente
    inInitList = false;
    pendingInitList = false;
    initListDepth = 0;
}

// --------- Semantico: declarar/usar/fechar ---------
void Semantico::declarar(const Token* tok) {
    warn("Declarando símbolo: " + tok->getLexeme() + " na posição: " + std::to_string(tok->getPosition()));
    if (!tok) return;
    if (tok->getId() != t_ID) return;

    const std::string nome = tok->getLexeme();
    if (nome.empty()) return;

    if (pilhaEscopos.empty()) abrirEscopo();

    // (1) Duplicidade no BLOCO atual
    if (existeNoEscopoAtual(nome)) {
        throw SemanticError("Símbolo '" + nome + "' já existe neste escopo",
                            tok->getPosition());
    }

    // (2) Proibir sombreamento dentro da MESMA FUNÇÃO
    if (escopoAtual() != "global" && existeNoEscopoDaFuncaoAtual(nome)) {
        throw SemanticError(
            "Símbolo '" + nome + "' já foi declarado anteriormente na função '" + escopoAtual() + "'.",
            tok->getPosition()
            );
    }

    if (tipoAtual.empty()) {
        throw SemanticError("Declaração de '" + nome + "' sem tipo corrente",
                            tok->getPosition());
    }

    Simbolo sim;
    sim.tipo = tipoAtual;
    sim.nome = nome;
    sim.usado = false;
    sim.inicializado = false;
    sim.modalidade = "variavel";
    sim.escopo = escopoAtual();

    pilhaEscopos.back().push_back(sim);
    tabelaSimbolo.push_back(sim);

    g_ultimoIdVisto = nome;
    g_ultimoIdAntesDaAtrib = nome;
    ultimoDeclaradoNome = nome;
    warn("Símbolo declarado: " + sim.nome + ", inicializado: " + std::to_string(sim.inicializado));
}

// *** CORREÇÃO: busca do símbolo deve respeitar sombreamento (rbegin -> rend) ***
void Semantico::usar(const Token* tok) {
    warn("Usando símbolo: " + tok->getLexeme());
    const std::string nome = tok->getLexeme();
    if (nome.empty()) return;

    bool encontrado = false;
    for (auto it = pilhaEscopos.rbegin(); it != pilhaEscopos.rend(); ++it) {
        for (auto& simbolo : *it) {
            if (simbolo.nome == nome) {
                if (!simbolo.inicializado) {
                    warn("Aviso: Símbolo '" + nome +
                         "' (tipo: " + simbolo.tipo +
                         ", escopo: " + simbolo.escopo +
                         ") usado sem inicialização na posição " +
                         std::to_string(tok->getPosition()));
                }
                simbolo.usado = true;
                for (auto& s : tabelaSimbolo)
                    if (s.nome == simbolo.nome && s.tipo == simbolo.tipo && s.escopo == simbolo.escopo)
                        s.usado = true;
                encontrado = true;
                break;
            }
        }
        if (encontrado) break;
    }
    if (!encontrado) {
        throw SemanticError("Símbolo '" + nome + "' não declarado neste escopo", tok->getPosition());
    }
}

void Semantico::fecharEscopo() {
    if (pilhaEscopos.empty()) return;

    for (const auto& simbolo : pilhaEscopos.back()) {
        if (!simbolo.usado) {
            warn("Aviso: Símbolo '" + simbolo.nome +
                 "' (tipo: " + simbolo.tipo +
                 ", escopo: " + simbolo.escopo +
                 ") declarado mas não usado.");
        }
    }
    pilhaEscopos.pop_back();

    if (!pilhaEscopoEhFuncao.empty()) {
        bool eraFunc = pilhaEscopoEhFuncao.back();
        pilhaEscopoEhFuncao.pop_back();
        if (eraFunc && !pilhaFuncoes.empty())
            pilhaFuncoes.pop_back();
    }
}

void Semantico::verificarNaoUsados() const {
    for (const auto& simbolo : tabelaSimbolo) {
        if (!simbolo.usado) {
            warn("Aviso: Símbolo '" + simbolo.nome +
                 "' (tipo: " + simbolo.tipo +
                 ", escopo: " + simbolo.escopo +
                 ") declarado mas não usado.");
        }
    }
}

void Semantico::warn(const std::string& msg) const {
    std::cerr << msg << std::endl;
    mensagens_.push_back(msg);
    if (logger_) logger_(msg);
}

void Semantico::executeAction(int action, const Token* token)
{
    warn("Ação #" + std::to_string(action) + ", Token: " + (token ? token->getLexeme() : "null") +
         ", Posição: " + (token ? std::to_string(token->getPosition()) : "-1") +
         ", modoDeclaracao: " + std::to_string(modoDeclaracao) +
         ", ultimoDeclaradoNome: " + ultimoDeclaradoNome);
    switch (action) {
    case 2:
        if (token && modoDeclaracao && lastDeclaredPos != token->getPosition()) {
            declarar(token);
            lastDeclaredPos = token->getPosition();
            g_ultimoIdVisto = token->getLexeme();
            g_ultimoIdAntesDaAtrib = g_ultimoIdVisto;
            ultimoDeclaradoNome = g_ultimoIdVisto;
        }
        return;
    case 4:
        warn("Ação #4: Usando ID " + token->getLexeme());
        usar(token);
        return;
    case 3:
        warn("Finalizando declaração. modoDeclaracao = " + std::to_string(modoDeclaracao));
        endDeclaracao();
        warn("Após endDeclaracao: modoDeclaracao = " + std::to_string(modoDeclaracao));
        return;

    case 10:  // ID[expr] -> vetor
        if (!ultimoDeclaradoNome.empty())
            marcarUltimoDeclaradoComoVetor(ultimoDeclaradoNome);
        return;

    case 11:
        warn("Ação #11: Marcando inicialização de " + ultimoDeclaradoNome);
        if (!ultimoDeclaradoNome.empty()) {
            marcarInicializadoPorNome(ultimoDeclaradoNome, pilhaEscopos, tabelaSimbolo);
        }
        return;

    case 12:  // ID[...] = { ... }
        if (!ultimoDeclaradoNome.empty())
            marcarInicializadoPorNome(ultimoDeclaradoNome, pilhaEscopos, tabelaSimbolo);
        inInitList = false; initListDepth = 0; pendingInitList = false;
        return;

    case 13:  // Marcar inicialização após atribuição
        warn("Ação #13: Marcando inicialização após atribuição de " + g_ultimoIdAntesDaAtrib);
        if (!g_ultimoIdAntesDaAtrib.empty()) {
            if (g_ultimoIdVisto.find('[') != std::string::npos) {
                // Trata atribuição a elemento de vetor (ex.: v[0] = 3)
                std::string nomeVetor = g_ultimoIdAntesDaAtrib;
                marcarElementoVetorInicializado(nomeVetor, -1, pilhaEscopos, tabelaSimbolo);
            } else {
                marcarInicializadoPorNome(g_ultimoIdAntesDaAtrib, pilhaEscopos, tabelaSimbolo);
            }
        }
        return;

    default:
        break;
    }

    if (!token) return;
    const int id = token->getId();

    switch (id) {
    // TIPOS
    case t_KEY_INT:
    case t_KEY_FLOAT:
    case t_KEY_CHAR:
    case t_KEY_STRING:
    case t_KEY_BOOL:
    case t_KEY_DOUBLE:
    case t_KEY_LONG:
    case t_KEY_VOID:
        beginDeclaracao(token->getLexeme());
        break;

    // PARENTS (assinatura)
    case t_DELIM_PARENTESESE:
        if (modoDeclaracao) {
            g_inParamList = true;
            g_paramBuffer.clear();
            g_funcEmConstrucao = g_ultimoIdVisto;
            promoverParaFuncao(g_funcEmConstrucao, pilhaEscopos, tabelaSimbolo);
        }
        break;

    case t_DELIM_PARENTESESD:
        if (g_inParamList) {
            g_inParamList = false;
            g_nextBraceIsFuncBody = true;
        }
        endDeclaracao();
        break;

    // IDENTIFICADORES
    case t_ID:
        warn("Processando ID: " + token->getLexeme() + ", Posição: " + std::to_string(token->getPosition()) +
             ", modoDeclaracao: " + std::to_string(modoDeclaracao));
        if (g_inParamList) {
            if (tipoAtual.empty())
                throw SemanticError("Parâmetro sem tipo declarado", token->getPosition());
            if (lastDeclaredPos != token->getPosition()) {
                Simbolo p;
                p.tipo = tipoAtual; p.nome = token->getLexeme();
                p.usado = false; p.inicializado = true;
                p.modalidade = "parametro";
                p.escopo = g_funcEmConstrucao.empty() ? "global" : g_funcEmConstrucao;
                g_paramBuffer.push_back(p);
                tabelaSimbolo.push_back(p);
                lastDeclaredPos = token->getPosition();
            }
        } else if (modoDeclaracao && lastDeclaredPos != token->getPosition()) {
            declarar(token);
            lastDeclaredPos = token->getPosition();
            g_ultimoIdVisto = token->getLexeme();
            g_ultimoIdAntesDaAtrib = g_ultimoIdVisto;
            ultimoDeclaradoNome = g_ultimoIdVisto;
        } else {
            usar(token);
            g_ultimoIdVisto = token->getLexeme();
            g_ultimoIdAntesDaAtrib = g_ultimoIdVisto; // Atualiza antes da atribuição
        }
        break;

    // VÍRGULA
    case t_DELIM_VIRGULA:
        if (modoDeclaracao || g_inParamList) {
            lastDeclaredPos = -1;
            ultimoDeclaradoNome.clear();
        }
        break;

    // PONTO E VÍRGULA
    case t_DELIM_PONTOVIRGULA:
        warn("Finalizando declaração. modoDeclaracao = " + std::to_string(modoDeclaracao));
        endDeclaracao();
        g_ultimoIdVisto.clear();
        g_ultimoIdAntesDaAtrib.clear();
        break;

    // CHAVES
    case t_DELIM_CHAVEE: {
        if (modoDeclaracao && (pendingInitList || inInitList)) {
            inInitList = true;
            ++initListDepth;
            break;
        }

        abrirEscopo();
        bool ehFunc = false;
        if (g_nextBraceIsFuncBody) {
            ehFunc = true;
            g_nextBraceIsFuncBody = false;
            if (!g_funcEmConstrucao.empty())
                pilhaFuncoes.push_back(g_funcEmConstrucao);

            auto& escopoAtual = pilhaEscopos.back();
            for (const auto& p : g_paramBuffer) {
                bool dup = std::any_of(escopoAtual.begin(), escopoAtual.end(),
                                       [&](const Simbolo& s){ return s.nome == p.nome; });
                if (!dup) escopoAtual.push_back(p);
            }
            g_paramBuffer.clear();
            ultimoDeclaradoNome.clear();
        }
        pilhaEscopoEhFuncao.push_back(ehFunc);
        break;
    }

    case t_DELIM_CHAVED:
        if (inInitList) {
            if (initListDepth > 0) --initListDepth;
            if (initListDepth == 0) {
                inInitList = false; pendingInitList = false;
                if (!ultimoDeclaradoNome.empty())
                    marcarInicializadoPorNome(ultimoDeclaradoNome, pilhaEscopos, tabelaSimbolo);
            }
            break;
        }

        fecharEscopo();
        g_ultimoIdVisto.clear();
        g_ultimoIdAntesDaAtrib.clear();
        break;

    // '='
    case t_OPR_ATRIB:
        if (modoDeclaracao) {
            pendingInitList = true;
            if (!ultimoDeclaradoNome.empty()) {
                marcarInicializadoPorNome(ultimoDeclaradoNome, pilhaEscopos, tabelaSimbolo);
            } else if (!g_ultimoIdVisto.empty()) {
                marcarInicializadoPorNome(g_ultimoIdVisto, pilhaEscopos, tabelaSimbolo);
            }
        }
        break;

    // '['
    case t_DELIM_COLCHETESE:
        if (modoDeclaracao) {
            const std::string alvo = !ultimoDeclaradoNome.empty() ? ultimoDeclaradoNome : g_ultimoIdVisto;
            marcarUltimoDeclaradoComoVetor(alvo);
        } else {
            marcarUsadoPorNome(g_ultimoIdVisto, pilhaEscopos, tabelaSimbolo);
        }
        break;

    default:
        warn("Token inesperado: " + token->getLexeme() + " na posição " + std::to_string(token->getPosition()));
        if (id != t_DELIM_PONTOVIRGULA && id != t_DELIM_CHAVEE && id != t_DELIM_CHAVED) {
            return; // Ignorar e continuar
        }
        break;
    }
}
