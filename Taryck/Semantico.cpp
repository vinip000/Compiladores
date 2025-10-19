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
// e, se for 'main', marca como USADA (ponto de entrada)
static void promoverParaFuncao(
    const std::string& nomeFunc,
    std::vector<std::vector<Simbolo>>& pilhaEscopos,
    std::vector<Simbolo>& tabelaSimbolo
    ){
    if (nomeFunc.empty() || pilhaEscopos.empty()) return;

    // no escopo atual (global neste ponto)
    for (auto& s : pilhaEscopos.back()) {
        if (s.nome == nomeFunc) {
            s.modalidade   = "funcao";
            s.escopo       = "global";
            s.inicializado = true; // “existe”

            // ajuste: tratar 'main' como usada por ser ponto de entrada
            if (nomeFunc == "main") {
                s.usado = true;
            }

            // refletir na tabela de exibição
            for (auto& t : tabelaSimbolo) {
                if (t.nome == s.nome && t.tipo == s.tipo && t.escopo == "global") {
                    t.modalidade   = "funcao";
                    t.inicializado = true;
                    if (nomeFunc == "main") t.usado = true; // espelha
                }
            }
            return;
        }
    }
}

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
                // Se quiser considerar "atribuição conta como uso", descomente a linha abaixo:
                // simbolo.usado = true;

                for (auto& s : tabelaSimbolo)
                    if (s.nome == simbolo.nome && s.tipo == simbolo.tipo && s.escopo == simbolo.escopo) {
                        s.inicializado = true;
                        // E espelhe também aqui se habilitar o comportamento acima:
                        // s.usado = true;
                    }
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

void Semantico::marcarUltimoDeclaradoComoVetor(const std::string& nome) {
    if (nome.empty() || pilhaEscopos.empty()) return;
    auto& escopoAtual = pilhaEscopos.back();
    for (auto& sim : escopoAtual) {
        if (sim.nome == nome) {
            sim.modalidade = "vetor";
            // refletir na tabela de exibição
            for (auto& s : tabelaSimbolo)
                if (s.nome == sim.nome && s.tipo == sim.tipo && s.escopo == sim.escopo)
                    s.modalidade = "vetor";
            return;
        }
    }
}

// --------- Semantico: declarar/usar/fechar ---------
void Semantico::declarar(const Token* tok) {
    if (!tok) return;

    // Só declara se realmente for um identificador
    if (tok->getId() != t_ID) return;

    const std::string nome = tok->getLexeme();
    if (nome.empty()) return;

    if (pilhaEscopos.empty()) abrirEscopo();

    if (existeNoEscopoAtual(nome)) {
        throw SemanticError("Símbolo '" + nome + "' já existe neste escopo",
                            tok->getPosition());
    }
    if (tipoAtual.empty()) {
        throw SemanticError("Declaração de '" + nome + "' sem tipo corrente",
                            tok->getPosition());
    }

    Simbolo sim;
    sim.tipo         = tipoAtual;
    sim.nome         = nome;
    sim.usado        = false;
    sim.inicializado = false;
    sim.modalidade   = "variavel";
    sim.escopo       = escopoAtual();

    pilhaEscopos.back().push_back(sim);
    tabelaSimbolo.push_back(sim);

    g_ultimoIdVisto        = nome;
    g_ultimoIdAntesDaAtrib = nome;
    ultimoDeclaradoNome    = nome;
}

void Semantico::usar(const Token* tok) {
    const std::string nome = tok->getLexeme();
    if (nome.empty()) return;

    bool encontrado = false;
    for (auto& escopo : pilhaEscopos) {
        for (auto& simbolo : escopo) {
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
                encontrado = true; break;
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

    // avisos do escopo
    for (const auto& simbolo : pilhaEscopos.back()) {
        if (!simbolo.usado) {
            warn("Aviso: Símbolo '" + simbolo.nome +
                 "' (tipo: " + simbolo.tipo +
                 ", escopo: " + simbolo.escopo +
                 ") declarado mas não usado.");
        }
    }
    pilhaEscopos.pop_back();

    // se o escopo fechado era corpo de função, sair do escopo da função
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

// logger/avisos
void Semantico::warn(const std::string& msg) const {
    std::cerr << msg << std::endl;
    mensagens_.push_back(msg);
    if (logger_) logger_(msg);
}

void Semantico::executeAction(int action, const Token* token)
{
    // Marcadores da gramática
    switch (action) {
    case 2:   // ID em declaração (garante declaração mesmo se o gerador não cair no case t_ID)
        if (token && modoDeclaracao && lastDeclaredPos != token->getPosition()) {
            declarar(token);
            lastDeclaredPos        = token->getPosition();
            g_ultimoIdVisto        = token->getLexeme();
            g_ultimoIdAntesDaAtrib = g_ultimoIdVisto;
            ultimoDeclaradoNome    = g_ultimoIdVisto;
        }
        return;

    case 10:  // ID [expr]  -> vetor
        if (!ultimoDeclaradoNome.empty())
            marcarUltimoDeclaradoComoVetor(ultimoDeclaradoNome);
        return;

    case 11:  // ID = <expr>  (inicialização escalar)
        if (!ultimoDeclaradoNome.empty())
            marcarInicializadoPorNome(ultimoDeclaradoNome, pilhaEscopos, tabelaSimbolo);
        return;

    case 12:  // ID [ ... ] = { ... }  (inicialização por lista)
        if (!ultimoDeclaradoNome.empty())
            marcarInicializadoPorNome(ultimoDeclaradoNome, pilhaEscopos, tabelaSimbolo);
        // encerra qualquer estado de lista
        inInitList = false; initListDepth = 0; pendingInitList = false;
        return;

    default:
        break;
    }

    if (!token) return;
    const int id = token->getId();

    switch (id) {
    // ---------------- TIPOS ----------------
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

    // --------------- '(' (assinatura de função) ---------------
    case t_DELIM_PARENTESESE:
        if (modoDeclaracao) {
            g_inParamList = true;
            g_paramBuffer.clear();
            g_funcEmConstrucao = g_ultimoIdVisto;

            // promove p/ função (ajusta modalidade/escopo/inicializado)
            promoverParaFuncao(g_funcEmConstrucao, pilhaEscopos, tabelaSimbolo);

            // reforço: tratar 'main' como usada (em pilha e em tabela)
            if (g_funcEmConstrucao == "main") {
                // marca na pilha
                for (auto &esc : pilhaEscopos) {
                    for (auto &sym : esc) {
                        if (sym.nome == "main" && sym.modalidade == "funcao" && sym.escopo == "global") {
                            sym.usado = true;
                        }
                    }
                }
                // espelha em tabelaSimbolo
                for (auto &t : tabelaSimbolo) {
                    if (t.nome == "main" && t.modalidade == "funcao" && t.escopo == "global") {
                        t.usado = true;
                    }
                }
            }
        }
        break;

    case t_DELIM_PARENTESESD:
        if (g_inParamList) {
            g_inParamList = false;
            g_nextBraceIsFuncBody = true;
        }
        endDeclaracao();
        break;

    // --------------- IDENTIFICADORES ---------------
    case t_ID:
        if (g_inParamList) {
            // parâmetro
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
        } else if (modoDeclaracao) {
            if (lastDeclaredPos != token->getPosition()) {
                declarar(token);
                lastDeclaredPos = token->getPosition();
            }
            g_ultimoIdVisto        = token->getLexeme();
            g_ultimoIdAntesDaAtrib = g_ultimoIdVisto;
            ultimoDeclaradoNome    = g_ultimoIdVisto;
        } else {
            usar(token);
            g_ultimoIdVisto        = token->getLexeme();
            g_ultimoIdAntesDaAtrib = g_ultimoIdVisto;
        }
        break;

    // --------------- VÍRGULA ---------------
    case t_DELIM_VIRGULA:
        if (modoDeclaracao || g_inParamList) {
            lastDeclaredPos = -1;
            ultimoDeclaradoNome.clear();
        }
        break;

    // --------------- PONTO E VÍRGULA ---------------
    case t_DELIM_PONTOVIRGULA:
        endDeclaracao();
        g_ultimoIdVisto.clear();
        g_ultimoIdAntesDaAtrib.clear();
        // (endDeclaracao já limpa estados de init-list)
        break;

    // --------------- CHAVES ---------------
    case t_DELIM_CHAVEE: {
        // SE for parte de lista de inicialização: NÃO abre escopo.
        if (modoDeclaracao && (pendingInitList || inInitList)) {
            inInitList = true;
            ++initListDepth;
            break;
        }

        // Bloco “de verdade”
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
        // Se estávamos em uma lista de inicialização, só fecha a lista.
        if (inInitList) {
            if (initListDepth > 0) --initListDepth;
            if (initListDepth == 0) {
                inInitList = false; pendingInitList = false;
                // por segurança, marca inicializado
                if (!ultimoDeclaradoNome.empty())
                    marcarInicializadoPorNome(ultimoDeclaradoNome, pilhaEscopos, tabelaSimbolo);
            }
            break;
        }

        // Fechamento de bloco real
        fecharEscopo();
        g_ultimoIdVisto.clear();
        g_ultimoIdAntesDaAtrib.clear();
        break;

    // --------------- '=' ---------------
    case t_OPR_ATRIB:
        if (modoDeclaracao) {
            // pode vir um '{' logo em seguida
            pendingInitList = true;
            // (a marcação “inicializado” do escalar fica a cargo de #11;
            //  se vier '{', #12/fecho de lista marcará)
        } else {
            marcarInicializadoPorNome(g_ultimoIdAntesDaAtrib, pilhaEscopos, tabelaSimbolo);
        }
        break;

    // --------------- '[' ---------------
    case t_DELIM_COLCHETESE:
        if (modoDeclaracao) {
            const std::string alvo = !ultimoDeclaradoNome.empty() ? ultimoDeclaradoNome : g_ultimoIdVisto;
            marcarUltimoDeclaradoComoVetor(alvo);
        } else {
            marcarUsadoPorNome(g_ultimoIdVisto, pilhaEscopos, tabelaSimbolo);
        }
        break;

    default:
        break;
    }
}
