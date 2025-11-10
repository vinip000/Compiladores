#include "CodeGeneratorBIP.h"
#include <fstream>
#include <algorithm>

// =================== internos ===================
static inline bool isIdentChar(unsigned char c) {
    return std::isalnum(c) || c=='_' || c=='$';
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
    if (s.escopo != "global") return false;
    if (s.modalidade == "parametro" || s.modalidade == "funcao") return false;
    if (s.modalidade == "variavel" || s.modalidade == "vetor") return true;
    if (s.isVetor) return true;
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
        out << "   " << opt_.dataComment << " " << s.tipo;
        if (N > 1) out << " [" << N << "]";
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
    emitInstr("LDI " + lbl);
    emitInstr("LD 0");
}
void CodeGeneratorBIP::emitStoreId(const std::string& nome) {
    std::string lbl = sanitizeLabel(nome);
    emitInstr("LDI " + lbl);
    emitInstr("STO 0");
}

// vetores com deslocamento constante
void CodeGeneratorBIP::emitLoadIdOffset(const std::string& nome, int k) {
    std::string lbl = sanitizeLabel(nome);
    emitInstr("LDI " + lbl);
    emitInstr("LD " + std::to_string(k));
}
void CodeGeneratorBIP::emitStoreIdOffset(const std::string& nome, int k) {
    std::string lbl = sanitizeLabel(nome);
    emitInstr("LDI " + lbl);
    emitInstr("STO " + std::to_string(k));
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
    else emitLoadId(src);

    if (destIsArray) emitStoreIdOffset(dest, destIndex);
    else emitStoreId(dest);
}

// Vetor com índice variável (v[i] = x)
void CodeGeneratorBIP::emitAssignVarIndex(const std::string& dest, const std::string& idx,
                                          const std::string& src) {
    emitLoadId(src);        // valor de src no AC
    emitInstr("PUSH");      // salva no topo da pilha

    emitLoadId(idx);        // índice i → AC
    emitInstr("LDI " + sanitizeLabel(dest));
    emitAdd();              // soma base + índice

    emitInstr("POP");       // recupera valor de src
    emitInstr("STO 0");     // armazena em v[i]
}

// Atribuição com operação simples (a = b + c)
void CodeGeneratorBIP::emitAssignSimpleExpr(const std::string& dest,
                                            const std::string& op1,
                                            const std::string& oper,
                                            const std::string& op2) {
    emitLoadId(op1);
    emitInstr("PUSH");
    emitLoadId(op2);
    emitInstr("POP");

    if (oper == "+") emitAdd();
    else if (oper == "-") emitSub();
    else if (oper == "*") emitMul();
    else if (oper == "/") emitDiv();

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
