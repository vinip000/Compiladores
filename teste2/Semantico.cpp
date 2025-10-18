#include "Semantico.h"
#include "Token.h"
#include "SemanticError.h"

#include <iostream>
#include <algorithm>
#include <string>

// --------- estado auxiliar existente ---------
static std::string g_ultimoIdVisto;
static std::string g_ultimoIdAntesDaAtrib;

static bool g_inParamList = false;
static bool g_nextBraceIsFuncBody = false;
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

// promove símbolo para função (já era seu, mantido)
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
                for (auto& s : tabelaSimbolo)
                    if (s.nome == simbolo.nome && s.tipo == simbolo.tipo && s.escopo == simbolo.escopo)
                        s.inicializado = true;
                return;
            }
        }
    }
}

// --------- membros privados auxiliares (existentes) ---------
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
            for (auto& s : tabelaSimbolo)
                if (s.nome == sim.nome && s.tipo == sim.tipo && s.escopo == sim.escopo)
                    s.modalidade = "vetor";
            return;
        }
    }
}

// --------- NOVO: mapeamento de tipos e regras ---------
TipoPrim Semantico::mapTipoString(const std::string& tlex) {
    if (tlex == "int")    return TipoPrim::TP_INT;
    if (tlex == "float")  return TipoPrim::TP_FLOAT;
    if (tlex == "double") return TipoPrim::TP_DOUBLE;
    if (tlex == "long")   return TipoPrim::TP_LONG;
    if (tlex == "char")   return TipoPrim::TP_CHAR;
    if (tlex == "bool")   return TipoPrim::TP_BOOL;
    if (tlex == "string") return TipoPrim::TP_STRING;
    return TipoPrim::TP_INVALID;
}
std::string Semantico::toString(TipoPrim t) {
    switch (t) {
    case TipoPrim::TP_INT: return "int";
    case TipoPrim::TP_FLOAT: return "float";
    case TipoPrim::TP_DOUBLE: return "double";
    case TipoPrim::TP_LONG: return "long";
    case TipoPrim::TP_CHAR: return "char";
    case TipoPrim::TP_BOOL: return "bool";
    case TipoPrim::TP_STRING: return "string";
    default: return "<tipo inválido>";
    }
}

// Op. binária: combina (a,b) com base em 'op' e retorna tipo resultante (ou lança erro)
TipoPrim Semantico::promoverBinario(TipoPrim a, TipoPrim b, const std::string& op, int pos) {
    auto erro = [&](const std::string& msg){
        throw SemanticError("Tipo inválido em '" + op + "': " + msg, pos);
    };

    // Comparações / relacionais -> bool (desde que operandos compatíveis e não string em <,>, etc.)
    if (op=="==" || op=="!=" || op=="<" || op=="<=" || op==">" || op==">=") {
        if (a==TipoPrim::TP_STRING || b==TipoPrim::TP_STRING) {
            if (op=="==" || op=="!=") return TipoPrim::TP_BOOL; // permitir igualdade com string
            erro("comparação relacional com string");
        }
        return TipoPrim::TP_BOOL;
    }

    // Lógicos
    if (op=="&&" || op=="||") {
        if (a!=TipoPrim::TP_BOOL || b!=TipoPrim::TP_BOOL) erro("operadores lógicos exigem bool");
        return TipoPrim::TP_BOOL;
    }

    // Aritméticos
    if (op=="+" || op=="-" || op=="*" || op=="/" || op=="%") {
        // Concatenação (opcional): permitir string + string
        if (op=="+" && a==TipoPrim::TP_STRING && b==TipoPrim::TP_STRING) return TipoPrim::TP_STRING;

        // proibir string em qualquer outro aritmético
        if (a==TipoPrim::TP_STRING || b==TipoPrim::TP_STRING)
            erro("operações aritméticas com string não são permitidas");

        // '%' só para inteiros/long/char (tratando char como int)
        if (op=="%") {
            auto intLike = [](TipoPrim t){ return t==TipoPrim::TP_INT || t==TipoPrim::TP_LONG || t==TipoPrim::TP_CHAR || t==TipoPrim::TP_BOOL; };
            if (!intLike(a) || !intLike(b)) erro("operador % exige inteiros");
            // promove para 'maior' inteiro; aqui padronizamos para int/long
            return (a==TipoPrim::TP_LONG || b==TipoPrim::TP_LONG) ? TipoPrim::TP_LONG : TipoPrim::TP_INT;
        }

        // Promoção numérica padrão (char/bool -> int -> long -> float -> double)
        auto rank = [](TipoPrim t)->int{
            switch (t) {
            case TipoPrim::TP_BOOL: return 0;
            case TipoPrim::TP_CHAR: return 1;
            case TipoPrim::TP_INT: return 2;
            case TipoPrim::TP_LONG: return 3;
            case TipoPrim::TP_FLOAT: return 4;
            case TipoPrim::TP_DOUBLE: return 5;
            default: return -999;
            }
        };
        if (rank(a)<0 || rank(b)<0) erro("operandos não numéricos");
        int r = std::max(rank(a), rank(b));
        switch (r) {
        case 5: return TipoPrim::TP_DOUBLE;
        case 4: return TipoPrim::TP_FLOAT;
        case 3: return TipoPrim::TP_LONG;
        default: return TipoPrim::TP_INT;
        }
    }

    // operador desconhecido
    erro("operador desconhecido");
    return TipoPrim::TP_INVALID;
}

bool Semantico::atribuivel(TipoPrim dest, TipoPrim src) {
    if (dest == src) return true;
    // regras simples:
    // numérico amplo recebe numérico estreito
    auto num = [](TipoPrim t){
        return t==TipoPrim::TP_BOOL || t==TipoPrim::TP_CHAR || t==TipoPrim::TP_INT || t==TipoPrim::TP_LONG || t==TipoPrim::TP_FLOAT || t==TipoPrim::TP_DOUBLE;
    };
    if (num(dest) && num(src)) {
        // permitir char/bool->int/long/float/double; int->float/double; long->double; (evite perdas perigosas)
        // aqui aceitamos "widening"; se quiser ser mais rígido, restrinja
        auto rank = [](TipoPrim t)->int{
            switch (t) {
            case TipoPrim::TP_BOOL: return 0;
            case TipoPrim::TP_CHAR: return 1;
            case TipoPrim::TP_INT: return 2;
            case TipoPrim::TP_LONG: return 3;
            case TipoPrim::TP_FLOAT: return 4;
            case TipoPrim::TP_DOUBLE: return 5;
            default: return -999;
            }
        };
        return rank(src) <= rank(dest);
    }
    // string só recebe string
    if (dest==TipoPrim::TP_STRING) return src==TipoPrim::TP_STRING;
    return false;
}

std::optional<TipoPrim> Semantico::tipoDeIdent(const std::string& nome) const {
    for (auto it = pilhaEscopos.rbegin(); it != pilhaEscopos.rend(); ++it) {
        for (const auto& s : *it) {
            if (s.nome == nome) return mapTipoString(s.tipo);
        }
    }
    return std::nullopt;
}

void Semantico::checarAtribuicaoFinal(int pos) {
    if (!atribPendente) return;
    if (pilhaTiposExp.empty())
        throw SemanticError("Expressão à direita de '=' está vazia", pos);

    TipoPrim rhs = pilhaTiposExp.back();
    pilhaTiposExp.pop_back();

    if (!atribuivel(tipoLHS, rhs)) {
        throw SemanticError(
            "Atribuição incompatível: variável '" + nomeLHS + "' (" + toString(tipoLHS) +
                ") recebe expressão do tipo " + toString(rhs), pos);
    }
    atribPendente = false;
    nomeLHS.clear();
    tipoLHS = TipoPrim::TP_INVALID;
}

// --------- Semantico: declarar/usar/fechar (existentes) ---------
void Semantico::declarar(const Token* tok) {
    const std::string nome = tok->getLexeme();
    if (nome.empty()) return;

    if (pilhaEscopos.empty())
        abrirEscopo();

    if (existeNoEscopoAtual(nome)) {
        throw SemanticError("Símbolo '" + nome + "' já existe neste escopo", tok->getPosition());
    }
    if (tipoAtual.empty()) {
        throw SemanticError("Declaração de '" + nome + "' sem tipo corrente", tok->getPosition());
    }

    Simbolo sim;
    sim.tipo        = tipoAtual;
    sim.nome        = nome;
    sim.usado       = false;
    sim.inicializado= false;
    sim.modalidade  = "variavel";
    sim.escopo      = escopoAtual();

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
                    std::cerr << "Aviso: Símbolo '" << nome
                              << "' (tipo: " << simbolo.tipo
                              << ", escopo: " << simbolo.escopo
                              << ") usado sem inicialização na posição "
                              << tok->getPosition() << std::endl;
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

    for (const auto& simbolo : pilhaEscopos.back()) {
        if (!simbolo.usado) {
            std::cerr << "Aviso: Símbolo '" << simbolo.nome
                      << "' (tipo: " << simbolo.tipo
                      << ", escopo: " << simbolo.escopo
                      << ") declarado mas não usado.\n";
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
            std::cerr << "Aviso: Símbolo '" << simbolo.nome
                      << "' (tipo: " << simbolo.tipo
                      << ", escopo: " << simbolo.escopo
                      << ") declarado mas não usado.\n";
        }
    }
}

// --------- NOVO: helpers chamados pela gramática ---------
void Semantico::pushTipoIdent(const std::string& nome, int pos) {
    auto t = tipoDeIdent(nome);
    if (!t.has_value())
        throw SemanticError("Uso de identificador não declarado: " + nome, pos);
    pilhaTiposExp.push_back(*t);
}

void Semantico::binop(const std::string& op, int pos) {
    if (pilhaTiposExp.size() < 2)
        throw SemanticError("Expressão malformada para operador '" + op + "'", pos);
    TipoPrim b = pilhaTiposExp.back(); pilhaTiposExp.pop_back();
    TipoPrim a = pilhaTiposExp.back(); pilhaTiposExp.pop_back();
    pilhaTiposExp.push_back(promoverBinario(a, b, op, pos));
}

void Semantico::unop(const std::string& op, int pos) {
    if (pilhaTiposExp.empty())
        throw SemanticError("Expressão malformada para operador '" + op + "'", pos);
    TipoPrim a = pilhaTiposExp.back(); pilhaTiposExp.pop_back();

    if (op=="!") {
        if (a != TipoPrim::TP_BOOL)
            throw SemanticError("Operador '!' exige bool", pos);
        pilhaTiposExp.push_back(TipoPrim::TP_BOOL);
    } else if (op=="-" || op=="+") {
        // unários numéricos
        auto num = [](TipoPrim t){
            return t==TipoPrim::TP_BOOL || t==TipoPrim::TP_CHAR || t==TipoPrim::TP_INT ||
                   t==TipoPrim::TP_LONG || t==TipoPrim::TP_FLOAT || t==TipoPrim::TP_DOUBLE;
        };
        if (!num(a)) throw SemanticError("Operador unário exige numérico", pos);
        pilhaTiposExp.push_back(a);
    } else {
        throw SemanticError("Operador unário desconhecido '" + op + "'", pos);
    }
}

void Semantico::indexacao(int pos) {
    // v[expr]: o topo deve ser int
    if (pilhaTiposExp.empty())
        throw SemanticError("Indexação sem expressão de índice", pos);
    TipoPrim idx = pilhaTiposExp.back(); pilhaTiposExp.pop_back();
    if (!(idx==TipoPrim::TP_INT || idx==TipoPrim::TP_LONG || idx==TipoPrim::TP_CHAR || idx==TipoPrim::TP_BOOL))
        throw SemanticError("Índice de vetor deve ser inteiro", pos);
    // o resultado da indexação tem o tipo do vetor; aqui assumimos que
    // antes de chamar indexacao, o tipo do vetor já foi empurrado.
    if (pilhaTiposExp.empty())
        throw SemanticError("Indexação sem operando vetor", pos);
    // não mudamos o tipo do vetor no topo (ele já representa o elemento)
}

void Semantico::chamadaFuncTermina(int /*pos*/) {
    // Gancho para no futuro empurrar tipo de retorno.
    // Por ora, não altera pilha de tipos.
}

// --------- Dispatcher ---------
void Semantico::executeAction(int action, const Token* token) {
    // Ações especiais já usadas por você (mantidas)
    switch (action) {
    case 10: // ID [...] em DECLARAÇÃO -> vetor
        if (!ultimoDeclaradoNome.empty())
            marcarUltimoDeclaradoComoVetor(ultimoDeclaradoNome);
        return;
    case 11: // ID = <expr> em DECLARAÇÃO -> inicializado
        if (!ultimoDeclaradoNome.empty())
            marcarInicializadoPorNome(ultimoDeclaradoNome, pilhaEscopos, tabelaSimbolo);
        return;

    // ====== NOVO: ganchos por ação # na gramática ======
    // Você pode usar estes números na sua gramática GALS.
    // #100: literal inteiro
    case 100: pushTipoLiteral(TipoPrim::TP_INT); return;
    // #101: literal float
    case 101: pushTipoLiteral(TipoPrim::TP_FLOAT); return;
    // #102: literal double
    case 102: pushTipoLiteral(TipoPrim::TP_DOUBLE); return;
    // #103: literal char
    case 103: pushTipoLiteral(TipoPrim::TP_CHAR); return;
    // #104: literal bool
    case 104: pushTipoLiteral(TipoPrim::TP_BOOL); return;
    // #105: literal string
    case 105: pushTipoLiteral(TipoPrim::TP_STRING); return;

    // #110: ao ler um ID em contexto de expressão, empurrar seu tipo
    case 110:
        if (token) pushTipoIdent(token->getLexeme(), token->getPosition());
        return;

    // binários
    case 120: binop("+", token ? token->getPosition() : -1); return;
    case 121: binop("-", token ? token->getPosition() : -1); return;
    case 122: binop("*", token ? token->getPosition() : -1); return;
    case 123: binop("/", token ? token->getPosition() : -1); return;
    case 124: binop("%", token ? token->getPosition() : -1); return;
    case 125: binop("==", token ? token->getPosition() : -1); return;
    case 126: binop("!=", token ? token->getPosition() : -1); return;
    case 127: binop("<", token ? token->getPosition() : -1); return;
    case 128: binop("<=", token ? token->getPosition() : -1); return;
    case 129: binop(">", token ? token->getPosition() : -1); return;
    case 130: binop(">=", token ? token->getPosition() : -1); return;
    case 131: binop("&&", token ? token->getPosition() : -1); return;
    case 132: binop("||", token ? token->getPosition() : -1); return;

    // unários
    case 140: unop("-", token ? token->getPosition() : -1); return; // menos unário
    case 141: unop("+", token ? token->getPosition() : -1); return; // mais unário
    case 142: unop("!", token ? token->getPosition() : -1); return; // not lógico

    // indexação
    case 150: indexacao(token ? token->getPosition() : -1); return;

    // #160: fim de expressão (ex.: antes de DELIM_PONTOVIRGULA) — útil para validar atribuição pendente
    case 160: checarAtribuicaoFinal(token ? token->getPosition() : -1); return;

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
        beginDeclaracao(token->getLexeme());
        break;

    case t_KEY_VOID:
        beginDeclaracao(token->getLexeme());
        break;

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

    case t_ID:
        if (g_inParamList) {
            if (tipoAtual.empty())
                throw SemanticError("Parâmetro sem tipo declarado", token->getPosition());

            if (lastDeclaredPos != token->getPosition()) {
                const std::string nomeParam = token->getLexeme();

                Simbolo p;
                p.tipo        = tipoAtual;
                p.nome        = nomeParam;
                p.usado       = false;
                p.inicializado= true;
                p.modalidade  = "parametro";
                p.escopo      = g_funcEmConstrucao.empty() ? "global" : g_funcEmConstrucao;
                g_paramBuffer.push_back(p);
                tabelaSimbolo.push_back(p);

                lastDeclaredPos = token->getPosition();
            }
        }
        else if (modoDeclaracao) {
            if (lastDeclaredPos != token->getPosition()) {
                declarar(token);
                lastDeclaredPos = token->getPosition();
            }
            g_ultimoIdVisto        = token->getLexeme();
            g_ultimoIdAntesDaAtrib = g_ultimoIdVisto;
            ultimoDeclaradoNome    = g_ultimoIdVisto;
        }
        else {
            usar(token);
            g_ultimoIdVisto        = token->getLexeme();
            g_ultimoIdAntesDaAtrib = g_ultimoIdVisto;

            // *** NOVO: se ID está em expressão, empurre o tipo
            pushTipoIdent(g_ultimoIdVisto, token->getPosition());
        }
        break;

    case t_DELIM_VIRGULA:
        if (modoDeclaracao || g_inParamList) {
            lastDeclaredPos = -1;
            ultimoDeclaradoNome.clear();
        }
        break;

    case t_DELIM_PONTOVIRGULA:
        // fim de statement: se havia atribuição pendente, valide agora
        checarAtribuicaoFinal(token->getPosition());
        endDeclaracao();
        g_ultimoIdVisto.clear();
        g_ultimoIdAntesDaAtrib.clear();
        ultimoDeclaradoNome.clear();
        break;

    case t_DELIM_CHAVEE: {
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
        fecharEscopo();
        g_ultimoIdVisto.clear();
        g_ultimoIdAntesDaAtrib.clear();
        break;

    case t_OPR_ATRIB: {
        // já era usado por você para "inicializado"; agora também marcamos atribuição pendente
        if (modoDeclaracao && !ultimoDeclaradoNome.empty()) {
            marcarInicializadoPorNome(ultimoDeclaradoNome, pilhaEscopos, tabelaSimbolo);
            nomeLHS = ultimoDeclaradoNome;
        } else {
            marcarInicializadoPorNome(g_ultimoIdAntesDaAtrib, pilhaEscopos, tabelaSimbolo);
            nomeLHS = g_ultimoIdAntesDaAtrib;
        }
        // resolve tipo do LHS
        auto t = tipoDeIdent(nomeLHS);
        if (!t.has_value()) throw SemanticError("LHS da atribuição não declarado: " + nomeLHS, token->getPosition());
        tipoLHS = *t;
        atribPendente = true;
        break;
    }

    case t_DELIM_COLCHETESE:
        if (modoDeclaracao) {
            const std::string alvo = !ultimoDeclaradoNome.empty() ? ultimoDeclaradoNome : g_ultimoIdVisto;
            marcarUltimoDeclaradoComoVetor(alvo);
        } else {
            marcarUsadoPorNome(g_ultimoIdVisto, pilhaEscopos, tabelaSimbolo);
            // Em expressão: assumimos que antes você empurrou o tipo do vetor (ID).
            // Agora virá a expressão do índice (com push via #100..#110) e depois
            // chame a ação #150 (indexacao) para validar o índice.
        }
        break;

    default:
        break;
    }
}
