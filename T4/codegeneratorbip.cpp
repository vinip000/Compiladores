#include "CodeGeneratorBIP.h"
#include <fstream>
#include <algorithm>

// =================== internos ===================
static inline bool isIdentChar(unsigned char c) {
    return std::isalnum(c) || c=='_' || c=='$';
}

static bool isIntegerLiteral(const std::string& s) {
    if (s.empty()) return false;

    size_t i = 0;
    if (s[0] == '+' || s[0] == '-') {
        i = 1;
        if (i >= s.size()) return false;
    }

    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

// =================== ctor ===================
CodeGeneratorBIP::CodeGeneratorBIP(const Options& opt)
    : opt_(opt) {}

// =================== helpers estáticos ===================
std::string CodeGeneratorBIP::sanitizeLabel(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (unsigned char c : s) r.push_back(isIdentChar(c) ? char(c) : '_');
    if (r.empty()) r = "sym";
    return r;
}

bool CodeGeneratorBIP::isGlobalDataCandidate(const Simbolo& s) {
    // NÃO entra em .data:
    if (s.modalidade == "funcao" || s.modalidade == "parametro") {
        return false;
    }

    // ENTRA em .data:
    // - variáveis escalares
    if (s.modalidade == "variavel") {
        return true;
    }

    // - vetores (se sua linguagem tiver)
    if (s.modalidade == "vetor" || s.isVetor) {
        return true;
    }

    return false;
}

// =================== .data ===================
std::string CodeGeneratorBIP::buildDataSection(const std::vector<Simbolo>& tabela) const {
    std::vector<const Simbolo*> cand;
    cand.reserve(tabela.size());
    for (const auto& s : tabela)
        if (isGlobalDataCandidate(s)) cand.push_back(&s);

    if (opt_.sortByName) {
        std::sort(cand.begin(), cand.end(),
                  [](const Simbolo* a, const Simbolo* b){ return a->nome < b->nome; });
    }

    std::ostringstream out;
    if (opt_.includeDataHeader) out << ".data\n";

    std::unordered_set<std::string> used;
    for (const Simbolo* ps : cand) {
        const auto& s = *ps;
        std::string label = sanitizeLabel(s.nome);

        int k = 1;
        while (used.count(label)) label = sanitizeLabel(s.nome) + "_" + std::to_string(k++);
        used.insert(label);

        int N = (s.modalidade == "vetor" || s.isVetor)
                    ? (s.vetorTam > 0 ? s.vetorTam : 1)
                    : 1;

        out << label << " : ";
        for (int i = 0; i < N; ++i) {
            out << "0";
            if (i+1 < N) out << " ";
        }
        out << "\n";
    }
    out << "\n";
    return out.str();
}

bool CodeGeneratorBIP::emitDataToFile(const std::string& outPath,
                                      const std::vector<Simbolo>& tabela,
                                      std::function<void(const std::string&)> logger) const {
    const std::string text = buildDataSection(tabela);
    std::ofstream ofs(outPath, std::ios::binary);
    if (!ofs) {
        if (logger) logger("erro: não foi possível abrir " + outPath + " para escrita");
        return false;
    }
    ofs << text;
    if (!ofs.good()) {
        if (logger) logger("erro: falha ao gravar em " + outPath);
        return false;
    }
    if (logger) logger("gerou seção .data em: " + outPath);
    return true;
}

// =================== .text – API ===================
void CodeGeneratorBIP::clearText() { text_.clear(); }

void CodeGeneratorBIP::emitInstr(const std::string& instr) { text_.push_back(instr); }

void CodeGeneratorBIP::emitLabel(const std::string& label) {
    std::ostringstream oss; oss << sanitizeLabel(label) << ":";
    text_.push_back(oss.str());
}

std::string CodeGeneratorBIP::newLabel(const std::string& prefix) {
    std::ostringstream oss; oss << prefix << (++labelCounter_);
    return oss.str();
}

// globais: LDI nome ; LD/STO k
void CodeGeneratorBIP::emitLoadId(const std::string& nome) {
    std::string lbl = sanitizeLabel(nome);
    emitInstr("LD " + lbl);      // ACC <- Mem[lbl]
}

void CodeGeneratorBIP::emitStoreId(const std::string& nome) {
    std::string lbl = sanitizeLabel(nome);
    emitInstr("STO " + lbl);     // Mem[lbl] <- ACC
}

// vetores com deslocamento constante
void CodeGeneratorBIP::emitLoadIdOffset(const std::string& nome, int k) {
    std::string lbl = sanitizeLabel(nome);

    // índice constante k no $indr
    emitInstr("LDI " + std::to_string(k));
    emitInstr("STO $indr");

    // carrega vetor[$indr] em ACC
    emitInstr("LDV " + lbl);
}

// ACC -> vetor[k]
void CodeGeneratorBIP::emitStoreIdOffset(const std::string& nome, int k) {
    std::string lbl = sanitizeLabel(nome);

    // índice constante k no $indr
    emitInstr("LDI " + std::to_string(k));
    emitInstr("STO $indr");

    // armazena ACC em vetor[$indr]
    emitInstr("STOV " + lbl);
}

// aritmética
void CodeGeneratorBIP::emitAdd() { emitInstr("ADD"); }
void CodeGeneratorBIP::emitSub() { emitInstr("SUB"); }
void CodeGeneratorBIP::emitMul() { emitInstr("MUL"); }
void CodeGeneratorBIP::emitDiv() { emitInstr("DIV"); }

// bit a bit
void CodeGeneratorBIP::emitAnd() { emitInstr("AND"); }
void CodeGeneratorBIP::emitOr()  { emitInstr("OR");  }
void CodeGeneratorBIP::emitXor() { emitInstr("XOR"); }
void CodeGeneratorBIP::emitNot() { emitInstr("NOT"); }
void CodeGeneratorBIP::emitShl() { emitInstr("SHL"); }
void CodeGeneratorBIP::emitShr() { emitInstr("SHR"); }

// desvios
void CodeGeneratorBIP::emitJmp(const std::string& label) {
    emitInstr("JMP " + sanitizeLabel(label));
}
void CodeGeneratorBIP::emitJz(const std::string& label) {
    emitInstr("JZ " + sanitizeLabel(label));
}

// =================== Atribuições ===================
// Simples: variável/vetor ← variável/vetor
void CodeGeneratorBIP::emitAssign(const std::string& dest, bool destIsArray, int destIndex,
                                  const std::string& src,  bool srcIsArray,  int srcIndex) {
    if (srcIsArray) emitLoadIdOffset(src, srcIndex);
    else            emitLoadId(src);

    if (destIsArray) emitStoreIdOffset(dest, destIndex);
    else             emitStoreId(dest);
}

// Vetor com índice variável (v[i] = x)
void CodeGeneratorBIP::emitAssignVarIndex(const std::string& dest,
                                          const std::string& idx,
                                          const std::string& src) {
    std::string lblDest = sanitizeLabel(dest);

    // idx -> $indr
    emitLoadId(idx);            // LD idx
    emitInstr("STO $indr");

    // src -> ACC
    emitLoadId(src);            // LD src

    // ACC -> vetor[$indr]
    emitInstr("STOV " + lblDest);
}

// Atribuição com operação simples (a = b + c)
void CodeGeneratorBIP::emitAssignSimpleExpr(const std::string& dest,
                                            const std::string& op1,
                                            const std::string& oper,
                                            const std::string& op2) {
    // Caso especial: dest = <constante>
    // (oper vazio e sem op2 → usamos op1 como literal)
    if (oper.empty() && op2.empty() && isIntegerLiteral(op1)) {
        emitInstr("LDI " + op1);   // imediato numérico é válido no BIP
        emitStoreId(dest);         // STO dest
        return;
    }

    // Carrega op1 (variável ou constante)
    if (isIntegerLiteral(op1))
        emitInstr("LDI " + op1);
    else
        emitLoadId(op1);

    emitInstr("PUSH");

    // Carrega op2 (variável ou constante)
    if (isIntegerLiteral(op2))
        emitInstr("LDI " + op2);
    else
        emitLoadId(op2);

    emitInstr("POP");

    if (oper == "+")      emitAdd();
    else if (oper == "-") emitSub();
    else if (oper == "*") emitMul();
    else if (oper == "/") emitDiv();
    // (se precisar de mais operadores, encaixa aqui)

    emitStoreId(dest);
}

// =================== construção da .text / programa ===================
std::string CodeGeneratorBIP::buildTextSection() const {
    std::ostringstream oss;
    if (opt_.includeTextHeader) oss << ".text\n";
    oss << opt_.entryLabel << ":\n";
    for (const auto& l : text_) {
        if (!l.empty() && l.back() == ':') oss << l << "\n";
        else                               oss << "    " << l << "\n";
    }
    oss << "    HLT 0\n";
    return oss.str();
}

std::string CodeGeneratorBIP::buildProgram(const std::vector<Simbolo>& tabela) const {
    std::ostringstream oss;
    oss << buildDataSection(tabela);
    oss << buildTextSection();
    return oss.str();
}
